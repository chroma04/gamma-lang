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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tu1089hhijadf;
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; } f1; } gm_t6h2b31csotun;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_th2lqbo77pvsc v1; } f1; } gm_tccklsmdasd9s;
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; int64_t v1; uint8_t v2; gm_trqfvmjtt4sij v3; void * v4; gm_b16_8 v5; } f1; } gm_tktr4rmbindgv;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint64_t f0; gm_tk9n3vlila9qg f1; } gm_toa5kh9gm9738;
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_b16_8 v1; } f1; } gm_tptmds653bni0;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; void * v1; } f1; } gm_trssmtuv8jr0v;
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
_Static_assert(sizeof(gm_tu1089hhijadf) == 56 && _Alignof(gm_tu1089hhijadf) == 8 && offsetof(gm_tu1089hhijadf, f0) == 0 && offsetof(gm_tu1089hhijadf, f1) == 8 && offsetof(gm_tu1089hhijadf, f2) == 16 && offsetof(gm_tu1089hhijadf, f3) == 24 && offsetof(gm_tu1089hhijadf, f4) == 40, "layout of `Version`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tu1089hhijadf v1; } f1; } gm_tvtl3n8qta358;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_ti0g3bmheuq9o v1; } f1; } gm_t171adjpb0m86;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tin97tt4ud0s1 v1; } f1; } gm_t62efos6ssciq;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_ti0g3bmheuq9o f2; } gm_t6b36gvdk984b;
_Static_assert(sizeof(gm_t6h2b31csotun) == 32 && _Alignof(gm_t6h2b31csotun) == 8 && offsetof(gm_t6h2b31csotun, f0) == 0 && offsetof(gm_t6h2b31csotun, f1) == 8, "layout of `ManifestError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_ti0g3bmheuq9o v1; } f1; } gm_tacj1cskem694;
_Static_assert(sizeof(gm_tccklsmdasd9s) == 32 && _Alignof(gm_tccklsmdasd9s) == 8 && offsetof(gm_tccklsmdasd9s, f0) == 0 && offsetof(gm_tccklsmdasd9s, f1) == 8, "layout of `ManifestError!?string`");
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tktr4rmbindgv) == 48 && _Alignof(gm_tktr4rmbindgv) == 8 && offsetof(gm_tktr4rmbindgv, f0) == 0 && offsetof(gm_tktr4rmbindgv, f1) == 8, "layout of `Value`");
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
_Static_assert(sizeof(gm_toa5kh9gm9738) == 48 && _Alignof(gm_toa5kh9gm9738) == 8 && offsetof(gm_toa5kh9gm9738, f0) == 0 && offsetof(gm_toa5kh9gm9738, f1) == 8, "layout of `(i: usize, x: LintRule)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tin97tt4ud0s1 v0; gm_b16_8 v1; gm_b16_8 v2; gm_b16_8 v3; } f1; } gm_toq2f2q4opoa3;
_Static_assert(sizeof(gm_tptmds653bni0) == 32 && _Alignof(gm_tptmds653bni0) == 8 && offsetof(gm_tptmds653bni0, f0) == 0 && offsetof(gm_tptmds653bni0, f1) == 8, "layout of `ManifestError![]const string`");
typedef struct { gm_b16_8 f0; gm_tktr4rmbindgv f1; uint64_t f2; } gm_trc3jdv2g5qre;
_Static_assert(sizeof(gm_trssmtuv8jr0v) == 40 && _Alignof(gm_trssmtuv8jr0v) == 8 && offsetof(gm_trssmtuv8jr0v, f0) == 0 && offsetof(gm_trssmtuv8jr0v, f1) == 8, "layout of `TomlError!^Table`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
_Static_assert(sizeof(gm_tvtl3n8qta358) == 64 && _Alignof(gm_tvtl3n8qta358) == 8 && offsetof(gm_tvtl3n8qta358, f0) == 0 && offsetof(gm_tvtl3n8qta358, f1) == 8, "layout of `string!Version`");
_Static_assert(sizeof(gm_t171adjpb0m86) == 160 && _Alignof(gm_t171adjpb0m86) == 8 && offsetof(gm_t171adjpb0m86, f0) == 0 && offsetof(gm_t171adjpb0m86, f1) == 8, "layout of `string!BuildSettings`");
_Static_assert(sizeof(gm_t62efos6ssciq) == 48 && _Alignof(gm_t62efos6ssciq) == 8 && offsetof(gm_t62efos6ssciq, f0) == 0 && offsetof(gm_t62efos6ssciq, f1) == 8, "layout of `string!VersionReq`");
_Static_assert(sizeof(gm_t6b36gvdk984b) == 176 && _Alignof(gm_t6b36gvdk984b) == 8 && offsetof(gm_t6b36gvdk984b, f0) == 0 && offsetof(gm_t6b36gvdk984b, f1) == 16 && offsetof(gm_t6b36gvdk984b, f2) == 24, "layout of `Profile`");
_Static_assert(sizeof(gm_tacj1cskem694) == 160 && _Alignof(gm_tacj1cskem694) == 8 && offsetof(gm_tacj1cskem694, f0) == 0 && offsetof(gm_tacj1cskem694, f1) == 8, "layout of `ManifestError!BuildSettings`");
typedef struct { gm_b16_8 f0; gm_toq2f2q4opoa3 f1; gm_th2lqbo77pvsc f2; } gm_te5jk64pjl28k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tjgmtdrk88t7m;
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_toq2f2q4opoa3) == 48 && _Alignof(gm_toq2f2q4opoa3) == 8 && offsetof(gm_toq2f2q4opoa3, f0) == 0 && offsetof(gm_toq2f2q4opoa3, f1) == 8, "layout of `GitSpec`");
_Static_assert(sizeof(gm_trc3jdv2g5qre) == 72 && _Alignof(gm_trc3jdv2g5qre) == 8 && offsetof(gm_trc3jdv2g5qre, f0) == 0 && offsetof(gm_trc3jdv2g5qre, f1) == 16 && offsetof(gm_trc3jdv2g5qre, f2) == 64, "layout of `Entry`");
_Static_assert(sizeof(gm_te5jk64pjl28k) == 88 && _Alignof(gm_te5jk64pjl28k) == 8 && offsetof(gm_te5jk64pjl28k, f0) == 0 && offsetof(gm_te5jk64pjl28k, f1) == 16 && offsetof(gm_te5jk64pjl28k, f2) == 64, "layout of `GitSource`");
_Static_assert(sizeof(gm_tjgmtdrk88t7m) == 328 && _Alignof(gm_tjgmtdrk88t7m) == 8 && offsetof(gm_tjgmtdrk88t7m, f0) == 0 && offsetof(gm_tjgmtdrk88t7m, f1) == 8, "layout of `ManifestError!Manifest`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_te5jk64pjl28k v1; gm_tt008mgut8glb v2; } f1; } gm_tnlfbo6r4vlhg;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_tnlfbo6r4vlhg f2; } gm_t7tiv6vif86qg;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tnlfbo6r4vlhg v1; } f1; } gm_tmn9qm2bp7f46;
_Static_assert(sizeof(gm_tnlfbo6r4vlhg) == 96 && _Alignof(gm_tnlfbo6r4vlhg) == 8 && offsetof(gm_tnlfbo6r4vlhg, f0) == 0 && offsetof(gm_tnlfbo6r4vlhg, f1) == 8, "layout of `Source`");
_Static_assert(sizeof(gm_t7tiv6vif86qg) == 120 && _Alignof(gm_t7tiv6vif86qg) == 8 && offsetof(gm_t7tiv6vif86qg, f0) == 0 && offsetof(gm_t7tiv6vif86qg, f1) == 16 && offsetof(gm_t7tiv6vif86qg, f2) == 24, "layout of `Dependency`");
_Static_assert(sizeof(gm_tmn9qm2bp7f46) == 104 && _Alignof(gm_tmn9qm2bp7f46) == 8 && offsetof(gm_tmn9qm2bp7f46, f0) == 0 && offsetof(gm_tmn9qm2bp7f46, f1) == 8, "layout of `ManifestError!Source`");
struct gm_fr_as_str_982o16v {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_as_table_4f7n31g {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    void * ret;
};
struct gm_fr_build_list_f41mb8u {
    gm_frame h;
    gm_trc3jdv2g5qre l0; /* e */
    gm_tptmds653bni0 ret;
};
struct gm_fr_build_settings_brkieib {
    gm_frame h;
    gm_t6b36gvdk984b l5; /* p */
    gm_trqfvmjtt4sij l6; /* known */
    gm_t6b36gvdk984b l9; /* p */
    gm_t171adjpb0m86 ret;
};
struct gm_fr_byte_text_9ib4q2b {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_check_subdir_2uljb06 {
    gm_frame h;
    gm_t6h2b31csotun ret;
};
struct gm_fr_debug_char_p6i205h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_debug_escape_t8t1h0j {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_dep_string_mmgrsgi {
    gm_frame h;
    gm_tccklsmdasd9s ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lqrk916 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lrh8b84 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qmhr0p3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_for_body_vgf3305 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_1ncs69t {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_23qvjoj {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3aass3a {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4af676s {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4eslq57 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4pbmdvl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_55igra0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5bo63p7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_64tgs30 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6cnv7j9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6ld92gp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_79gt991 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7duae7m {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7p2ook8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7sbr40s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_850ujed {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_86t06at {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_97etovl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9d8u2dd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aome4un {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dd3t0vb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fn68qf0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fqmocq1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gs0k8gk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h0dnf9n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hvnghft {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_i3a6tth {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ivhanlp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_je4jsfp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jh4rfvo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jrdns83 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jv6n7oh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ll295o3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_llhsr0b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mcrh28a {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mvlqlcp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nll5o74 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p73hfi0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p8qi5m3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pah1p62 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qjfu8g6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rlu0tid {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rp4le08 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uqgrksh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_usjmuj4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_is_alnum_n81ervr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_alpha_12215ga {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_control_65ubfpo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_digit_2hoggnj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_hex_digit_q8s6utq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ident_cont_317fpdb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ident_start_rjefpfm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_identifier_kvlcjjo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_lower_4ebokoh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_space_or_control_3uo6i3o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_space_uen38t5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_subdir_sjdvkdc {
    gm_frame h;
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_is_upper_iekrftp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_lint_rule_o3ccae1 {
    gm_frame h;
    gm_tk9n3vlila9qg l4; /* r */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_list_7393u00 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7mv9vi7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_hs66gbq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lower_ascii_uggh3ca {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_mfail_uqq7o7g {
    gm_frame h;
    gm_tf1bjg81u0glv ret;
};
struct gm_fr_name_problem_6r4mp11 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_normalize_url_faghdja {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_overlay_pjf5fej {
    gm_frame h;
    gm_ti0g3bmheuq9o l0; /* base */
    gm_ti0g3bmheuq9o l1; /* over */
    gm_ti0g3bmheuq9o ret;
};
struct gm_fr_parse_build_c2teosa {
    gm_frame h;
    gm_ti0g3bmheuq9o l2; /* b */
    gm_trc3jdv2g5qre l5; /* e */
    gm_tacj1cskem694 ret;
};
struct gm_fr_parse_dependency_8qmcjrm {
    gm_frame h;
    gm_tktr4rmbindgv l1; /* item */
    gm_trc3jdv2g5qre l6; /* e */
    gm_trqfvmjtt4sij l11; /* sources */
    gm_tfeab279c16rr l12; /* <tmp> */
    gm_t9bmh80edou9c l13; /* <tmp> */
    gm_t0uuhaljsf410 l18; /* <tmp> */
    gm_tdmi4fk9nhqa7 l19; /* <tmp> */
    gm_t0uuhaljsf410 l37; /* <tmp> */
    gm_tdmi4fk9nhqa7 l38; /* <tmp> */
    gm_trqfvmjtt4sij l44; /* present */
    gm_t0uuhaljsf410 l45; /* <tmp> */
    gm_tdmi4fk9nhqa7 l46; /* <tmp> */
    gm_toq2f2q4opoa3 l50; /* spec */
    gm_tin97tt4ud0s1 l53; /* req */
    gm_tmn9qm2bp7f46 ret;
};
struct gm_fr_parse_manifest_b61s3ln {
    gm_frame h;
    gm_tm43kqe3e8b4a l3; /* m */
    gm_trc3jdv2g5qre l16; /* e */
    gm_tnlfbo6r4vlhg l18; /* source */
    gm_trc3jdv2g5qre l25; /* e */
    gm_trc3jdv2g5qre l31; /* e */
    gm_ti0g3bmheuq9o l40; /* settings */
    gm_tjgmtdrk88t7m ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_7hnmra3 {
    gm_frame h;
    gm_t6b36gvdk984b l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_dob2kfb {
    gm_frame h;
    gm_tftksr4op114e l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pdknc9g {
    gm_frame h;
    gm_t7tiv6vif86qg l1; /* x */
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
struct gm_fr_regrow_c7q5gk0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_na50r9m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ngq1kvf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_spec_equal_foa1umn {
    gm_frame h;
    gm_toq2f2q4opoa3 l0; /* a */
    gm_toq2f2q4opoa3 l1; /* b */
    gm_tin97tt4ud0s1 l2; /* x */
    gm_tin97tt4ud0s1 l3; /* y */
    uint8_t ret;
};
struct gm_fr_spec_text_9k2gjft {
    gm_frame h;
    gm_toq2f2q4opoa3 l0; /* s */
    gm_tin97tt4ud0s1 l1; /* r */
    gm_b16_8 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_string_array_bn13c0i {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    gm_trqfvmjtt4sij l1; /* items */
    gm_trqfvmjtt4sij l2; /* out */
    gm_tktr4rmbindgv l5; /* it */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_to_lower_0fu6efk {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_trim_ascii_c9j36d1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_url_problem_38kdh3a {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_write_kki8q0t {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0);
__attribute__((aligned(4))) void gm_f_build_list_f41mb8u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_build_settings_brkieib(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_byte_text_9ib4q2b(void *env, gm_node *cx, gm_node *inst, uint8_t a0);
__attribute__((aligned(4))) void gm_f_check_subdir_2uljb06(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint64_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_char_p6i205h(void *env, gm_node *cx, gm_node *inst, uint8_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_escape_t8t1h0j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_dep_string_mmgrsgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lqrk916(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lrh8b84(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmhr0p3(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vgf3305(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_0dva8tv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_15426cv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_1ncs69t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_23qvjoj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_3aass3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_4af676s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_4eslq57(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_4pbmdvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_55igra0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5bo63p7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_64tgs30(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6cnv7j9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_6ld92gp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_79gt991(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_7duae7m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_7p2ook8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_7sbr40s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_850ujed(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_86t06at(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_97etovl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9d8u2dd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_aome4un(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_dd3t0vb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fn68qf0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_fqmocq1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_gs0k8gk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_h0dnf9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_hvnghft(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_i3a6tth(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ivhanlp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_je4jsfp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_jh4rfvo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_jrdns83(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_jv6n7oh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ll295o3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_llhsr0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_format_mcrh28a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mvlqlcp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_nll5o74(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_p73hfi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_p8qi5m3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_pah1p62(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qjfu8g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_rlu0tid(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_rp4le08(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_uqgrksh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_usjmuj4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void * gm_f_get_9rme0gb(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_control_65ubfpo(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_hex_digit_q8s6utq(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_start_rjefpfm(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_identifier_kvlcjjo(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_or_control_3uo6i3o(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_subdir_sjdvkdc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lint_rule_o3ccae1(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_7393u00(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7mv9vi7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_hs66gbq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_lower_ascii_uggh3ca(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_mfail_uqq7o7g(void *gm_out, uint64_t a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf1bjg81u0glv gm_f_mfail_uqq7o7g_c(uint64_t a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_name_problem_6r4mp11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_url_faghdja(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_overlay_pjf5fej(void *gm_out, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_ti0g3bmheuq9o gm_f_overlay_pjf5fej_c(gm_ti0g3bmheuq9o a0, gm_ti0g3bmheuq9o a1);
__attribute__((aligned(4))) void gm_f_parse_build_c2teosa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_dependency_8qmcjrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_toml_gjtmf79(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_version_9a6ns50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_version_req_alipvfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7hnmra3(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_dob2kfb(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pdknc9g(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_c7q5gk0(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_na50r9m(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ngq1kvf(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) uint8_t gm_f_req_equal_7ia8ip6(void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_req_string_fk059jr(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn_c(gm_toq2f2q4opoa3 a0, gm_toq2f2q4opoa3 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_text_9k2gjft(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s7d377q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_string_array_bn13c0i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_to_lower_0fu6efk(uint8_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_ascii_c9j36d1(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_url_problem_38kdh3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kki8q0t(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uqn5uk3(void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_b16_8 a3);
extern uint8_t gm_c42ctv8im24[920] __attribute__((aligned(8)));
extern uint8_t gm_cb60tlkkq8o[152] __attribute__((aligned(8)));
extern uint8_t gm_cev3sfhfj2p[48] __attribute__((aligned(8)));
extern uint8_t gm_cfcggcra0bi[128] __attribute__((aligned(8)));
extern uint8_t gm_cka4oohkshv[512] __attribute__((aligned(8)));
extern uint8_t gm_ct5id4947nt[144] __attribute__((aligned(8)));
extern void *const gm_hl5aqi4imlms[];
extern uint8_t gm_klkc6gi5fh5[40] __attribute__((aligned(8)));
extern uint8_t gm_ko53fduln7u[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02419ud122hi[8];
extern const uint8_t gm_s0i05enq0tmg0[44];
extern const uint8_t gm_s0iqiubfm7e7e[7];
extern const uint8_t gm_s0jho24omhfui[7];
extern const uint8_t gm_s0k6ioi39f878[4];
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s0ri8hi6r25m8[37];
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s230fdcusa4us[33];
extern const uint8_t gm_s2568a81aaotm[70];
extern const uint8_t gm_s2go8ju7rh9bh[38];
extern const uint8_t gm_s2nmq6ak9nl9d[5];
extern const uint8_t gm_s3dj47j0skrv7[60];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s419erk5rpblh[39];
extern const uint8_t gm_s41sp7ht9l505[67];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s460n81rl2c9c[5];
extern const uint8_t gm_s47llp68ptjgu[8];
extern const uint8_t gm_s4b0873bt7c1q[5];
extern const uint8_t gm_s4cdme6b7eogn[56];
extern const uint8_t gm_s4k7tmsh4glbh[77];
extern const uint8_t gm_s4m15r14tbnat[61];
extern const uint8_t gm_s5ojatldfpfs2[8];
extern const uint8_t gm_s5oredgotak9m[58];
extern const uint8_t gm_s5pgobtluqp1f[29];
extern const uint8_t gm_s5tee1epg2e5a[24];
extern const uint8_t gm_s6f1t1a0knpo4[82];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s6iuue4hdrbt9[5];
extern const uint8_t gm_s6m48vdr7h6mr[77];
extern const uint8_t gm_s6v08aos37nfe[8];
extern const uint8_t gm_s7j1ioljmsuka[19];
extern const uint8_t gm_s8f9rv6hrr024[11];
extern const uint8_t gm_s8ok287s0vtoq[38];
extern const uint8_t gm_s8sv6qdepk5ba[4];
extern const uint8_t gm_s920j0georape[5];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_s9eqqrnq18n3l[31];
extern const uint8_t gm_s9r72jgd5hb05[5];
extern const uint8_t gm_sa2h9vonan0ma[4];
extern const uint8_t gm_sagc0kltjb8fc[12];
extern const uint8_t gm_saja8vc684oud[41];
extern const uint8_t gm_sb21gep34rmm9[36];
extern const uint8_t gm_sbcjfjm1sdn3u[42];
extern const uint8_t gm_sckqm6be9dvme[13];
extern const uint8_t gm_scs7jonv1denn[64];
extern const uint8_t gm_sd1smnnii4cig[7];
extern const uint8_t gm_sd1tvohjm34vg[37];
extern const uint8_t gm_sdbqk29a3n8jm[13];
extern const uint8_t gm_sdru6cvprml58[29];
extern const uint8_t gm_setdfq42nhg1q[5];
extern const uint8_t gm_sf9ilivr4c86m[4];
extern const uint8_t gm_sfag3nu74qn7r[31];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sflee3fpg0btv[5];
extern const uint8_t gm_sfvspsnv23gbt[7];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg9l06oo908p3[45];
extern const uint8_t gm_sgdafdv2n5c25[5];
extern const uint8_t gm_sh4f06c2ecgj1[5];
extern const uint8_t gm_shc8a6qgqrqaf[42];
extern const uint8_t gm_shl9ceh0q5tpe[6];
extern const uint8_t gm_sitcmrrnvoedc[55];
extern const uint8_t gm_sius9pki5u4e3[5];
extern const uint8_t gm_sj2jft9n0jm50[3];
extern const uint8_t gm_sjqf7m3o9alo0[9];
extern const uint8_t gm_sjrtlfuv3os00[75];
extern const uint8_t gm_skbosgm06mmhc[57];
extern const uint8_t gm_skeo8nnnc4o7f[40];
extern const uint8_t gm_skki6c17jgbtj[30];
extern const uint8_t gm_sknsei1oiabnd[13];
extern const uint8_t gm_skof48dte1s1o[13];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skp2amifebhq6[41];
extern const uint8_t gm_sl0oflhpdomi7[5];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sla3alt5340mf[8];
extern const uint8_t gm_slb6cj7ndgvdm[6];
extern const uint8_t gm_slc3n2ku5uff5[55];
extern const uint8_t gm_slfpvqm0f02j6[8];
extern const uint8_t gm_slllafvm0pbeb[6];
extern const uint8_t gm_sm294gmh9789j[39];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_smimi08sfrg0n[5];
extern const uint8_t gm_sn4utba4jl66l[9];
extern const uint8_t gm_sn57nkh1rblgd[62];
extern const uint8_t gm_sn5ntorhqodmg[39];
extern const uint8_t gm_sn674iiiekfov[74];
extern const uint8_t gm_sna94ju8c8em3[48];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snja7imrvdfb7[11];
extern const uint8_t gm_snqvo5bgc2fch[8];
extern const uint8_t gm_sob5t135sl7q9[34];
extern const uint8_t gm_sogv4el7uffui[58];
extern const uint8_t gm_son0j0k0t10ro[8];
extern const uint8_t gm_soofuqhnovv5o[5];
extern const uint8_t gm_sp6f2rgoi8toe[7];
extern const uint8_t gm_sp7ec3ksfdilk[5];
extern const uint8_t gm_spuk9sqnoo3ai[13];
extern const uint8_t gm_sq3r7vbcbs4ll[26];
extern const uint8_t gm_sq6e9d3107t3s[11];
extern const uint8_t gm_sqmifqg6egoj0[41];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3nt8pf0qk5gn[];
extern const char gm_srcc24pnihhgkug[];
extern const uint8_t gm_srcf8l485c5uq[10];
extern const char gm_srcg0ns322676oc[];
extern const char gm_srci5ni72s6oj5f[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_st3p5nb1e6cc8[5];
extern const uint8_t gm_su0gvqh5vvvlj[37];
extern const uint8_t gm_sukjjfkvr8401[40];
extern const uint8_t gm_sv1gci5lo7scg[5];
extern const uint8_t gm_svkmjs6p8bhtg[9];
extern const gm_tmpl gm_tpl0scna75sco5f;
extern const gm_tmpl gm_tpl1dleoebfmvhp;
extern const gm_tmpl gm_tpl21i1s6vvb3m4;
extern const gm_tmpl gm_tpl2d7ij9b2sa2u;
extern const gm_tmpl gm_tpl2n8jqp2rhn52;
extern const gm_tmpl gm_tpl5qm9nkkj1gjg;
extern const gm_tmpl gm_tpl63iuq3je4sbr;
extern const gm_tmpl gm_tpl726l5jbs5bmh;
extern const gm_tmpl gm_tpl80egqk8smgev;
extern const gm_tmpl gm_tpl8ro9atn5p92f;
extern const gm_tmpl gm_tpl9d1siq0c9pir;
extern const gm_tmpl gm_tpl9fvss98clvrp;
extern const gm_tmpl gm_tpl9qc0voknsp7g;
extern const gm_tmpl gm_tplbk1k026jl879;
extern const gm_tmpl gm_tplcilp3mvhfo4n;
extern const gm_tmpl gm_tplde4760b0jcjn;
extern const gm_tmpl gm_tpldi9jj8a80irn;
extern const gm_tmpl gm_tpled61een8fpfm;
extern const gm_tmpl gm_tplf2ivserdf4ic;
extern const gm_tmpl gm_tplfm1jmgsrn2ua;
extern const gm_tmpl gm_tplfqju53pb0vaj;
extern const gm_tmpl gm_tplhghj93ore8u2;
extern const gm_tmpl gm_tplhrqv5p3pfbh5;
extern const gm_tmpl gm_tpli9uflldtkfq3;
extern const gm_tmpl gm_tplibsdn3f4sfh0;
extern const gm_tmpl gm_tpll60ng9eo3ai8;
extern const gm_tmpl gm_tpllg65m2i6jvjk;
extern const gm_tmpl gm_tplllolcurrktr6;
extern const gm_tmpl gm_tplmo1fik79j6ea;
extern const gm_tmpl gm_tplmsd0sfgf2v66;
extern const gm_tmpl gm_tplnokap11rv9jr;
extern const gm_tmpl gm_tplns4e3al0r331;
extern const gm_tmpl gm_tplo203818ggfa8;
extern const gm_tmpl gm_tplod1bbc3c9s5j;
extern const gm_tmpl gm_tplp3rnngb19vkd;
extern const gm_tmpl gm_tplq85e99v067d0;
extern const gm_tmpl gm_tplq8t63lu2docj;
extern const gm_tmpl gm_tplqnufra7h2p1s;
extern const gm_tmpl gm_tplr9kq0pku58q3;
extern const gm_tmpl gm_tpls4ecclriepmb;
extern const gm_tmpl gm_tpltdoamgi2k9f9;
extern const gm_tmpl gm_tplto3kksb9sot8;
extern const gm_tmpl gm_tplucd2r10hh63v;
extern const gm_tmpl gm_tplv2usruqglqv9;
extern const gm_tmpl gm_tplvi7pmohm1ihu;
extern const gm_tmpl gm_tplvqfop4uu1u5q;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_str_982o16v F;
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)0ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l1 = t7;
            gm_b16_8 t8 = gm_l1;
            gm_th2lqbo77pvsc t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t9), t8);
            ((uint8_t *)&t9)[16] = 1;
            gm_l2 = t9;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_th2lqbo77pvsc t10;
            memset(&t10, 0, sizeof t10);
            gm_l2 = t10;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 50, 51);
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&gm_l2), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_table_4f7n31g F;
    void * gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)4ULL));
            if (!t6) {
                goto gm_lb2;
            }
            void * t7 = GM_LD(void *, (((uint8_t *)a0) + 8));
            gm_l1 = t7;
            void * t8 = gm_l1;
            gm_l2 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((void *)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 56, 53);
    }
gm_lb1:;
    void * t9 = gm_l2;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_byte_text_9ib4q2b(void *env, gm_node *cx, gm_node *inst, uint8_t a0) {
    struct gm_fr_byte_text_9ib4q2b F;
    uint8_t gm_l0; /* c */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l2;
    gm_f_make_2ip78bj((void *)&gm_l2, (void *)NULL, cx, NULL, ((uint64_t)1ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klkc6gi5fh5)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t2 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t2) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcg0ns322676oc, 199, 10);
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
    gm_l1 = t3;
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t5, gm_srcg0ns322676oc, 200, 5);
    uint8_t t6 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t4) + (uintptr_t)((uint64_t)0ULL) * 1), t6);
    gm_b16_8 t7 = gm_l1;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lqrk916(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lqrk916 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 120)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lrh8b84(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lrh8b84 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 176)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmhr0p3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmhr0p3 F;
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
inline __attribute__((aligned(4))) void gm_f_format_1ncs69t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1ncs69t F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sckqm6be9dvme));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl21i1s6vvb3m4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_23qvjoj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_23qvjoj F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0i05enq0tmg0));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(43));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_15426cv((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhrqv5p3pfbh5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_3aass3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3aass3a F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9r72jgd5hb05));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl80egqk8smgev), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_4af676s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4af676s F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s6f1t1a0knpo4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(81));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplod1bbc3c9s5j), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_4eslq57(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4eslq57 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skki6c17jgbtj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplq85e99v067d0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_4pbmdvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_4pbmdvl F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sagc0kltjb8fc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpll60ng9eo3ai8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_55igra0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_55igra0 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s41sp7ht9l505));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(66));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl2d7ij9b2sa2u), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5bo63p7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_5bo63p7 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjrtlfuv3os00));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(74));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqnufra7h2p1s), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_64tgs30(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_64tgs30 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb21gep34rmm9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(35));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2n8jqp2rhn52), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6cnv7j9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6cnv7j9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skp2amifebhq6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9fvss98clvrp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_6ld92gp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6ld92gp F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s9eqqrnq18n3l));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(30));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0scna75sco5f), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_79gt991(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_79gt991 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjqf7m3o9alo0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplto3kksb9sot8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7duae7m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7duae7m F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s230fdcusa4us));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplv2usruqglqv9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_7p2ook8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7p2ook8 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shc8a6qgqrqaf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(41));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfm1jmgsrn2ua), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_7sbr40s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7sbr40s F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_saja8vc684oud));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(40));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpls4ecclriepmb), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_850ujed(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_850ujed F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3dj47j0skrv7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(59));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplq8t63lu2docj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_86t06at(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_86t06at F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn674iiiekfov));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(73));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplp3rnngb19vkd), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_97etovl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_97etovl F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8ok287s0vtoq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplibsdn3f4sfh0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9d8u2dd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_9d8u2dd F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4k7tmsh4glbh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(76));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmo1fik79j6ea), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_aome4un(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_aome4un F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4cdme6b7eogn));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(55));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplr9kq0pku58q3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_dd3t0vb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dd3t0vb F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn5ntorhqodmg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfqju53pb0vaj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fn68qf0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fn68qf0 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbcjfjm1sdn3u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplns4e3al0r331), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fqmocq1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fqmocq1 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqmifqg6egoj0));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(40));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplde4760b0jcjn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_gs0k8gk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_gs0k8gk F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4m15r14tbnat));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(60));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl8ro9atn5p92f), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_h0dnf9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h0dnf9n F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn4utba4jl66l));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpled61een8fpfm), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_hvnghft(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hvnghft F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn57nkh1rblgd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(61));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvi7pmohm1ihu), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_i3a6tth(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_i3a6tth F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg9l06oo908p3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplllolcurrktr6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ivhanlp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ivhanlp F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5pgobtluqp1f));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9qc0voknsp7g), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_je4jsfp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_je4jsfp F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sukjjfkvr8401));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(39));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplmsd0sfgf2v66), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jh4rfvo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jh4rfvo F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdru6cvprml58));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldi9jj8a80irn), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_jrdns83(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jrdns83 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2568a81aaotm));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(69));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl63iuq3je4sbr), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_jv6n7oh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jv6n7oh F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_scs7jonv1denn));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(63));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplucd2r10hh63v), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ll295o3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ll295o3 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skbosgm06mmhc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplo203818ggfa8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_llhsr0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_llhsr0b F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spuk9sqnoo3ai));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_0dva8tv((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcilp3mvhfo4n), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_mcrh28a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mcrh28a F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sogv4el7uffui));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(57));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbk1k026jl879), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mvlqlcp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mvlqlcp F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sitcmrrnvoedc));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(54));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpltdoamgi2k9f9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_nll5o74(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nll5o74 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5oredgotak9m));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(57));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnokap11rv9jr), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_p73hfi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_p73hfi0 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s419erk5rpblh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(38));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplf2ivserdf4ic), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_p8qi5m3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_p8qi5m3 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skeo8nnnc4o7f));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhghj93ore8u2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_pah1p62(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_pah1p62 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0ri8hi6r25m8));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(36));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1dleoebfmvhp), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qjfu8g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qjfu8g6 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sd1tvohjm34vg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(36));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpli9uflldtkfq3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_rlu0tid(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rlu0tid F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sknsei1oiabnd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl726l5jbs5bmh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_rp4le08(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rp4le08 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s6m48vdr7h6mr));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(76));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5qm9nkkj1gjg), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_uqgrksh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_uqgrksh F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sm294gmh9789j));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(38));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllg65m2i6jvjk), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_usjmuj4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_usjmuj4 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s2go8ju7rh9bh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(37));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvqfop4uu1u5q), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_control_65ubfpo(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_control_65ubfpo F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = gm_l0;
    uint8_t t3 = (t2 < ((uint8_t)32ULL));
    if (t3) {
        gm_l1 = ((uint8_t)1);
    } else {
        uint8_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint8_t)127ULL)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ident_cont_317fpdb F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_alnum_n81ervr(t2);
    if (t3) {
        gm_l1 = ((uint8_t)1);
    } else {
        uint8_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint8_t)95ULL)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_start_rjefpfm(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ident_start_rjefpfm F;
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
        uint8_t t5 = ((t4 == ((uint8_t)95ULL)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_or_control_3uo6i3o(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_space_or_control_3uo6i3o F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = gm_l0;
    uint8_t t3 = ((t2 == ((uint8_t)32ULL)));
    if (t3) {
        gm_l1 = ((uint8_t)1);
    } else {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_control_65ubfpo(t4);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_space_uen38t5 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 25u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    uint8_t gm_l5;
    uint8_t t2 = gm_l0;
    uint8_t t3 = ((t2 == ((uint8_t)32ULL)));
    if (t3) {
        gm_l5 = ((uint8_t)1);
    } else {
        uint8_t t4 = gm_l0;
        uint8_t t5 = ((t4 == ((uint8_t)9ULL)));
        gm_l5 = t5;
    }
    uint8_t t6 = gm_l5;
    if (t6) {
        gm_l4 = ((uint8_t)1);
    } else {
        uint8_t t7 = gm_l0;
        uint8_t t8 = ((t7 == ((uint8_t)10ULL)));
        gm_l4 = t8;
    }
    uint8_t t9 = gm_l4;
    if (t9) {
        gm_l3 = ((uint8_t)1);
    } else {
        uint8_t t10 = gm_l0;
        uint8_t t11 = ((t10 == ((uint8_t)13ULL)));
        gm_l3 = t11;
    }
    uint8_t t12 = gm_l3;
    if (t12) {
        gm_l2 = ((uint8_t)1);
    } else {
        uint8_t t13 = gm_l0;
        uint8_t t14 = ((t13 == ((uint8_t)11ULL)));
        gm_l2 = t14;
    }
    uint8_t t15 = gm_l2;
    if (t15) {
        gm_l1 = ((uint8_t)1);
    } else {
        uint8_t t16 = gm_l0;
        uint8_t t17 = ((t16 == ((uint8_t)12ULL)));
        gm_l1 = t17;
    }
    uint8_t t18 = gm_l1;
    F.ret = t18;
    goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_list_7393u00(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7393u00 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lqrk916();
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
inline __attribute__((aligned(4))) void gm_f_list_7mv9vi7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7mv9vi7 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qmhr0p3();
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
inline __attribute__((aligned(4))) void gm_f_list_hs66gbq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_hs66gbq F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lrh8b84();
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
inline __attribute__((aligned(4))) void gm_f_mfail_uqq7o7g(void *gm_out, uint64_t a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mfail_uqq7o7g F;
    uint64_t gm_l0; /* line */
    gm_b16_8 gm_l1; /* msg */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_tf1bjg81u0glv t4;
    GM_ST(uint64_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 8), t3);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7hnmra3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_7hnmra3 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_lrh8b84();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_na50r9m(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 176), ((uint8_t *)a1), 176);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 176)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_dob2kfb(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_dob2kfb F;
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
        gm_b16_8 t9 = gm_f_empty_slice_qmhr0p3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ngq1kvf(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pdknc9g(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pdknc9g F;
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
        gm_b16_8 t9 = gm_f_empty_slice_lqrk916();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_c7q5gk0(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 120), ((uint8_t *)a1), 120);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 120)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_c7q5gk0(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_c7q5gk0 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)120ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_na50r9m(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_na50r9m F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)176ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ngq1kvf(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ngq1kvf F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_to_lower_0fu6efk(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_lower_0fu6efk F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 10u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_f_is_upper_iekrftp(t2);
    if (t3) {
        uint8_t t4 = gm_l0;
        uint8_t r5; bool o6 = __builtin_add_overflow(t4, ((uint8_t)32ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srci5ni72s6oj5f, 42, 53);
        uint8_t t7 = r5;
        gm_l1 = t7;
    } else {
        uint8_t t8 = gm_l0;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kki8q0t(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_kki8q0t F;
    void * gm_l0; /* l */
    gm_t9qkh0rsooc8d gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t9qkh0rsooc8d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_snja7imrvdfb7));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t5 = gm_f_write_uqn5uk3(t2, t3, ((void *)&gm_tpl9d1siq0c9pir), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_build_list_f41mb8u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1) {
    struct gm_fr_build_list_f41mb8u F;
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 354u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l2;
    gm_tptmds653bni0 gm_l3;
    gm_tabb710d9mq2e gm_l4;
    gm_tktr4rmbindgv t2 = GM_LD(gm_tktr4rmbindgv, (((uint8_t *)a0) + 16));
    gm_f_string_array_bn13c0i((void *)&gm_l2, (void *)NULL, cx, NULL, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t3) {
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
        gm_tptmds653bni0 t5;
        GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t5 + 24, 0);
        ((uint8_t *)&t5)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
        gm_l3 = t5;
    } else {
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)a0) + 64));
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)a0));
        gm_b16_8 t8 = gm_l1;
        gm_t6j6smj7qoctu t9;
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t7);
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 16), t8);
        gm_f_format_p73hfi0((void *)&gm_l4, (void *)NULL, cx, NULL, &t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcg0ns322676oc, 471, 54);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_tf1bjg81u0glv t13;
        gm_f_mfail_uqq7o7g((void *)&t13, t6, t12);
        gm_tptmds653bni0 t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
        ((uint8_t *)&t14)[0] = 1;
        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t14 + 8), t13);
        gm_l3 = t14;
    }
    memmove(gm_out, ((uint8_t *)&gm_l3), 32);
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
__attribute__((aligned(4))) void gm_f_build_settings_brkieib(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_build_settings_brkieib F;
    void * gm_l0; /* m */
    gm_th2lqbo77pvsc gm_l1; /* profile */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 1669u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_b16_8 gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tabb710d9mq2e gm_l14;
    uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l1))[16] != 0);
    if (t12) {
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
        gm_l10 = t13;
    } else {
        void * t14 = gm_l0;
        gm_ti0g3bmheuq9o t15 = GM_LD(gm_ti0g3bmheuq9o, (((uint8_t *)t14) + 88));
        gm_t171adjpb0m86 t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
        ((uint8_t *)&t16)[0] = 0;
        GM_ST(gm_ti0g3bmheuq9o, ((uint8_t *)&t16 + 8), t15);
        memmove(gm_out, ((uint8_t *)&t16), 160);
        goto gm_fdone;
    }
    gm_b16_8 t17 = gm_l10;
    gm_l2 = t17;
    void * t18 = gm_l0;
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t18) + 240));
    gm_l3 = t19;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t20 = gm_l4;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t25 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t23) + (uintptr_t)t25 * 176), 176);
                        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                        gm_b16_8 t27 = gm_l2;
                        uint8_t t28 = (gm_eq0iqiubfm7e(((uint8_t *)&t26), ((uint8_t *)&t27)));
                        if (t28) {
                            void * t29 = gm_l0;
                            gm_ti0g3bmheuq9o t30;
                            gm_f_overlay_pjf5fej((void *)&t30, &(*(gm_ti0g3bmheuq9o *)(((uint8_t *)t29) + 88)), &(*(gm_ti0g3bmheuq9o *)(((uint8_t *)&F.l5) + 24)));
                            gm_t171adjpb0m86 t31;
                            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                            ((uint8_t *)&t31)[0] = 0;
                            GM_ST(gm_ti0g3bmheuq9o, ((uint8_t *)&t31 + 8), t30);
                            memmove(gm_out, ((uint8_t *)&t31), 160);
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 388, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t32 = gm_l4;
            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
            uint64_t t35 = r33;
            gm_l4 = t35;
        }
    }
gm_lb1:;
    void * t36 = gm_l0;
    uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)t36) + 240) + 8));
    uint8_t t38 = ((t37 == ((uint64_t)0ULL)));
    if (t38) {
        gm_b16_8 t39 = gm_l2;
        gm_tnpifv20irnph t40;
        GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t39);
        gm_f_format_jrdns83((void *)&gm_l11, (void *)NULL, cx, NULL, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcg0ns322676oc, 392, 17);
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
        gm_t171adjpb0m86 t44;
        memset(&t44, 0, sizeof t44);
        ((uint8_t *)&t44)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t44 + 8), t43);
        memmove(gm_out, ((uint8_t *)&t44), 160);
        goto gm_fdone;
    }
    gm_trqfvmjtt4sij t45;
    gm_f_list_epvducg((void *)&t45, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t45;
    void * t46 = gm_l0;
    gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t46) + 240));
    gm_l7 = t47;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t48 = gm_l8;
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t50 = (t48 < t49);
            if (!t50) {
                goto gm_lb6;
            }
            {
                void * t51 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t53 = gm_l8;
                {
                    {
                        memmove(((uint8_t *)&F.l9), (((uint8_t *)t51) + (uintptr_t)t53 * 176), 176);
                        void * t54 = (void *)((uint8_t *)&F.l6);
                        gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                        gm_tftb1odc739t5 t56 = gm_f_push_3ef746b(t54, t55);
                        gm_l12 = t56;
                        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcg0ns322676oc, 395, 33);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 395, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t58 = gm_l8;
            uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
            uint64_t t61 = r59;
            gm_l8 = t61;
        }
    }
gm_lb6:;
    gm_b16_8 t62 = gm_l2;
    gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
    gm_b16_8 t65;
    GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sg5gdnrith2i6));
    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(2));
    gm_f_join_sfknljb((void *)&gm_l13, (void *)NULL, cx, NULL, t64, t65);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcg0ns322676oc, 396, 75);
    gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
    gm_t6j6smj7qoctu t68;
    GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t62);
    GM_ST(gm_b16_8, ((uint8_t *)&t68 + 16), t67);
    gm_f_format_7p2ook8((void *)&gm_l14, (void *)NULL, cx, NULL, &t68);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcg0ns322676oc, 396, 13);
    gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
    gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
    gm_t171adjpb0m86 t72;
    memset(&t72, 0, sizeof t72);
    ((uint8_t *)&t72)[0] = 1;
    GM_ST(gm_b16_8, ((uint8_t *)&t72 + 8), t71);
    memmove(gm_out, ((uint8_t *)&t72), 160);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_byte_text_9ib4q2b(void *env, gm_node *cx, gm_node *inst, uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_check_subdir_2uljb06(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint64_t a2) {
    struct gm_fr_check_subdir_2uljb06 F;
    gm_b16_8 gm_l0; /* name */
    gm_th2lqbo77pvsc gm_l1; /* subdir */
    uint64_t gm_l2; /* line */
    gm_b16_8 gm_l3; /* s */
    GM_STACK_CHECK(&F, 284u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l4;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)a1))[16] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)a1));
            gm_l3 = t5;
            gm_b16_8 t6 = gm_l3;
            uint8_t t7 = gm_f_is_subdir_sjdvkdc((void *)NULL, cx, NULL, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t8 = (uint8_t)!t7;
            if (t8) {
                uint64_t t9 = gm_l2;
                gm_b16_8 t10 = gm_l0;
                gm_tnpifv20irnph t11;
                GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
                gm_f_format_86t06at((void *)&gm_l4, (void *)NULL, cx, NULL, t11);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcg0ns322676oc, 586, 33);
                gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
                gm_tf1bjg81u0glv t15;
                gm_f_mfail_uqq7o7g((void *)&t15, t9, t14);
                gm_t6h2b31csotun t16;
                GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
                ((uint8_t *)&t16)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t16 + 8), t15);
                memmove(gm_out, ((uint8_t *)&t16), 32);
                goto gm_fdone;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_t6h2b31csotun t17;
    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t17 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t17 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t17 + 24, 0);
    ((uint8_t *)&t17)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t17 + 8), GM_ZERO(gm_b0_1));
    memmove(gm_out, ((uint8_t *)&t17), 32);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_debug_char_p6i205h(void *env, gm_node *cx, gm_node *inst, uint8_t a0) {
    struct gm_fr_debug_char_p6i205h F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK(&F, 342u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    gm_b16_8 gm_l2;
    uint8_t gm_l3;
    gm_tabb710d9mq2e gm_l4;
    gm_tabb710d9mq2e gm_l5;
    {
        {
            uint8_t t9 = gm_l0;
            uint8_t t10 = ((t9 == ((uint8_t)10ULL)));
            if (!t10) {
                goto gm_lb2;
            }
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s460n81rl2c9c));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(4));
            gm_l1 = t11;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t12 = gm_l0;
            uint8_t t13 = ((t12 == ((uint8_t)9ULL)));
            if (!t13) {
                goto gm_lb3;
            }
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sh4f06c2ecgj1));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(4));
            gm_l1 = t14;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t15 = gm_l0;
            uint8_t t16 = ((t15 == ((uint8_t)13ULL)));
            if (!t16) {
                goto gm_lb4;
            }
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_setdfq42nhg1q));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
            gm_l1 = t17;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t18 = gm_l0;
            uint8_t t19 = ((t18 == ((uint8_t)39ULL)));
            if (!t19) {
                goto gm_lb5;
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s2nmq6ak9nl9d));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(4));
            gm_l1 = t20;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t21 = gm_l0;
            uint8_t t22 = ((t21 == ((uint8_t)92ULL)));
            if (!t22) {
                goto gm_lb6;
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s4b0873bt7c1q));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
            gm_l1 = t23;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t24 = gm_l0;
            uint8_t t25 = (t24 < ((uint8_t)32ULL));
            if (t25) {
                gm_l3 = ((uint8_t)1);
            } else {
                uint8_t t26 = gm_l0;
                uint8_t t27 = ((t26 == ((uint8_t)127ULL)));
                gm_l3 = t27;
            }
            uint8_t t28 = gm_l3;
            if (t28) {
                uint8_t t29 = gm_l0;
                gm_t9qkh0rsooc8d t30;
                GM_ST(uint8_t, ((uint8_t *)&t30 + 0), t29);
                gm_f_format_llhsr0b((void *)&gm_l4, (void *)NULL, cx, NULL, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcg0ns322676oc, 194, 37);
                gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                gm_l2 = t33;
            } else {
                uint8_t t34 = gm_l0;
                gm_b16_8 t35 = gm_f_byte_text_9ib4q2b((void *)NULL, cx, NULL, t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t36;
                GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t35);
                gm_f_format_3aass3a((void *)&gm_l5, (void *)NULL, cx, NULL, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcg0ns322676oc, 194, 78);
                gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                gm_l2 = t39;
            }
            gm_b16_8 t40 = gm_l2;
            gm_l1 = t40;
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 188, 50);
    }
gm_lb1:;
    gm_b16_8 t41 = gm_l1;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_escape_t8t1h0j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_debug_escape_t8t1h0j F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 440u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    uint8_t gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_trqfvmjtt4sij t15;
    gm_f_list_uvfu1k1((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t15;
    gm_b16_8 t16 = gm_l0;
    gm_l2 = t16;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l3;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb1;
            }
            {
                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t22 = gm_l3;
                {
                    {
                        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t20) + (uintptr_t)t22 * 1));
                        gm_l4 = t23;
                        {
                            {
                                uint8_t t24 = gm_l4;
                                uint8_t t25 = ((t24 == ((uint8_t)10ULL)));
                                if (!t25) {
                                    goto gm_lb7;
                                }
                                void * t26 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t27;
                                GM_ST(void *, (uint8_t *)&t27, (void *)(gm_s6gt8b7hsisgu));
                                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t28 = gm_f_extend_0cs85gs(t26, t27);
                                gm_l5 = t28;
                                uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                                if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcg0ns322676oc, 209, 21);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t30 = gm_l4;
                                uint8_t t31 = ((t30 == ((uint8_t)9ULL)));
                                if (!t31) {
                                    goto gm_lb8;
                                }
                                void * t32 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t33;
                                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s22g482qk7a40));
                                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t34 = gm_f_extend_0cs85gs(t32, t33);
                                gm_l6 = t34;
                                uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                                if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcg0ns322676oc, 210, 21);
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t36 = gm_l4;
                                uint8_t t37 = ((t36 == ((uint8_t)13ULL)));
                                if (!t37) {
                                    goto gm_lb9;
                                }
                                void * t38 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t39;
                                GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s9cb6j5ok7bdk));
                                GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t40 = gm_f_extend_0cs85gs(t38, t39);
                                gm_l7 = t40;
                                uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                                if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcg0ns322676oc, 211, 21);
                                goto gm_lb6;
                            }
gm_lb9:;
                            {
                                uint8_t t42 = gm_l4;
                                uint8_t t43 = ((t42 == ((uint8_t)34ULL)));
                                if (!t43) {
                                    goto gm_lb10;
                                }
                                void * t44 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t45;
                                GM_ST(void *, (uint8_t *)&t45, (void *)(gm_smee1janbl2t9));
                                GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t46 = gm_f_extend_0cs85gs(t44, t45);
                                gm_l8 = t46;
                                uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                                if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcg0ns322676oc, 212, 20);
                                goto gm_lb6;
                            }
gm_lb10:;
                            {
                                uint8_t t48 = gm_l4;
                                uint8_t t49 = ((t48 == ((uint8_t)39ULL)));
                                if (!t49) {
                                    goto gm_lb11;
                                }
                                void * t50 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t51;
                                GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sj2jft9n0jm50));
                                GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t52 = gm_f_extend_0cs85gs(t50, t51);
                                gm_l9 = t52;
                                uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcg0ns322676oc, 213, 21);
                                goto gm_lb6;
                            }
gm_lb11:;
                            {
                                uint8_t t54 = gm_l4;
                                uint8_t t55 = ((t54 == ((uint8_t)92ULL)));
                                if (!t55) {
                                    goto gm_lb12;
                                }
                                void * t56 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t57;
                                GM_ST(void *, (uint8_t *)&t57, (void *)(gm_s3p8gug7bvl1p));
                                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t58 = gm_f_extend_0cs85gs(t56, t57);
                                gm_l10 = t58;
                                uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcg0ns322676oc, 214, 21);
                                goto gm_lb6;
                            }
gm_lb12:;
                            {
                                uint8_t t60 = gm_l4;
                                uint8_t t61 = (t60 < ((uint8_t)32ULL));
                                if (t61) {
                                    gm_l11 = ((uint8_t)1);
                                } else {
                                    uint8_t t62 = gm_l4;
                                    uint8_t t63 = ((t62 == ((uint8_t)127ULL)));
                                    gm_l11 = t63;
                                }
                                uint8_t t64 = gm_l11;
                                if (t64) {
                                    void * t65 = (void *)((uint8_t *)&F.l1);
                                    uint8_t t66 = gm_l4;
                                    gm_t9qkh0rsooc8d t67;
                                    GM_ST(uint8_t, ((uint8_t *)&t67 + 0), t66);
                                    gm_tftb1odc739t5 t68 = gm_f_write_kki8q0t(t65, t67);
                                    gm_l12 = t68;
                                    uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                    if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcg0ns322676oc, 215, 45);
                                } else {
                                    void * t70 = (void *)((uint8_t *)&F.l1);
                                    uint8_t t71 = gm_l4;
                                    gm_tftb1odc739t5 t72 = gm_f_push_t83rg2m(t70, t71);
                                    gm_l13 = t72;
                                    uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                    if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcg0ns322676oc, 215, 83);
                                }
                                goto gm_lb6;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 208, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 207, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t74 = gm_l3;
            uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
            uint64_t t77 = r75;
            gm_l3 = t77;
        }
    }
gm_lb1:;
    gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
    F.ret = t79;
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
inline __attribute__((aligned(4))) void gm_f_dep_string_mmgrsgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_dep_string_mmgrsgi F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* key */
    gm_b16_8 gm_l2; /* name */
    void * gm_l3; /* e */
    gm_b16_8 gm_l4; /* s */
    GM_STACK_CHECK(&F, 579u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * gm_l5;
    void * gm_l6;
    gm_th2lqbo77pvsc gm_l7;
    gm_b16_8 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    void * t4 = gm_f_get_9rme0gb(t2, t3);
    gm_l5 = t4;
    uint8_t t5 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
    if (t5) {
        void * t6 = GM_LD(void *, ((uint8_t *)&gm_l5));
        gm_l6 = t6;
    } else {
        gm_th2lqbo77pvsc t7;
        memset(&t7, 0, sizeof t7);
        gm_tccklsmdasd9s t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
        ((uint8_t *)&t8)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t8 + 8), t7);
        memmove(gm_out, ((uint8_t *)&t8), 32);
        goto gm_fdone;
    }
    void * t9 = gm_l6;
    gm_l3 = t9;
    void * t10 = gm_l3;
    gm_tktr4rmbindgv t11 = GM_LD(gm_tktr4rmbindgv, (((uint8_t *)t10) + 16));
    gm_f_as_str_982o16v((void *)&gm_l7, &t11);
    uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
    if (t12) {
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
        gm_l8 = t13;
    } else {
        void * t14 = gm_l3;
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t14) + 64));
        gm_b16_8 t16 = gm_l1;
        gm_b16_8 t17 = gm_l2;
        gm_t6j6smj7qoctu t18;
        GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t16);
        GM_ST(gm_b16_8, ((uint8_t *)&t18 + 16), t17);
        gm_f_format_fqmocq1((void *)&gm_l9, (void *)NULL, cx, NULL, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcg0ns322676oc, 479, 53);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        gm_tf1bjg81u0glv t22;
        gm_f_mfail_uqq7o7g((void *)&t22, t15, t21);
        gm_tccklsmdasd9s t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        ((uint8_t *)&t23)[0] = 1;
        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t23 + 8), t22);
        memmove(gm_out, ((uint8_t *)&t23), 32);
        goto gm_fdone;
    }
    gm_b16_8 t24 = gm_l8;
    gm_l4 = t24;
    gm_b16_8 t25 = gm_l4;
    gm_th2lqbo77pvsc t26;
    GM_ST(uint8_t, (uint8_t *)&t26 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t26 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t26 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t26), t25);
    ((uint8_t *)&t26)[16] = 1;
    gm_tccklsmdasd9s t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
    ((uint8_t *)&t27)[0] = 0;
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t27 + 8), t26);
    memmove(gm_out, ((uint8_t *)&t27), 32);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vgf3305(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_vgf3305 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 174u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
            if (t6) {
                gm_l2 = ((uint8_t)1);
            } else {
                gm_b16_8 t7 = gm_l1;
                gm_b16_8 t8;
                GM_ST(void *, (uint8_t *)&t8, (void *)(gm_snenkhfev166s));
                GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(2));
                gm_b16_8 t9;
                GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sl3q52ep6aa53));
                GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(1));
                gm_b16_8 t10;
                GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sp7ec3ksfdilk));
                GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(4));
                gm_tfeab279c16rr t11;
                GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t8);
                GM_ST(gm_b16_8, ((uint8_t *)&t11 + 16), t9);
                GM_ST(gm_b16_8, ((uint8_t *)&t11 + 32), t10);
                uint8_t t12 = 0;
                if (gm_eq0iqiubfm7e(((uint8_t *)&t11) + 0, ((uint8_t *)&t7))) t12 = 1;
                if (gm_eq0iqiubfm7e(((uint8_t *)&t11) + 16, ((uint8_t *)&t7))) t12 = 1;
                if (gm_eq0iqiubfm7e(((uint8_t *)&t11) + 32, ((uint8_t *)&t7))) t12 = 1;
                uint8_t t13 = (uint8_t)(t12);
                gm_l2 = t13;
            }
            uint8_t t14 = gm_l2;
            if (t14) {
                uint8_t t15 = 0;
                ((struct gm_fr_is_subdir_sjdvkdc *)((&F.h)->up))->ret = ((uint8_t)0);
                gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t15;
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 270, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_hex_digit_q8s6utq(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_hex_digit_q8s6utq F;
    uint8_t gm_l0; /* c */
    uint8_t gm_l1; /* <tmp> */
    uint8_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 31u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    uint8_t gm_l5;
    uint8_t gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t t2 = gm_l0;
    gm_l1 = t2;
    uint8_t t3 = gm_l1;
    uint8_t t4 = (((uint8_t)48ULL) <= t3);
    if (t4) {
        uint8_t t5 = gm_l1;
        uint8_t t6 = (t5 <= ((uint8_t)57ULL));
        gm_l6 = t6;
    } else {
        gm_l6 = ((uint8_t)0);
    }
    uint8_t t7 = gm_l6;
    if (t7) {
        gm_l5 = ((uint8_t)1);
    } else {
        uint8_t t8 = gm_l0;
        gm_l2 = t8;
        uint8_t t9 = gm_l2;
        uint8_t t10 = (((uint8_t)97ULL) <= t9);
        if (t10) {
            uint8_t t11 = gm_l2;
            uint8_t t12 = (t11 <= ((uint8_t)102ULL));
            gm_l7 = t12;
        } else {
            gm_l7 = ((uint8_t)0);
        }
        uint8_t t13 = gm_l7;
        gm_l5 = t13;
    }
    uint8_t t14 = gm_l5;
    if (t14) {
        gm_l4 = ((uint8_t)1);
    } else {
        uint8_t t15 = gm_l0;
        gm_l3 = t15;
        uint8_t t16 = gm_l3;
        uint8_t t17 = (((uint8_t)65ULL) <= t16);
        if (t17) {
            uint8_t t18 = gm_l3;
            uint8_t t19 = (t18 <= ((uint8_t)70ULL));
            gm_l8 = t19;
        } else {
            gm_l8 = ((uint8_t)0);
        }
        uint8_t t20 = gm_l8;
        gm_l4 = t20;
    }
    uint8_t t21 = gm_l4;
    F.ret = t21;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_identifier_kvlcjjo(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_identifier_kvlcjjo F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* d */
    GM_STACK_CHECK(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
    if (t8) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t9 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t10, gm_srcg0ns322676oc, 153, 34);
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t9) + (uintptr_t)((uint64_t)0ULL) * 1));
    uint8_t t12 = gm_f_is_ident_start_rjefpfm(t11);
    uint8_t t13 = (uint8_t)!t12;
    if (t13) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t14 = gm_l0;
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
                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t18) + (uintptr_t)t20 * 1));
                        gm_l3 = t21;
                        uint8_t t22 = gm_l3;
                        uint8_t t23 = gm_f_is_ident_cont_317fpdb(t22);
                        uint8_t t24 = (uint8_t)!t23;
                        if (t24) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 154, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t25 = gm_l2;
            uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
            uint64_t t28 = r26;
            gm_l2 = t28;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_space_or_control_3uo6i3o(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_subdir_sjdvkdc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_subdir_sjdvkdc F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    GM_STACK_CHECK(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
    if (t4) {
        gm_l2 = ((uint8_t)1);
    } else {
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
        uint8_t t7 = gm_f_starts_with_6vvt01e(t5, t6);
        gm_l2 = t7;
    }
    uint8_t t8 = gm_l2;
    if (t8) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t9 = gm_l0;
    gm_l1 = t9;
    {
        gm_node wn10 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl5aqi4imlms, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t13 = gm_l1;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(1));
        gm_b0_1 t15 = gm_f_split_obtinjv_s7d377q((void *)NULL, (&wn10), NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_ll12;
        }
    }
    goto gm_ls16;
gm_ll12:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we11; }
    goto gm_lad;
gm_ls16:;
gm_lb1:;
we11:;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lint_rule_o3ccae1(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lint_rule_o3ccae1 F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 410u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toa5kh9gm9738 gm_l5;
    void * t7 = (void *)((uint8_t *)gm_c42ctv8im24);
    uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t9 = (uintptr_t)((uint64_t)23ULL);
    if (t8 > t9 || t9 > (uintptr_t)((uint64_t)23ULL)) gm_trap_slice(t8, t9, ((uint64_t)23ULL), gm_srcg0ns322676oc, 378, 27);
    uint64_t t10 = ((uintptr_t)((uint64_t)23ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t7 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
    gm_l1 = t11;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l2;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                uint64_t t15 = gm_l2;
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t18 = gm_l2;
                gm_tk9n3vlila9qg t19 = GM_LD(gm_tk9n3vlila9qg, (((uint8_t *)t16) + (uintptr_t)t18 * 40));
                gm_toa5kh9gm9738 t20;
                GM_ST(uint64_t, ((uint8_t *)&t20 + 0), t15);
                GM_ST(gm_tk9n3vlila9qg, ((uint8_t *)&t20 + 8), t19);
                gm_l5 = t20;
                {
                    {
                        uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
                        gm_l3 = t21;
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l5) + 8), 40);
                        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                        gm_b16_8 t23 = gm_l0;
                        uint8_t t24 = (gm_eq0iqiubfm7e(((uint8_t *)&t22), ((uint8_t *)&t23)));
                        if (t24) {
                            uint64_t t25 = gm_l3;
                            gm_tf5srh4mp4b3i t26;
                            GM_ST(uint8_t, (uint8_t *)&t26 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t26 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t26 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t26), t25);
                            ((uint8_t *)&t26)[8] = 1;
                            F.ret = t26;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 378, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t27 = gm_l2;
            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
            uint64_t t30 = r28;
            gm_l2 = t30;
        }
    }
gm_lb1:;
    gm_tf5srh4mp4b3i t31;
    memset(&t31, 0, sizeof t31);
    F.ret = t31;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_lower_ascii_uggh3ca(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lower_ascii_uggh3ca F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* out */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint8_t gm_l5; /* c */
    GM_STACK_CHECK(&F, 327u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l6;
    gm_tf5srh4mp4b3i gm_l7;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_f_make_2ip78bj((void *)&gm_l6, (void *)NULL, cx, NULL, t7, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ko53fduln7u)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcg0ns322676oc, 258, 12);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
    gm_l1 = t9;
    gm_b16_8 t10 = gm_l0;
    gm_l2 = t10;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t11 = gm_l3;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                uint64_t t14 = gm_l3;
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t17 = gm_l3;
                uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t15) + (uintptr_t)t17 * 1));
                gm_tf5srh4mp4b3i t19;
                GM_ST(uint8_t, (uint8_t *)&t19 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t14);
                GM_ST(uint8_t, ((uint8_t *)&t19 + 8), t18);
                gm_l7 = t19;
                {
                    {
                        uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                        gm_l4 = t20;
                        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 8));
                        gm_l5 = t21;
                        void * t22 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t24 = gm_l4;
                        if ((uintptr_t)t24 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t24, t23, gm_srcg0ns322676oc, 259, 32);
                        uint8_t t25 = gm_l5;
                        uint8_t t26 = gm_f_to_lower_0fu6efk(t25);
                        GM_ST(uint8_t, (((uint8_t *)t22) + (uintptr_t)t24 * 1), t26);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 259, 5);
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
    gm_b16_8 t31 = gm_l1;
    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
    F.ret = t32;
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
extern __attribute__((aligned(4))) void gm_f_mfail_uqq7o7g(void *gm_out, uint64_t a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf1bjg81u0glv gm_f_mfail_uqq7o7g_c(uint64_t a0, gm_b16_8 a1) { gm_tf1bjg81u0glv gm_r; gm_f_mfail_uqq7o7g(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_name_problem_6r4mp11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_name_problem_6r4mp11 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* what */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* k */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* k */
    GM_STACK_CHECK(&F, 1548u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tabb710d9mq2e gm_l11;
    uint8_t gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_b16_8 t12 = gm_l0;
    uint8_t t13 = gm_f_is_identifier_kvlcjjo(t12);
    uint8_t t14 = (uint8_t)!t13;
    if (t14) {
        gm_b16_8 t15 = gm_l1;
        gm_b16_8 t16 = gm_l0;
        gm_t6j6smj7qoctu t17;
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t15);
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t16);
        gm_f_format_rp4le08((void *)&gm_l8, (void *)NULL, cx, NULL, &t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcg0ns322676oc, 168, 16);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
        gm_th2lqbo77pvsc t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t21), t20);
        ((uint8_t *)&t21)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t21), 24);
        goto gm_fdone;
    }
    gm_b16_8 t22 = gm_l0;
    gm_b16_8 t23;
    GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sfdbfrv46lihd));
    GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
    uint8_t t24 = (gm_eq0iqiubfm7e(((uint8_t *)&t22), ((uint8_t *)&t23)));
    if (t24) {
        gm_b16_8 t25 = gm_l1;
        gm_tnpifv20irnph t26;
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
        gm_f_format_i3a6tth((void *)&gm_l9, (void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcg0ns322676oc, 170, 26);
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        gm_th2lqbo77pvsc t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t30), 24);
        goto gm_fdone;
    }
    void * t31 = (void *)((uint8_t *)gm_cka4oohkshv);
    uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t33 = (uintptr_t)((uint64_t)32ULL);
    if (t32 > t33 || t33 > (uintptr_t)((uint64_t)32ULL)) gm_trap_slice(t32, t33, ((uint64_t)32ULL), gm_srcg0ns322676oc, 171, 5);
    uint64_t t34 = ((uintptr_t)((uint64_t)32ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t35;
    GM_ST(void *, (uint8_t *)&t35, (void *)(((uint8_t *)t31 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(t34));
    gm_l2 = t35;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t36 = gm_l3;
            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t38 = (t36 < t37);
            if (!t38) {
                goto gm_lb1;
            }
            {
                void * t39 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t41 = gm_l3;
                {
                    {
                        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)t39) + (uintptr_t)t41 * 16));
                        gm_l4 = t42;
                        gm_b16_8 t43 = gm_l4;
                        gm_b16_8 t44 = gm_l0;
                        uint8_t t45 = (gm_eq0iqiubfm7e(((uint8_t *)&t43), ((uint8_t *)&t44)));
                        if (t45) {
                            gm_b16_8 t46 = gm_l1;
                            gm_b16_8 t47 = gm_l0;
                            gm_t6j6smj7qoctu t48;
                            GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t46);
                            GM_ST(gm_b16_8, ((uint8_t *)&t48 + 16), t47);
                            gm_f_format_usjmuj4((void *)&gm_l10, (void *)NULL, cx, NULL, &t48);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                            if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srcg0ns322676oc, 172, 28);
                            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                            gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                            gm_th2lqbo77pvsc t52;
                            GM_ST(uint8_t, (uint8_t *)&t52 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t52 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t52 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t52), t51);
                            ((uint8_t *)&t52)[16] = 1;
                            memmove(gm_out, ((uint8_t *)&t52), 24);
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 171, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t53 = gm_l3;
            uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
            uint64_t t56 = r54;
            gm_l3 = t56;
        }
    }
gm_lb1:;
    void * t57 = (void *)((uint8_t *)gm_cev3sfhfj2p);
    uint64_t t58 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t59 = (uintptr_t)((uint64_t)3ULL);
    if (t58 > t59 || t59 > (uintptr_t)((uint64_t)3ULL)) gm_trap_slice(t58, t59, ((uint64_t)3ULL), gm_srcg0ns322676oc, 174, 5);
    uint64_t t60 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t61;
    GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t57 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(t60));
    gm_l5 = t61;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t62 = gm_l6;
            uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t64 = (t62 < t63);
            if (!t64) {
                goto gm_lb6;
            }
            {
                void * t65 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t67 = gm_l6;
                {
                    {
                        gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)t65) + (uintptr_t)t67 * 16));
                        gm_l7 = t68;
                        gm_b16_8 t69 = gm_l7;
                        gm_b16_8 t70 = gm_l0;
                        uint8_t t71 = (gm_eq0iqiubfm7e(((uint8_t *)&t69), ((uint8_t *)&t70)));
                        if (t71) {
                            gm_b16_8 t72 = gm_l1;
                            gm_b16_8 t73 = gm_l0;
                            gm_t6j6smj7qoctu t74;
                            GM_ST(gm_b16_8, ((uint8_t *)&t74 + 0), t72);
                            GM_ST(gm_b16_8, ((uint8_t *)&t74 + 16), t73);
                            gm_f_format_usjmuj4((void *)&gm_l11, (void *)NULL, cx, NULL, &t74);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                            if (!t75) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcg0ns322676oc, 175, 28);
                            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                            gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                            gm_th2lqbo77pvsc t78;
                            GM_ST(uint8_t, (uint8_t *)&t78 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t78 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t78 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t78), t77);
                            ((uint8_t *)&t78)[16] = 1;
                            memmove(gm_out, ((uint8_t *)&t78), 24);
                            goto gm_fdone;
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 174, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t79 = gm_l6;
            uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
            uint64_t t82 = r80;
            gm_l6 = t82;
        }
    }
gm_lb6:;
    gm_b16_8 t83 = gm_l0;
    uint8_t t84 = gm_f_is_reserved_type_name_tuq4qsj(t83);
    if (t84) {
        gm_l12 = ((uint8_t)1);
    } else {
        gm_b16_8 t85 = gm_l0;
        gm_b16_8 t86;
        GM_ST(void *, (uint8_t *)&t86, (void *)(gm_s0iqiubfm7e7e));
        GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(6));
        gm_b16_8 t87;
        GM_ST(void *, (uint8_t *)&t87, (void *)(gm_s47llp68ptjgu));
        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(7));
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sflee3fpg0btv));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(4));
        gm_tfeab279c16rr t89;
        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t86);
        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 16), t87);
        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 32), t88);
        uint8_t t90 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t89) + 0, ((uint8_t *)&t85))) t90 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t89) + 16, ((uint8_t *)&t85))) t90 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t89) + 32, ((uint8_t *)&t85))) t90 = 1;
        uint8_t t91 = (uint8_t)(t90);
        gm_l12 = t91;
    }
    uint8_t t92 = gm_l12;
    if (t92) {
        gm_b16_8 t93 = gm_l1;
        gm_b16_8 t94 = gm_l0;
        gm_t6j6smj7qoctu t95;
        GM_ST(gm_b16_8, ((uint8_t *)&t95 + 0), t93);
        GM_ST(gm_b16_8, ((uint8_t *)&t95 + 16), t94);
        gm_f_format_mvlqlcp((void *)&gm_l13, (void *)NULL, cx, NULL, &t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
        if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcg0ns322676oc, 178, 16);
        gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
        gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
        gm_th2lqbo77pvsc t99;
        GM_ST(uint8_t, (uint8_t *)&t99 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t99 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t99 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t99), t98);
        ((uint8_t *)&t99)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t99), 24);
        goto gm_fdone;
    }
    gm_b16_8 t100 = gm_l0;
    gm_b16_8 t101;
    GM_ST(void *, (uint8_t *)&t101, (void *)(gm_s8sv6qdepk5ba));
    GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(3));
    gm_b16_8 t102;
    GM_ST(void *, (uint8_t *)&t102, (void *)(gm_st3p5nb1e6cc8));
    GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(4));
    gm_t6j6smj7qoctu t103;
    GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t101);
    GM_ST(gm_b16_8, ((uint8_t *)&t103 + 16), t102);
    uint8_t t104 = 0;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t103) + 0, ((uint8_t *)&t100))) t104 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t103) + 16, ((uint8_t *)&t100))) t104 = 1;
    uint8_t t105 = (uint8_t)(t104);
    if (t105) {
        gm_b16_8 t106 = gm_l1;
        gm_b16_8 t107 = gm_l0;
        gm_t6j6smj7qoctu t108;
        GM_ST(gm_b16_8, ((uint8_t *)&t108 + 0), t106);
        GM_ST(gm_b16_8, ((uint8_t *)&t108 + 16), t107);
        gm_f_format_gs0k8gk((void *)&gm_l14, (void *)NULL, cx, NULL, &t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcg0ns322676oc, 181, 16);
        gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
        gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
        gm_th2lqbo77pvsc t112;
        GM_ST(uint8_t, (uint8_t *)&t112 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t112 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t112 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t112), t111);
        ((uint8_t *)&t112)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t112), 24);
        goto gm_fdone;
    }
    gm_th2lqbo77pvsc t113;
    memset(&t113, 0, sizeof t113);
    memmove(gm_out, ((uint8_t *)&t113), 24);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_normalize_url_faghdja(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_normalize_url_faghdja F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* u */
    GM_STACK_CHECK_SHALLOW(&F, 427u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t8 = gm_l0;
    gm_b16_8 t9 = gm_f_trim_ascii_c9j36d1(t8);
    gm_l1 = t9;
    {
        for (;;) {
            gm_b16_8 t10 = gm_l1;
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_st3ja0nqi37bv));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(1));
            uint8_t t12 = gm_f_ends_with_biriu5l(t10, t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
                if (o17) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 239, 39);
                uint64_t t18 = r16;
                uint64_t t19 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t20 = (uintptr_t)t18;
                if (t19 > t20 || t20 > (uintptr_t)t14) gm_trap_slice(t19, t20, t14, gm_srcg0ns322676oc, 239, 34);
                uint64_t t21 = ((uintptr_t)t18) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(((uint8_t *)t13 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(t21));
                gm_l1 = t22;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_b16_8 t23 = gm_l1;
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sp7ec3ksfdilk));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(4));
    uint8_t t25 = gm_f_ends_with_biriu5l(t23, t24);
    if (t25) {
        void * t26 = GM_LD(void *, ((uint8_t *)&gm_l1));
        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t r29; bool o30 = __builtin_sub_overflow(t28, ((uint64_t)4ULL), &r29);
        if (o30) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 241, 18);
        uint64_t t31 = r29;
        uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t33 = (uintptr_t)t31;
        if (t32 > t33 || t33 > (uintptr_t)t27) gm_trap_slice(t32, t33, t27, gm_srcg0ns322676oc, 241, 13);
        uint64_t t34 = ((uintptr_t)t31) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(((uint8_t *)t26 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(t34));
        gm_l1 = t35;
        {
            for (;;) {
                gm_b16_8 t36 = gm_l1;
                gm_b16_8 t37;
                GM_ST(void *, (uint8_t *)&t37, (void *)(gm_st3ja0nqi37bv));
                GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(1));
                uint8_t t38 = gm_f_ends_with_biriu5l(t36, t37);
                if (!t38) {
                    goto gm_lb4;
                }
                {
                    void * t39 = GM_LD(void *, ((uint8_t *)&gm_l1));
                    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                    uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                    uint64_t r42; bool o43 = __builtin_sub_overflow(t41, ((uint64_t)1ULL), &r42);
                    if (o43) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 242, 43);
                    uint64_t t44 = r42;
                    uint64_t t45 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t46 = (uintptr_t)t44;
                    if (t45 > t46 || t46 > (uintptr_t)t40) gm_trap_slice(t45, t46, t40, gm_srcg0ns322676oc, 242, 38);
                    uint64_t t47 = ((uintptr_t)t44) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t48;
                    GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t39 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47));
                    gm_l1 = t48;
                }
gm_lb6:;
            }
        }
gm_lb4:;
    }
    gm_b16_8 t49 = gm_l1;
    F.ret = t49;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_overlay_pjf5fej(void *gm_out, const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_overlay_pjf5fej F;
    GM_STACK_CHECK_SHALLOW(&F, 464u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_taari1s0m51a7 gm_l2;
    gm_tqs5p4n2tq19v gm_l3;
    gm_th2lqbo77pvsc gm_l4;
    gm_th2lqbo77pvsc gm_l5;
    gm_th2lqbo77pvsc gm_l6;
    gm_th2lqbo77pvsc gm_l7;
    gm_th2lqbo77pvsc gm_l8;
    gm_th2lqbo77pvsc gm_l9;
    uint8_t t2 = (uint8_t)((((uint8_t *)a1))[1] != 0);
    if (t2) {
        gm_taari1s0m51a7 t3 = GM_LD(gm_taari1s0m51a7, ((uint8_t *)a1));
        gm_l2 = t3;
    } else {
        gm_taari1s0m51a7 t4 = GM_LD(gm_taari1s0m51a7, ((uint8_t *)a0));
        gm_l2 = t4;
    }
    gm_taari1s0m51a7 t5 = gm_l2;
    uint8_t t6 = (uint8_t)(((((uint8_t *)a1) + 2))[1] != 0);
    if (t6) {
        gm_tqs5p4n2tq19v t7 = GM_LD(gm_tqs5p4n2tq19v, (((uint8_t *)a1) + 2));
        gm_l3 = t7;
    } else {
        gm_tqs5p4n2tq19v t8 = GM_LD(gm_tqs5p4n2tq19v, (((uint8_t *)a0) + 2));
        gm_l3 = t8;
    }
    gm_tqs5p4n2tq19v t9 = gm_l3;
    uint8_t t10 = (uint8_t)(((((uint8_t *)a1) + 8))[16] != 0);
    if (t10) {
        memmove(((uint8_t *)&gm_l4), (((uint8_t *)a1) + 8), 24);
    } else {
        memmove(((uint8_t *)&gm_l4), (((uint8_t *)a0) + 8), 24);
    }
    uint8_t t11 = (uint8_t)(((((uint8_t *)a1) + 32))[16] != 0);
    gm_th2lqbo77pvsc t12 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l4));
    if (t11) {
        memmove(((uint8_t *)&gm_l5), (((uint8_t *)a1) + 32), 24);
    } else {
        memmove(((uint8_t *)&gm_l5), (((uint8_t *)a0) + 32), 24);
    }
    uint8_t t13 = (uint8_t)(((((uint8_t *)a1) + 56))[16] != 0);
    gm_th2lqbo77pvsc t14 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l5));
    if (t13) {
        memmove(((uint8_t *)&gm_l6), (((uint8_t *)a1) + 56), 24);
    } else {
        memmove(((uint8_t *)&gm_l6), (((uint8_t *)a0) + 56), 24);
    }
    uint8_t t15 = (uint8_t)(((((uint8_t *)a1) + 80))[16] != 0);
    gm_th2lqbo77pvsc t16 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l6));
    if (t15) {
        memmove(((uint8_t *)&gm_l7), (((uint8_t *)a1) + 80), 24);
    } else {
        memmove(((uint8_t *)&gm_l7), (((uint8_t *)a0) + 80), 24);
    }
    uint8_t t17 = (uint8_t)(((((uint8_t *)a1) + 104))[16] != 0);
    gm_th2lqbo77pvsc t18 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l7));
    if (t17) {
        memmove(((uint8_t *)&gm_l8), (((uint8_t *)a1) + 104), 24);
    } else {
        memmove(((uint8_t *)&gm_l8), (((uint8_t *)a0) + 104), 24);
    }
    uint8_t t19 = (uint8_t)(((((uint8_t *)a1) + 128))[16] != 0);
    gm_th2lqbo77pvsc t20 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l8));
    if (t19) {
        memmove(((uint8_t *)&gm_l9), (((uint8_t *)a1) + 128), 24);
    } else {
        memmove(((uint8_t *)&gm_l9), (((uint8_t *)a0) + 128), 24);
    }
    gm_th2lqbo77pvsc t21 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l9));
    gm_ti0g3bmheuq9o t22;
    GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
    GM_ST(gm_taari1s0m51a7, ((uint8_t *)&t22 + 0), t5);
    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t22 + 2), t9);
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t22 + 8), t12);
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t22 + 32), t14);
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t22 + 56), t16);
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t22 + 80), t18);
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t22 + 104), t20);
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t22 + 128), t21);
    memmove(gm_out, ((uint8_t *)&t22), 152);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_ti0g3bmheuq9o gm_f_overlay_pjf5fej_c(gm_ti0g3bmheuq9o a0, gm_ti0g3bmheuq9o a1) { gm_ti0g3bmheuq9o gm_r; gm_f_overlay_pjf5fej(&gm_r, &a0, &a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_parse_build_c2teosa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_parse_build_c2teosa F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* what */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l6; /* line */
    gm_b16_8 gm_l7; /* k */
    uint8_t gm_l8; /* known */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l11; /* x */
    gm_b16_8 gm_l12; /* s */
    uint8_t gm_l13; /* v */
    gm_b16_8 gm_l14; /* l */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* p */
    gm_b16_8 gm_l18; /* l */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l21; /* n */
    uint8_t gm_l22; /* bad */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint8_t gm_l25; /* c */
    GM_STACK_CHECK(&F, 5452u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l26;
    gm_tabb710d9mq2e gm_l27;
    gm_th2lqbo77pvsc gm_l28;
    gm_b16_8 gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_taari1s0m51a7 gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_tptmds653bni0 gm_l34;
    gm_tptmds653bni0 gm_l35;
    gm_tptmds653bni0 gm_l36;
    gm_tabb710d9mq2e gm_l37;
    gm_tptmds653bni0 gm_l38;
    uint8_t gm_l39;
    uint8_t gm_l40;
    gm_tabb710d9mq2e gm_l41;
    {
        gm_taari1s0m51a7 t50;
        memset(&t50, 0, sizeof t50);
        gm_tqs5p4n2tq19v t51;
        memset(&t51, 0, sizeof t51);
        gm_th2lqbo77pvsc t52;
        memset(&t52, 0, sizeof t52);
        gm_th2lqbo77pvsc t53;
        memset(&t53, 0, sizeof t53);
        gm_th2lqbo77pvsc t54;
        memset(&t54, 0, sizeof t54);
        gm_th2lqbo77pvsc t55;
        memset(&t55, 0, sizeof t55);
        gm_th2lqbo77pvsc t56;
        memset(&t56, 0, sizeof t56);
        gm_th2lqbo77pvsc t57;
        memset(&t57, 0, sizeof t57);
        gm_ti0g3bmheuq9o t58;
        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
        GM_ST(gm_taari1s0m51a7, ((uint8_t *)&t58 + 0), t50);
        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t58 + 2), t51);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t58 + 8), t52);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t58 + 32), t53);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t58 + 56), t54);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t58 + 80), t55);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t58 + 104), t56);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t58 + 128), t57);
        F.l2 = t58;
        void * t59 = gm_l0;
        gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)t59));
        gm_l3 = t60;
        gm_l4 = ((uint64_t)0ULL);
        {
            for (;;) {
                {
                    uint64_t t61 = gm_l4;
                    uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t63 = (t61 < t62);
                    if (!t63) {
                        goto gm_lb1;
                    }
                    {
                        {
                            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t66 = gm_l4;
                            {
                                {
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l5), (((uint8_t *)t64) + (uintptr_t)t66 * 72), 72);
                                            uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 64));
                                            gm_l6 = t67;
                                            gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                                            gm_l7 = t68;
                                            gm_l8 = ((uint8_t)0);
                                            void * t69 = (void *)((uint8_t *)gm_cfcggcra0bi);
                                            uint64_t t70 = (uintptr_t)((uint64_t)0ULL);
                                            uint64_t t71 = (uintptr_t)((uint64_t)8ULL);
                                            if (t70 > t71 || t71 > (uintptr_t)((uint64_t)8ULL)) gm_trap_slice(t70, t71, ((uint64_t)8ULL), gm_srcg0ns322676oc, 418, 9);
                                            uint64_t t72 = ((uintptr_t)((uint64_t)8ULL)) - (uintptr_t)((uint64_t)0ULL);
                                            gm_b16_8 t73;
                                            GM_ST(void *, (uint8_t *)&t73, (void *)(((uint8_t *)t69 + (uintptr_t)((uint64_t)0ULL) * 16)));
                                            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(t72));
                                            gm_l9 = t73;
                                            gm_l10 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t74 = gm_l10;
                                                    uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                                    uint8_t t76 = (t74 < t75);
                                                    if (!t76) {
                                                        goto gm_lb6;
                                                    }
                                                    {
                                                        void * t77 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                                        uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                                        uint64_t t79 = gm_l10;
                                                        {
                                                            {
                                                                gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)t77) + (uintptr_t)t79 * 16));
                                                                gm_l11 = t80;
                                                                gm_b16_8 t81 = gm_l11;
                                                                gm_b16_8 t82 = gm_l7;
                                                                uint8_t t83 = (gm_eq0iqiubfm7e(((uint8_t *)&t81), ((uint8_t *)&t82)));
                                                                if (t83) {
                                                                    gm_l8 = ((uint8_t)1);
                                                                }
                                                                goto gm_lb9;
                                                            }
gm_lb10:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 418, 9);
                                                        }
gm_lb9:;
                                                    }
gm_lb8:;
                                                    uint64_t t84 = gm_l10;
                                                    uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                                                    uint64_t t87 = r85;
                                                    gm_l10 = t87;
                                                }
                                            }
gm_lb6:;
                                            uint8_t t88 = gm_l8;
                                            uint8_t t89 = (uint8_t)!t88;
                                            if (t89) {
                                                uint64_t t90 = gm_l6;
                                                gm_b16_8 t91 = gm_l1;
                                                gm_b16_8 t92 = gm_l7;
                                                void * t93 = (void *)((uint8_t *)gm_cfcggcra0bi);
                                                uint64_t t94 = (uintptr_t)((uint64_t)0ULL);
                                                uint64_t t95 = (uintptr_t)((uint64_t)8ULL);
                                                if (t94 > t95 || t95 > (uintptr_t)((uint64_t)8ULL)) gm_trap_slice(t94, t95, ((uint64_t)8ULL), gm_srcg0ns322676oc, 422, 109);
                                                uint64_t t96 = ((uintptr_t)((uint64_t)8ULL)) - (uintptr_t)((uint64_t)0ULL);
                                                gm_b16_8 t97;
                                                GM_ST(void *, (uint8_t *)&t97, (void *)(((uint8_t *)t93 + (uintptr_t)((uint64_t)0ULL) * 16)));
                                                GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(t96));
                                                gm_b16_8 t98;
                                                GM_ST(void *, (uint8_t *)&t98, (void *)(gm_sg5gdnrith2i6));
                                                GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(2));
                                                gm_f_join_sfknljb((void *)&gm_l26, (void *)NULL, cx, NULL, t97, t98);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                                if (!t99) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_srcg0ns322676oc, 422, 100);
                                                gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                                                gm_tfeab279c16rr t101;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t101 + 0), t91);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t101 + 16), t92);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t101 + 32), t100);
                                                gm_f_format_23qvjoj((void *)&gm_l27, (void *)NULL, cx, NULL, &t101);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                                if (!t102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_srcg0ns322676oc, 422, 33);
                                                gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
                                                gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                                                gm_tf1bjg81u0glv t105;
                                                gm_f_mfail_uqq7o7g((void *)&t105, t90, t104);
                                                gm_tacj1cskem694 t106;
                                                memset(&t106, 0, sizeof t106);
                                                ((uint8_t *)&t106)[0] = 1;
                                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t106 + 8), t105);
                                                memmove(gm_out, ((uint8_t *)&t106), 160);
                                                goto gm_fdone;
                                            }
                                            {
                                                {
                                                    {
                                                        gm_b16_8 t107 = gm_l7;
                                                        gm_b16_8 t108;
                                                        GM_ST(void *, (uint8_t *)&t108, (void *)(gm_sgdafdv2n5c25));
                                                        GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(4));
                                                        uint8_t t109 = (gm_eq0iqiubfm7e(((uint8_t *)&t107), ((uint8_t *)&t108)));
                                                        if (!t109) {
                                                            goto gm_lb12;
                                                        }
                                                        gm_tktr4rmbindgv t110 = GM_LD(gm_tktr4rmbindgv, (((uint8_t *)&F.l5) + 16));
                                                        gm_f_as_str_982o16v((void *)&gm_l28, &t110);
                                                        uint8_t t111 = (uint8_t)((((uint8_t *)&gm_l28))[16] != 0);
                                                        if (t111) {
                                                            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l28));
                                                            gm_l29 = t112;
                                                        } else {
                                                            uint64_t t113 = gm_l6;
                                                            gm_b16_8 t114 = gm_l1;
                                                            gm_tnpifv20irnph t115;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t115 + 0), t114);
                                                            gm_f_format_4eslq57((void *)&gm_l30, (void *)NULL, cx, NULL, t115);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint8_t t116 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                                                            if (!t116) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srcg0ns322676oc, 426, 63);
                                                            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                                                            gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                                                            gm_tf1bjg81u0glv t119;
                                                            gm_f_mfail_uqq7o7g((void *)&t119, t113, t118);
                                                            gm_tacj1cskem694 t120;
                                                            memset(&t120, 0, sizeof t120);
                                                            ((uint8_t *)&t120)[0] = 1;
                                                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t120 + 8), t119);
                                                            memmove(gm_out, ((uint8_t *)&t120), 160);
                                                            goto gm_fdone;
                                                        }
                                                        gm_b16_8 t121 = gm_l29;
                                                        gm_l12 = t121;
                                                        {
                                                            {
                                                                gm_b16_8 t122 = gm_l12;
                                                                gm_b16_8 t123;
                                                                GM_ST(void *, (uint8_t *)&t123, (void *)(gm_shl9ceh0q5tpe));
                                                                GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(5));
                                                                uint8_t t124 = (gm_eq0iqiubfm7e(((uint8_t *)&t122), ((uint8_t *)&t123)));
                                                                if (!t124) {
                                                                    goto gm_lb14;
                                                                }
                                                                gm_t9qkh0rsooc8d t125;
                                                                GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)0ULL));
                                                                gm_taari1s0m51a7 t126;
                                                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t126), t125);
                                                                ((uint8_t *)&t126)[1] = 1;
                                                                gm_l31 = t126;
                                                                goto gm_lb13;
                                                            }
gm_lb14:;
                                                            {
                                                                gm_b16_8 t127 = gm_l12;
                                                                gm_b16_8 t128;
                                                                GM_ST(void *, (uint8_t *)&t128, (void *)(gm_s02419ud122hi));
                                                                GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(7));
                                                                uint8_t t129 = (gm_eq0iqiubfm7e(((uint8_t *)&t127), ((uint8_t *)&t128)));
                                                                if (!t129) {
                                                                    goto gm_lb15;
                                                                }
                                                                gm_t9qkh0rsooc8d t130;
                                                                GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)1ULL));
                                                                gm_taari1s0m51a7 t131;
                                                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t131), t130);
                                                                ((uint8_t *)&t131)[1] = 1;
                                                                gm_l31 = t131;
                                                                goto gm_lb13;
                                                            }
gm_lb15:;
                                                            {
                                                                gm_b16_8 t132 = gm_l12;
                                                                gm_b16_8 t133;
                                                                GM_ST(void *, (uint8_t *)&t133, (void *)(gm_s6iuue4hdrbt9));
                                                                GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(4));
                                                                uint8_t t134 = (gm_eq0iqiubfm7e(((uint8_t *)&t132), ((uint8_t *)&t133)));
                                                                if (!t134) {
                                                                    goto gm_lb16;
                                                                }
                                                                gm_t9qkh0rsooc8d t135;
                                                                GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)2ULL));
                                                                gm_taari1s0m51a7 t136;
                                                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t136), t135);
                                                                ((uint8_t *)&t136)[1] = 1;
                                                                gm_l31 = t136;
                                                                goto gm_lb13;
                                                            }
gm_lb16:;
                                                            {
                                                                gm_b16_8 t137 = gm_l12;
                                                                gm_b16_8 t138;
                                                                GM_ST(void *, (uint8_t *)&t138, (void *)(gm_sl0oflhpdomi7));
                                                                GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(4));
                                                                uint8_t t139 = (gm_eq0iqiubfm7e(((uint8_t *)&t137), ((uint8_t *)&t138)));
                                                                if (!t139) {
                                                                    goto gm_lb17;
                                                                }
                                                                gm_t9qkh0rsooc8d t140;
                                                                GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)3ULL));
                                                                gm_taari1s0m51a7 t141;
                                                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t141), t140);
                                                                ((uint8_t *)&t141)[1] = 1;
                                                                gm_l31 = t141;
                                                                goto gm_lb13;
                                                            }
gm_lb17:;
                                                            {
                                                                uint64_t t142 = gm_l6;
                                                                gm_b16_8 t143 = gm_l12;
                                                                gm_b16_8 t144 = gm_l1;
                                                                gm_t6j6smj7qoctu t145;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t145 + 0), t143);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t145 + 16), t144);
                                                                gm_f_format_mcrh28a((void *)&gm_l32, (void *)NULL, cx, NULL, &t145);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                uint8_t t146 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                                                if (!t146) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_srcg0ns322676oc, 432, 46);
                                                                gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                                                                gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&t147));
                                                                gm_tf1bjg81u0glv t149;
                                                                gm_f_mfail_uqq7o7g((void *)&t149, t142, t148);
                                                                gm_tacj1cskem694 t150;
                                                                memset(&t150, 0, sizeof t150);
                                                                ((uint8_t *)&t150)[0] = 1;
                                                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t150 + 8), t149);
                                                                memmove(gm_out, ((uint8_t *)&t150), 160);
                                                                goto gm_fdone;
                                                            }
gm_lb18:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 427, 26);
                                                        }
gm_lb13:;
                                                        gm_taari1s0m51a7 t151 = gm_l31;
                                                        GM_ST(gm_taari1s0m51a7, ((uint8_t *)&F.l2), t151);
                                                        goto gm_lb11;
                                                    }
gm_lb12:;
                                                }
                                                {
                                                    {
                                                        gm_b16_8 t152 = gm_l7;
                                                        gm_b16_8 t153;
                                                        GM_ST(void *, (uint8_t *)&t153, (void *)(gm_sq6e9d3107t3s));
                                                        GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(10));
                                                        uint8_t t154 = (gm_eq0iqiubfm7e(((uint8_t *)&t152), ((uint8_t *)&t153)));
                                                        if (!t154) {
                                                            goto gm_lb19;
                                                        }
                                                        {
                                                            {
                                                                uint8_t t155 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 16) + 0));
                                                                uint8_t t156 = (uint8_t)(t155 == ((uint8_t)2ULL));
                                                                if (!t156) {
                                                                    goto gm_lb21;
                                                                }
                                                                uint8_t t157 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 16) + 8));
                                                                gm_l13 = t157;
                                                                uint8_t t158 = gm_l13;
                                                                gm_tqs5p4n2tq19v t159;
                                                                GM_ST(uint8_t, ((uint8_t *)&t159), t158);
                                                                ((uint8_t *)&t159)[1] = 1;
                                                                GM_ST(gm_tqs5p4n2tq19v, (((uint8_t *)&F.l2) + 2), t159);
                                                                goto gm_lb20;
                                                            }
gm_lb21:;
                                                            uint64_t t160 = gm_l6;
                                                            gm_b16_8 t161 = gm_l1;
                                                            gm_tnpifv20irnph t162;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t162 + 0), t161);
                                                            gm_f_format_6cnv7j9((void *)&gm_l33, (void *)NULL, cx, NULL, t162);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint8_t t163 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                                            if (!t163) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srcg0ns322676oc, 436, 62);
                                                            gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                                                            gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
                                                            gm_tf1bjg81u0glv t166;
                                                            gm_f_mfail_uqq7o7g((void *)&t166, t160, t165);
                                                            gm_tacj1cskem694 t167;
                                                            memset(&t167, 0, sizeof t167);
                                                            ((uint8_t *)&t167)[0] = 1;
                                                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t167 + 8), t166);
                                                            memmove(gm_out, ((uint8_t *)&t167), 160);
                                                            goto gm_fdone;
                                                        }
gm_lb20:;
                                                        goto gm_lb11;
                                                    }
gm_lb19:;
                                                    {
                                                        gm_b16_8 t168 = gm_l7;
                                                        gm_b16_8 t169;
                                                        GM_ST(void *, (uint8_t *)&t169, (void *)(gm_sp6f2rgoi8toe));
                                                        GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(6));
                                                        uint8_t t170 = (gm_eq0iqiubfm7e(((uint8_t *)&t168), ((uint8_t *)&t169)));
                                                        if (!t170) {
                                                            goto gm_lb22;
                                                        }
                                                        gm_b16_8 t171 = gm_l1;
                                                        gm_trc3jdv2g5qre t172 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)&F.l5));
                                                        gm_f_build_list_f41mb8u((void *)&gm_l34, (void *)NULL, cx, NULL, &t172, t171);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t173 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                                        if (t173) {
                                                        } else {
                                                            gm_tf1bjg81u0glv t174 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l34) + 8));
                                                            gm_tacj1cskem694 t175;
                                                            memset(&t175, 0, sizeof t175);
                                                            ((uint8_t *)&t175)[0] = 1;
                                                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t175 + 8), t174);
                                                            memmove(gm_out, ((uint8_t *)&t175), 160);
                                                            goto gm_fdone;
                                                        }
                                                        gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                                                        gm_th2lqbo77pvsc t177;
                                                        GM_ST(uint8_t, (uint8_t *)&t177 + 17, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t177 + 18, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t177 + 20, 0);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t177), t176);
                                                        ((uint8_t *)&t177)[16] = 1;
                                                        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 8), t177);
                                                        goto gm_lb11;
                                                    }
gm_lb22:;
                                                    {
                                                        gm_b16_8 t178 = gm_l7;
                                                        gm_b16_8 t179;
                                                        GM_ST(void *, (uint8_t *)&t179, (void *)(gm_slfpvqm0f02j6));
                                                        GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(7));
                                                        uint8_t t180 = (gm_eq0iqiubfm7e(((uint8_t *)&t178), ((uint8_t *)&t179)));
                                                        if (!t180) {
                                                            goto gm_lb23;
                                                        }
                                                        gm_b16_8 t181 = gm_l1;
                                                        gm_trc3jdv2g5qre t182 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)&F.l5));
                                                        gm_f_build_list_f41mb8u((void *)&gm_l35, (void *)NULL, cx, NULL, &t182, t181);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t183 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                                        if (t183) {
                                                        } else {
                                                            gm_tf1bjg81u0glv t184 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l35) + 8));
                                                            gm_tacj1cskem694 t185;
                                                            memset(&t185, 0, sizeof t185);
                                                            ((uint8_t *)&t185)[0] = 1;
                                                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t185 + 8), t184);
                                                            memmove(gm_out, ((uint8_t *)&t185), 160);
                                                            goto gm_fdone;
                                                        }
                                                        gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                                        gm_th2lqbo77pvsc t187;
                                                        GM_ST(uint8_t, (uint8_t *)&t187 + 17, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t187 + 18, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t187 + 20, 0);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t187), t186);
                                                        ((uint8_t *)&t187)[16] = 1;
                                                        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 32), t187);
                                                        goto gm_lb11;
                                                    }
gm_lb23:;
                                                }
                                                {
                                                    {
                                                        {
                                                            {
                                                                gm_b16_8 t188 = gm_l7;
                                                                gm_b16_8 t189;
                                                                GM_ST(void *, (uint8_t *)&t189, (void *)(gm_srcf8l485c5uq));
                                                                GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)(9));
                                                                uint8_t t190 = (gm_eq0iqiubfm7e(((uint8_t *)&t188), ((uint8_t *)&t189)));
                                                                if (!t190) {
                                                                    goto gm_lb26;
                                                                }
                                                                goto gm_lb25;
                                                            }
gm_lb26:;
                                                            {
                                                                gm_b16_8 t191 = gm_l7;
                                                                gm_b16_8 t192;
                                                                GM_ST(void *, (uint8_t *)&t192, (void *)(gm_sdbqk29a3n8jm));
                                                                GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(12));
                                                                uint8_t t193 = (gm_eq0iqiubfm7e(((uint8_t *)&t191), ((uint8_t *)&t192)));
                                                                if (!t193) {
                                                                    goto gm_lb27;
                                                                }
                                                                goto gm_lb25;
                                                            }
gm_lb27:;
                                                            {
                                                                gm_b16_8 t194 = gm_l7;
                                                                gm_b16_8 t195;
                                                                GM_ST(void *, (uint8_t *)&t195, (void *)(gm_svkmjs6p8bhtg));
                                                                GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(8));
                                                                uint8_t t196 = (gm_eq0iqiubfm7e(((uint8_t *)&t194), ((uint8_t *)&t195)));
                                                                if (!t196) {
                                                                    goto gm_lb28;
                                                                }
                                                                goto gm_lb25;
                                                            }
gm_lb28:;
                                                            goto gm_lb24;
                                                        }
gm_lb25:;
                                                        gm_b16_8 t197 = gm_l1;
                                                        gm_trc3jdv2g5qre t198 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)&F.l5));
                                                        gm_f_build_list_f41mb8u((void *)&gm_l36, (void *)NULL, cx, NULL, &t198, t197);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t199 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                                        if (t199) {
                                                        } else {
                                                            gm_tf1bjg81u0glv t200 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l36) + 8));
                                                            gm_tacj1cskem694 t201;
                                                            memset(&t201, 0, sizeof t201);
                                                            ((uint8_t *)&t201)[0] = 1;
                                                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t201 + 8), t200);
                                                            memmove(gm_out, ((uint8_t *)&t201), 160);
                                                            goto gm_fdone;
                                                        }
                                                        gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                                                        gm_l14 = t202;
                                                        gm_b16_8 t203 = gm_l14;
                                                        gm_l15 = t203;
                                                        gm_l16 = ((uint64_t)0ULL);
                                                        {
                                                            for (;;) {
                                                                uint64_t t204 = gm_l16;
                                                                uint64_t t205 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                                uint8_t t206 = (t204 < t205);
                                                                if (!t206) {
                                                                    goto gm_lb29;
                                                                }
                                                                {
                                                                    void * t207 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                                                    uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                                    uint64_t t209 = gm_l16;
                                                                    {
                                                                        {
                                                                            gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)t207) + (uintptr_t)t209 * 16));
                                                                            gm_l17 = t210;
                                                                            uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                                                            uint8_t t212 = ((t211 == ((uint64_t)0ULL)));
                                                                            if (t212) {
                                                                                uint64_t t213 = gm_l6;
                                                                                gm_b16_8 t214 = gm_l7;
                                                                                gm_b16_8 t215 = gm_l1;
                                                                                gm_t6j6smj7qoctu t216;
                                                                                GM_ST(gm_b16_8, ((uint8_t *)&t216 + 0), t214);
                                                                                GM_ST(gm_b16_8, ((uint8_t *)&t216 + 16), t215);
                                                                                gm_f_format_6ld92gp((void *)&gm_l37, (void *)NULL, cx, NULL, &t216);
                                                                                if (gm_unw.kind) {
                                                                                    goto gm_lad;
                                                                                }
                                                                                uint8_t t217 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                                                                if (!t217) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_srcg0ns322676oc, 444, 57);
                                                                                gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                                                                                gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&t218));
                                                                                gm_tf1bjg81u0glv t220;
                                                                                gm_f_mfail_uqq7o7g((void *)&t220, t213, t219);
                                                                                gm_tacj1cskem694 t221;
                                                                                memset(&t221, 0, sizeof t221);
                                                                                ((uint8_t *)&t221)[0] = 1;
                                                                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t221 + 8), t220);
                                                                                memmove(gm_out, ((uint8_t *)&t221), 160);
                                                                                goto gm_fdone;
                                                                            }
                                                                            goto gm_lb32;
                                                                        }
gm_lb33:;
                                                                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 443, 17);
                                                                    }
gm_lb32:;
                                                                }
gm_lb31:;
                                                                uint64_t t222 = gm_l16;
                                                                uint64_t r223; bool o224 = __builtin_add_overflow(t222, ((uint64_t)1ULL), &r223);
                                                                uint64_t t225 = r223;
                                                                gm_l16 = t225;
                                                            }
                                                        }
gm_lb29:;
                                                        {
                                                            {
                                                                gm_b16_8 t226 = gm_l7;
                                                                gm_b16_8 t227;
                                                                GM_ST(void *, (uint8_t *)&t227, (void *)(gm_srcf8l485c5uq));
                                                                GM_ST(uintptr_t, (uint8_t *)&t227 + 8, (uintptr_t)(9));
                                                                uint8_t t228 = (gm_eq0iqiubfm7e(((uint8_t *)&t226), ((uint8_t *)&t227)));
                                                                if (!t228) {
                                                                    goto gm_lb35;
                                                                }
                                                                gm_b16_8 t229 = gm_l14;
                                                                gm_th2lqbo77pvsc t230;
                                                                GM_ST(uint8_t, (uint8_t *)&t230 + 17, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&t230 + 18, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&t230 + 20, 0);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t230), t229);
                                                                ((uint8_t *)&t230)[16] = 1;
                                                                GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 56), t230);
                                                                goto gm_lb34;
                                                            }
gm_lb35:;
                                                            {
                                                                gm_b16_8 t231 = gm_l7;
                                                                gm_b16_8 t232;
                                                                GM_ST(void *, (uint8_t *)&t232, (void *)(gm_sdbqk29a3n8jm));
                                                                GM_ST(uintptr_t, (uint8_t *)&t232 + 8, (uintptr_t)(12));
                                                                uint8_t t233 = (gm_eq0iqiubfm7e(((uint8_t *)&t231), ((uint8_t *)&t232)));
                                                                if (!t233) {
                                                                    goto gm_lb36;
                                                                }
                                                                gm_b16_8 t234 = gm_l14;
                                                                gm_th2lqbo77pvsc t235;
                                                                GM_ST(uint8_t, (uint8_t *)&t235 + 17, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&t235 + 18, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&t235 + 20, 0);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t235), t234);
                                                                ((uint8_t *)&t235)[16] = 1;
                                                                GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 80), t235);
                                                                goto gm_lb34;
                                                            }
gm_lb36:;
                                                            {
                                                                gm_b16_8 t236 = gm_l14;
                                                                gm_th2lqbo77pvsc t237;
                                                                GM_ST(uint8_t, (uint8_t *)&t237 + 17, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&t237 + 18, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&t237 + 20, 0);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t237), t236);
                                                                ((uint8_t *)&t237)[16] = 1;
                                                                GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 128), t237);
                                                                goto gm_lb34;
                                                            }
gm_lb37:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 446, 17);
                                                        }
gm_lb34:;
                                                        goto gm_lb11;
                                                    }
gm_lb24:;
                                                }
                                                {
                                                    {
                                                        gm_b16_8 t238 = gm_l1;
                                                        gm_trc3jdv2g5qre t239 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)&F.l5));
                                                        gm_f_build_list_f41mb8u((void *)&gm_l38, (void *)NULL, cx, NULL, &t239, t238);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t240 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                                        if (t240) {
                                                        } else {
                                                            gm_tf1bjg81u0glv t241 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l38) + 8));
                                                            gm_tacj1cskem694 t242;
                                                            memset(&t242, 0, sizeof t242);
                                                            ((uint8_t *)&t242)[0] = 1;
                                                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t242 + 8), t241);
                                                            memmove(gm_out, ((uint8_t *)&t242), 160);
                                                            goto gm_fdone;
                                                        }
                                                        gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                                                        gm_l18 = t243;
                                                        gm_b16_8 t244 = gm_l18;
                                                        gm_l19 = t244;
                                                        gm_l20 = ((uint64_t)0ULL);
                                                        {
                                                            for (;;) {
                                                                uint64_t t245 = gm_l20;
                                                                uint64_t t246 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                                                uint8_t t247 = (t245 < t246);
                                                                if (!t247) {
                                                                    goto gm_lb39;
                                                                }
                                                                {
                                                                    void * t248 = GM_LD(void *, ((uint8_t *)&gm_l19));
                                                                    uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                                                    uint64_t t250 = gm_l20;
                                                                    {
                                                                        {
                                                                            gm_b16_8 t251 = GM_LD(gm_b16_8, (((uint8_t *)t248) + (uintptr_t)t250 * 16));
                                                                            gm_l21 = t251;
                                                                            uint64_t t252 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                                                            uint8_t t253 = ((t252 == ((uint64_t)0ULL)));
                                                                            if (t253) {
                                                                                gm_l39 = ((uint8_t)1);
                                                                            } else {
                                                                                gm_b16_8 t254 = gm_l21;
                                                                                gm_b16_8 t255;
                                                                                GM_ST(void *, (uint8_t *)&t255, (void *)(gm_skolmnfc0ccbd));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t255 + 8, (uintptr_t)(1));
                                                                                uint8_t t256 = gm_f_starts_with_6vvt01e(t254, t255);
                                                                                gm_l39 = t256;
                                                                            }
                                                                            uint8_t t257 = gm_l39;
                                                                            gm_l22 = t257;
                                                                            gm_b16_8 t258 = gm_l21;
                                                                            gm_l23 = t258;
                                                                            gm_l24 = ((uint64_t)0ULL);
                                                                            {
                                                                                for (;;) {
                                                                                    uint64_t t259 = gm_l24;
                                                                                    uint64_t t260 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                                                    uint8_t t261 = (t259 < t260);
                                                                                    if (!t261) {
                                                                                        goto gm_lb44;
                                                                                    }
                                                                                    {
                                                                                        void * t262 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                                                                        uint64_t t263 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                                                        uint64_t t264 = gm_l24;
                                                                                        {
                                                                                            {
                                                                                                uint8_t t265 = GM_LD(uint8_t, (((uint8_t *)t262) + (uintptr_t)t264 * 1));
                                                                                                gm_l25 = t265;
                                                                                                uint8_t t266 = gm_l25;
                                                                                                uint8_t t267 = gm_f_is_space_uen38t5(t266);
                                                                                                if (t267) {
                                                                                                    gm_l40 = ((uint8_t)1);
                                                                                                } else {
                                                                                                    uint8_t t268 = gm_l25;
                                                                                                    uint8_t t269 = ((t268 == ((uint8_t)47ULL)));
                                                                                                    gm_l40 = t269;
                                                                                                }
                                                                                                uint8_t t270 = gm_l40;
                                                                                                if (t270) {
                                                                                                    gm_l22 = ((uint8_t)1);
                                                                                                }
                                                                                                goto gm_lb47;
                                                                                            }
gm_lb48:;
                                                                                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 456, 21);
                                                                                        }
gm_lb47:;
                                                                                    }
gm_lb46:;
                                                                                    uint64_t t271 = gm_l24;
                                                                                    uint64_t r272; bool o273 = __builtin_add_overflow(t271, ((uint64_t)1ULL), &r272);
                                                                                    uint64_t t274 = r272;
                                                                                    gm_l24 = t274;
                                                                                }
                                                                            }
gm_lb44:;
                                                                            uint8_t t275 = gm_l22;
                                                                            if (t275) {
                                                                                uint64_t t276 = gm_l6;
                                                                                gm_b16_8 t277 = gm_l21;
                                                                                gm_b16_8 t278 = gm_l1;
                                                                                gm_t6j6smj7qoctu t279;
                                                                                GM_ST(gm_b16_8, ((uint8_t *)&t279 + 0), t277);
                                                                                GM_ST(gm_b16_8, ((uint8_t *)&t279 + 16), t278);
                                                                                gm_f_format_jv6n7oh((void *)&gm_l41, (void *)NULL, cx, NULL, &t279);
                                                                                if (gm_unw.kind) {
                                                                                    goto gm_lad;
                                                                                }
                                                                                uint8_t t280 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                                                                if (!t280) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_srcg0ns322676oc, 460, 45);
                                                                                gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
                                                                                gm_b16_8 t282 = GM_LD(gm_b16_8, ((uint8_t *)&t281));
                                                                                gm_tf1bjg81u0glv t283;
                                                                                gm_f_mfail_uqq7o7g((void *)&t283, t276, t282);
                                                                                gm_tacj1cskem694 t284;
                                                                                memset(&t284, 0, sizeof t284);
                                                                                ((uint8_t *)&t284)[0] = 1;
                                                                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t284 + 8), t283);
                                                                                memmove(gm_out, ((uint8_t *)&t284), 160);
                                                                                goto gm_fdone;
                                                                            }
                                                                            goto gm_lb42;
                                                                        }
gm_lb43:;
                                                                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 454, 17);
                                                                    }
gm_lb42:;
                                                                }
gm_lb41:;
                                                                uint64_t t285 = gm_l20;
                                                                uint64_t r286; bool o287 = __builtin_add_overflow(t285, ((uint64_t)1ULL), &r286);
                                                                uint64_t t288 = r286;
                                                                gm_l20 = t288;
                                                            }
                                                        }
gm_lb39:;
                                                        gm_b16_8 t289 = gm_l18;
                                                        gm_th2lqbo77pvsc t290;
                                                        GM_ST(uint8_t, (uint8_t *)&t290 + 17, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t290 + 18, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&t290 + 20, 0);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t290), t289);
                                                        ((uint8_t *)&t290)[16] = 1;
                                                        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 104), t290);
                                                        goto gm_lb11;
                                                    }
gm_lb38:;
                                                }
                                                {
                                                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 424, 9);
                                                }
                                            }
gm_lb11:;
                                        }
                                        {
                                            goto gm_lb4;
                                        }
                                    }
gm_lb5:;
                                }
                                {
                                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 414, 5);
                                }
                            }
gm_lb4:;
                        }
                    }
gm_lb3:;
                }
                {
                    uint64_t t291 = gm_l4;
                    uint64_t r292; bool o293 = __builtin_add_overflow(t291, ((uint64_t)1ULL), &r292);
                    uint64_t t294 = r292;
                    gm_l4 = t294;
                }
            }
        }
gm_lb1:;
    }
    {
        gm_ti0g3bmheuq9o t295 = GM_LD(gm_ti0g3bmheuq9o, ((uint8_t *)&F.l2));
        gm_tacj1cskem694 t296;
        GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
        ((uint8_t *)&t296)[0] = 0;
        GM_ST(gm_ti0g3bmheuq9o, ((uint8_t *)&t296 + 8), t295);
        memmove(gm_out, ((uint8_t *)&t296), 160);
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
__attribute__((aligned(4))) void gm_f_parse_dependency_8qmcjrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint64_t a2) {
    struct gm_fr_parse_dependency_8qmcjrm F;
    gm_b16_8 gm_l0; /* name */
    uint64_t gm_l2; /* line */
    void * gm_l3; /* t */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l7; /* known */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* k */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l16; /* k */
    gm_b16_8 gm_l17; /* url0 */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l22; /* k */
    gm_b16_8 gm_l23; /* url */
    gm_b16_8 gm_l24; /* p */
    gm_th2lqbo77pvsc gm_l25; /* sha */
    gm_b16_8 gm_l26; /* h0 */
    gm_b16_8 gm_l27; /* h1 */
    gm_b16_8 gm_l28; /* h */
    uint8_t gm_l29; /* ok */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint8_t gm_l32; /* c */
    gm_th2lqbo77pvsc gm_l33; /* subdir */
    gm_th2lqbo77pvsc gm_l34; /* path */
    gm_th2lqbo77pvsc gm_l35; /* git_url */
    gm_b16_8 gm_l36; /* p */
    gm_b16_8 gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    gm_b16_8 gm_l41; /* k */
    gm_b16_8 gm_l42; /* url */
    gm_b16_8 gm_l43; /* p */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    gm_b16_8 gm_l49; /* k */
    gm_b16_8 gm_l51; /* v */
    gm_b16_8 gm_l52; /* e */
    gm_b16_8 gm_l54; /* v */
    gm_b16_8 gm_l55; /* v */
    gm_b16_8 gm_l56; /* v */
    uint8_t gm_l57; /* ok */
    gm_b16_8 gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    uint8_t gm_l60; /* c */
    gm_th2lqbo77pvsc gm_l61; /* subdir */
    GM_STACK_CHECK(&F, 11878u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    void * gm_l62;
    void * gm_l63;
    gm_tabb710d9mq2e gm_l64;
    gm_tabb710d9mq2e gm_l65;
    uint8_t gm_l66;
    void * gm_l67;
    gm_tftb1odc739t5 gm_l68;
    gm_tabb710d9mq2e gm_l69;
    gm_tccklsmdasd9s gm_l70;
    gm_th2lqbo77pvsc gm_l71;
    uint8_t gm_l72;
    void * gm_l73;
    gm_tabb710d9mq2e gm_l74;
    gm_b16_8 gm_l75;
    gm_tabb710d9mq2e gm_l76;
    gm_th2lqbo77pvsc gm_l77;
    gm_tabb710d9mq2e gm_l78;
    gm_tccklsmdasd9s gm_l79;
    gm_th2lqbo77pvsc gm_l80;
    gm_th2lqbo77pvsc gm_l81;
    gm_b16_8 gm_l82;
    void * gm_l83;
    gm_tabb710d9mq2e gm_l84;
    gm_tccklsmdasd9s gm_l85;
    gm_t6h2b31csotun gm_l86;
    uint8_t gm_l87;
    void * gm_l88;
    gm_tabb710d9mq2e gm_l89;
    gm_tccklsmdasd9s gm_l90;
    gm_tccklsmdasd9s gm_l91;
    uint8_t gm_l92;
    uint8_t gm_l93;
    gm_tabb710d9mq2e gm_l94;
    uint8_t gm_l95;
    void * gm_l96;
    gm_tabb710d9mq2e gm_l97;
    gm_tccklsmdasd9s gm_l98;
    gm_tabb710d9mq2e gm_l99;
    gm_b16_8 gm_l100;
    gm_tabb710d9mq2e gm_l101;
    gm_th2lqbo77pvsc gm_l102;
    gm_tabb710d9mq2e gm_l103;
    uint8_t gm_l104;
    void * gm_l105;
    gm_tftb1odc739t5 gm_l106;
    gm_tabb710d9mq2e gm_l107;
    gm_tccklsmdasd9s gm_l108;
    gm_th2lqbo77pvsc gm_l109;
    gm_t62efos6ssciq gm_l110;
    gm_tin97tt4ud0s1 gm_l111;
    void * gm_l112;
    gm_tabb710d9mq2e gm_l113;
    gm_tccklsmdasd9s gm_l114;
    gm_th2lqbo77pvsc gm_l115;
    gm_tccklsmdasd9s gm_l116;
    gm_th2lqbo77pvsc gm_l117;
    gm_tccklsmdasd9s gm_l118;
    gm_th2lqbo77pvsc gm_l119;
    gm_tabb710d9mq2e gm_l120;
    gm_tccklsmdasd9s gm_l121;
    gm_t6h2b31csotun gm_l122;
    {
        void * t83 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)((uint8_t *)a1)));
        gm_l62 = t83;
        uint8_t t84 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l62)) != 0);
        if (t84) {
            void * t85 = GM_LD(void *, ((uint8_t *)&gm_l62));
            gm_l63 = t85;
        } else {
            uint64_t t86 = gm_l2;
            gm_b16_8 t87 = gm_l0;
            gm_tnpifv20irnph t88;
            GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t87);
            gm_f_format_9d8u2dd((void *)&gm_l64, (void *)NULL, cx, NULL, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
            if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 8, gm_srcg0ns322676oc, 485, 29);
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 8));
            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
            gm_tf1bjg81u0glv t92;
            gm_f_mfail_uqq7o7g((void *)&t92, t86, t91);
            gm_tmn9qm2bp7f46 t93;
            memset(&t93, 0, sizeof t93);
            ((uint8_t *)&t93)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t93 + 8), t92);
            memmove(gm_out, ((uint8_t *)&t93), 104);
            goto gm_fdone;
        }
        void * t94 = gm_l63;
        gm_l3 = t94;
        void * t95 = gm_l3;
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)t95));
        gm_l4 = t96;
        gm_l5 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t97 = gm_l5;
                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t99 = (t97 < t98);
                if (!t99) {
                    goto gm_lb1;
                }
                {
                    void * t100 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t102 = gm_l5;
                    {
                        {
                            memmove(((uint8_t *)&F.l6), (((uint8_t *)t100) + (uintptr_t)t102 * 72), 72);
                            gm_l7 = ((uint8_t)0);
                            void * t103 = (void *)((uint8_t *)gm_ct5id4947nt);
                            uint64_t t104 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t105 = (uintptr_t)((uint64_t)9ULL);
                            if (t104 > t105 || t105 > (uintptr_t)((uint64_t)9ULL)) gm_trap_slice(t104, t105, ((uint64_t)9ULL), gm_srcg0ns322676oc, 489, 9);
                            uint64_t t106 = ((uintptr_t)((uint64_t)9ULL)) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t107;
                            GM_ST(void *, (uint8_t *)&t107, (void *)(((uint8_t *)t103 + (uintptr_t)((uint64_t)0ULL) * 16)));
                            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(t106));
                            gm_l8 = t107;
                            gm_l9 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t108 = gm_l9;
                                    uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    uint8_t t110 = (t108 < t109);
                                    if (!t110) {
                                        goto gm_lb6;
                                    }
                                    {
                                        void * t111 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                        uint64_t t113 = gm_l9;
                                        {
                                            {
                                                gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)t111) + (uintptr_t)t113 * 16));
                                                gm_l10 = t114;
                                                gm_b16_8 t115 = gm_l10;
                                                gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                                                uint8_t t117 = (gm_eq0iqiubfm7e(((uint8_t *)&t115), ((uint8_t *)&t116)));
                                                if (t117) {
                                                    gm_l7 = ((uint8_t)1);
                                                }
                                                goto gm_lb9;
                                            }
gm_lb10:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 489, 9);
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                    uint64_t t118 = gm_l9;
                                    uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                                    uint64_t t121 = r119;
                                    gm_l9 = t121;
                                }
                            }
gm_lb6:;
                            uint8_t t122 = gm_l7;
                            uint8_t t123 = (uint8_t)!t122;
                            if (t123) {
                                uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 64));
                                gm_b16_8 t125 = gm_l0;
                                gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                                gm_t6j6smj7qoctu t127;
                                GM_ST(gm_b16_8, ((uint8_t *)&t127 + 0), t125);
                                GM_ST(gm_b16_8, ((uint8_t *)&t127 + 16), t126);
                                gm_f_format_je4jsfp((void *)&gm_l65, (void *)NULL, cx, NULL, &t127);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t128 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                                if (!t128) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_srcg0ns322676oc, 492, 46);
                                gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                                gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                                gm_tf1bjg81u0glv t131;
                                gm_f_mfail_uqq7o7g((void *)&t131, t124, t130);
                                gm_tmn9qm2bp7f46 t132;
                                memset(&t132, 0, sizeof t132);
                                ((uint8_t *)&t132)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t132 + 8), t131);
                                memmove(gm_out, ((uint8_t *)&t132), 104);
                                goto gm_fdone;
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 487, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t133 = gm_l5;
                uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                uint64_t t136 = r134;
                gm_l5 = t136;
            }
        }
gm_lb1:;
    }
    {
        gm_trqfvmjtt4sij t137;
        gm_f_list_epvducg((void *)&t137, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t137;
        gm_b16_8 t138;
        GM_ST(void *, (uint8_t *)&t138, (void *)(gm_smimi08sfrg0n));
        GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(4));
        gm_b16_8 t139;
        GM_ST(void *, (uint8_t *)&t139, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(3));
        gm_b16_8 t140;
        GM_ST(void *, (uint8_t *)&t140, (void *)(gm_sa2h9vonan0ma));
        GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(3));
        gm_tfeab279c16rr t141;
        GM_ST(gm_b16_8, ((uint8_t *)&t141 + 0), t138);
        GM_ST(gm_b16_8, ((uint8_t *)&t141 + 16), t139);
        GM_ST(gm_b16_8, ((uint8_t *)&t141 + 32), t140);
        F.l12 = t141;
        gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
        gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 16));
        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 32));
        gm_t9bmh80edou9c t145;
        GM_ST(gm_b16_8, ((uint8_t *)&t145 + 0), t142);
        GM_ST(gm_b16_8, ((uint8_t *)&t145 + 16), t143);
        GM_ST(gm_b16_8, ((uint8_t *)&t145 + 32), t144);
        F.l13 = t145;
        void * t146 = (void *)((uint8_t *)&F.l13);
        uint64_t t147 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t148 = (uintptr_t)((uint64_t)3ULL);
        if (t147 > t148 || t148 > (uintptr_t)((uint64_t)3ULL)) gm_trap_slice(t147, t148, ((uint64_t)3ULL), gm_srcg0ns322676oc, 495, 5);
        uint64_t t149 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t150;
        GM_ST(void *, (uint8_t *)&t150, (void *)(((uint8_t *)t146 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(t149));
        gm_l14 = t150;
        gm_l15 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t151 = gm_l15;
                uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t153 = (t151 < t152);
                if (!t153) {
                    goto gm_lb11;
                }
                {
                    void * t154 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint64_t t156 = gm_l15;
                    {
                        {
                            gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)t154) + (uintptr_t)t156 * 16));
                            gm_l16 = t157;
                            void * t158 = gm_l3;
                            gm_b16_8 t159 = gm_l16;
                            void * t160 = gm_f_get_9rme0gb(t158, t159);
                            gm_l67 = t160;
                            {
                                {
                                    uint8_t t161 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l67)) != 0);
                                    if (t161) {
                                        goto gm_lb17;
                                    }
                                    gm_l66 = ((uint8_t)0);
                                    goto gm_lb16;
                                }
gm_lb17:;
                                {
                                    gm_l66 = ((uint8_t)1);
                                    goto gm_lb16;
                                }
gm_lb18:;
                                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 496, 12);
                            }
gm_lb16:;
                            uint8_t t162 = gm_l66;
                            if (t162) {
                                void * t163 = (void *)((uint8_t *)&F.l11);
                                gm_b16_8 t164 = gm_l16;
                                gm_tftb1odc739t5 t165 = gm_f_push_3ef746b(t163, t164);
                                gm_l68 = t165;
                                uint8_t t166 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                                if (!t166) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_srcg0ns322676oc, 496, 35);
                            }
                            goto gm_lb14;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 495, 5);
                    }
gm_lb14:;
                }
gm_lb13:;
                uint64_t t167 = gm_l15;
                uint64_t r168; bool o169 = __builtin_add_overflow(t167, ((uint64_t)1ULL), &r168);
                uint64_t t170 = r168;
                gm_l15 = t170;
            }
        }
gm_lb11:;
        uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
        uint8_t t172 = (t171 > ((uint64_t)1ULL));
        if (t172) {
            uint64_t t173 = gm_l2;
            gm_b16_8 t174 = gm_l0;
            void * t175 = GM_LD(void *, ((uint8_t *)&F.l11));
            uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t176)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t176, gm_srcg0ns322676oc, 499, 105);
            gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)t175) + (uintptr_t)((uint64_t)0ULL) * 16));
            void * t178 = GM_LD(void *, ((uint8_t *)&F.l11));
            uint64_t t179 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t179)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t179, gm_srcg0ns322676oc, 499, 123);
            gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)t178) + (uintptr_t)((uint64_t)1ULL) * 16));
            gm_tfeab279c16rr t181;
            GM_ST(gm_b16_8, ((uint8_t *)&t181 + 0), t174);
            GM_ST(gm_b16_8, ((uint8_t *)&t181 + 16), t177);
            GM_ST(gm_b16_8, ((uint8_t *)&t181 + 32), t180);
            gm_f_format_aome4un((void *)&gm_l69, (void *)NULL, cx, NULL, &t181);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t182 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
            if (!t182) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 8, gm_srcg0ns322676oc, 499, 29);
            gm_b16_8 t183 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l69) + 8));
            gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&t183));
            gm_tf1bjg81u0glv t185;
            gm_f_mfail_uqq7o7g((void *)&t185, t173, t184);
            gm_tmn9qm2bp7f46 t186;
            memset(&t186, 0, sizeof t186);
            ((uint8_t *)&t186)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t186 + 8), t185);
            memmove(gm_out, ((uint8_t *)&t186), 104);
            goto gm_fdone;
        }
    }
    {
        {
            {
                void * t187 = gm_l3;
                gm_b16_8 t188;
                GM_ST(void *, (uint8_t *)&t188, (void *)(gm_sa2h9vonan0ma));
                GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(3));
                gm_b16_8 t189 = gm_l0;
                gm_f_dep_string_mmgrsgi((void *)&gm_l70, (void *)NULL, cx, NULL, t187, t188, t189);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t190 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                if (t190) {
                } else {
                    gm_tf1bjg81u0glv t191 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l70) + 8));
                    gm_tmn9qm2bp7f46 t192;
                    memset(&t192, 0, sizeof t192);
                    ((uint8_t *)&t192)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t192 + 8), t191);
                    memmove(gm_out, ((uint8_t *)&t192), 104);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&gm_l71), (((uint8_t *)&gm_l70) + 8), 24);
                uint8_t t193 = (uint8_t)((((uint8_t *)&gm_l71))[16] != 0);
                if (!t193) {
                    goto gm_lb20;
                }
                gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l71));
                gm_l17 = t194;
                gm_b16_8 t195;
                GM_ST(void *, (uint8_t *)&t195, (void *)(gm_sla3alt5340mf));
                GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(7));
                gm_b16_8 t196;
                GM_ST(void *, (uint8_t *)&t196, (void *)(gm_s420tm9ua7ggg));
                GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(3));
                gm_b16_8 t197;
                GM_ST(void *, (uint8_t *)&t197, (void *)(gm_s0jho24omhfui));
                GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(6));
                gm_b16_8 t198;
                GM_ST(void *, (uint8_t *)&t198, (void *)(gm_sf9ilivr4c86m));
                GM_ST(uintptr_t, (uint8_t *)&t198 + 8, (uintptr_t)(3));
                gm_t0uuhaljsf410 t199;
                GM_ST(gm_b16_8, ((uint8_t *)&t199 + 0), t195);
                GM_ST(gm_b16_8, ((uint8_t *)&t199 + 16), t196);
                GM_ST(gm_b16_8, ((uint8_t *)&t199 + 32), t197);
                GM_ST(gm_b16_8, ((uint8_t *)&t199 + 48), t198);
                F.l18 = t199;
                gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 16));
                gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 32));
                gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 48));
                gm_tdmi4fk9nhqa7 t204;
                GM_ST(gm_b16_8, ((uint8_t *)&t204 + 0), t200);
                GM_ST(gm_b16_8, ((uint8_t *)&t204 + 16), t201);
                GM_ST(gm_b16_8, ((uint8_t *)&t204 + 32), t202);
                GM_ST(gm_b16_8, ((uint8_t *)&t204 + 48), t203);
                F.l19 = t204;
                void * t205 = (void *)((uint8_t *)&F.l19);
                uint64_t t206 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t207 = (uintptr_t)((uint64_t)4ULL);
                if (t206 > t207 || t207 > (uintptr_t)((uint64_t)4ULL)) gm_trap_slice(t206, t207, ((uint64_t)4ULL), gm_srcg0ns322676oc, 502, 9);
                uint64_t t208 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t209;
                GM_ST(void *, (uint8_t *)&t209, (void *)(((uint8_t *)t205 + (uintptr_t)((uint64_t)0ULL) * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(t208));
                gm_l20 = t209;
                gm_l21 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t210 = gm_l21;
                        uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                        uint8_t t212 = (t210 < t211);
                        if (!t212) {
                            goto gm_lb21;
                        }
                        {
                            void * t213 = GM_LD(void *, ((uint8_t *)&gm_l20));
                            uint64_t t214 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                            uint64_t t215 = gm_l21;
                            {
                                {
                                    gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)t213) + (uintptr_t)t215 * 16));
                                    gm_l22 = t216;
                                    void * t217 = gm_l3;
                                    gm_b16_8 t218 = gm_l22;
                                    void * t219 = gm_f_get_9rme0gb(t217, t218);
                                    gm_l73 = t219;
                                    {
                                        {
                                            uint8_t t220 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l73)) != 0);
                                            if (t220) {
                                                goto gm_lb27;
                                            }
                                            gm_l72 = ((uint8_t)0);
                                            goto gm_lb26;
                                        }
gm_lb27:;
                                        {
                                            gm_l72 = ((uint8_t)1);
                                            goto gm_lb26;
                                        }
gm_lb28:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 503, 16);
                                    }
gm_lb26:;
                                    uint8_t t221 = gm_l72;
                                    if (t221) {
                                        uint64_t t222 = gm_l2;
                                        gm_b16_8 t223 = gm_l22;
                                        gm_b16_8 t224 = gm_l0;
                                        gm_t6j6smj7qoctu t225;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t225 + 0), t223);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t225 + 16), t224);
                                        gm_f_format_64tgs30((void *)&gm_l74, (void *)NULL, cx, NULL, &t225);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t226 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                                        if (!t226) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 8, gm_srcg0ns322676oc, 503, 59);
                                        gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l74) + 8));
                                        gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&t227));
                                        gm_tf1bjg81u0glv t229;
                                        gm_f_mfail_uqq7o7g((void *)&t229, t222, t228);
                                        gm_tmn9qm2bp7f46 t230;
                                        memset(&t230, 0, sizeof t230);
                                        ((uint8_t *)&t230)[0] = 1;
                                        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t230 + 8), t229);
                                        memmove(gm_out, ((uint8_t *)&t230), 104);
                                        goto gm_fdone;
                                    }
                                    goto gm_lb24;
                                }
gm_lb25:;
                                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 502, 9);
                            }
gm_lb24:;
                        }
gm_lb23:;
                        uint64_t t231 = gm_l21;
                        uint64_t r232; bool o233 = __builtin_add_overflow(t231, ((uint64_t)1ULL), &r232);
                        uint64_t t234 = r232;
                        gm_l21 = t234;
                    }
                }
gm_lb21:;
                gm_b16_8 t235 = gm_l17;
                gm_b16_8 t236 = gm_f_trim_ascii_c9j36d1(t235);
                gm_l75 = t236;
                uint64_t t237 = GM_LD(uint64_t, (((uint8_t *)&gm_l75) + 8));
                uint8_t t238 = ((t237 == ((uint64_t)0ULL)));
                if (t238) {
                    uint64_t t239 = gm_l2;
                    gm_b16_8 t240 = gm_l0;
                    gm_tnpifv20irnph t241;
                    GM_ST(gm_b16_8, ((uint8_t *)&t241 + 0), t240);
                    gm_f_format_97etovl((void *)&gm_l76, (void *)NULL, cx, NULL, t241);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t242 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                    if (!t242) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 8, gm_srcg0ns322676oc, 505, 60);
                    gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l76) + 8));
                    gm_b16_8 t244 = GM_LD(gm_b16_8, ((uint8_t *)&t243));
                    gm_tf1bjg81u0glv t245;
                    gm_f_mfail_uqq7o7g((void *)&t245, t239, t244);
                    gm_tmn9qm2bp7f46 t246;
                    memset(&t246, 0, sizeof t246);
                    ((uint8_t *)&t246)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t246 + 8), t245);
                    memmove(gm_out, ((uint8_t *)&t246), 104);
                    goto gm_fdone;
                }
                gm_b16_8 t247 = gm_l17;
                gm_b16_8 t248 = gm_f_trim_ascii_c9j36d1(t247);
                gm_l23 = t248;
                {
                    {
                        gm_b16_8 t249 = gm_l23;
                        gm_f_url_problem_38kdh3a((void *)&gm_l77, (void *)NULL, cx, NULL, t249);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t250 = (uint8_t)((((uint8_t *)&gm_l77))[16] != 0);
                        if (!t250) {
                            goto gm_lb30;
                        }
                        gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l77));
                        gm_l24 = t251;
                        uint64_t t252 = gm_l2;
                        gm_b16_8 t253 = gm_l0;
                        gm_b16_8 t254 = gm_l24;
                        gm_t6j6smj7qoctu t255;
                        GM_ST(gm_b16_8, ((uint8_t *)&t255 + 0), t253);
                        GM_ST(gm_b16_8, ((uint8_t *)&t255 + 16), t254);
                        gm_f_format_qjfu8g6((void *)&gm_l78, (void *)NULL, cx, NULL, &t255);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t256 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                        if (!t256) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 8, gm_srcg0ns322676oc, 508, 33);
                        gm_b16_8 t257 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l78) + 8));
                        gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&t257));
                        gm_tf1bjg81u0glv t259;
                        gm_f_mfail_uqq7o7g((void *)&t259, t252, t258);
                        gm_tmn9qm2bp7f46 t260;
                        memset(&t260, 0, sizeof t260);
                        ((uint8_t *)&t260)[0] = 1;
                        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t260 + 8), t259);
                        memmove(gm_out, ((uint8_t *)&t260), 104);
                        goto gm_fdone;
                    }
gm_lb30:;
                }
gm_lb29:;
                gm_th2lqbo77pvsc t261;
                memset(&t261, 0, sizeof t261);
                gm_l25 = t261;
                {
                    {
                        void * t262 = gm_l3;
                        gm_b16_8 t263;
                        GM_ST(void *, (uint8_t *)&t263, (void *)(gm_sd1smnnii4cig));
                        GM_ST(uintptr_t, (uint8_t *)&t263 + 8, (uintptr_t)(6));
                        gm_b16_8 t264 = gm_l0;
                        gm_f_dep_string_mmgrsgi((void *)&gm_l79, (void *)NULL, cx, NULL, t262, t263, t264);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t265 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                        if (t265) {
                        } else {
                            gm_tf1bjg81u0glv t266 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l79) + 8));
                            gm_tmn9qm2bp7f46 t267;
                            memset(&t267, 0, sizeof t267);
                            ((uint8_t *)&t267)[0] = 1;
                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t267 + 8), t266);
                            memmove(gm_out, ((uint8_t *)&t267), 104);
                            goto gm_fdone;
                        }
                        memmove(((uint8_t *)&gm_l80), (((uint8_t *)&gm_l79) + 8), 24);
                        uint8_t t268 = (uint8_t)((((uint8_t *)&gm_l80))[16] != 0);
                        if (!t268) {
                            goto gm_lb32;
                        }
                        gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l80));
                        gm_l26 = t269;
                        gm_b16_8 t270 = gm_l26;
                        gm_b16_8 t271 = gm_f_trim_ascii_c9j36d1(t270);
                        gm_l27 = t271;
                        gm_b16_8 t272 = gm_l27;
                        gm_b16_8 t273;
                        GM_ST(void *, (uint8_t *)&t273, (void *)(gm_son0j0k0t10ro));
                        GM_ST(uintptr_t, (uint8_t *)&t273 + 8, (uintptr_t)(7));
                        gm_f_strip_prefix_b4949s8((void *)&gm_l81, t272, t273);
                        uint8_t t274 = (uint8_t)((((uint8_t *)&gm_l81))[16] != 0);
                        if (t274) {
                            gm_b16_8 t275 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l81));
                            gm_l82 = t275;
                        } else {
                            gm_b16_8 t276 = gm_l27;
                            gm_l82 = t276;
                        }
                        gm_b16_8 t277 = gm_l82;
                        gm_l27 = t277;
                        gm_b16_8 t278 = gm_l27;
                        gm_b16_8 t279 = gm_f_lower_ascii_uggh3ca((void *)NULL, cx, NULL, t278);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l28 = t279;
                        uint64_t t280 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                        uint8_t t281 = ((t280 == ((uint64_t)64ULL)));
                        gm_l29 = t281;
                        gm_b16_8 t282 = gm_l28;
                        gm_l30 = t282;
                        gm_l31 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t283 = gm_l31;
                                uint64_t t284 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                uint8_t t285 = (t283 < t284);
                                if (!t285) {
                                    goto gm_lb33;
                                }
                                {
                                    void * t286 = GM_LD(void *, ((uint8_t *)&gm_l30));
                                    uint64_t t287 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                    uint64_t t288 = gm_l31;
                                    {
                                        {
                                            uint8_t t289 = GM_LD(uint8_t, (((uint8_t *)t286) + (uintptr_t)t288 * 1));
                                            gm_l32 = t289;
                                            uint8_t t290 = gm_l32;
                                            uint8_t t291 = gm_f_is_hex_digit_q8s6utq(t290);
                                            uint8_t t292 = (uint8_t)!t291;
                                            if (t292) {
                                                gm_l29 = ((uint8_t)0);
                                            }
                                            goto gm_lb36;
                                        }
gm_lb37:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 516, 13);
                                    }
gm_lb36:;
                                }
gm_lb35:;
                                uint64_t t293 = gm_l31;
                                uint64_t r294; bool o295 = __builtin_add_overflow(t293, ((uint64_t)1ULL), &r294);
                                uint64_t t296 = r294;
                                gm_l31 = t296;
                            }
                        }
gm_lb33:;
                        uint8_t t297 = gm_l29;
                        uint8_t t298 = (uint8_t)!t297;
                        if (t298) {
                            void * t299 = gm_l3;
                            gm_b16_8 t300;
                            GM_ST(void *, (uint8_t *)&t300, (void *)(gm_sd1smnnii4cig));
                            GM_ST(uintptr_t, (uint8_t *)&t300 + 8, (uintptr_t)(6));
                            void * t301 = gm_f_get_9rme0gb(t299, t300);
                            gm_l83 = t301;
                            uint8_t t302 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l83)) != 0);
                            if (!t302) gm_trap("unwrap of null", gm_srcg0ns322676oc, 520, 31);
                            void * t303 = GM_LD(void *, ((uint8_t *)&gm_l83));
                            uint64_t t304 = GM_LD(uint64_t, (((uint8_t *)t303) + 64));
                            gm_b16_8 t305 = gm_l0;
                            gm_tnpifv20irnph t306;
                            GM_ST(gm_b16_8, ((uint8_t *)&t306 + 0), t305);
                            gm_f_format_nll5o74((void *)&gm_l84, (void *)NULL, cx, NULL, t306);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t307 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
                            if (!t307) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l84) + 8, gm_srcg0ns322676oc, 520, 54);
                            gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l84) + 8));
                            gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
                            gm_tf1bjg81u0glv t310;
                            gm_f_mfail_uqq7o7g((void *)&t310, t304, t309);
                            gm_tmn9qm2bp7f46 t311;
                            memset(&t311, 0, sizeof t311);
                            ((uint8_t *)&t311)[0] = 1;
                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t311 + 8), t310);
                            memmove(gm_out, ((uint8_t *)&t311), 104);
                            goto gm_fdone;
                        }
                        gm_b16_8 t312 = gm_l28;
                        gm_th2lqbo77pvsc t313;
                        GM_ST(uint8_t, (uint8_t *)&t313 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t313 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t313 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t313), t312);
                        ((uint8_t *)&t313)[16] = 1;
                        gm_l25 = t313;
                        goto gm_lb31;
                    }
gm_lb32:;
                }
gm_lb31:;
                void * t314 = gm_l3;
                gm_b16_8 t315;
                GM_ST(void *, (uint8_t *)&t315, (void *)(gm_sfvspsnv23gbt));
                GM_ST(uintptr_t, (uint8_t *)&t315 + 8, (uintptr_t)(6));
                gm_b16_8 t316 = gm_l0;
                gm_f_dep_string_mmgrsgi((void *)&gm_l85, (void *)NULL, cx, NULL, t314, t315, t316);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t317 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
                if (t317) {
                } else {
                    gm_tf1bjg81u0glv t318 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l85) + 8));
                    gm_tmn9qm2bp7f46 t319;
                    memset(&t319, 0, sizeof t319);
                    ((uint8_t *)&t319)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t319 + 8), t318);
                    memmove(gm_out, ((uint8_t *)&t319), 104);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&gm_l33), (((uint8_t *)&gm_l85) + 8), 24);
                gm_b16_8 t320 = gm_l0;
                uint64_t t321 = gm_l2;
                gm_th2lqbo77pvsc t322 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l33));
                gm_f_check_subdir_2uljb06((void *)&gm_l86, (void *)NULL, cx, NULL, t320, &t322, t321);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t323 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
                if (t323) {
                } else {
                    gm_tf1bjg81u0glv t324 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l86) + 8));
                    gm_tmn9qm2bp7f46 t325;
                    memset(&t325, 0, sizeof t325);
                    ((uint8_t *)&t325)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t325 + 8), t324);
                    memmove(gm_out, ((uint8_t *)&t325), 104);
                    goto gm_fdone;
                }
                gm_b16_8 t326 = gm_l23;
                gm_th2lqbo77pvsc t327 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l25));
                gm_th2lqbo77pvsc t328 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l33));
                gm_tt008mgut8glb t329;
                GM_ST(gm_b16_8, ((uint8_t *)&t329 + 0), t326);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t329 + 16), t327);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t329 + 40), t328);
                gm_tnlfbo6r4vlhg t330;
                GM_ST(uint8_t, (uint8_t *)&t330 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t330 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t330 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t330 + 72, 0);
                GM_ST(uint64_t, (uint8_t *)&t330 + 80, 0);
                GM_ST(uint64_t, (uint8_t *)&t330 + 88, 0);
                GM_ST(uint8_t, ((uint8_t *)&t330 + 0), ((uint8_t)2ULL));
                GM_ST(gm_tt008mgut8glb, ((uint8_t *)&t330 + 8), t329);
                gm_tmn9qm2bp7f46 t331;
                GM_ST(uint8_t, (uint8_t *)&t331 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t331 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t331 + 4, 0);
                ((uint8_t *)&t331)[0] = 0;
                GM_ST(gm_tnlfbo6r4vlhg, ((uint8_t *)&t331 + 8), t330);
                memmove(gm_out, ((uint8_t *)&t331), 104);
                goto gm_fdone;
            }
gm_lb20:;
        }
gm_lb19:;
    }
    {
        void * t332 = gm_l3;
        gm_b16_8 t333;
        GM_ST(void *, (uint8_t *)&t333, (void *)(gm_sd1smnnii4cig));
        GM_ST(uintptr_t, (uint8_t *)&t333 + 8, (uintptr_t)(6));
        void * t334 = gm_f_get_9rme0gb(t332, t333);
        gm_l88 = t334;
        {
            {
                uint8_t t335 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l88)) != 0);
                if (t335) {
                    goto gm_lb39;
                }
                gm_l87 = ((uint8_t)0);
                goto gm_lb38;
            }
gm_lb39:;
            {
                gm_l87 = ((uint8_t)1);
                goto gm_lb38;
            }
gm_lb40:;
            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 528, 8);
        }
gm_lb38:;
        uint8_t t336 = gm_l87;
        if (t336) {
            uint64_t t337 = gm_l2;
            gm_b16_8 t338 = gm_l0;
            gm_tnpifv20irnph t339;
            GM_ST(gm_b16_8, ((uint8_t *)&t339 + 0), t338);
            gm_f_format_p8qi5m3((void *)&gm_l89, (void *)NULL, cx, NULL, t339);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t340 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
            if (!t340) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l89) + 8, gm_srcg0ns322676oc, 528, 58);
            gm_b16_8 t341 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l89) + 8));
            gm_b16_8 t342 = GM_LD(gm_b16_8, ((uint8_t *)&t341));
            gm_tf1bjg81u0glv t343;
            gm_f_mfail_uqq7o7g((void *)&t343, t337, t342);
            gm_tmn9qm2bp7f46 t344;
            memset(&t344, 0, sizeof t344);
            ((uint8_t *)&t344)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t344 + 8), t343);
            memmove(gm_out, ((uint8_t *)&t344), 104);
            goto gm_fdone;
        }
        void * t345 = gm_l3;
        gm_b16_8 t346;
        GM_ST(void *, (uint8_t *)&t346, (void *)(gm_smimi08sfrg0n));
        GM_ST(uintptr_t, (uint8_t *)&t346 + 8, (uintptr_t)(4));
        gm_b16_8 t347 = gm_l0;
        gm_f_dep_string_mmgrsgi((void *)&gm_l90, (void *)NULL, cx, NULL, t345, t346, t347);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t348 = (uint8_t)!((((uint8_t *)&gm_l90))[0] != 0);
        if (t348) {
        } else {
            gm_tf1bjg81u0glv t349 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l90) + 8));
            gm_tmn9qm2bp7f46 t350;
            memset(&t350, 0, sizeof t350);
            ((uint8_t *)&t350)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t350 + 8), t349);
            memmove(gm_out, ((uint8_t *)&t350), 104);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&gm_l34), (((uint8_t *)&gm_l90) + 8), 24);
        void * t351 = gm_l3;
        gm_b16_8 t352;
        GM_ST(void *, (uint8_t *)&t352, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t352 + 8, (uintptr_t)(3));
        gm_b16_8 t353 = gm_l0;
        gm_f_dep_string_mmgrsgi((void *)&gm_l91, (void *)NULL, cx, NULL, t351, t352, t353);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t354 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
        if (t354) {
        } else {
            gm_tf1bjg81u0glv t355 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l91) + 8));
            gm_tmn9qm2bp7f46 t356;
            memset(&t356, 0, sizeof t356);
            ((uint8_t *)&t356)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t356 + 8), t355);
            memmove(gm_out, ((uint8_t *)&t356), 104);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&gm_l35), (((uint8_t *)&gm_l91) + 8), 24);
        {
            {
                {
                    {
                        uint8_t t357 = (uint8_t)((((uint8_t *)&gm_l34))[16] != 0);
                        if (t357) {
                            goto gm_lb44;
                        }
                        gm_l92 = ((uint8_t)1);
                        goto gm_lb43;
                    }
gm_lb44:;
                    {
                        gm_l92 = ((uint8_t)0);
                        goto gm_lb43;
                    }
gm_lb45:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 531, 8);
                }
gm_lb43:;
                uint8_t t358 = gm_l92;
                if (!t358) {
                    goto gm_lb42;
                }
                {
                    {
                        uint8_t t359 = (uint8_t)((((uint8_t *)&gm_l35))[16] != 0);
                        if (t359) {
                            goto gm_lb47;
                        }
                        gm_l93 = ((uint8_t)1);
                        goto gm_lb46;
                    }
gm_lb47:;
                    {
                        gm_l93 = ((uint8_t)0);
                        goto gm_lb46;
                    }
gm_lb48:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 531, 25);
                }
gm_lb46:;
                uint8_t t360 = gm_l93;
                if (!t360) {
                    goto gm_lb42;
                }
                uint64_t t361 = gm_l2;
                gm_b16_8 t362 = gm_l0;
                gm_tnpifv20irnph t363;
                GM_ST(gm_b16_8, ((uint8_t *)&t363 + 0), t362);
                gm_f_format_5bo63p7((void *)&gm_l94, (void *)NULL, cx, NULL, t363);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t364 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
                if (!t364) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l94) + 8, gm_srcg0ns322676oc, 532, 29);
                gm_b16_8 t365 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l94) + 8));
                gm_b16_8 t366 = GM_LD(gm_b16_8, ((uint8_t *)&t365));
                gm_tf1bjg81u0glv t367;
                gm_f_mfail_uqq7o7g((void *)&t367, t361, t366);
                gm_tmn9qm2bp7f46 t368;
                memset(&t368, 0, sizeof t368);
                ((uint8_t *)&t368)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t368 + 8), t367);
                memmove(gm_out, ((uint8_t *)&t368), 104);
                goto gm_fdone;
            }
gm_lb42:;
        }
gm_lb41:;
    }
    {
        {
            {
                uint8_t t369 = (uint8_t)((((uint8_t *)&gm_l34))[16] != 0);
                if (!t369) {
                    goto gm_lb50;
                }
                gm_b16_8 t370 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l34));
                gm_l36 = t370;
                gm_b16_8 t371;
                GM_ST(void *, (uint8_t *)&t371, (void *)(gm_s420tm9ua7ggg));
                GM_ST(uintptr_t, (uint8_t *)&t371 + 8, (uintptr_t)(3));
                gm_b16_8 t372;
                GM_ST(void *, (uint8_t *)&t372, (void *)(gm_s0jho24omhfui));
                GM_ST(uintptr_t, (uint8_t *)&t372 + 8, (uintptr_t)(6));
                gm_b16_8 t373;
                GM_ST(void *, (uint8_t *)&t373, (void *)(gm_sf9ilivr4c86m));
                GM_ST(uintptr_t, (uint8_t *)&t373 + 8, (uintptr_t)(3));
                gm_b16_8 t374;
                GM_ST(void *, (uint8_t *)&t374, (void *)(gm_sfvspsnv23gbt));
                GM_ST(uintptr_t, (uint8_t *)&t374 + 8, (uintptr_t)(6));
                gm_t0uuhaljsf410 t375;
                GM_ST(gm_b16_8, ((uint8_t *)&t375 + 0), t371);
                GM_ST(gm_b16_8, ((uint8_t *)&t375 + 16), t372);
                GM_ST(gm_b16_8, ((uint8_t *)&t375 + 32), t373);
                GM_ST(gm_b16_8, ((uint8_t *)&t375 + 48), t374);
                F.l37 = t375;
                gm_b16_8 t376 = GM_LD(gm_b16_8, ((uint8_t *)&F.l37));
                gm_b16_8 t377 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 16));
                gm_b16_8 t378 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 32));
                gm_b16_8 t379 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 48));
                gm_tdmi4fk9nhqa7 t380;
                GM_ST(gm_b16_8, ((uint8_t *)&t380 + 0), t376);
                GM_ST(gm_b16_8, ((uint8_t *)&t380 + 16), t377);
                GM_ST(gm_b16_8, ((uint8_t *)&t380 + 32), t378);
                GM_ST(gm_b16_8, ((uint8_t *)&t380 + 48), t379);
                F.l38 = t380;
                void * t381 = (void *)((uint8_t *)&F.l38);
                uint64_t t382 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t383 = (uintptr_t)((uint64_t)4ULL);
                if (t382 > t383 || t383 > (uintptr_t)((uint64_t)4ULL)) gm_trap_slice(t382, t383, ((uint64_t)4ULL), gm_srcg0ns322676oc, 535, 9);
                uint64_t t384 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t385;
                GM_ST(void *, (uint8_t *)&t385, (void *)(((uint8_t *)t381 + (uintptr_t)((uint64_t)0ULL) * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t385 + 8, (uintptr_t)(t384));
                gm_l39 = t385;
                gm_l40 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t386 = gm_l40;
                        uint64_t t387 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                        uint8_t t388 = (t386 < t387);
                        if (!t388) {
                            goto gm_lb51;
                        }
                        {
                            void * t389 = GM_LD(void *, ((uint8_t *)&gm_l39));
                            uint64_t t390 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                            uint64_t t391 = gm_l40;
                            {
                                {
                                    gm_b16_8 t392 = GM_LD(gm_b16_8, (((uint8_t *)t389) + (uintptr_t)t391 * 16));
                                    gm_l41 = t392;
                                    void * t393 = gm_l3;
                                    gm_b16_8 t394 = gm_l41;
                                    void * t395 = gm_f_get_9rme0gb(t393, t394);
                                    gm_l96 = t395;
                                    {
                                        {
                                            uint8_t t396 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l96)) != 0);
                                            if (t396) {
                                                goto gm_lb57;
                                            }
                                            gm_l95 = ((uint8_t)0);
                                            goto gm_lb56;
                                        }
gm_lb57:;
                                        {
                                            gm_l95 = ((uint8_t)1);
                                            goto gm_lb56;
                                        }
gm_lb58:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 536, 16);
                                    }
gm_lb56:;
                                    uint8_t t397 = gm_l95;
                                    if (t397) {
                                        uint64_t t398 = gm_l2;
                                        gm_b16_8 t399 = gm_l41;
                                        gm_b16_8 t400 = gm_l0;
                                        gm_t6j6smj7qoctu t401;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t401 + 0), t399);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t401 + 16), t400);
                                        gm_f_format_64tgs30((void *)&gm_l97, (void *)NULL, cx, NULL, &t401);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t402 = (uint8_t)!((((uint8_t *)&gm_l97))[0] != 0);
                                        if (!t402) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l97) + 8, gm_srcg0ns322676oc, 536, 59);
                                        gm_b16_8 t403 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l97) + 8));
                                        gm_b16_8 t404 = GM_LD(gm_b16_8, ((uint8_t *)&t403));
                                        gm_tf1bjg81u0glv t405;
                                        gm_f_mfail_uqq7o7g((void *)&t405, t398, t404);
                                        gm_tmn9qm2bp7f46 t406;
                                        memset(&t406, 0, sizeof t406);
                                        ((uint8_t *)&t406)[0] = 1;
                                        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t406 + 8), t405);
                                        memmove(gm_out, ((uint8_t *)&t406), 104);
                                        goto gm_fdone;
                                    }
                                    goto gm_lb54;
                                }
gm_lb55:;
                                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 535, 9);
                            }
gm_lb54:;
                        }
gm_lb53:;
                        uint64_t t407 = gm_l40;
                        uint64_t r408; bool o409 = __builtin_add_overflow(t407, ((uint64_t)1ULL), &r408);
                        uint64_t t410 = r408;
                        gm_l40 = t410;
                    }
                }
gm_lb51:;
                void * t411 = gm_l3;
                gm_b16_8 t412;
                GM_ST(void *, (uint8_t *)&t412, (void *)(gm_sla3alt5340mf));
                GM_ST(uintptr_t, (uint8_t *)&t412 + 8, (uintptr_t)(7));
                gm_b16_8 t413 = gm_l0;
                gm_f_dep_string_mmgrsgi((void *)&gm_l98, (void *)NULL, cx, NULL, t411, t412, t413);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t414 = (uint8_t)!((((uint8_t *)&gm_l98))[0] != 0);
                if (t414) {
                } else {
                    gm_tf1bjg81u0glv t415 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l98) + 8));
                    gm_tmn9qm2bp7f46 t416;
                    memset(&t416, 0, sizeof t416);
                    ((uint8_t *)&t416)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t416 + 8), t415);
                    memmove(gm_out, ((uint8_t *)&t416), 104);
                    goto gm_fdone;
                }
                uint64_t t417 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                uint8_t t418 = ((t417 == ((uint64_t)0ULL)));
                gm_th2lqbo77pvsc t419 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&gm_l98) + 8));
                if (t418) {
                    uint64_t t420 = gm_l2;
                    gm_b16_8 t421 = gm_l0;
                    gm_tnpifv20irnph t422;
                    GM_ST(gm_b16_8, ((uint8_t *)&t422 + 0), t421);
                    gm_f_format_dd3t0vb((void *)&gm_l99, (void *)NULL, cx, NULL, t422);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t423 = (uint8_t)!((((uint8_t *)&gm_l99))[0] != 0);
                    if (!t423) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l99) + 8, gm_srcg0ns322676oc, 540, 45);
                    gm_b16_8 t424 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l99) + 8));
                    gm_b16_8 t425 = GM_LD(gm_b16_8, ((uint8_t *)&t424));
                    gm_tf1bjg81u0glv t426;
                    gm_f_mfail_uqq7o7g((void *)&t426, t420, t425);
                    gm_tmn9qm2bp7f46 t427;
                    memset(&t427, 0, sizeof t427);
                    ((uint8_t *)&t427)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t427 + 8), t426);
                    memmove(gm_out, ((uint8_t *)&t427), 104);
                    goto gm_fdone;
                }
                gm_b16_8 t428 = gm_l36;
                gm_tnlfbo6r4vlhg t429;
                memset(&t429, 0, sizeof t429);
                GM_ST(uint8_t, ((uint8_t *)&t429 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t429 + 8), t428);
                gm_tmn9qm2bp7f46 t430;
                GM_ST(uint8_t, (uint8_t *)&t430 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t430 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t430 + 4, 0);
                ((uint8_t *)&t430)[0] = 0;
                GM_ST(gm_tnlfbo6r4vlhg, ((uint8_t *)&t430 + 8), t429);
                memmove(gm_out, ((uint8_t *)&t430), 104);
                goto gm_fdone;
            }
gm_lb50:;
        }
gm_lb49:;
    }
    {
        uint8_t t431 = (uint8_t)((((uint8_t *)&gm_l35))[16] != 0);
        if (!t431) gm_trap("unwrap of null", gm_srcg0ns322676oc, 543, 12);
        gm_b16_8 t432 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l35));
        gm_l42 = t432;
        gm_b16_8 t433 = gm_l42;
        gm_b16_8 t434 = gm_f_normalize_url_faghdja(t433);
        gm_l100 = t434;
        uint64_t t435 = GM_LD(uint64_t, (((uint8_t *)&gm_l100) + 8));
        uint8_t t436 = ((t435 == ((uint64_t)0ULL)));
        if (t436) {
            uint64_t t437 = gm_l2;
            gm_b16_8 t438 = gm_l0;
            gm_tnpifv20irnph t439;
            GM_ST(gm_b16_8, ((uint8_t *)&t439 + 0), t438);
            gm_f_format_fn68qf0((void *)&gm_l101, (void *)NULL, cx, NULL, t439);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t440 = (uint8_t)!((((uint8_t *)&gm_l101))[0] != 0);
            if (!t440) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l101) + 8, gm_srcg0ns322676oc, 544, 58);
            gm_b16_8 t441 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l101) + 8));
            gm_b16_8 t442 = GM_LD(gm_b16_8, ((uint8_t *)&t441));
            gm_tf1bjg81u0glv t443;
            gm_f_mfail_uqq7o7g((void *)&t443, t437, t442);
            gm_tmn9qm2bp7f46 t444;
            memset(&t444, 0, sizeof t444);
            ((uint8_t *)&t444)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t444 + 8), t443);
            memmove(gm_out, ((uint8_t *)&t444), 104);
            goto gm_fdone;
        }
        {
            {
                gm_b16_8 t445 = gm_l42;
                gm_b16_8 t446 = gm_f_trim_ascii_c9j36d1(t445);
                gm_f_url_problem_38kdh3a((void *)&gm_l102, (void *)NULL, cx, NULL, t446);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t447 = (uint8_t)((((uint8_t *)&gm_l102))[16] != 0);
                if (!t447) {
                    goto gm_lb60;
                }
                gm_b16_8 t448 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l102));
                gm_l43 = t448;
                uint64_t t449 = gm_l2;
                gm_b16_8 t450 = gm_l0;
                gm_b16_8 t451 = gm_l43;
                gm_t6j6smj7qoctu t452;
                GM_ST(gm_b16_8, ((uint8_t *)&t452 + 0), t450);
                GM_ST(gm_b16_8, ((uint8_t *)&t452 + 16), t451);
                gm_f_format_7sbr40s((void *)&gm_l103, (void *)NULL, cx, NULL, &t452);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t453 = (uint8_t)!((((uint8_t *)&gm_l103))[0] != 0);
                if (!t453) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l103) + 8, gm_srcg0ns322676oc, 546, 29);
                gm_b16_8 t454 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l103) + 8));
                gm_b16_8 t455 = GM_LD(gm_b16_8, ((uint8_t *)&t454));
                gm_tf1bjg81u0glv t456;
                gm_f_mfail_uqq7o7g((void *)&t456, t449, t455);
                gm_tmn9qm2bp7f46 t457;
                memset(&t457, 0, sizeof t457);
                ((uint8_t *)&t457)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t457 + 8), t456);
                memmove(gm_out, ((uint8_t *)&t457), 104);
                goto gm_fdone;
            }
gm_lb60:;
        }
gm_lb59:;
        gm_trqfvmjtt4sij t458;
        gm_f_list_epvducg((void *)&t458, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l44 = t458;
        gm_b16_8 t459;
        GM_ST(void *, (uint8_t *)&t459, (void *)(gm_sla3alt5340mf));
        GM_ST(uintptr_t, (uint8_t *)&t459 + 8, (uintptr_t)(7));
        gm_b16_8 t460;
        GM_ST(void *, (uint8_t *)&t460, (void *)(gm_s420tm9ua7ggg));
        GM_ST(uintptr_t, (uint8_t *)&t460 + 8, (uintptr_t)(3));
        gm_b16_8 t461;
        GM_ST(void *, (uint8_t *)&t461, (void *)(gm_s0jho24omhfui));
        GM_ST(uintptr_t, (uint8_t *)&t461 + 8, (uintptr_t)(6));
        gm_b16_8 t462;
        GM_ST(void *, (uint8_t *)&t462, (void *)(gm_sf9ilivr4c86m));
        GM_ST(uintptr_t, (uint8_t *)&t462 + 8, (uintptr_t)(3));
        gm_t0uuhaljsf410 t463;
        GM_ST(gm_b16_8, ((uint8_t *)&t463 + 0), t459);
        GM_ST(gm_b16_8, ((uint8_t *)&t463 + 16), t460);
        GM_ST(gm_b16_8, ((uint8_t *)&t463 + 32), t461);
        GM_ST(gm_b16_8, ((uint8_t *)&t463 + 48), t462);
        F.l45 = t463;
        gm_b16_8 t464 = GM_LD(gm_b16_8, ((uint8_t *)&F.l45));
        gm_b16_8 t465 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 16));
        gm_b16_8 t466 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 32));
        gm_b16_8 t467 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 48));
        gm_tdmi4fk9nhqa7 t468;
        GM_ST(gm_b16_8, ((uint8_t *)&t468 + 0), t464);
        GM_ST(gm_b16_8, ((uint8_t *)&t468 + 16), t465);
        GM_ST(gm_b16_8, ((uint8_t *)&t468 + 32), t466);
        GM_ST(gm_b16_8, ((uint8_t *)&t468 + 48), t467);
        F.l46 = t468;
        void * t469 = (void *)((uint8_t *)&F.l46);
        uint64_t t470 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t471 = (uintptr_t)((uint64_t)4ULL);
        if (t470 > t471 || t471 > (uintptr_t)((uint64_t)4ULL)) gm_trap_slice(t470, t471, ((uint64_t)4ULL), gm_srcg0ns322676oc, 549, 5);
        uint64_t t472 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t473;
        GM_ST(void *, (uint8_t *)&t473, (void *)(((uint8_t *)t469 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t473 + 8, (uintptr_t)(t472));
        gm_l47 = t473;
        gm_l48 = ((uint64_t)0ULL);
    }
    {
        {
            for (;;) {
                uint64_t t474 = gm_l48;
                uint64_t t475 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                uint8_t t476 = (t474 < t475);
                if (!t476) {
                    goto gm_lb61;
                }
                {
                    void * t477 = GM_LD(void *, ((uint8_t *)&gm_l47));
                    uint64_t t478 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                    uint64_t t479 = gm_l48;
                    {
                        {
                            gm_b16_8 t480 = GM_LD(gm_b16_8, (((uint8_t *)t477) + (uintptr_t)t479 * 16));
                            gm_l49 = t480;
                            void * t481 = gm_l3;
                            gm_b16_8 t482 = gm_l49;
                            void * t483 = gm_f_get_9rme0gb(t481, t482);
                            gm_l105 = t483;
                            {
                                {
                                    uint8_t t484 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l105)) != 0);
                                    if (t484) {
                                        goto gm_lb67;
                                    }
                                    gm_l104 = ((uint8_t)0);
                                    goto gm_lb66;
                                }
gm_lb67:;
                                {
                                    gm_l104 = ((uint8_t)1);
                                    goto gm_lb66;
                                }
gm_lb68:;
                                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 550, 12);
                            }
gm_lb66:;
                            uint8_t t485 = gm_l104;
                            if (t485) {
                                void * t486 = (void *)((uint8_t *)&F.l44);
                                gm_b16_8 t487 = gm_l49;
                                gm_tftb1odc739t5 t488 = gm_f_push_3ef746b(t486, t487);
                                gm_l106 = t488;
                                uint8_t t489 = (uint8_t)!((((uint8_t *)&gm_l106))[0] != 0);
                                if (!t489) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l106) + 1, gm_srcg0ns322676oc, 550, 35);
                            }
                            goto gm_lb64;
                        }
gm_lb65:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 549, 5);
                    }
gm_lb64:;
                }
gm_lb63:;
                uint64_t t490 = gm_l48;
                uint64_t r491; bool o492 = __builtin_add_overflow(t490, ((uint64_t)1ULL), &r491);
                uint64_t t493 = r491;
                gm_l48 = t493;
            }
        }
gm_lb61:;
        uint64_t t494 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 8));
        uint8_t t495 = (t494 > ((uint64_t)1ULL));
        if (t495) {
            uint64_t t496 = gm_l2;
            gm_b16_8 t497 = gm_l0;
            void * t498 = GM_LD(void *, ((uint8_t *)&F.l44));
            uint64_t t499 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t499)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t499, gm_srcg0ns322676oc, 556, 13);
            gm_b16_8 t500 = GM_LD(gm_b16_8, (((uint8_t *)t498) + (uintptr_t)((uint64_t)0ULL) * 16));
            void * t501 = GM_LD(void *, ((uint8_t *)&F.l44));
            uint64_t t502 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t502)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t502, gm_srcg0ns322676oc, 557, 13);
            gm_b16_8 t503 = GM_LD(gm_b16_8, (((uint8_t *)t501) + (uintptr_t)((uint64_t)1ULL) * 16));
            gm_tfeab279c16rr t504;
            GM_ST(gm_b16_8, ((uint8_t *)&t504 + 0), t497);
            GM_ST(gm_b16_8, ((uint8_t *)&t504 + 16), t500);
            GM_ST(gm_b16_8, ((uint8_t *)&t504 + 32), t503);
            gm_f_format_4af676s((void *)&gm_l107, (void *)NULL, cx, NULL, &t504);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t505 = (uint8_t)!((((uint8_t *)&gm_l107))[0] != 0);
            if (!t505) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l107) + 8, gm_srcg0ns322676oc, 553, 29);
            gm_b16_8 t506 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l107) + 8));
            gm_b16_8 t507 = GM_LD(gm_b16_8, ((uint8_t *)&t506));
            gm_tf1bjg81u0glv t508;
            gm_f_mfail_uqq7o7g((void *)&t508, t496, t507);
            gm_tmn9qm2bp7f46 t509;
            memset(&t509, 0, sizeof t509);
            ((uint8_t *)&t509)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t509 + 8), t508);
            memmove(gm_out, ((uint8_t *)&t509), 104);
            goto gm_fdone;
        }
    }
    {
        gm_toq2f2q4opoa3 t510;
        memset(&t510, 0, sizeof t510);
        GM_ST(uint8_t, ((uint8_t *)&t510 + 0), ((uint8_t)0ULL));
        F.l50 = t510;
        {
            {
                void * t511 = gm_l3;
                gm_b16_8 t512;
                GM_ST(void *, (uint8_t *)&t512, (void *)(gm_sla3alt5340mf));
                GM_ST(uintptr_t, (uint8_t *)&t512 + 8, (uintptr_t)(7));
                gm_b16_8 t513 = gm_l0;
                gm_f_dep_string_mmgrsgi((void *)&gm_l108, (void *)NULL, cx, NULL, t511, t512, t513);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t514 = (uint8_t)!((((uint8_t *)&gm_l108))[0] != 0);
                if (t514) {
                } else {
                    gm_tf1bjg81u0glv t515 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l108) + 8));
                    gm_tmn9qm2bp7f46 t516;
                    memset(&t516, 0, sizeof t516);
                    ((uint8_t *)&t516)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t516 + 8), t515);
                    memmove(gm_out, ((uint8_t *)&t516), 104);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&gm_l109), (((uint8_t *)&gm_l108) + 8), 24);
                uint8_t t517 = (uint8_t)((((uint8_t *)&gm_l109))[16] != 0);
                if (!t517) {
                    goto gm_lb70;
                }
                gm_b16_8 t518 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l109));
                gm_l51 = t518;
                gm_b16_8 t519 = gm_l51;
                gm_b16_8 t520 = gm_f_trim_ascii_c9j36d1(t519);
                gm_f_parse_version_req_alipvfi((void *)&gm_l110, (void *)NULL, cx, NULL, t520);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t521 = (uint8_t)!((((uint8_t *)&gm_l110))[0] != 0);
                if (t521) {
                    memmove(((uint8_t *)&gm_l111), (((uint8_t *)&gm_l110) + 8), 40);
                } else {
                    gm_b16_8 t522 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l110) + 8));
                    gm_l52 = t522;
                    void * t523 = gm_l3;
                    gm_b16_8 t524;
                    GM_ST(void *, (uint8_t *)&t524, (void *)(gm_sla3alt5340mf));
                    GM_ST(uintptr_t, (uint8_t *)&t524 + 8, (uintptr_t)(7));
                    void * t525 = gm_f_get_9rme0gb(t523, t524);
                    gm_l112 = t525;
                    uint8_t t526 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l112)) != 0);
                    if (!t526) gm_trap("unwrap of null", gm_srcg0ns322676oc, 563, 27);
                    void * t527 = GM_LD(void *, ((uint8_t *)&gm_l112));
                    uint64_t t528 = GM_LD(uint64_t, (((uint8_t *)t527) + 64));
                    gm_b16_8 t529 = gm_l51;
                    gm_b16_8 t530 = gm_l52;
                    gm_t6j6smj7qoctu t531;
                    GM_ST(gm_b16_8, ((uint8_t *)&t531 + 0), t529);
                    GM_ST(gm_b16_8, ((uint8_t *)&t531 + 16), t530);
                    gm_f_format_pah1p62((void *)&gm_l113, (void *)NULL, cx, NULL, &t531);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t532 = (uint8_t)!((((uint8_t *)&gm_l113))[0] != 0);
                    if (!t532) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l113) + 8, gm_srcg0ns322676oc, 563, 51);
                    gm_b16_8 t533 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l113) + 8));
                    gm_b16_8 t534 = GM_LD(gm_b16_8, ((uint8_t *)&t533));
                    gm_tf1bjg81u0glv t535;
                    gm_f_mfail_uqq7o7g((void *)&t535, t528, t534);
                    gm_tmn9qm2bp7f46 t536;
                    memset(&t536, 0, sizeof t536);
                    ((uint8_t *)&t536)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t536 + 8), t535);
                    memmove(gm_out, ((uint8_t *)&t536), 104);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&F.l53), ((uint8_t *)&gm_l111), 40);
                gm_tin97tt4ud0s1 t537 = GM_LD(gm_tin97tt4ud0s1, ((uint8_t *)&F.l53));
                gm_toq2f2q4opoa3 t538;
                GM_ST(uint8_t, (uint8_t *)&t538 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t538 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t538 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t538 + 0), ((uint8_t)1ULL));
                GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&t538 + 8), t537);
                F.l50 = t538;
                goto gm_lb69;
            }
gm_lb70:;
            {
                {
                    void * t539 = gm_l3;
                    gm_b16_8 t540;
                    GM_ST(void *, (uint8_t *)&t540, (void *)(gm_s420tm9ua7ggg));
                    GM_ST(uintptr_t, (uint8_t *)&t540 + 8, (uintptr_t)(3));
                    gm_b16_8 t541 = gm_l0;
                    gm_f_dep_string_mmgrsgi((void *)&gm_l114, (void *)NULL, cx, NULL, t539, t540, t541);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t542 = (uint8_t)!((((uint8_t *)&gm_l114))[0] != 0);
                    if (t542) {
                    } else {
                        gm_tf1bjg81u0glv t543 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l114) + 8));
                        gm_tmn9qm2bp7f46 t544;
                        memset(&t544, 0, sizeof t544);
                        ((uint8_t *)&t544)[0] = 1;
                        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t544 + 8), t543);
                        memmove(gm_out, ((uint8_t *)&t544), 104);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&gm_l115), (((uint8_t *)&gm_l114) + 8), 24);
                    uint8_t t545 = (uint8_t)((((uint8_t *)&gm_l115))[16] != 0);
                    if (!t545) {
                        goto gm_lb72;
                    }
                    gm_b16_8 t546 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l115));
                    gm_l54 = t546;
                    gm_b16_8 t547 = gm_l54;
                    gm_toq2f2q4opoa3 t548;
                    GM_ST(uint8_t, (uint8_t *)&t548 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t548 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t548 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t548 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t548 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t548 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t548 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t548 + 8), t547);
                    F.l50 = t548;
                    goto gm_lb71;
                }
gm_lb72:;
                {
                    {
                        void * t549 = gm_l3;
                        gm_b16_8 t550;
                        GM_ST(void *, (uint8_t *)&t550, (void *)(gm_s0jho24omhfui));
                        GM_ST(uintptr_t, (uint8_t *)&t550 + 8, (uintptr_t)(6));
                        gm_b16_8 t551 = gm_l0;
                        gm_f_dep_string_mmgrsgi((void *)&gm_l116, (void *)NULL, cx, NULL, t549, t550, t551);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t552 = (uint8_t)!((((uint8_t *)&gm_l116))[0] != 0);
                        if (t552) {
                        } else {
                            gm_tf1bjg81u0glv t553 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l116) + 8));
                            gm_tmn9qm2bp7f46 t554;
                            memset(&t554, 0, sizeof t554);
                            ((uint8_t *)&t554)[0] = 1;
                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t554 + 8), t553);
                            memmove(gm_out, ((uint8_t *)&t554), 104);
                            goto gm_fdone;
                        }
                        memmove(((uint8_t *)&gm_l117), (((uint8_t *)&gm_l116) + 8), 24);
                        uint8_t t555 = (uint8_t)((((uint8_t *)&gm_l117))[16] != 0);
                        if (!t555) {
                            goto gm_lb74;
                        }
                        gm_b16_8 t556 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l117));
                        gm_l55 = t556;
                        gm_b16_8 t557 = gm_l55;
                        gm_toq2f2q4opoa3 t558;
                        GM_ST(uint8_t, (uint8_t *)&t558 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t558 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t558 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t558 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t558 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t558 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t558 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t558 + 8), t557);
                        F.l50 = t558;
                        goto gm_lb73;
                    }
gm_lb74:;
                    {
                        {
                            void * t559 = gm_l3;
                            gm_b16_8 t560;
                            GM_ST(void *, (uint8_t *)&t560, (void *)(gm_sf9ilivr4c86m));
                            GM_ST(uintptr_t, (uint8_t *)&t560 + 8, (uintptr_t)(3));
                            gm_b16_8 t561 = gm_l0;
                            gm_f_dep_string_mmgrsgi((void *)&gm_l118, (void *)NULL, cx, NULL, t559, t560, t561);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t562 = (uint8_t)!((((uint8_t *)&gm_l118))[0] != 0);
                            if (t562) {
                            } else {
                                gm_tf1bjg81u0glv t563 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l118) + 8));
                                gm_tmn9qm2bp7f46 t564;
                                memset(&t564, 0, sizeof t564);
                                ((uint8_t *)&t564)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t564 + 8), t563);
                                memmove(gm_out, ((uint8_t *)&t564), 104);
                                goto gm_fdone;
                            }
                            memmove(((uint8_t *)&gm_l119), (((uint8_t *)&gm_l118) + 8), 24);
                            uint8_t t565 = (uint8_t)((((uint8_t *)&gm_l119))[16] != 0);
                            if (!t565) {
                                goto gm_lb76;
                            }
                            gm_b16_8 t566 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l119));
                            gm_l56 = t566;
                            uint64_t t567 = GM_LD(uint64_t, (((uint8_t *)&gm_l56) + 8));
                            uint8_t t568 = (t567 > ((uint64_t)0ULL));
                            gm_l57 = t568;
                            gm_b16_8 t569 = gm_l56;
                            gm_l58 = t569;
                            gm_l59 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t570 = gm_l59;
                                    uint64_t t571 = GM_LD(uint64_t, (((uint8_t *)&gm_l58) + 8));
                                    uint8_t t572 = (t570 < t571);
                                    if (!t572) {
                                        goto gm_lb77;
                                    }
                                    {
                                        void * t573 = GM_LD(void *, ((uint8_t *)&gm_l58));
                                        uint64_t t574 = GM_LD(uint64_t, (((uint8_t *)&gm_l58) + 8));
                                        uint64_t t575 = gm_l59;
                                        {
                                            {
                                                uint8_t t576 = GM_LD(uint8_t, (((uint8_t *)t573) + (uintptr_t)t575 * 1));
                                                gm_l60 = t576;
                                                uint8_t t577 = gm_l60;
                                                uint8_t t578 = gm_f_is_hex_digit_q8s6utq(t577);
                                                uint8_t t579 = (uint8_t)!t578;
                                                if (t579) {
                                                    gm_l57 = ((uint8_t)0);
                                                }
                                                goto gm_lb80;
                                            }
gm_lb81:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 572, 9);
                                        }
gm_lb80:;
                                    }
gm_lb79:;
                                    uint64_t t580 = gm_l59;
                                    uint64_t r581; bool o582 = __builtin_add_overflow(t580, ((uint64_t)1ULL), &r581);
                                    uint64_t t583 = r581;
                                    gm_l59 = t583;
                                }
                            }
gm_lb77:;
                            uint8_t t584 = gm_l57;
                            uint8_t t585 = (uint8_t)!t584;
                            if (t585) {
                                uint64_t t586 = gm_l2;
                                gm_b16_8 t587 = gm_l0;
                                gm_tnpifv20irnph t588;
                                GM_ST(gm_b16_8, ((uint8_t *)&t588 + 0), t587);
                                gm_f_format_ll295o3((void *)&gm_l120, (void *)NULL, cx, NULL, t588);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t589 = (uint8_t)!((((uint8_t *)&gm_l120))[0] != 0);
                                if (!t589) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l120) + 8, gm_srcg0ns322676oc, 575, 41);
                                gm_b16_8 t590 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l120) + 8));
                                gm_b16_8 t591 = GM_LD(gm_b16_8, ((uint8_t *)&t590));
                                gm_tf1bjg81u0glv t592;
                                gm_f_mfail_uqq7o7g((void *)&t592, t586, t591);
                                gm_tmn9qm2bp7f46 t593;
                                memset(&t593, 0, sizeof t593);
                                ((uint8_t *)&t593)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t593 + 8), t592);
                                memmove(gm_out, ((uint8_t *)&t593), 104);
                                goto gm_fdone;
                            }
                            gm_b16_8 t594 = gm_l56;
                            gm_b16_8 t595 = gm_f_lower_ascii_uggh3ca((void *)NULL, cx, NULL, t594);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_toq2f2q4opoa3 t596;
                            GM_ST(uint8_t, (uint8_t *)&t596 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t596 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t596 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t596 + 24, 0);
                            GM_ST(uint64_t, (uint8_t *)&t596 + 32, 0);
                            GM_ST(uint64_t, (uint8_t *)&t596 + 40, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t596 + 0), ((uint8_t)4ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t596 + 8), t595);
                            F.l50 = t596;
                            goto gm_lb75;
                        }
gm_lb76:;
                    }
gm_lb75:;
                }
gm_lb73:;
            }
gm_lb71:;
        }
gm_lb69:;
    }
    {
        void * t597 = gm_l3;
        gm_b16_8 t598;
        GM_ST(void *, (uint8_t *)&t598, (void *)(gm_sfvspsnv23gbt));
        GM_ST(uintptr_t, (uint8_t *)&t598 + 8, (uintptr_t)(6));
        gm_b16_8 t599 = gm_l0;
        gm_f_dep_string_mmgrsgi((void *)&gm_l121, (void *)NULL, cx, NULL, t597, t598, t599);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t600 = (uint8_t)!((((uint8_t *)&gm_l121))[0] != 0);
        if (t600) {
        } else {
            gm_tf1bjg81u0glv t601 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l121) + 8));
            gm_tmn9qm2bp7f46 t602;
            memset(&t602, 0, sizeof t602);
            ((uint8_t *)&t602)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t602 + 8), t601);
            memmove(gm_out, ((uint8_t *)&t602), 104);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&gm_l61), (((uint8_t *)&gm_l121) + 8), 24);
        gm_b16_8 t603 = gm_l0;
        uint64_t t604 = gm_l2;
        gm_th2lqbo77pvsc t605 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l61));
        gm_f_check_subdir_2uljb06((void *)&gm_l122, (void *)NULL, cx, NULL, t603, &t605, t604);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t606 = (uint8_t)!((((uint8_t *)&gm_l122))[0] != 0);
        if (t606) {
        } else {
            gm_tf1bjg81u0glv t607 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l122) + 8));
            gm_tmn9qm2bp7f46 t608;
            memset(&t608, 0, sizeof t608);
            ((uint8_t *)&t608)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t608 + 8), t607);
            memmove(gm_out, ((uint8_t *)&t608), 104);
            goto gm_fdone;
        }
        gm_b16_8 t609 = gm_l42;
        gm_toq2f2q4opoa3 t610 = GM_LD(gm_toq2f2q4opoa3, ((uint8_t *)&F.l50));
        gm_th2lqbo77pvsc t611 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l61));
        gm_te5jk64pjl28k t612;
        GM_ST(gm_b16_8, ((uint8_t *)&t612 + 0), t609);
        GM_ST(gm_toq2f2q4opoa3, ((uint8_t *)&t612 + 16), t610);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t612 + 64), t611);
        gm_tnlfbo6r4vlhg t613;
        GM_ST(uint8_t, (uint8_t *)&t613 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t613 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t613 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t613 + 0), ((uint8_t)1ULL));
        GM_ST(gm_te5jk64pjl28k, ((uint8_t *)&t613 + 8), t612);
        gm_tmn9qm2bp7f46 t614;
        GM_ST(uint8_t, (uint8_t *)&t614 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t614 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t614 + 4, 0);
        ((uint8_t *)&t614)[0] = 0;
        GM_ST(gm_tnlfbo6r4vlhg, ((uint8_t *)&t614 + 8), t613);
        memmove(gm_out, ((uint8_t *)&t614), 104);
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
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_manifest_b61s3ln F;
    gm_b16_8 gm_l0; /* text */
    gm_tob1snuii5bme gm_l1; /* e */
    void * gm_l2; /* doc */
    void * gm_l4; /* pkg */
    void * gm_l5; /* t */
    void * gm_l6; /* item */
    gm_b16_8 gm_l7; /* n */
    gm_b16_8 gm_l8; /* problem */
    void * gm_l9; /* item */
    gm_b16_8 gm_l10; /* v */
    gm_b16_8 gm_l11; /* e */
    void * gm_l12; /* deps */
    void * gm_l13; /* t */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l17; /* problem */
    void * gm_l19; /* b */
    void * gm_l20; /* t */
    void * gm_l21; /* l */
    void * gm_l22; /* t */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_b16_8 gm_l26; /* level */
    void * gm_l27; /* ps */
    void * gm_l28; /* t */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint8_t gm_l32; /* ok */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint8_t gm_l35; /* c */
    uint8_t gm_l36; /* <tmp> */
    uint8_t gm_l37; /* <tmp> */
    uint8_t gm_l38; /* <tmp> */
    void * gm_l39; /* pt */
    GM_STACK_CHECK(&F, 12240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_trssmtuv8jr0v gm_l41;
    void * gm_l42;
    void * gm_l43;
    void * gm_l44;
    void * gm_l45;
    void * gm_l46;
    gm_th2lqbo77pvsc gm_l47;
    gm_b16_8 gm_l48;
    gm_th2lqbo77pvsc gm_l49;
    void * gm_l50;
    gm_th2lqbo77pvsc gm_l51;
    gm_b16_8 gm_l52;
    gm_tvtl3n8qta358 gm_l53;
    gm_tu1089hhijadf gm_l54;
    gm_tabb710d9mq2e gm_l55;
    void * gm_l56;
    void * gm_l57;
    void * gm_l58;
    gm_th2lqbo77pvsc gm_l59;
    gm_tmn9qm2bp7f46 gm_l60;
    gm_tftb1odc739t5 gm_l61;
    void * gm_l62;
    void * gm_l63;
    void * gm_l64;
    gm_tacj1cskem694 gm_l65;
    void * gm_l66;
    void * gm_l67;
    void * gm_l68;
    uint8_t gm_l69;
    gm_tf5srh4mp4b3i gm_l70;
    gm_tabb710d9mq2e gm_l71;
    gm_th2lqbo77pvsc gm_l72;
    gm_b16_8 gm_l73;
    gm_tabb710d9mq2e gm_l74;
    gm_tftb1odc739t5 gm_l75;
    void * gm_l76;
    void * gm_l77;
    void * gm_l78;
    uint8_t gm_l79;
    uint8_t gm_l80;
    uint8_t gm_l81;
    uint8_t gm_l82;
    uint8_t gm_l83;
    uint8_t gm_l84;
    gm_tabb710d9mq2e gm_l85;
    void * gm_l86;
    void * gm_l87;
    gm_tabb710d9mq2e gm_l88;
    gm_tabb710d9mq2e gm_l89;
    gm_tacj1cskem694 gm_l90;
    gm_tftb1odc739t5 gm_l91;
    {
        gm_b16_8 t45 = gm_l0;
        gm_f_parse_toml_gjtmf79((void *)&gm_l41, (void *)NULL, cx, NULL, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
        if (t46) {
            void * t47 = GM_LD(void *, (((uint8_t *)&gm_l41) + 8));
            gm_l42 = t47;
        } else {
            memmove(((uint8_t *)&gm_l1), (((uint8_t *)&gm_l41) + 8), 32);
            uint64_t t48 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
            gm_tf1bjg81u0glv t50;
            gm_f_mfail_uqq7o7g((void *)&t50, t48, t49);
            gm_tjgmtdrk88t7m t51;
            memset(&t51, 0, sizeof t51);
            ((uint8_t *)&t51)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t51 + 8), t50);
            memmove(gm_out, ((uint8_t *)&t51), 328);
            goto gm_fdone;
        }
        void * t52 = gm_l42;
        gm_l2 = t52;
        gm_th2lqbo77pvsc t53;
        memset(&t53, 0, sizeof t53);
        gm_th2lqbo77pvsc t54;
        memset(&t54, 0, sizeof t54);
        gm_trqfvmjtt4sij t55;
        gm_f_list_7393u00((void *)&t55, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ti0g3bmheuq9o t56 = GM_LD(gm_ti0g3bmheuq9o, ((uint8_t *)gm_cb60tlkkq8o));
        gm_trqfvmjtt4sij t57;
        gm_f_list_hs66gbq((void *)&t57, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t58;
        gm_f_list_7mv9vi7((void *)&t58, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tm43kqe3e8b4a t59;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t59 + 0), t53);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t59 + 24), t54);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t59 + 48), t55);
        GM_ST(gm_ti0g3bmheuq9o, ((uint8_t *)&t59 + 88), t56);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t59 + 240), t57);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t59 + 280), t58);
        F.l3 = t59;
        {
            {
                void * t60 = gm_l2;
                gm_b16_8 t61;
                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_snqvo5bgc2fch));
                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(7));
                void * t62 = gm_f_get_9rme0gb(t60, t61);
                gm_l43 = t62;
                uint8_t t63 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l43)) != 0);
                if (!t63) {
                    goto gm_lb2;
                }
                void * t64 = GM_LD(void *, ((uint8_t *)&gm_l43));
                gm_l4 = t64;
                void * t65 = gm_l4;
                void * t66 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t65) + 16)));
                gm_l44 = t66;
                uint8_t t67 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l44)) != 0);
                if (t67) {
                    void * t68 = GM_LD(void *, ((uint8_t *)&gm_l44));
                    gm_l45 = t68;
                } else {
                    void * t69 = gm_l4;
                    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)t69) + 64));
                    gm_b16_8 t71;
                    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sq3r7vbcbs4ll));
                    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(25));
                    gm_tf1bjg81u0glv t72;
                    gm_f_mfail_uqq7o7g((void *)&t72, t70, t71);
                    gm_tjgmtdrk88t7m t73;
                    memset(&t73, 0, sizeof t73);
                    ((uint8_t *)&t73)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t73 + 8), t72);
                    memmove(gm_out, ((uint8_t *)&t73), 328);
                    goto gm_fdone;
                }
                void * t74 = gm_l45;
                gm_l5 = t74;
                {
                    {
                        void * t75 = gm_l5;
                        gm_b16_8 t76;
                        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_sius9pki5u4e3));
                        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(4));
                        void * t77 = gm_f_get_9rme0gb(t75, t76);
                        gm_l46 = t77;
                        uint8_t t78 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l46)) != 0);
                        if (!t78) {
                            goto gm_lb4;
                        }
                        void * t79 = GM_LD(void *, ((uint8_t *)&gm_l46));
                        gm_l6 = t79;
                        void * t80 = gm_l6;
                        gm_tktr4rmbindgv t81 = GM_LD(gm_tktr4rmbindgv, (((uint8_t *)t80) + 16));
                        gm_f_as_str_982o16v((void *)&gm_l47, &t81);
                        uint8_t t82 = (uint8_t)((((uint8_t *)&gm_l47))[16] != 0);
                        if (t82) {
                            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l47));
                            gm_l48 = t83;
                        } else {
                            void * t84 = gm_l6;
                            uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)t84) + 64));
                            gm_b16_8 t86;
                            GM_ST(void *, (uint8_t *)&t86, (void *)(gm_sob5t135sl7q9));
                            GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(33));
                            gm_tf1bjg81u0glv t87;
                            gm_f_mfail_uqq7o7g((void *)&t87, t85, t86);
                            gm_tjgmtdrk88t7m t88;
                            memset(&t88, 0, sizeof t88);
                            ((uint8_t *)&t88)[0] = 1;
                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t88 + 8), t87);
                            memmove(gm_out, ((uint8_t *)&t88), 328);
                            goto gm_fdone;
                        }
                        gm_b16_8 t89 = gm_l48;
                        gm_l7 = t89;
                        {
                            {
                                gm_b16_8 t90 = gm_l7;
                                gm_b16_8 t91;
                                GM_ST(void *, (uint8_t *)&t91, (void *)(gm_snqvo5bgc2fch));
                                GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(7));
                                gm_f_name_problem_6r4mp11((void *)&gm_l49, (void *)NULL, cx, NULL, t90, t91);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t92 = (uint8_t)((((uint8_t *)&gm_l49))[16] != 0);
                                if (!t92) {
                                    goto gm_lb6;
                                }
                                gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l49));
                                gm_l8 = t93;
                                void * t94 = gm_l6;
                                uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)t94) + 64));
                                gm_b16_8 t96 = gm_l8;
                                gm_tf1bjg81u0glv t97;
                                gm_f_mfail_uqq7o7g((void *)&t97, t95, t96);
                                gm_tjgmtdrk88t7m t98;
                                memset(&t98, 0, sizeof t98);
                                ((uint8_t *)&t98)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t98 + 8), t97);
                                memmove(gm_out, ((uint8_t *)&t98), 328);
                                goto gm_fdone;
                            }
gm_lb6:;
                        }
gm_lb5:;
                        gm_b16_8 t99 = gm_l7;
                        gm_th2lqbo77pvsc t100;
                        GM_ST(uint8_t, (uint8_t *)&t100 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t100 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t100 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t100), t99);
                        ((uint8_t *)&t100)[16] = 1;
                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l3), t100);
                        goto gm_lb3;
                    }
gm_lb4:;
                }
gm_lb3:;
                {
                    {
                        void * t101 = gm_l5;
                        gm_b16_8 t102;
                        GM_ST(void *, (uint8_t *)&t102, (void *)(gm_sla3alt5340mf));
                        GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(7));
                        void * t103 = gm_f_get_9rme0gb(t101, t102);
                        gm_l50 = t103;
                        uint8_t t104 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l50)) != 0);
                        if (!t104) {
                            goto gm_lb8;
                        }
                        void * t105 = GM_LD(void *, ((uint8_t *)&gm_l50));
                        gm_l9 = t105;
                        void * t106 = gm_l9;
                        gm_tktr4rmbindgv t107 = GM_LD(gm_tktr4rmbindgv, (((uint8_t *)t106) + 16));
                        gm_f_as_str_982o16v((void *)&gm_l51, &t107);
                        uint8_t t108 = (uint8_t)((((uint8_t *)&gm_l51))[16] != 0);
                        if (t108) {
                            gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l51));
                            gm_l52 = t109;
                        } else {
                            void * t110 = gm_l9;
                            uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)t110) + 64));
                            gm_b16_8 t112;
                            GM_ST(void *, (uint8_t *)&t112, (void *)(gm_su0gvqh5vvvlj));
                            GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(36));
                            gm_tf1bjg81u0glv t113;
                            gm_f_mfail_uqq7o7g((void *)&t113, t111, t112);
                            gm_tjgmtdrk88t7m t114;
                            memset(&t114, 0, sizeof t114);
                            ((uint8_t *)&t114)[0] = 1;
                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t114 + 8), t113);
                            memmove(gm_out, ((uint8_t *)&t114), 328);
                            goto gm_fdone;
                        }
                        gm_b16_8 t115 = gm_l52;
                        gm_l10 = t115;
                        gm_b16_8 t116 = gm_l10;
                        gm_f_parse_version_9a6ns50((void *)&gm_l53, (void *)NULL, cx, NULL, t116);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                        if (t117) {
                            memmove(((uint8_t *)&gm_l54), (((uint8_t *)&gm_l53) + 8), 56);
                        } else {
                            gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l53) + 8));
                            gm_l11 = t118;
                            void * t119 = gm_l9;
                            uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)t119) + 64));
                            gm_b16_8 t121 = gm_l10;
                            gm_b16_8 t122 = gm_l11;
                            gm_t6j6smj7qoctu t123;
                            GM_ST(gm_b16_8, ((uint8_t *)&t123 + 0), t121);
                            GM_ST(gm_b16_8, ((uint8_t *)&t123 + 16), t122);
                            gm_f_format_7duae7m((void *)&gm_l55, (void *)NULL, cx, NULL, &t123);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                            if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 8, gm_srcg0ns322676oc, 290, 42);
                            gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
                            gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                            gm_tf1bjg81u0glv t127;
                            gm_f_mfail_uqq7o7g((void *)&t127, t120, t126);
                            gm_tjgmtdrk88t7m t128;
                            memset(&t128, 0, sizeof t128);
                            ((uint8_t *)&t128)[0] = 1;
                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t128 + 8), t127);
                            memmove(gm_out, ((uint8_t *)&t128), 328);
                            goto gm_fdone;
                        }
                        gm_b16_8 t129 = gm_l10;
                        gm_th2lqbo77pvsc t130;
                        GM_ST(uint8_t, (uint8_t *)&t130 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t130 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t130 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t130), t129);
                        ((uint8_t *)&t130)[16] = 1;
                        gm_tu1089hhijadf t131 = GM_LD(gm_tu1089hhijadf, ((uint8_t *)&gm_l54));
                        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l3) + 24), t130);
                        goto gm_lb7;
                    }
gm_lb8:;
                }
gm_lb7:;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        {
            {
                void * t132 = gm_l2;
                gm_b16_8 t133;
                GM_ST(void *, (uint8_t *)&t133, (void *)(gm_skof48dte1s1o));
                GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(12));
                void * t134 = gm_f_get_9rme0gb(t132, t133);
                gm_l56 = t134;
                uint8_t t135 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l56)) != 0);
                if (!t135) {
                    goto gm_lb10;
                }
                void * t136 = GM_LD(void *, ((uint8_t *)&gm_l56));
                gm_l12 = t136;
                void * t137 = gm_l12;
                void * t138 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t137) + 16)));
                gm_l57 = t138;
                uint8_t t139 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l57)) != 0);
                if (t139) {
                    void * t140 = GM_LD(void *, ((uint8_t *)&gm_l57));
                    gm_l58 = t140;
                } else {
                    void * t141 = gm_l12;
                    uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)t141) + 64));
                    gm_b16_8 t143;
                    GM_ST(void *, (uint8_t *)&t143, (void *)(gm_sfag3nu74qn7r));
                    GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(30));
                    gm_tf1bjg81u0glv t144;
                    gm_f_mfail_uqq7o7g((void *)&t144, t142, t143);
                    gm_tjgmtdrk88t7m t145;
                    memset(&t145, 0, sizeof t145);
                    ((uint8_t *)&t145)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t145 + 8), t144);
                    memmove(gm_out, ((uint8_t *)&t145), 328);
                    goto gm_fdone;
                }
                void * t146 = gm_l58;
                gm_l13 = t146;
                void * t147 = gm_l13;
                gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)t147));
                gm_l14 = t148;
                gm_l15 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t149 = gm_l15;
                        uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                        uint8_t t151 = (t149 < t150);
                        if (!t151) {
                            goto gm_lb11;
                        }
                        {
                            void * t152 = GM_LD(void *, ((uint8_t *)&gm_l14));
                            uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                            uint64_t t154 = gm_l15;
                            {
                                {
                                    memmove(((uint8_t *)&F.l16), (((uint8_t *)t152) + (uintptr_t)t154 * 72), 72);
                                    {
                                        {
                                            gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
                                            gm_b16_8 t156;
                                            GM_ST(void *, (uint8_t *)&t156, (void *)(gm_s8f9rv6hrr024));
                                            GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(10));
                                            gm_f_name_problem_6r4mp11((void *)&gm_l59, (void *)NULL, cx, NULL, t155, t156);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t157 = (uint8_t)((((uint8_t *)&gm_l59))[16] != 0);
                                            if (!t157) {
                                                goto gm_lb17;
                                            }
                                            gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l59));
                                            gm_l17 = t158;
                                            uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 64));
                                            gm_b16_8 t160 = gm_l17;
                                            gm_tf1bjg81u0glv t161;
                                            gm_f_mfail_uqq7o7g((void *)&t161, t159, t160);
                                            gm_tjgmtdrk88t7m t162;
                                            memset(&t162, 0, sizeof t162);
                                            ((uint8_t *)&t162)[0] = 1;
                                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t162 + 8), t161);
                                            memmove(gm_out, ((uint8_t *)&t162), 328);
                                            goto gm_fdone;
                                        }
gm_lb17:;
                                    }
gm_lb16:;
                                    gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
                                    uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 64));
                                    gm_tktr4rmbindgv t165 = GM_LD(gm_tktr4rmbindgv, (((uint8_t *)&F.l16) + 16));
                                    gm_f_parse_dependency_8qmcjrm((void *)&gm_l60, (void *)NULL, cx, NULL, t163, &t165, t164);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t166 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                    if (t166) {
                                    } else {
                                        gm_tf1bjg81u0glv t167 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l60) + 8));
                                        gm_tjgmtdrk88t7m t168;
                                        memset(&t168, 0, sizeof t168);
                                        ((uint8_t *)&t168)[0] = 1;
                                        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t168 + 8), t167);
                                        memmove(gm_out, ((uint8_t *)&t168), 328);
                                        goto gm_fdone;
                                    }
                                    memmove(((uint8_t *)&F.l18), (((uint8_t *)&gm_l60) + 8), 96);
                                    void * t169 = (void *)(((uint8_t *)&F.l3) + 48);
                                    gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
                                    uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 64));
                                    gm_tnlfbo6r4vlhg t172 = GM_LD(gm_tnlfbo6r4vlhg, ((uint8_t *)&F.l18));
                                    gm_t7tiv6vif86qg t173;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t173 + 0), t170);
                                    GM_ST(uint64_t, ((uint8_t *)&t173 + 16), t171);
                                    GM_ST(gm_tnlfbo6r4vlhg, ((uint8_t *)&t173 + 24), t172);
                                    gm_tftb1odc739t5 t174 = gm_f_push_pdknc9g(t169, &t173);
                                    gm_l61 = t174;
                                    uint8_t t175 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                    if (!t175) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srcg0ns322676oc, 300, 13);
                                    goto gm_lb14;
                                }
gm_lb15:;
                                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 297, 9);
                            }
gm_lb14:;
                        }
gm_lb13:;
                        uint64_t t176 = gm_l15;
                        uint64_t r177; bool o178 = __builtin_add_overflow(t176, ((uint64_t)1ULL), &r177);
                        uint64_t t179 = r177;
                        gm_l15 = t179;
                    }
                }
gm_lb11:;
                goto gm_lb9;
            }
gm_lb10:;
        }
gm_lb9:;
    }
    {
        {
            {
                void * t180 = gm_l2;
                gm_b16_8 t181;
                GM_ST(void *, (uint8_t *)&t181, (void *)(gm_slllafvm0pbeb));
                GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(5));
                void * t182 = gm_f_get_9rme0gb(t180, t181);
                gm_l62 = t182;
                uint8_t t183 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l62)) != 0);
                if (!t183) {
                    goto gm_lb19;
                }
                void * t184 = GM_LD(void *, ((uint8_t *)&gm_l62));
                gm_l19 = t184;
                void * t185 = gm_l19;
                void * t186 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t185) + 16)));
                gm_l63 = t186;
                uint8_t t187 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l63)) != 0);
                if (t187) {
                    void * t188 = GM_LD(void *, ((uint8_t *)&gm_l63));
                    gm_l64 = t188;
                } else {
                    void * t189 = gm_l19;
                    uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)t189) + 64));
                    gm_b16_8 t191;
                    GM_ST(void *, (uint8_t *)&t191, (void *)(gm_s5tee1epg2e5a));
                    GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(23));
                    gm_tf1bjg81u0glv t192;
                    gm_f_mfail_uqq7o7g((void *)&t192, t190, t191);
                    gm_tjgmtdrk88t7m t193;
                    memset(&t193, 0, sizeof t193);
                    ((uint8_t *)&t193)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t193 + 8), t192);
                    memmove(gm_out, ((uint8_t *)&t193), 328);
                    goto gm_fdone;
                }
                void * t194 = gm_l64;
                gm_l20 = t194;
                void * t195 = gm_l20;
                gm_b16_8 t196;
                GM_ST(void *, (uint8_t *)&t196, (void *)(gm_s5ojatldfpfs2));
                GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(7));
                gm_f_parse_build_c2teosa((void *)&gm_l65, (void *)NULL, cx, NULL, t195, t196);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t197 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                if (t197) {
                } else {
                    gm_tf1bjg81u0glv t198 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l65) + 8));
                    gm_tjgmtdrk88t7m t199;
                    memset(&t199, 0, sizeof t199);
                    ((uint8_t *)&t199)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t199 + 8), t198);
                    memmove(gm_out, ((uint8_t *)&t199), 328);
                    goto gm_fdone;
                }
                memmove((((uint8_t *)&F.l3) + 88), (((uint8_t *)&gm_l65) + 8), 152);
                goto gm_lb18;
            }
gm_lb19:;
        }
gm_lb18:;
        {
            {
                void * t200 = gm_l2;
                gm_b16_8 t201;
                GM_ST(void *, (uint8_t *)&t201, (void *)(gm_sv1gci5lo7scg));
                GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(4));
                void * t202 = gm_f_get_9rme0gb(t200, t201);
                gm_l66 = t202;
                uint8_t t203 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l66)) != 0);
                if (!t203) {
                    goto gm_lb21;
                }
                void * t204 = GM_LD(void *, ((uint8_t *)&gm_l66));
                gm_l21 = t204;
                void * t205 = gm_l21;
                void * t206 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t205) + 16)));
                gm_l67 = t206;
                uint8_t t207 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l67)) != 0);
                if (t207) {
                    void * t208 = GM_LD(void *, ((uint8_t *)&gm_l67));
                    gm_l68 = t208;
                } else {
                    void * t209 = gm_l21;
                    uint64_t t210 = GM_LD(uint64_t, (((uint8_t *)t209) + 64));
                    gm_b16_8 t211;
                    GM_ST(void *, (uint8_t *)&t211, (void *)(gm_sna94ju8c8em3));
                    GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(47));
                    gm_tf1bjg81u0glv t212;
                    gm_f_mfail_uqq7o7g((void *)&t212, t210, t211);
                    gm_tjgmtdrk88t7m t213;
                    memset(&t213, 0, sizeof t213);
                    ((uint8_t *)&t213)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t213 + 8), t212);
                    memmove(gm_out, ((uint8_t *)&t213), 328);
                    goto gm_fdone;
                }
                void * t214 = gm_l68;
                gm_l22 = t214;
                void * t215 = gm_l22;
                gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)t215));
                gm_l23 = t216;
                gm_l24 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t217 = gm_l24;
                        uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                        uint8_t t219 = (t217 < t218);
                        if (!t219) {
                            goto gm_lb22;
                        }
                        {
                            void * t220 = GM_LD(void *, ((uint8_t *)&gm_l23));
                            uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                            uint64_t t222 = gm_l24;
                            {
                                {
                                    memmove(((uint8_t *)&F.l25), (((uint8_t *)t220) + (uintptr_t)t222 * 72), 72);
                                    {
                                        {
                                            gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
                                            gm_b16_8 t224;
                                            GM_ST(void *, (uint8_t *)&t224, (void *)(gm_s0nf1rg04f3m5));
                                            GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(3));
                                            uint8_t t225 = (!gm_eq0iqiubfm7e(((uint8_t *)&t223), ((uint8_t *)&t224)));
                                            if (!t225) {
                                                goto gm_lb28;
                                            }
                                            gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
                                            gm_tf5srh4mp4b3i t227 = gm_f_lint_rule_o3ccae1(t226);
                                            gm_l70 = t227;
                                            {
                                                {
                                                    uint8_t t228 = (uint8_t)((((uint8_t *)&gm_l70))[8] != 0);
                                                    if (t228) {
                                                        goto gm_lb30;
                                                    }
                                                    gm_l69 = ((uint8_t)1);
                                                    goto gm_lb29;
                                                }
gm_lb30:;
                                                {
                                                    gm_l69 = ((uint8_t)0);
                                                    goto gm_lb29;
                                                }
gm_lb31:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 310, 39);
                                            }
gm_lb29:;
                                            uint8_t t229 = gm_l69;
                                            if (!t229) {
                                                goto gm_lb28;
                                            }
                                            uint64_t t230 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 64));
                                            gm_b16_8 t231 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
                                            gm_tnpifv20irnph t232;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t232 + 0), t231);
                                            gm_f_format_55igra0((void *)&gm_l71, (void *)NULL, cx, NULL, t232);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t233 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                                            if (!t233) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 8, gm_srcg0ns322676oc, 311, 39);
                                            gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 8));
                                            gm_b16_8 t235 = GM_LD(gm_b16_8, ((uint8_t *)&t234));
                                            gm_tf1bjg81u0glv t236;
                                            gm_f_mfail_uqq7o7g((void *)&t236, t230, t235);
                                            gm_tjgmtdrk88t7m t237;
                                            memset(&t237, 0, sizeof t237);
                                            ((uint8_t *)&t237)[0] = 1;
                                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t237 + 8), t236);
                                            memmove(gm_out, ((uint8_t *)&t237), 328);
                                            goto gm_fdone;
                                        }
gm_lb28:;
                                    }
gm_lb27:;
                                    gm_tktr4rmbindgv t238 = GM_LD(gm_tktr4rmbindgv, (((uint8_t *)&F.l25) + 16));
                                    gm_f_as_str_982o16v((void *)&gm_l72, &t238);
                                    uint8_t t239 = (uint8_t)((((uint8_t *)&gm_l72))[16] != 0);
                                    if (t239) {
                                        gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l72));
                                        gm_l73 = t240;
                                    } else {
                                        gm_b16_8 t241;
                                        GM_ST(void *, (uint8_t *)&t241, (void *)(gm_sr4qbrk6tlgaa));
                                        GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(0));
                                        gm_l73 = t241;
                                    }
                                    gm_b16_8 t242 = gm_l73;
                                    gm_l26 = t242;
                                    gm_b16_8 t243 = gm_l26;
                                    gm_b16_8 t244;
                                    GM_ST(void *, (uint8_t *)&t244, (void *)(gm_slb6cj7ndgvdm));
                                    GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)(5));
                                    gm_b16_8 t245;
                                    GM_ST(void *, (uint8_t *)&t245, (void *)(gm_soofuqhnovv5o));
                                    GM_ST(uintptr_t, (uint8_t *)&t245 + 8, (uintptr_t)(4));
                                    gm_b16_8 t246;
                                    GM_ST(void *, (uint8_t *)&t246, (void *)(gm_s920j0georape));
                                    GM_ST(uintptr_t, (uint8_t *)&t246 + 8, (uintptr_t)(4));
                                    gm_tfeab279c16rr t247;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t247 + 0), t244);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t247 + 16), t245);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t247 + 32), t246);
                                    uint8_t t248 = 0;
                                    if (gm_eq0iqiubfm7e(((uint8_t *)&t247) + 0, ((uint8_t *)&t243))) t248 = 1;
                                    if (gm_eq0iqiubfm7e(((uint8_t *)&t247) + 16, ((uint8_t *)&t243))) t248 = 1;
                                    if (gm_eq0iqiubfm7e(((uint8_t *)&t247) + 32, ((uint8_t *)&t243))) t248 = 1;
                                    uint8_t t249 = (uint8_t)(!t248);
                                    if (t249) {
                                        uint64_t t250 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 64));
                                        gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
                                        gm_tnpifv20irnph t252;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t252 + 0), t251);
                                        gm_f_format_hvnghft((void *)&gm_l74, (void *)NULL, cx, NULL, t252);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t253 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                                        if (!t253) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 8, gm_srcg0ns322676oc, 315, 39);
                                        gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l74) + 8));
                                        gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                                        gm_tf1bjg81u0glv t256;
                                        gm_f_mfail_uqq7o7g((void *)&t256, t250, t255);
                                        gm_tjgmtdrk88t7m t257;
                                        memset(&t257, 0, sizeof t257);
                                        ((uint8_t *)&t257)[0] = 1;
                                        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t257 + 8), t256);
                                        memmove(gm_out, ((uint8_t *)&t257), 328);
                                        goto gm_fdone;
                                    }
                                    void * t258 = (void *)(((uint8_t *)&F.l3) + 280);
                                    gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
                                    gm_b16_8 t260 = gm_l26;
                                    uint64_t t261 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 64));
                                    gm_tftksr4op114e t262;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t262 + 0), t259);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t262 + 16), t260);
                                    GM_ST(uint64_t, ((uint8_t *)&t262 + 32), t261);
                                    gm_tftb1odc739t5 t263 = gm_f_push_dob2kfb(t258, &t262);
                                    gm_l75 = t263;
                                    uint8_t t264 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                                    if (!t264) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 1, gm_srcg0ns322676oc, 317, 13);
                                    goto gm_lb25;
                                }
gm_lb26:;
                                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 309, 9);
                            }
gm_lb25:;
                        }
gm_lb24:;
                        uint64_t t265 = gm_l24;
                        uint64_t r266; bool o267 = __builtin_add_overflow(t265, ((uint64_t)1ULL), &r266);
                        uint64_t t268 = r266;
                        gm_l24 = t268;
                    }
                }
gm_lb22:;
                goto gm_lb20;
            }
gm_lb21:;
        }
gm_lb20:;
    }
    {
        {
            {
                void * t269 = gm_l2;
                gm_b16_8 t270;
                GM_ST(void *, (uint8_t *)&t270, (void *)(gm_s6v08aos37nfe));
                GM_ST(uintptr_t, (uint8_t *)&t270 + 8, (uintptr_t)(7));
                void * t271 = gm_f_get_9rme0gb(t269, t270);
                gm_l76 = t271;
                uint8_t t272 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l76)) != 0);
                if (!t272) {
                    goto gm_lb33;
                }
                void * t273 = GM_LD(void *, ((uint8_t *)&gm_l76));
                gm_l27 = t273;
                void * t274 = gm_l27;
                void * t275 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t274) + 16)));
                gm_l77 = t275;
                uint8_t t276 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l77)) != 0);
                if (t276) {
                    void * t277 = GM_LD(void *, ((uint8_t *)&gm_l77));
                    gm_l78 = t277;
                } else {
                    void * t278 = gm_l27;
                    uint64_t t279 = GM_LD(uint64_t, (((uint8_t *)t278) + 64));
                    gm_b16_8 t280;
                    GM_ST(void *, (uint8_t *)&t280, (void *)(gm_slc3n2ku5uff5));
                    GM_ST(uintptr_t, (uint8_t *)&t280 + 8, (uintptr_t)(54));
                    gm_tf1bjg81u0glv t281;
                    gm_f_mfail_uqq7o7g((void *)&t281, t279, t280);
                    gm_tjgmtdrk88t7m t282;
                    memset(&t282, 0, sizeof t282);
                    ((uint8_t *)&t282)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t282 + 8), t281);
                    memmove(gm_out, ((uint8_t *)&t282), 328);
                    goto gm_fdone;
                }
                void * t283 = gm_l78;
                gm_l28 = t283;
                void * t284 = gm_l28;
                gm_b16_8 t285 = GM_LD(gm_b16_8, ((uint8_t *)t284));
                gm_l29 = t285;
                gm_l30 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t286 = gm_l30;
                        uint64_t t287 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                        uint8_t t288 = (t286 < t287);
                        if (!t288) {
                            goto gm_lb34;
                        }
                        {
                            void * t289 = GM_LD(void *, ((uint8_t *)&gm_l29));
                            uint64_t t290 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                            uint64_t t291 = gm_l30;
                            {
                                {
                                    memmove(((uint8_t *)&F.l31), (((uint8_t *)t289) + (uintptr_t)t291 * 72), 72);
                                    uint64_t t292 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 8));
                                    uint8_t t293 = (t292 > ((uint64_t)0ULL));
                                    gm_l32 = t293;
                                    gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
                                    gm_l33 = t294;
                                    gm_l34 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t295 = gm_l34;
                                            uint64_t t296 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                            uint8_t t297 = (t295 < t296);
                                            if (!t297) {
                                                goto gm_lb39;
                                            }
                                            {
                                                void * t298 = GM_LD(void *, ((uint8_t *)&gm_l33));
                                                uint64_t t299 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                                uint64_t t300 = gm_l34;
                                                {
                                                    {
                                                        uint8_t t301 = GM_LD(uint8_t, (((uint8_t *)t298) + (uintptr_t)t300 * 1));
                                                        gm_l35 = t301;
                                                        uint8_t t302 = gm_l35;
                                                        gm_l36 = t302;
                                                        uint8_t t303 = gm_l36;
                                                        uint8_t t304 = (((uint8_t)97ULL) <= t303);
                                                        if (t304) {
                                                            uint8_t t305 = gm_l36;
                                                            uint8_t t306 = (t305 <= ((uint8_t)122ULL));
                                                            gm_l82 = t306;
                                                        } else {
                                                            gm_l82 = ((uint8_t)0);
                                                        }
                                                        uint8_t t307 = gm_l82;
                                                        if (t307) {
                                                            gm_l81 = ((uint8_t)1);
                                                        } else {
                                                            uint8_t t308 = gm_l35;
                                                            gm_l37 = t308;
                                                            uint8_t t309 = gm_l37;
                                                            uint8_t t310 = (((uint8_t)65ULL) <= t309);
                                                            if (t310) {
                                                                uint8_t t311 = gm_l37;
                                                                uint8_t t312 = (t311 <= ((uint8_t)90ULL));
                                                                gm_l83 = t312;
                                                            } else {
                                                                gm_l83 = ((uint8_t)0);
                                                            }
                                                            uint8_t t313 = gm_l83;
                                                            gm_l81 = t313;
                                                        }
                                                        uint8_t t314 = gm_l81;
                                                        if (t314) {
                                                            gm_l80 = ((uint8_t)1);
                                                        } else {
                                                            uint8_t t315 = gm_l35;
                                                            gm_l38 = t315;
                                                            uint8_t t316 = gm_l38;
                                                            uint8_t t317 = (((uint8_t)48ULL) <= t316);
                                                            if (t317) {
                                                                uint8_t t318 = gm_l38;
                                                                uint8_t t319 = (t318 <= ((uint8_t)57ULL));
                                                                gm_l84 = t319;
                                                            } else {
                                                                gm_l84 = ((uint8_t)0);
                                                            }
                                                            uint8_t t320 = gm_l84;
                                                            gm_l80 = t320;
                                                        }
                                                        uint8_t t321 = gm_l80;
                                                        if (t321) {
                                                            gm_l79 = ((uint8_t)1);
                                                        } else {
                                                            uint8_t t322 = gm_l35;
                                                            gm_tqs5p4n2tq19v t323;
                                                            GM_ST(uint8_t, ((uint8_t *)&t323 + 0), ((uint8_t)95ULL));
                                                            GM_ST(uint8_t, ((uint8_t *)&t323 + 1), ((uint8_t)45ULL));
                                                            uint8_t t324 = 0;
                                                            if (gm_eqjmccqdb90f(((uint8_t *)&t323) + 0, ((uint8_t *)&t322))) t324 = 1;
                                                            if (gm_eqjmccqdb90f(((uint8_t *)&t323) + 1, ((uint8_t *)&t322))) t324 = 1;
                                                            uint8_t t325 = (uint8_t)(t324);
                                                            gm_l79 = t325;
                                                        }
                                                        uint8_t t326 = gm_l79;
                                                        uint8_t t327 = (uint8_t)!t326;
                                                        if (t327) {
                                                            gm_l32 = ((uint8_t)0);
                                                        }
                                                        goto gm_lb42;
                                                    }
gm_lb43:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 324, 13);
                                                }
gm_lb42:;
                                            }
gm_lb41:;
                                            uint64_t t328 = gm_l34;
                                            uint64_t r329; bool o330 = __builtin_add_overflow(t328, ((uint64_t)1ULL), &r329);
                                            uint64_t t331 = r329;
                                            gm_l34 = t331;
                                        }
                                    }
gm_lb39:;
                                    uint8_t t332 = gm_l32;
                                    uint8_t t333 = (uint8_t)!t332;
                                    if (t333) {
                                        uint64_t t334 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 64));
                                        gm_b16_8 t335 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
                                        gm_tnpifv20irnph t336;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t336 + 0), t335);
                                        gm_f_format_850ujed((void *)&gm_l85, (void *)NULL, cx, NULL, t336);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t337 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
                                        if (!t337) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l85) + 8, gm_srcg0ns322676oc, 328, 39);
                                        gm_b16_8 t338 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l85) + 8));
                                        gm_b16_8 t339 = GM_LD(gm_b16_8, ((uint8_t *)&t338));
                                        gm_tf1bjg81u0glv t340;
                                        gm_f_mfail_uqq7o7g((void *)&t340, t334, t339);
                                        gm_tjgmtdrk88t7m t341;
                                        memset(&t341, 0, sizeof t341);
                                        ((uint8_t *)&t341)[0] = 1;
                                        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t341 + 8), t340);
                                        memmove(gm_out, ((uint8_t *)&t341), 328);
                                        goto gm_fdone;
                                    }
                                    void * t342 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)&F.l31) + 16)));
                                    gm_l86 = t342;
                                    uint8_t t343 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l86)) != 0);
                                    if (t343) {
                                        void * t344 = GM_LD(void *, ((uint8_t *)&gm_l86));
                                        gm_l87 = t344;
                                    } else {
                                        uint64_t t345 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 64));
                                        gm_b16_8 t346 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
                                        gm_tnpifv20irnph t347;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t347 + 0), t346);
                                        gm_f_format_jh4rfvo((void *)&gm_l88, (void *)NULL, cx, NULL, t347);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t348 = (uint8_t)!((((uint8_t *)&gm_l88))[0] != 0);
                                        if (!t348) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l88) + 8, gm_srcg0ns322676oc, 330, 64);
                                        gm_b16_8 t349 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l88) + 8));
                                        gm_b16_8 t350 = GM_LD(gm_b16_8, ((uint8_t *)&t349));
                                        gm_tf1bjg81u0glv t351;
                                        gm_f_mfail_uqq7o7g((void *)&t351, t345, t350);
                                        gm_tjgmtdrk88t7m t352;
                                        memset(&t352, 0, sizeof t352);
                                        ((uint8_t *)&t352)[0] = 1;
                                        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t352 + 8), t351);
                                        memmove(gm_out, ((uint8_t *)&t352), 328);
                                        goto gm_fdone;
                                    }
                                    void * t353 = gm_l87;
                                    gm_l39 = t353;
                                    void * t354 = gm_l39;
                                    gm_b16_8 t355 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
                                    gm_tnpifv20irnph t356;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t356 + 0), t355);
                                    gm_f_format_1ncs69t((void *)&gm_l89, (void *)NULL, cx, NULL, t356);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t357 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                                    if (!t357) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l89) + 8, gm_srcg0ns322676oc, 331, 45);
                                    gm_b16_8 t358 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l89) + 8));
                                    gm_b16_8 t359 = GM_LD(gm_b16_8, ((uint8_t *)&t358));
                                    gm_f_parse_build_c2teosa((void *)&gm_l90, (void *)NULL, cx, NULL, t354, t359);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t360 = (uint8_t)!((((uint8_t *)&gm_l90))[0] != 0);
                                    if (t360) {
                                    } else {
                                        gm_tf1bjg81u0glv t361 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&gm_l90) + 8));
                                        gm_tjgmtdrk88t7m t362;
                                        memset(&t362, 0, sizeof t362);
                                        ((uint8_t *)&t362)[0] = 1;
                                        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t362 + 8), t361);
                                        memmove(gm_out, ((uint8_t *)&t362), 328);
                                        goto gm_fdone;
                                    }
                                    memmove(((uint8_t *)&F.l40), (((uint8_t *)&gm_l90) + 8), 152);
                                    void * t363 = (void *)(((uint8_t *)&F.l3) + 240);
                                    gm_b16_8 t364 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
                                    uint64_t t365 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 64));
                                    gm_ti0g3bmheuq9o t366 = GM_LD(gm_ti0g3bmheuq9o, ((uint8_t *)&F.l40));
                                    gm_t6b36gvdk984b t367;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t367 + 0), t364);
                                    GM_ST(uint64_t, ((uint8_t *)&t367 + 16), t365);
                                    GM_ST(gm_ti0g3bmheuq9o, ((uint8_t *)&t367 + 24), t366);
                                    gm_tftb1odc739t5 t368 = gm_f_push_7hnmra3(t363, &t367);
                                    gm_l91 = t368;
                                    uint8_t t369 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                                    if (!t369) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 1, gm_srcg0ns322676oc, 332, 13);
                                    goto gm_lb37;
                                }
gm_lb38:;
                                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 322, 9);
                            }
gm_lb37:;
                        }
gm_lb36:;
                        uint64_t t370 = gm_l30;
                        uint64_t r371; bool o372 = __builtin_add_overflow(t370, ((uint64_t)1ULL), &r371);
                        uint64_t t373 = r371;
                        gm_l30 = t373;
                    }
                }
gm_lb34:;
                goto gm_lb32;
            }
gm_lb33:;
        }
gm_lb32:;
    }
    {
        gm_tm43kqe3e8b4a t374 = GM_LD(gm_tm43kqe3e8b4a, ((uint8_t *)&F.l3));
        gm_tjgmtdrk88t7m t375;
        GM_ST(uint8_t, (uint8_t *)&t375 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t375 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t375 + 4, 0);
        ((uint8_t *)&t375)[0] = 0;
        GM_ST(gm_tm43kqe3e8b4a, ((uint8_t *)&t375 + 8), t374);
        memmove(gm_out, ((uint8_t *)&t375), 328);
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
inline __attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_spec_equal_foa1umn F;
    gm_b16_8 gm_l4; /* x */
    gm_b16_8 gm_l5; /* y */
    gm_b16_8 gm_l6; /* x */
    gm_b16_8 gm_l7; /* y */
    gm_b16_8 gm_l8; /* x */
    gm_b16_8 gm_l9; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 436u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    {
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)0ULL));
            if (!t21) {
                goto gm_lb2;
            }
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t23 = (uint8_t)((t22 == ((uint8_t)0ULL)));
            gm_toq2f2q4opoa3 t24 = GM_LD(gm_toq2f2q4opoa3, ((uint8_t *)a1));
            gm_toq2f2q4opoa3 t25;
            memset(&t25, 0, sizeof t25);
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            gm_l10 = t23;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)1ULL));
            if (!t27) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&F.l2), (((uint8_t *)a0) + 8), 40);
            {
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
                    if (!t29) {
                        goto gm_lb5;
                    }
                    memmove(((uint8_t *)&F.l3), (((uint8_t *)a1) + 8), 40);
                    void * t30 = (void *)((uint8_t *)&F.l2);
                    void * t31 = (void *)((uint8_t *)&F.l3);
                    uint8_t t32 = gm_f_req_equal_7ia8ip6(t30, t31);
                    gm_l11 = t32;
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    gm_l11 = ((uint8_t)0);
                    goto gm_lb4;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 44, 24);
            }
gm_lb4:;
            uint8_t t33 = gm_l11;
            gm_l10 = t33;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)2ULL));
            if (!t35) {
                goto gm_lb7;
            }
            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l4 = t36;
            {
                {
                    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t38 = (uint8_t)(t37 == ((uint8_t)2ULL));
                    if (!t38) {
                        goto gm_lb9;
                    }
                    gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
                    gm_l5 = t39;
                    gm_b16_8 t40 = gm_l4;
                    gm_b16_8 t41 = gm_l5;
                    uint8_t t42 = (gm_eq0iqiubfm7e(((uint8_t *)&t40), ((uint8_t *)&t41)));
                    gm_l12 = t42;
                    goto gm_lb8;
                }
gm_lb9:;
                {
                    gm_l12 = ((uint8_t)0);
                    goto gm_lb8;
                }
gm_lb10:;
                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 48, 20);
            }
gm_lb8:;
            uint8_t t43 = gm_l12;
            gm_l10 = t43;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)3ULL));
            if (!t45) {
                goto gm_lb11;
            }
            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l6 = t46;
            {
                {
                    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t48 = (uint8_t)(t47 == ((uint8_t)3ULL));
                    if (!t48) {
                        goto gm_lb13;
                    }
                    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
                    gm_l7 = t49;
                    gm_b16_8 t50 = gm_l6;
                    gm_b16_8 t51 = gm_l7;
                    uint8_t t52 = (gm_eq0iqiubfm7e(((uint8_t *)&t50), ((uint8_t *)&t51)));
                    gm_l13 = t52;
                    goto gm_lb12;
                }
gm_lb13:;
                {
                    gm_l13 = ((uint8_t)0);
                    goto gm_lb12;
                }
gm_lb14:;
                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 52, 23);
            }
gm_lb12:;
            uint8_t t53 = gm_l13;
            gm_l10 = t53;
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)4ULL));
            if (!t55) {
                goto gm_lb15;
            }
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l8 = t56;
            {
                {
                    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t58 = (uint8_t)(t57 == ((uint8_t)4ULL));
                    if (!t58) {
                        goto gm_lb17;
                    }
                    gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
                    gm_l9 = t59;
                    gm_b16_8 t60 = gm_l8;
                    gm_b16_8 t61 = gm_l9;
                    uint8_t t62 = (gm_eq0iqiubfm7e(((uint8_t *)&t60), ((uint8_t *)&t61)));
                    gm_l14 = t62;
                    goto gm_lb16;
                }
gm_lb17:;
                {
                    gm_l14 = ((uint8_t)0);
                    goto gm_lb16;
                }
gm_lb18:;
                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 56, 20);
            }
gm_lb16:;
            uint8_t t63 = gm_l14;
            gm_l10 = t63;
            goto gm_lb1;
        }
gm_lb15:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 42, 12);
    }
gm_lb1:;
    uint8_t t64 = gm_l10;
    F.ret = t64;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn_c(gm_toq2f2q4opoa3 a0, gm_toq2f2q4opoa3 a1) { return gm_f_spec_equal_foa1umn(&a0, &a1); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_spec_text_9k2gjft(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_spec_text_9k2gjft F;
    gm_b16_8 gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 550u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_tabb710d9mq2e gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    {
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)0ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s7j1ioljmsuka));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(18));
            gm_l5 = t10;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)1ULL));
            if (!t12) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&F.l1), (((uint8_t *)a0) + 8), 40);
            void * t13 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t14 = gm_f_req_string_fk059jr((void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t15;
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
            gm_f_format_rlu0tid((void *)&gm_l6, (void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcg0ns322676oc, 34, 20);
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            gm_l5 = t18;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)2ULL));
            if (!t20) {
                goto gm_lb4;
            }
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l2 = t21;
            gm_b16_8 t22 = gm_l2;
            gm_tnpifv20irnph t23;
            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t22);
            gm_f_format_79gt991((void *)&gm_l7, (void *)NULL, cx, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcg0ns322676oc, 35, 16);
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            gm_l5 = t26;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)3ULL));
            if (!t28) {
                goto gm_lb5;
            }
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l3 = t29;
            gm_b16_8 t30 = gm_l3;
            gm_tnpifv20irnph t31;
            GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
            gm_f_format_4pbmdvl((void *)&gm_l8, (void *)NULL, cx, NULL, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcg0ns322676oc, 36, 19);
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
            gm_l5 = t34;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)4ULL));
            if (!t36) {
                goto gm_lb6;
            }
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l4 = t37;
            gm_b16_8 t38 = gm_l4;
            gm_tnpifv20irnph t39;
            GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t38);
            gm_f_format_h0dnf9n((void *)&gm_l9, (void *)NULL, cx, NULL, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcg0ns322676oc, 37, 16);
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_l5 = t42;
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 32, 58);
    }
gm_lb1:;
    gm_b16_8 t43 = gm_l5;
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
inline __attribute__((aligned(4))) void gm_f_string_array_bn13c0i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_string_array_bn13c0i F;
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* s */
    GM_STACK_CHECK(&F, 409u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc gm_l7;
    gm_b16_8 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)3ULL));
            if (!t10) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l1), (((uint8_t *)a0) + 8), 40);
            gm_trqfvmjtt4sij t11;
            gm_f_list_epvducg((void *)&t11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l2 = t11;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
            gm_l3 = t12;
            gm_l4 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t13 = gm_l4;
                    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t15 = (t13 < t14);
                    if (!t15) {
                        goto gm_lb3;
                    }
                    {
                        void * t16 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t18 = gm_l4;
                        {
                            {
                                memmove(((uint8_t *)&F.l5), (((uint8_t *)t16) + (uintptr_t)t18 * 48), 48);
                                gm_tktr4rmbindgv t19 = GM_LD(gm_tktr4rmbindgv, ((uint8_t *)&F.l5));
                                gm_f_as_str_982o16v((void *)&gm_l7, &t19);
                                uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
                                if (t20) {
                                    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                                    gm_l8 = t21;
                                } else {
                                    gm_th2lqbo77pvsc t22;
                                    memset(&t22, 0, sizeof t22);
                                    memmove(gm_out, ((uint8_t *)&t22), 24);
                                    goto gm_fdone;
                                }
                                gm_b16_8 t23 = gm_l8;
                                gm_l6 = t23;
                                void * t24 = (void *)((uint8_t *)&F.l2);
                                gm_b16_8 t25 = gm_l6;
                                gm_tftb1odc739t5 t26 = gm_f_push_3ef746b(t24, t25);
                                gm_l9 = t26;
                                uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcg0ns322676oc, 407, 9);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 405, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t28 = gm_l4;
                    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                    uint64_t t31 = r29;
                    gm_l4 = t31;
                }
            }
gm_lb3:;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            gm_th2lqbo77pvsc t34;
            GM_ST(uint8_t, (uint8_t *)&t34 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t34 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t34 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t34), t33);
            ((uint8_t *)&t34)[16] = 1;
            memmove(gm_out, ((uint8_t *)&t34), 24);
            goto gm_fdone;
        }
gm_lb2:;
        gm_th2lqbo77pvsc t35;
        memset(&t35, 0, sizeof t35);
        memmove(gm_out, ((uint8_t *)&t35), 24);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_ascii_c9j36d1(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_ascii_c9j36d1 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 270u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l2 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l1;
            uint64_t t10 = gm_l2;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            void * t12 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t14 = gm_l1;
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t12) + (uintptr_t)t14 * 1));
            uint8_t t16 = gm_f_is_space_uen38t5(t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                uint64_t t17 = gm_l1;
                uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
                if (o19) gm_trap("integer overflow in `+`", gm_srcg0ns322676oc, 251, 47);
                uint64_t t20 = r18;
                gm_l1 = t20;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        for (;;) {
            uint64_t t21 = gm_l2;
            uint64_t t22 = gm_l1;
            uint8_t t23 = (t21 > t22);
            if (!t23) {
                goto gm_lb4;
            }
            void * t24 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t26 = gm_l2;
            uint64_t r27; bool o28 = __builtin_sub_overflow(t26, ((uint64_t)1ULL), &r27);
            if (o28) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 252, 40);
            uint64_t t29 = r27;
            if ((uintptr_t)t29 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t29, t25, gm_srcg0ns322676oc, 252, 38);
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t24) + (uintptr_t)t29 * 1));
            uint8_t t31 = gm_f_is_space_uen38t5(t30);
            if (!t31) {
                goto gm_lb4;
            }
            {
                uint64_t t32 = gm_l2;
                uint64_t r33; bool o34 = __builtin_sub_overflow(t32, ((uint64_t)1ULL), &r33);
                if (o34) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 252, 51);
                uint64_t t35 = r33;
                gm_l2 = t35;
            }
gm_lb6:;
        }
    }
gm_lb4:;
    void * t36 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t38 = gm_l1;
    uint64_t t39 = gm_l2;
    uint64_t t40 = (uintptr_t)t38;
    uint64_t t41 = (uintptr_t)t39;
    if (t40 > t41 || t41 > (uintptr_t)t37) gm_trap_slice(t40, t41, t37, gm_srcg0ns322676oc, 253, 12);
    uint64_t t42 = ((uintptr_t)t39) - (uintptr_t)t38;
    gm_b16_8 t43;
    GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t36 + (uintptr_t)t38 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(t42));
    F.ret = t43;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_url_problem_38kdh3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_url_problem_38kdh3a F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK(&F, 449u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l4;
    gm_tabb710d9mq2e gm_l5;
    gm_b16_8 t7 = gm_l0;
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_skolmnfc0ccbd));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(1));
    uint8_t t9 = gm_f_starts_with_6vvt01e(t7, t8);
    if (t9) {
        gm_b16_8 t10 = gm_l0;
        gm_tnpifv20irnph t11;
        GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
        gm_f_format_ivhanlp((void *)&gm_l4, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcg0ns322676oc, 227, 38);
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        gm_th2lqbo77pvsc t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t15), 24);
        goto gm_fdone;
    }
    gm_b16_8 t16 = gm_l0;
    gm_l1 = t16;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l2;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb1;
            }
            {
                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t22 = gm_l2;
                {
                    {
                        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t20) + (uintptr_t)t22 * 1));
                        gm_l3 = t23;
                        uint8_t t24 = gm_l3;
                        uint8_t t25 = gm_f_is_space_or_control_3uo6i3o(t24);
                        if (t25) {
                            gm_b16_8 t26 = gm_l0;
                            gm_b16_8 t27 = gm_f_debug_escape_t8t1h0j((void *)NULL, cx, NULL, t26);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t28 = gm_l3;
                            gm_b16_8 t29 = gm_f_debug_char_p6i205h((void *)NULL, cx, NULL, t28);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t6j6smj7qoctu t30;
                            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t27);
                            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 16), t29);
                            gm_f_format_uqgrksh((void *)&gm_l5, (void *)NULL, cx, NULL, &t30);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                            if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcg0ns322676oc, 230, 20);
                            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                            gm_th2lqbo77pvsc t34;
                            GM_ST(uint8_t, (uint8_t *)&t34 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t34 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t34 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t34), t33);
                            ((uint8_t *)&t34)[16] = 1;
                            memmove(gm_out, ((uint8_t *)&t34), 24);
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 228, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t35 = gm_l2;
            uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
            uint64_t t38 = r36;
            gm_l2 = t38;
        }
    }
gm_lb1:;
    gm_th2lqbo77pvsc t39;
    memset(&t39, 0, sizeof t39);
    memmove(gm_out, ((uint8_t *)&t39), 24);
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
