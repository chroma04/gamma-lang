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
typedef struct { __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_ta24rd13gupia;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta24rd13gupia) == 32 && _Alignof(gm_ta24rd13gupia) == 16 && offsetof(gm_ta24rd13gupia, f0) == 0 && offsetof(gm_ta24rd13gupia, f1) == 16, "layout of `?i128`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; uint64_t f3; } gm_teqsi7sd08g9f;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { void * f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_b16_8 f2; } gm_to3r6jqo24gs1;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; } gm_tov4mtrfp5e7v;
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t08olq97rscn3 v0; unsigned __int128 v1; } f1; } gm_t3d480iurj7vi;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_b16_8 f3; } gm_t4rau9abddl2u;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t6j6smj7qoctu v1; } f1; } gm_t9t4u07j77scv;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_teqsi7sd08g9f) == 48 && _Alignof(gm_teqsi7sd08g9f) == 8 && offsetof(gm_teqsi7sd08g9f, f0) == 0 && offsetof(gm_teqsi7sd08g9f, f1) == 16 && offsetof(gm_teqsi7sd08g9f, f2) == 32 && offsetof(gm_teqsi7sd08g9f, f3) == 40, "layout of `(string, string, u64, u64)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_to3r6jqo24gs1) == 32 && _Alignof(gm_to3r6jqo24gs1) == 8 && offsetof(gm_to3r6jqo24gs1, f0) == 0 && offsetof(gm_to3r6jqo24gs1, f1) == 8 && offsetof(gm_to3r6jqo24gs1, f2) == 16, "layout of `StaticCell`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_tov4mtrfp5e7v) == 12 && _Alignof(gm_tov4mtrfp5e7v) == 4 && offsetof(gm_tov4mtrfp5e7v, f0) == 0 && offsetof(gm_tov4mtrfp5e7v, f1) == 4 && offsetof(gm_tov4mtrfp5e7v, f2) == 8, "layout of `BitField`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_tob1snuii5bme v1; } f1; } gm_tt9fudfifskca;
_Static_assert(sizeof(gm_t3d480iurj7vi) == 48 && _Alignof(gm_t3d480iurj7vi) == 16 && offsetof(gm_t3d480iurj7vi, f0) == 0 && offsetof(gm_t3d480iurj7vi, f1) == 16, "layout of `Unsup!u128`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4rau9abddl2u) == 104 && _Alignof(gm_t4rau9abddl2u) == 8 && offsetof(gm_t4rau9abddl2u, f0) == 0 && offsetof(gm_t4rau9abddl2u, f1) == 8 && offsetof(gm_t4rau9abddl2u, f2) == 48 && offsetof(gm_t4rau9abddl2u, f3) == 88, "layout of `SiteConst`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint64_t f0; gm_tq7qjpkmqmc5s f1; } gm_t964gsbl2irde;
_Static_assert(sizeof(gm_t9t4u07j77scv) == 40 && _Alignof(gm_t9t4u07j77scv) == 8 && offsetof(gm_t9t4u07j77scv, f0) == 0 && offsetof(gm_t9t4u07j77scv, f1) == 8, "layout of `Unsup!ConstsText`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
_Static_assert(sizeof(gm_tt9fudfifskca) == 40 && _Alignof(gm_tt9fudfifskca) == 8 && offsetof(gm_tt9fudfifskca, f0) == 0 && offsetof(gm_tt9fudfifskca, f1) == 8, "layout of `Unsup!Reloc`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_t964gsbl2irde) == 112 && _Alignof(gm_t964gsbl2irde) == 8 && offsetof(gm_t964gsbl2irde, f0) == 0 && offsetof(gm_t964gsbl2irde, f1) == 8, "layout of `(i: usize, x: GlobalInfo)`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
typedef struct { uint64_t f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_thf8as07kj3uj;
_Static_assert(sizeof(gm_thf8as07kj3uj) == 64 && _Alignof(gm_thf8as07kj3uj) == 16 && offsetof(gm_thf8as07kj3uj, f0) == 0 && offsetof(gm_thf8as07kj3uj, f1) == 16, "layout of `(i: usize, x: Value)`");
struct gm_fr_adt_mpqv8qe {
    gm_frame h;
    void * ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_uu2harl {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_bit_width_u86m8ir {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_bits_layout_23rect9 {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_bits_of_ilgn83m {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_t6evimt84lndq l17; /* x */
    gm_t3d480iurj7vi ret;
};
struct gm_fr_cell_storage_1rtp280 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_cell_type_0jgf3ue {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_const_id_2mlh0og {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_const_storage_0d7nj5d {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_empty_slice_8rccll4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_al38tsc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_format_0o96lei {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_17eulka {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_317bju7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8n626pi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8sjjbd9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_be3fee1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fsfutor {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_gen_consts_6ibghrc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* decls */
    gm_trqfvmjtt4sij l2; /* init */
    gm_trqfvmjtt4sij l8; /* bytes */
    gm_trqfvmjtt4sij l11; /* relocs */
    gm_tq7qjpkmqmc5s l19; /* gl */
    gm_trqfvmjtt4sij l26; /* tls_init */
    gm_trqfvmjtt4sij l29; /* bytes */
    gm_trqfvmjtt4sij l32; /* relocs */
    gm_trqfvmjtt4sij l40; /* bytes */
    gm_trqfvmjtt4sij l43; /* relocs */
    gm_t4rau9abddl2u l50; /* sc */
    gm_t9t4u07j77scv ret;
};
struct gm_fr_get_5ktivom {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_get_pk8ncss {
    gm_frame h;
    gm_t9805qtonbsag ret;
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
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_list_opmir2c {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_place_reloc_blfij5d {
    gm_frame h;
    gm_tpautr4br7h3l l16; /* sd */
    gm_tt9fudfifskca ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_dffnss0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vgtfcjf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jc1rn8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ng75pjh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_store_int_mfkaqk5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
struct gm_fr_value_bytes_qm8egmj {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_tckdobunaulee l27; /* vd */
    gm_t6evimt84lndq l38; /* x */
    gm_t6evimt84lndq l44; /* x */
    gm_tpautr4br7h3l l46; /* sd */
    gm_t6evimt84lndq l53; /* x */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_write_60kg76i {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_80k6mh9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_dhp21l9 {
    gm_frame h;
    gm_tftksr4op114e l1; /* args */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_e5h23ca {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_lket48d {
    gm_frame h;
    gm_teqsi7sd08g9f l1; /* args */
    gm_teqsi7sd08g9f l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_qbs7gku {
    gm_frame h;
    gm_th2d9c7aju8hp l1; /* args */
    gm_th2d9c7aju8hp l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_snsbmln {
    gm_frame h;
    gm_tftksr4op114e l1; /* args */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_t09mk5p {
    gm_frame h;
    gm_tftksr4op114e l1; /* args */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_th5g0ba {
    gm_frame h;
    gm_thddbqb6snabm l1; /* args */
    gm_thddbqb6snabm l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ufh35j8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_vlrefbu {
    gm_frame h;
    gm_thddbqb6snabm l1; /* args */
    gm_thddbqb6snabm l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_name_g5lrlt3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_adt_mpqv8qe(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_of_ilgn83m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cell_storage_1rtp280(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_cell_type_0jgf3ue(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_const_storage_0d7nj5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_drain_leoibma(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8rccll4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_al38tsc(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_8gjlcrm(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_kfnukt3(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_fn_ptr_type_plain_bi8eu4l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_format_0o96lei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_17eulka(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_317bju7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8sjjbd9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_be3fee1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fjqj2pj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_gen_consts_6ibghrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_5ktivom(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_global_tag_gs7s3p6(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_qr08i40(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_opmir2c(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_module_key_m4hm4f5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_place_reloc_blfij5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_dffnss0(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vgtfcjf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_uldb5pe(void * a0, uint64_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_queue_cd51r8c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jc1rn8(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ng75pjh(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_static_bytes_u0uvco3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_store_int_mfkaqk5(void * a0, gm_b16_8 a1, uint64_t a2, unsigned __int128 a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_to_i128_o7ud1ue(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_fba8sj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_value_bytes_qm8egmj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint64_t a4, void * a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_60kg76i(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_80k6mh9(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_9bp3v08(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_av01663(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_b4h5rbv(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bbokkis(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dhp21l9(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e5h23ca(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f98j2rj(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hndtg2g(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lket48d(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qbs7gku(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s3v41br(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_sbqfppm(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_snsbmln(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_t09mk5p(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_th5g0ba(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ufh35j8(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vlrefbu(void * a0, const void * a1);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s05crl6fa1at4[6];
extern const uint8_t gm_s0ggo8ek7kfrt[34];
extern const uint8_t gm_s1rmhdquo3v18[29];
extern const uint8_t gm_s2uimijqjekbd[67];
extern const uint8_t gm_s31vj4bjbj0gj[36];
extern const uint8_t gm_s3js9g3l46oiv[40];
extern const uint8_t gm_s52bclt1m0fbl[76];
extern const uint8_t gm_s5oqt1mft8410[53];
extern const uint8_t gm_s61h006kvv5be[72];
extern const uint8_t gm_s61jd5hctkcmt[38];
extern const uint8_t gm_s67fd8s6hf84e[24];
extern const uint8_t gm_sa6mes8ijqmmk[30];
extern const uint8_t gm_sanq51h4cqdch[36];
extern const uint8_t gm_sb8jvk2vbau9p[25];
extern const uint8_t gm_sbbavs945cjhp[32];
extern const uint8_t gm_sbe61ep7tock5[5];
extern const uint8_t gm_sbe7f0bkvca75[43];
extern const uint8_t gm_scc54gv4a7k04[6];
extern const uint8_t gm_sd0eqtuh33j1c[71];
extern const uint8_t gm_sdsb9longvn22[40];
extern const uint8_t gm_sfali8nl85hc9[25];
extern const uint8_t gm_sg1uf2uigo01q[27];
extern const uint8_t gm_sgiqpj781s1a9[36];
extern const uint8_t gm_sgovtffcl93pk[15];
extern const uint8_t gm_sgspc9im84cfh[25];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sj7e2l95sfllk[48];
extern const uint8_t gm_skff6pclobvnu[40];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_slpqve1dqssq0[8];
extern const uint8_t gm_sq9qefps1uqei[10];
extern const uint8_t gm_sqrl3kgomorsv[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuj9anpi7p1mq[];
extern const char gm_srcuro7l7pd15c2[];
extern const uint8_t gm_sri1d6adh4kkk[11];
extern const uint8_t gm_sse9uo3ffn4rj[167];
extern const uint8_t gm_storp3otq7orh[59];
extern const uint8_t gm_svlcdvcv2bp94[24];
extern const uint8_t gm_svli4h2db9erk[7];
extern const gm_tmpl gm_tpl5i8vi7ph4hfi;
extern const gm_tmpl gm_tpl5ldtdub2029b;
extern const gm_tmpl gm_tpl7b4l0929mgk1;
extern const gm_tmpl gm_tpl7oj2vfshts60;
extern const gm_tmpl gm_tplahe50smef36f;
extern const gm_tmpl gm_tple34dj6br49e3;
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplfjoq6lkhl94q;
extern const gm_tmpl gm_tpli08vvf1bckj4;
extern const gm_tmpl gm_tpli9foo8ib8qrk;
extern const gm_tmpl gm_tplokvovhsdbec1;
extern const gm_tmpl gm_tplq0mvos8lisnf;
extern const gm_tmpl gm_tplq75gqr5tvlmd;
extern const gm_tmpl gm_tplqc1ev2l2fda9;
extern const gm_tmpl gm_tplqh3e2aojp71u;
extern const gm_tmpl gm_tplss5b4q9est2j;
extern const gm_tmpl gm_tplt5hu85fc8epn;
extern const gm_tmpl gm_tplu7gq2cm8aanh;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b) {
    if (a[16] != b[16]) return false;
    return !a[16] || gm_eq0iqiubfm7e(a, b);
}
inline __attribute__((aligned(4))) void * gm_f_adt_mpqv8qe(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_mpqv8qe F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 1088));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 1088) + 8));
    uint32_t t6 = gm_l1;
    uint64_t t7 = (uint64_t)t6;
    if ((uintptr_t)t7 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t7, t5, gm_srcuj9anpi7p1mq, 135, 63);
    void * t8 = (void *)(((uint8_t *)t4) + (uintptr_t)t7 * 184);
    F.ret = t8;
    goto gm_fret;
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_uu2harl F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
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
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bit_width_u86m8ir F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint32_t t5 = gm_f_field_bits_2ktfm1k((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_23rect9 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_bits_layout_u6kkccp((void *)&(*(gm_t57euh612ligr *)gm_out), (void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8rccll4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8rccll4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_al38tsc(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_al38tsc F;
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
inline __attribute__((aligned(4))) void gm_f_format_0o96lei(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_0o96lei F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svli4h2db9erk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqh3e2aojp71u), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_17eulka(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_17eulka F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sq9qefps1uqei));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpli08vvf1bckj4), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_317bju7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_317bju7 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s05crl6fa1at4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplq75gqr5tvlmd), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_8sjjbd9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8sjjbd9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgovtffcl93pk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7oj2vfshts60), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_be3fee1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_be3fee1 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sri1d6adh4kkk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplq0mvos8lisnf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fsfutor F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdsb9longvn22));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplu7gq2cm8aanh), t3);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_5ktivom(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_5ktivom F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 165u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    uint64_t gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_kfnukt3(t2, t3);
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
    uint32_t t13 = GM_LD(uint32_t, ((((uint8_t *)t10) + (uintptr_t)t12 * 24) + 16));
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
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_list_opmir2c(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_opmir2c F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8rccll4();
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
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nullable_pointer_7qfmn6o F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_is_nullable_pointer_nkie06s(t2, GM_CONV(gm_csguankko4lmhd, t3));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_of_e6hfj1r((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 16));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_dffnss0(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_dffnss0 F;
    void * gm_l0; /* l */
    gm_to3r6jqo24gs1 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_al38tsc();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_0jc1rn8(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vgtfcjf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vgtfcjf F;
    void * gm_l0; /* l */
    gm_tob1snuii5bme gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_8rccll4();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ng75pjh(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jc1rn8(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0jc1rn8 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ng75pjh(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ng75pjh F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t4, t5);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint64_t t5 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
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
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
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
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_unsupported_531q51r F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    gm_b16_8 t2 = gm_l1;
    gm_tnpifv20irnph t3;
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
    gm_f_format_fsfutor((void *)&gm_l2, (void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t4) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcd5arorrf8jkg, 38, 27);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_tl874t2atnlh3 t7 = gm_l0;
    gm_t08olq97rscn3 t8;
    GM_ST(uint32_t, (uint8_t *)&t8 + 28, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t6);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t8 + 16), t7);
    memmove(gm_out, ((uint8_t *)&t8), 32);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_60kg76i(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_60kg76i F;
    void * gm_l0; /* l */
    gm_tgbgti82mf879 gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_tgbgti82mf879 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5oqt1mft8410));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_tftb1odc739t5 t4 = gm_f_write_hndtg2g(t2, &(*(gm_tgbgti82mf879 *)((uint8_t *)a1)), ((void *)&gm_tpl5ldtdub2029b), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_80k6mh9(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_80k6mh9 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sanq51h4cqdch));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(35));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplokvovhsdbec1), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dhp21l9(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_dhp21l9 F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1rmhdquo3v18));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_tftb1odc739t5 t4 = gm_f_write_bbokkis(t2, &(*(gm_tftksr4op114e *)((uint8_t *)a1)), ((void *)&gm_tplahe50smef36f), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e5h23ca(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_e5h23ca F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sj7e2l95sfllk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_tftb1odc739t5 t4 = gm_f_write_s3v41br(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tpl7b4l0929mgk1), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lket48d(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_lket48d F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_storp3otq7orh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(58));
    gm_tftb1odc739t5 t4 = gm_f_write_b4h5rbv(t2, &(*(gm_teqsi7sd08g9f *)((uint8_t *)a1)), ((void *)&gm_tplt5hu85fc8epn), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qbs7gku(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_qbs7gku F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sse9uo3ffn4rj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(166));
    gm_tftb1odc739t5 t4 = gm_f_write_sbqfppm(t2, &(*(gm_th2d9c7aju8hp *)((uint8_t *)a1)), ((void *)&gm_tplss5b4q9est2j), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_snsbmln(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_snsbmln F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgspc9im84cfh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
    gm_tftb1odc739t5 t4 = gm_f_write_bbokkis(t2, &(*(gm_tftksr4op114e *)((uint8_t *)a1)), ((void *)&gm_tpli9foo8ib8qrk), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_t09mk5p(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_t09mk5p F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd0eqtuh33j1c));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(70));
    gm_tftb1odc739t5 t4 = gm_f_write_9bp3v08(t2, &(*(gm_tftksr4op114e *)((uint8_t *)a1)), ((void *)&gm_tplfjoq6lkhl94q), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_th5g0ba(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_th5g0ba F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s61h006kvv5be));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(71));
    gm_tftb1odc739t5 t4 = gm_f_write_f98j2rj(t2, &(*(gm_thddbqb6snabm *)((uint8_t *)a1)), ((void *)&gm_tple34dj6br49e3), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ufh35j8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ufh35j8 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s61jd5hctkcmt));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
    gm_tftb1odc739t5 t4 = gm_f_write_av01663(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpl5i8vi7ph4hfi), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vlrefbu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_vlrefbu F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s52bclt1m0fbl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(75));
    gm_tftb1odc739t5 t4 = gm_f_write_f98j2rj(t2, &(*(gm_thddbqb6snabm *)((uint8_t *)a1)), ((void *)&gm_tplqc1ev2l2fda9), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_bits_of_ilgn83m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_bits_of_ilgn83m F;
    void * gm_l0; /* g */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint8_t gm_l3; /* b */
    unsigned __int128 gm_l4; /* bits */
    uint32_t gm_l5; /* w */
    uint32_t gm_l6; /* index */
    uint32_t gm_l7; /* id */
    gm_t88rcucg1b7uk gm_l8; /* tag_ty */
    gm_b16_8 gm_l9; /* variants */
    __int128 gm_l10; /* tag */
    void * gm_l11; /* a */
    gm_tcmkompbl09il gm_l12; /* bl */
    unsigned __int128 gm_l13; /* raw */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* k */
    gm_tov4mtrfp5e7v gm_l18; /* f */
    GM_STACK_CHECK(&F, 2176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_t3d480iurj7vi gm_l19;
    gm_t3d480iurj7vi gm_l20;
    gm_t53at80pfdus3 gm_l21;
    gm_ta24rd13gupia gm_l22;
    __int128 gm_l23;
    gm_t57euh612ligr gm_l24;
    gm_tcmkompbl09il gm_l25;
    gm_thf8as07kj3uj gm_l26;
    gm_t3d480iurj7vi gm_l27;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)2ULL));
            if (!t18) {
                goto gm_lb2;
            }
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
            gm_l3 = t19;
            uint8_t t20 = gm_l3;
            if (t20) {
                gm_t3d480iurj7vi t21;
                GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t21 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t21 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t21 + 40, 0);
                ((uint8_t *)&t21)[0] = 0;
                GM_ST(unsigned __int128, ((uint8_t *)&t21 + 16), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)));
                gm_l19 = t21;
            } else {
                gm_t3d480iurj7vi t22;
                GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t22 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t22 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t22 + 40, 0);
                ((uint8_t *)&t22)[0] = 0;
                GM_ST(unsigned __int128, ((uint8_t *)&t22 + 16), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                gm_l19 = t22;
            }
            memmove(gm_out, ((uint8_t *)&gm_l19), 48);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
            if (!t24) {
                goto gm_lb3;
            }
            unsigned __int128 t25 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
            gm_l4 = t25;
            void * t26 = gm_l0;
            void * t27 = (void *)(((uint8_t *)t26) + 8);
            gm_t88rcucg1b7uk t28 = gm_l2;
            uint32_t t29 = gm_f_bit_width_u86m8ir((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t29;
            uint32_t t30 = gm_l5;
            uint8_t t31 = (t30 >= ((uint32_t)128ULL));
            if (t31) {
                unsigned __int128 t32 = gm_l4;
                gm_t3d480iurj7vi t33;
                GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t33 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t33 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t33 + 40, 0);
                ((uint8_t *)&t33)[0] = 0;
                GM_ST(unsigned __int128, ((uint8_t *)&t33 + 16), t32);
                gm_l20 = t33;
            } else {
                unsigned __int128 t34 = gm_l4;
                uint32_t t35 = gm_l5;
                unsigned __int128 t36 = (unsigned __int128)t35;
                unsigned __int128 n37 = (unsigned __int128)t36;
                if (n37 >= 128) gm_trap_shift((uint64_t)n37, "u128", gm_srcuro7l7pd15c2, 329, 57);
                unsigned __int128 r38 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n37);
                bool o39 = (unsigned __int128)(r38 >> (int)n37) != ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
                if (o39) gm_trap("integer overflow in `<<`", gm_srcuro7l7pd15c2, 329, 57);
                unsigned __int128 t40 = r38;
                unsigned __int128 r41; bool o42 = __builtin_sub_overflow(t40, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)), &r41);
                if (o42) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 329, 56);
                unsigned __int128 t43 = r41;
                unsigned __int128 t44 = (unsigned __int128)(t34 & t43);
                gm_t3d480iurj7vi t45;
                GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t45 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t45 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t45 + 40, 0);
                ((uint8_t *)&t45)[0] = 0;
                GM_ST(unsigned __int128, ((uint8_t *)&t45 + 16), t44);
                gm_l20 = t45;
            }
            memmove(gm_out, ((uint8_t *)&gm_l20), 48);
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)10ULL));
            if (!t47) {
                goto gm_lb4;
            }
            uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
            gm_l6 = t48;
            {
                {
                    void * t49 = gm_l0;
                    void * t50 = (void *)(((uint8_t *)t49) + 8);
                    gm_t88rcucg1b7uk t51 = gm_l2;
                    gm_f_kind_ghvav22((void *)&gm_l21, t50, GM_CONV(gm_csguankko4lmhd, t51));
                    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                    uint8_t t53 = (uint8_t)(t52 == ((uint8_t)17ULL));
                    if (!t53) {
                        goto gm_lb6;
                    }
                    uint32_t t54 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 8));
                    gm_l7 = t54;
                    {
                        {
                            void * t55 = gm_l0;
                            void * t56 = GM_LD(void *, ((uint8_t *)t55));
                            uint32_t t57 = gm_l7;
                            void * t58 = gm_f_adt_mpqv8qe(t56, t57);
                            uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t58) + 24) + 0));
                            uint8_t t60 = (uint8_t)(t59 == ((uint8_t)1ULL));
                            if (!t60) {
                                goto gm_lb8;
                            }
                            gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t58) + 24) + 8));
                            gm_l8 = t61;
                            gm_b16_8 t62 = GM_LD(gm_b16_8, (((((uint8_t *)t58) + 24) + 8) + 8));
                            gm_l9 = t62;
                            void * t63 = GM_LD(void *, ((uint8_t *)&gm_l9));
                            uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                            uint32_t t65 = gm_l6;
                            uint64_t t66 = (uint64_t)t65;
                            if ((uintptr_t)t66 >= (uintptr_t)(t64)) gm_trap_index((uintptr_t)t66, t64, gm_srcuro7l7pd15c2, 334, 35);
                            gm_tbci5ospk5nnn t67 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t63) + (uintptr_t)t66 * 72) + 24));
                            gm_f_to_i128_o7ud1ue((void *)&gm_l22, &t67);
                            uint8_t t68 = (uint8_t)((((uint8_t *)&gm_l22))[16] != 0);
                            if (t68) {
                                __int128 t69 = GM_LD(__int128, ((uint8_t *)&gm_l22));
                                gm_l23 = t69;
                            } else {
                                gm_l23 = ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                            }
                            __int128 t70 = gm_l23;
                            gm_l10 = t70;
                            void * t71 = gm_l0;
                            __int128 t72 = gm_l10;
                            unsigned __int128 t73 = (unsigned __int128)t72;
                            gm_t88rcucg1b7uk t74 = gm_l8;
                            gm_t6evimt84lndq t75;
                            GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t75 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t75 + 32, 0);
                            GM_ST(uint64_t, (uint8_t *)&t75 + 40, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)0ULL));
                            GM_ST(unsigned __int128, ((uint8_t *)&t75 + 16), t73);
                            gm_f_bits_of_ilgn83m((void *)&(*(gm_t3d480iurj7vi *)gm_out), (void *)NULL, cx, NULL, t71, &t75, t74);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_fdone;
                        }
gm_lb8:;
                        gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                        gm_b16_8 t77;
                        GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sfali8nl85hc9));
                        GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(24));
                        gm_t08olq97rscn3 t78;
                        gm_f_unsupported_531q51r((void *)&t78, (void *)NULL, cx, NULL, t76, t77);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t3d480iurj7vi t79;
                        GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t79 + 8, 0);
                        ((uint8_t *)&t79)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t79 + 16), t78);
                        memmove(gm_out, ((uint8_t *)&t79), 48);
                        goto gm_fdone;
                    }
gm_lb7:;
                }
gm_lb6:;
                gm_tl874t2atnlh3 t80 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                gm_b16_8 t81;
                GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sfali8nl85hc9));
                GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(24));
                gm_t08olq97rscn3 t82;
                gm_f_unsupported_531q51r((void *)&t82, (void *)NULL, cx, NULL, t80, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t3d480iurj7vi t83;
                GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t83 + 8, 0);
                ((uint8_t *)&t83)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t83 + 16), t82);
                memmove(gm_out, ((uint8_t *)&t83), 48);
                goto gm_fdone;
            }
gm_lb5:;
        }
gm_lb4:;
        {
            uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t85 = (uint8_t)(t84 == ((uint8_t)3ULL));
            if (!t85) {
                goto gm_lb9;
            }
            void * t86 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
            gm_l11 = t86;
            void * t87 = gm_l0;
            void * t88 = (void *)(((uint8_t *)t87) + 8);
            gm_t88rcucg1b7uk t89 = gm_l2;
            gm_f_bits_layout_23rect9((void *)&gm_l24, (void *)NULL, cx, NULL, t88, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t90 = (uint8_t)((((uint8_t *)&gm_l24))[24] != 0);
            if (t90) {
                memmove(((uint8_t *)&gm_l25), ((uint8_t *)&gm_l24), 24);
            } else {
                gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                gm_b16_8 t92;
                GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sfali8nl85hc9));
                GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(24));
                gm_t08olq97rscn3 t93;
                gm_f_unsupported_531q51r((void *)&t93, (void *)NULL, cx, NULL, t91, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t3d480iurj7vi t94;
                GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t94 + 8, 0);
                ((uint8_t *)&t94)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t94 + 16), t93);
                memmove(gm_out, ((uint8_t *)&t94), 48);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l25), 24);
            gm_l13 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
            void * t95 = gm_l11;
            gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)t95));
            gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
            gm_l14 = t97;
            gm_l15 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t98 = gm_l15;
                    uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint8_t t100 = (t98 < t99);
                    if (!t100) {
                        goto gm_lb10;
                    }
                    {
                        uint64_t t101 = gm_l15;
                        void * t102 = GM_LD(void *, ((uint8_t *)&gm_l14));
                        uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                        uint64_t t104 = gm_l15;
                        gm_t6evimt84lndq t105 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t102) + (uintptr_t)t104 * 48));
                        gm_thf8as07kj3uj t106;
                        GM_ST(uint64_t, (uint8_t *)&t106 + 8, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t106 + 0), t101);
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t106 + 16), t105);
                        gm_l26 = t106;
                        {
                            {
                                uint64_t t107 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
                                gm_l16 = t107;
                                memmove(((uint8_t *)&F.l17), (((uint8_t *)&gm_l26) + 16), 48);
                                void * t108 = GM_LD(void *, (((uint8_t *)&gm_l12) + 8));
                                uint64_t t109 = GM_LD(uint64_t, ((((uint8_t *)&gm_l12) + 8) + 8));
                                uint64_t t110 = gm_l16;
                                if ((uintptr_t)t110 >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)t110, t109, gm_srcuro7l7pd15c2, 341, 22);
                                gm_tov4mtrfp5e7v t111 = GM_LD(gm_tov4mtrfp5e7v, (((uint8_t *)t108) + (uintptr_t)t110 * 12));
                                gm_l18 = t111;
                                unsigned __int128 t112 = gm_l13;
                                void * t113 = gm_l0;
                                gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l18) + 8));
                                gm_t6evimt84lndq t115 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l17));
                                gm_f_bits_of_ilgn83m((void *)&gm_l27, (void *)NULL, cx, NULL, t113, &t115, t114);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t116 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                if (t116) {
                                } else {
                                    gm_t08olq97rscn3 t117 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l27) + 16));
                                    gm_t3d480iurj7vi t118;
                                    GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t118 + 8, 0);
                                    ((uint8_t *)&t118)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t118 + 16), t117);
                                    memmove(gm_out, ((uint8_t *)&t118), 48);
                                    goto gm_fdone;
                                }
                                unsigned __int128 t119 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l27) + 16));
                                uint32_t t120 = GM_LD(uint32_t, ((uint8_t *)&gm_l18));
                                unsigned __int128 t121 = (unsigned __int128)t120;
                                unsigned __int128 n122 = (unsigned __int128)t121;
                                if (n122 >= 128) gm_trap_shift((uint64_t)n122, "u128", gm_srcuro7l7pd15c2, 342, 24);
                                unsigned __int128 r123 = (unsigned __int128)((unsigned __int128)t119 << (int)n122);
                                bool o124 = (unsigned __int128)(r123 >> (int)n122) != t119;
                                if (o124) gm_trap("integer overflow in `<<`", gm_srcuro7l7pd15c2, 342, 24);
                                unsigned __int128 t125 = r123;
                                unsigned __int128 t126 = (unsigned __int128)(t112 | t125);
                                gm_l13 = t126;
                                goto gm_lb13;
                            }
gm_lb14:;
                            gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 340, 13);
                        }
gm_lb13:;
                    }
gm_lb12:;
                    uint64_t t127 = gm_l15;
                    uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                    uint64_t t130 = r128;
                    gm_l15 = t130;
                }
            }
gm_lb10:;
            unsigned __int128 t131 = gm_l13;
            gm_t3d480iurj7vi t132;
            GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t132 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t132 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t132 + 40, 0);
            ((uint8_t *)&t132)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t132 + 16), t131);
            memmove(gm_out, ((uint8_t *)&t132), 48);
            goto gm_fdone;
        }
gm_lb9:;
        {
            gm_tl874t2atnlh3 t133 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
            gm_b16_8 t134;
            GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sfali8nl85hc9));
            GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(24));
            gm_t08olq97rscn3 t135;
            gm_f_unsupported_531q51r((void *)&t135, (void *)NULL, cx, NULL, t133, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t3d480iurj7vi t136;
            GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t136 + 8, 0);
            ((uint8_t *)&t136)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t136 + 16), t135);
            memmove(gm_out, ((uint8_t *)&t136), 48);
            goto gm_fdone;
        }
gm_lb15:;
        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 325, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cell_storage_1rtp280(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_cell_storage_1rtp280 F;
    void * gm_l0; /* g */
    void * gm_l1; /* cell */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* key */
    uint64_t gm_l4; /* addr */
    uint32_t gm_l5; /* i */
    uint32_t gm_l6; /* i */
    gm_b16_8 gm_l7; /* name */
    GM_STACK_CHECK(&F, 427u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t9805qtonbsag gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    void * t4 = gm_l1;
    uint64_t t5 = (uintptr_t)t4;
    gm_l4 = t5;
    {
        {
            void * t6 = gm_l0;
            void * t7 = (void *)(((uint8_t *)t6) + 3528);
            uint64_t t8 = gm_l4;
            gm_t9805qtonbsag t9 = gm_f_get_5ktivom(t7, t8);
            gm_l8 = t9;
            uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l8))[4] != 0);
            if (!t10) {
                goto gm_lb2;
            }
            uint32_t t11 = GM_LD(uint32_t, ((uint8_t *)&gm_l8));
            gm_l5 = t11;
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 3488));
            uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 3488) + 8));
            uint32_t t15 = gm_l5;
            uint64_t t16 = (uint64_t)t15;
            if ((uintptr_t)t16 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t16, t14, gm_srcuro7l7pd15c2, 41, 43);
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((((uint8_t *)t13) + (uintptr_t)t16 * 32) + 16));
            F.ret = t17;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t18 = gm_l0;
    uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)t18) + 3488) + 8));
    uint32_t t20 = (uint32_t)t19;
    gm_l6 = t20;
    void * t21 = gm_l0;
    void * t22 = (void *)(((uint8_t *)t21) + 1272);
    gm_b16_8 t23;
    GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sbe61ep7tock5));
    GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
    gm_b16_8 t24 = gm_l3;
    gm_b16_8 t25 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t22, t23, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t25;
    void * t26 = gm_l0;
    void * t27 = (void *)(((uint8_t *)t26) + 3488);
    void * t28 = gm_l1;
    gm_t88rcucg1b7uk t29 = gm_l2;
    gm_b16_8 t30 = gm_l7;
    gm_to3r6jqo24gs1 t31;
    GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
    GM_ST(void *, ((uint8_t *)&t31 + 0), t28);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t31 + 8), t29);
    GM_ST(gm_b16_8, ((uint8_t *)&t31 + 16), t30);
    gm_tftb1odc739t5 t32 = gm_f_push_dffnss0(t27, &t31);
    gm_l9 = t32;
    uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcuro7l7pd15c2, 44, 5);
    void * t34 = gm_l0;
    void * t35 = (void *)(((uint8_t *)t34) + 3528);
    uint64_t t36 = gm_l4;
    uint32_t t37 = gm_l6;
    gm_tftb1odc739t5 t38 = gm_f_put_uldb5pe(t35, t36, t37);
    gm_l10 = t38;
    uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcuro7l7pd15c2, 45, 5);
    gm_b16_8 t40 = gm_l7;
    F.ret = t40;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_cell_type_0jgf3ue(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cell_type_0jgf3ue F;
    void * gm_l0; /* g */
    void * gm_l1; /* cell */
    uint32_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t9805qtonbsag gm_l3;
    uint32_t gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 3528);
    void * t4 = gm_l1;
    uint64_t t5 = (uintptr_t)t4;
    gm_t9805qtonbsag t6 = gm_f_get_5ktivom(t3, t5);
    gm_l3 = t6;
    uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
    if (t7) {
        uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
        gm_l4 = t8;
    } else {
        gm_tkn42u7na3t81 t9;
        memset(&t9, 0, sizeof t9);
        F.ret = t9;
        goto gm_fret;
    }
    uint32_t t10 = gm_l4;
    gm_l2 = t10;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, (((uint8_t *)t11) + 3488));
    uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)t11) + 3488) + 8));
    uint32_t t14 = gm_l2;
    uint64_t t15 = (uint64_t)t14;
    if ((uintptr_t)t15 >= (uintptr_t)(t13)) gm_trap_index((uintptr_t)t15, t13, gm_srcuro7l7pd15c2, 311, 12);
    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t12) + (uintptr_t)t15 * 32) + 8));
    gm_tkn42u7na3t81 t17;
    GM_ST(uint8_t, (uint8_t *)&t17 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t17 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t17), t16);
    ((uint8_t *)&t17)[4] = 1;
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_const_storage_0d7nj5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_const_storage_0d7nj5d F;
    void * gm_l0; /* g */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* at */
    void * gm_l4; /* s */
    uint32_t gm_l5; /* id */
    void * gm_l6; /* def */
    void * gm_l7; /* cell */
    GM_STACK_CHECK(&F, 699u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t9805qtonbsag gm_l8;
    uint32_t gm_l9;
    void * gm_l10;
    gm_tabb710d9mq2e gm_l11;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    gm_l4 = t4;
    void * t5 = gm_l4;
    gm_tl874t2atnlh3 t6 = gm_l1;
    gm_t88rcucg1b7uk t7 = gm_l2;
    gm_t9805qtonbsag t8 = gm_f_const_id_2mlh0og(t5, GM_CONV(gm_cseahvg8v7p0vs, t6), GM_CONV(gm_csguankko4lmhd, t7));
    gm_l8 = t8;
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l8))[4] != 0);
    if (t9) {
        uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)&gm_l8));
        gm_l9 = t10;
    } else {
        gm_tl874t2atnlh3 t11 = gm_l3;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s0ggo8ek7kfrt));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(33));
        gm_t08olq97rscn3 t13;
        gm_f_unsupported_531q51r((void *)&t13, (void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
        ((uint8_t *)&t14)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t14 + 8), t13);
        memmove(gm_out, ((uint8_t *)&t14), 40);
        goto gm_fdone;
    }
    uint32_t t15 = gm_l9;
    gm_l5 = t15;
    void * t16 = gm_l4;
    void * t17 = GM_LD(void *, (((uint8_t *)t16) + 2088));
    uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t16) + 2088) + 8));
    uint32_t t19 = gm_l5;
    uint64_t t20 = (uint64_t)t19;
    if ((uintptr_t)t20 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t20, t18, gm_srcuro7l7pd15c2, 33, 13);
    void * t21 = (void *)(((uint8_t *)t17) + (uintptr_t)t20 * 88);
    gm_l6 = t21;
    void * t22 = gm_l6;
    uint8_t t23 = (uint8_t)(GM_LD(void *, (((uint8_t *)t22) + 80)) != 0);
    if (t23) {
        void * t24 = GM_LD(void *, (((uint8_t *)t22) + 80));
        gm_l10 = t24;
    } else {
        gm_tl874t2atnlh3 t25 = gm_l3;
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s3js9g3l46oiv));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(39));
        gm_t08olq97rscn3 t27;
        gm_f_unsupported_531q51r((void *)&t27, (void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
        ((uint8_t *)&t28)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t28 + 8), t27);
        memmove(gm_out, ((uint8_t *)&t28), 40);
        goto gm_fdone;
    }
    void * t29 = gm_l10;
    gm_l7 = t29;
    void * t30 = gm_l0;
    void * t31 = gm_l7;
    gm_t88rcucg1b7uk t32 = gm_l2;
    void * t33 = gm_l0;
    void * t34 = gm_l6;
    uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t34) + 32));
    gm_b16_8 t36 = gm_f_module_key_m4hm4f5((void *)NULL, cx, NULL, t33, t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t37 = gm_l6;
    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)t37));
    void * t39 = gm_l0;
    void * t40 = (void *)(((uint8_t *)t39) + 8);
    gm_t88rcucg1b7uk t41 = gm_l2;
    gm_b16_8 t42 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t40, t41);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tfeab279c16rr t43;
    GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t36);
    GM_ST(gm_b16_8, ((uint8_t *)&t43 + 16), t38);
    GM_ST(gm_b16_8, ((uint8_t *)&t43 + 32), t42);
    gm_f_format_17eulka((void *)&gm_l11, (void *)NULL, cx, NULL, &t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcuro7l7pd15c2, 35, 36);
    gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
    gm_b16_8 t47 = gm_f_cell_storage_1rtp280((void *)NULL, cx, NULL, t30, t31, t32, t46);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t5o0tpv5jka2l t48;
    GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t48 + 24, 0);
    GM_ST(uint64_t, (uint8_t *)&t48 + 32, 0);
    ((uint8_t *)&t48)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t48 + 8), t47);
    memmove(gm_out, ((uint8_t *)&t48), 40);
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
__attribute__((aligned(4))) void gm_f_gen_consts_6ibghrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_gen_consts_6ibghrc F;
    void * gm_l0; /* g */
    uint64_t gm_l3; /* i */
    gm_to3r6jqo24gs1 gm_l4; /* sc */
    gm_b16_8 gm_l5; /* name */
    uint64_t gm_l6; /* size */
    uint64_t gm_l7; /* al */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l12; /* sb */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_tob1snuii5bme gm_l15; /* r */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* gi */
    gm_b16_8 gm_l20; /* t */
    uint64_t gm_l21; /* size */
    uint64_t gm_l22; /* al */
    gm_b16_8 gm_l23; /* link */
    gm_b16_8 gm_l24; /* tls */
    gm_b16_8 gm_l25; /* tls */
    void * gm_l27; /* target */
    void * gm_l28; /* cell */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    gm_b16_8 gm_l33; /* sb */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    gm_tob1snuii5bme gm_l36; /* r */
    gm_to3r6jqo24gs1 gm_l37; /* sc */
    gm_b16_8 gm_l38; /* name */
    uint64_t gm_l39; /* size */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    gm_b16_8 gm_l44; /* sb */
    gm_b16_8 gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    gm_tob1snuii5bme gm_l47; /* r */
    gm_b16_8 gm_l48; /* <tmp> */
    uint64_t gm_l49; /* <tmp> */
    gm_b16_8 gm_l51; /* name */
    gm_b16_8 gm_l52; /* sb */
    gm_b16_8 gm_l53; /* <tmp> */
    uint64_t gm_l54; /* <tmp> */
    gm_tf1bjg81u0glv gm_l55; /* r */
    gm_b16_8 gm_l56; /* target */
    GM_STACK_CHECK(&F, 6048u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_tqgtd5gtgj65s gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tftb1odc739t5 gm_l62;
    gm_tqgtd5gtgj65s gm_l63;
    gm_t964gsbl2irde gm_l64;
    gm_tabb710d9mq2e gm_l65;
    uint64_t gm_l66;
    gm_tftb1odc739t5 gm_l67;
    gm_b16_8 gm_l68;
    gm_tftb1odc739t5 gm_l69;
    gm_b16_8 gm_l70;
    gm_tftb1odc739t5 gm_l71;
    void * gm_l72;
    gm_tftb1odc739t5 gm_l73;
    gm_tqgtd5gtgj65s gm_l74;
    gm_tftb1odc739t5 gm_l75;
    gm_tftb1odc739t5 gm_l76;
    gm_tftb1odc739t5 gm_l77;
    gm_tftb1odc739t5 gm_l78;
    gm_tftb1odc739t5 gm_l79;
    gm_tftb1odc739t5 gm_l80;
    gm_tqgtd5gtgj65s gm_l81;
    gm_tftb1odc739t5 gm_l82;
    gm_tftb1odc739t5 gm_l83;
    gm_tqgtd5gtgj65s gm_l84;
    gm_tftb1odc739t5 gm_l85;
    gm_tftb1odc739t5 gm_l86;
    gm_tftb1odc739t5 gm_l87;
    gm_tftb1odc739t5 gm_l88;
    {
        gm_trqfvmjtt4sij t60;
        gm_f_list_uvfu1k1((void *)&t60, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t60;
        gm_trqfvmjtt4sij t61;
        gm_f_list_uvfu1k1((void *)&t61, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t61;
        void * t62 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t63;
        GM_ST(void *, (uint8_t *)&t63, (void *)(gm_s31vj4bjbj0gj));
        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(35));
        gm_tftb1odc739t5 t64 = gm_f_extend_0cs85gs(t62, t63);
        gm_l57 = t64;
        uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
        if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srcuro7l7pd15c2, 53, 5);
        gm_l3 = ((uint64_t)0ULL);
        {
            for (;;) {
                if (!((uint8_t)1)) {
                    goto gm_lb1;
                }
                {
                    {
                        for (;;) {
                            uint64_t t66 = gm_l3;
                            void * t67 = gm_l0;
                            uint64_t t68 = GM_LD(uint64_t, ((((uint8_t *)t67) + 3488) + 8));
                            uint8_t t69 = (t66 < t68);
                            if (!t69) {
                                goto gm_lb4;
                            }
                            {
                                void * t70 = gm_l0;
                                void * t71 = GM_LD(void *, (((uint8_t *)t70) + 3488));
                                uint64_t t72 = GM_LD(uint64_t, ((((uint8_t *)t70) + 3488) + 8));
                                uint64_t t73 = gm_l3;
                                if ((uintptr_t)t73 >= (uintptr_t)(t72)) gm_trap_index((uintptr_t)t73, t72, gm_srcuro7l7pd15c2, 59, 19);
                                memmove(((uint8_t *)&gm_l4), (((uint8_t *)t71) + (uintptr_t)t73 * 32), 32);
                                gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
                                gm_l5 = t74;
                                void * t75 = gm_l0;
                                gm_b16_8 t76 = gm_l5;
                                GM_ST(gm_b16_8, (((uint8_t *)t75) + 3584), t76);
                                uint64_t t77 = gm_l3;
                                uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                                if (o79) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 62, 13);
                                uint64_t t80 = r78;
                                gm_l3 = t80;
                                void * t81 = gm_l0;
                                void * t82 = (void *)(((uint8_t *)t81) + 8);
                                gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
                                uint64_t t84 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t82, t83);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l6 = t84;
                                void * t85 = gm_l0;
                                void * t86 = (void *)(((uint8_t *)t85) + 8);
                                gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
                                uint64_t t88 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t86, t87);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint64_t t89 = (t88 > ((uint64_t)1ULL) ? t88 : ((uint64_t)1ULL));
                                gm_l7 = t89;
                                void * t90 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t91 = gm_l5;
                                uint64_t t92 = gm_l6;
                                uint64_t t93 = (t92 > ((uint64_t)1ULL) ? t92 : ((uint64_t)1ULL));
                                uint64_t t94 = gm_l7;
                                gm_tgbgti82mf879 t95;
                                GM_ST(gm_b16_8, ((uint8_t *)&t95 + 0), t91);
                                GM_ST(uint64_t, ((uint8_t *)&t95 + 16), t93);
                                GM_ST(uint64_t, ((uint8_t *)&t95 + 24), t94);
                                gm_tftb1odc739t5 t96 = gm_f_write_60kg76i(t90, &t95);
                                gm_l58 = t96;
                                uint8_t t97 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                if (!t97) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_srcuro7l7pd15c2, 65, 13);
                                gm_trqfvmjtt4sij t98;
                                gm_f_list_uvfu1k1((void *)&t98, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l8 = t98;
                                gm_l10 = ((uint64_t)0ULL);
                                uint64_t t99 = gm_l6;
                                gm_l9 = t99;
                                {
                                    for (;;) {
                                        uint64_t t100 = gm_l10;
                                        uint64_t t101 = gm_l9;
                                        uint8_t t102 = (t100 < t101);
                                        if (!t102) {
                                            goto gm_lb7;
                                        }
                                        {
                                            {
                                                {
                                                    void * t103 = (void *)((uint8_t *)&F.l8);
                                                    gm_tftb1odc739t5 t104 = gm_f_push_t83rg2m(t103, ((uint8_t)0ULL));
                                                    gm_l59 = t104;
                                                    uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                                    if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srcuro7l7pd15c2, 67, 32);
                                                    goto gm_lb10;
                                                }
gm_lb11:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 67, 13);
                                            }
gm_lb10:;
                                        }
gm_lb9:;
                                        uint64_t t106 = gm_l10;
                                        uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                                        uint64_t t109 = r107;
                                        gm_l10 = t109;
                                    }
                                }
gm_lb7:;
                                gm_trqfvmjtt4sij t110;
                                gm_f_list_opmir2c((void *)&t110, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l11 = t110;
                                void * t111 = gm_l0;
                                void * t112 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                gm_t88rcucg1b7uk t113 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
                                gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                                void * t115 = (void *)((uint8_t *)&F.l11);
                                gm_t6evimt84lndq t116 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t112));
                                gm_f_value_bytes_qm8egmj((void *)&gm_l60, (void *)NULL, cx, NULL, t111, &t116, t113, t114, ((uint64_t)0ULL), t115);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                if (t117) {
                                } else {
                                    gm_t08olq97rscn3 t118 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l60) + 8));
                                    gm_t9t4u07j77scv t119;
                                    GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
                                    ((uint8_t *)&t119)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t119 + 8), t118);
                                    memmove(gm_out, ((uint8_t *)&t119), 40);
                                    goto gm_fdone;
                                }
                                uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                                uint8_t t121 = (t120 > ((uint64_t)0ULL));
                                if (t121) {
                                    void * t122 = gm_l0;
                                    gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                                    gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
                                    gm_b16_8 t125 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t122, t124);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l12 = t125;
                                    void * t126 = (void *)((uint8_t *)&F.l2);
                                    gm_b16_8 t127 = gm_l5;
                                    gm_b16_8 t128 = gm_l12;
                                    uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                                    gm_tftksr4op114e t130;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t130 + 0), t127);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t130 + 16), t128);
                                    GM_ST(uint64_t, ((uint8_t *)&t130 + 32), t129);
                                    gm_tftb1odc739t5 t131 = gm_f_write_snsbmln(t126, &t130);
                                    gm_l61 = t131;
                                    uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                    if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srcuro7l7pd15c2, 72, 17);
                                }
                                gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                                gm_l13 = t133;
                                gm_l14 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t134 = gm_l14;
                                        uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                        uint8_t t136 = (t134 < t135);
                                        if (!t136) {
                                            goto gm_lb12;
                                        }
                                        {
                                            void * t137 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                            uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                            uint64_t t139 = gm_l14;
                                            {
                                                {
                                                    memmove(((uint8_t *)&gm_l15), (((uint8_t *)t137) + (uintptr_t)t139 * 32), 32);
                                                    void * t140 = (void *)((uint8_t *)&F.l2);
                                                    gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                                                    uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 24));
                                                    gm_b16_8 t143 = gm_l5;
                                                    uint64_t t144 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                                    gm_thddbqb6snabm t145;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t145 + 0), t141);
                                                    GM_ST(uint64_t, ((uint8_t *)&t145 + 16), t142);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t145 + 24), t143);
                                                    GM_ST(uint64_t, ((uint8_t *)&t145 + 40), t144);
                                                    gm_tftb1odc739t5 t146 = gm_f_write_th5g0ba(t140, &t145);
                                                    gm_l62 = t146;
                                                    uint8_t t147 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                                    if (!t147) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_srcuro7l7pd15c2, 75, 17);
                                                    goto gm_lb15;
                                                }
gm_lb16:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 74, 13);
                                            }
gm_lb15:;
                                        }
gm_lb14:;
                                        uint64_t t148 = gm_l14;
                                        uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                                        uint64_t t151 = r149;
                                        gm_l14 = t151;
                                    }
                                }
gm_lb12:;
                            }
gm_lb6:;
                        }
                    }
gm_lb4:;
                    void * t152 = gm_l0;
                    gm_f_drain_leoibma((void *)&gm_l63, (void *)NULL, cx, NULL, t152);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                    if (t153) {
                    } else {
                        gm_t08olq97rscn3 t154 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l63) + 8));
                        gm_t9t4u07j77scv t155;
                        GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
                        ((uint8_t *)&t155)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t155 + 8), t154);
                        memmove(gm_out, ((uint8_t *)&t155), 40);
                        goto gm_fdone;
                    }
                    uint64_t t156 = gm_l3;
                    void * t157 = gm_l0;
                    uint64_t t158 = GM_LD(uint64_t, ((((uint8_t *)t157) + 3488) + 8));
                    uint8_t t159 = ((t156 == t158));
                    if (t159) {
                        goto gm_lb1;
                    }
                }
gm_lb3:;
            }
        }
gm_lb1:;
    }
    {
        void * t160 = gm_l0;
        void * t161 = GM_LD(void *, ((uint8_t *)t160));
        void * t162 = GM_LD(void *, ((uint8_t *)t161));
        gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)t162) + 1568));
        gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&t163));
        gm_l16 = t164;
        gm_l17 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t165 = gm_l17;
                uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint8_t t167 = (t165 < t166);
                if (!t167) {
                    goto gm_lb17;
                }
                {
                    uint64_t t168 = gm_l17;
                    void * t169 = GM_LD(void *, ((uint8_t *)&gm_l16));
                    uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint64_t t171 = gm_l17;
                    gm_tq7qjpkmqmc5s t172 = GM_LD(gm_tq7qjpkmqmc5s, (((uint8_t *)t169) + (uintptr_t)t171 * 104));
                    gm_t964gsbl2irde t173;
                    GM_ST(uint64_t, ((uint8_t *)&t173 + 0), t168);
                    GM_ST(gm_tq7qjpkmqmc5s, ((uint8_t *)&t173 + 8), t172);
                    gm_l64 = t173;
                    {
                        {
                            uint64_t t174 = GM_LD(uint64_t, ((uint8_t *)&gm_l64));
                            gm_l18 = t174;
                            memmove(((uint8_t *)&F.l19), (((uint8_t *)&gm_l64) + 8), 104);
                            void * t175 = gm_l0;
                            uint64_t t176 = gm_l18;
                            gm_b16_8 t177 = gm_f_global_tag_gs7s3p6((void *)NULL, cx, NULL, t175, t176);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l20 = t177;
                            void * t178 = gm_l0;
                            gm_b16_8 t179 = gm_l20;
                            gm_tnpifv20irnph t180;
                            GM_ST(gm_b16_8, ((uint8_t *)&t180 + 0), t179);
                            gm_f_format_0o96lei((void *)&gm_l65, (void *)NULL, cx, NULL, t180);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t181 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                            if (!t181) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_srcuro7l7pd15c2, 84, 27);
                            gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                            gm_b16_8 t183 = GM_LD(gm_b16_8, ((uint8_t *)&t182));
                            GM_ST(gm_b16_8, (((uint8_t *)t178) + 3584), t183);
                            void * t184 = gm_l0;
                            void * t185 = (void *)(((uint8_t *)t184) + 8);
                            gm_t88rcucg1b7uk t186 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 24));
                            uint64_t t187 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t185, t186);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l21 = t187;
                            void * t188 = gm_l0;
                            void * t189 = (void *)(((uint8_t *)t188) + 8);
                            gm_t88rcucg1b7uk t190 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 24));
                            uint64_t t191 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t189, t190);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t192 = (uint8_t)(((((uint8_t *)&F.l19) + 80))[8] != 0);
                            if (t192) {
                                uint64_t t193 = GM_LD(uint64_t, (((uint8_t *)&F.l19) + 80));
                                gm_l66 = t193;
                            } else {
                                gm_l66 = ((uint64_t)1ULL);
                            }
                            uint64_t t194 = gm_l66;
                            uint64_t t195 = (t191 > t194 ? t191 : t194);
                            uint64_t t196 = (t195 > ((uint64_t)1ULL) ? t195 : ((uint64_t)1ULL));
                            gm_l22 = t196;
                            gm_b16_8 t197;
                            GM_ST(void *, (uint8_t *)&t197, (void *)(gm_scc54gv4a7k04));
                            GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(5));
                            gm_th2lqbo77pvsc t198;
                            GM_ST(uint8_t, (uint8_t *)&t198 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t198 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t198 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t198), t197);
                            ((uint8_t *)&t198)[16] = 1;
                            gm_th2lqbo77pvsc t199 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l19) + 56));
                            uint8_t t200 = (gm_eq0ogca82g1f(((uint8_t *)&t199), ((uint8_t *)&t198)));
                            if (t200) {
                                void * t201 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t202 = gm_l20;
                                gm_tnpifv20irnph t203;
                                GM_ST(gm_b16_8, ((uint8_t *)&t203 + 0), t202);
                                gm_tftb1odc739t5 t204 = gm_f_write_80k6mh9(t201, t203);
                                gm_l67 = t204;
                                uint8_t t205 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                                if (!t205) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 1, gm_srcuro7l7pd15c2, 90, 13);
                                goto gm_lb19;
                            }
                            {
                                {
                                    uint8_t t206 = (uint8_t)(((((uint8_t *)&F.l19) + 56))[16] != 0);
                                    if (!t206) {
                                        goto gm_lb23;
                                    }
                                    gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 56));
                                    gm_l23 = t207;
                                    uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 48));
                                    if (t208) {
                                        gm_b16_8 t209;
                                        GM_ST(void *, (uint8_t *)&t209, (void *)(gm_slpqve1dqssq0));
                                        GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(7));
                                        gm_l68 = t209;
                                    } else {
                                        gm_b16_8 t210;
                                        GM_ST(void *, (uint8_t *)&t210, (void *)(gm_sr4qbrk6tlgaa));
                                        GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(0));
                                        gm_l68 = t210;
                                    }
                                    gm_b16_8 t211 = gm_l68;
                                    gm_l24 = t211;
                                    void * t212 = (void *)((uint8_t *)&F.l1);
                                    gm_b16_8 t213 = gm_l24;
                                    gm_b16_8 t214 = gm_l20;
                                    gm_b16_8 t215 = gm_l23;
                                    gm_b16_8 t216 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t215);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tfeab279c16rr t217;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t217 + 0), t213);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t217 + 16), t214);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t217 + 32), t216);
                                    gm_tftb1odc739t5 t218 = gm_f_write_e5h23ca(t212, &t217);
                                    gm_l69 = t218;
                                    uint8_t t219 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                                    if (!t219) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 1, gm_srcuro7l7pd15c2, 95, 13);
                                    goto gm_lb19;
                                }
gm_lb23:;
                            }
gm_lb22:;
                            uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 48));
                            if (t220) {
                                gm_b16_8 t221;
                                GM_ST(void *, (uint8_t *)&t221, (void *)(gm_slpqve1dqssq0));
                                GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(7));
                                gm_l70 = t221;
                            } else {
                                gm_b16_8 t222;
                                GM_ST(void *, (uint8_t *)&t222, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)(0));
                                gm_l70 = t222;
                            }
                            gm_b16_8 t223 = gm_l70;
                            gm_l25 = t223;
                            void * t224 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t225 = gm_l25;
                            gm_b16_8 t226 = gm_l20;
                            uint64_t t227 = gm_l21;
                            uint64_t t228 = (t227 > ((uint64_t)1ULL) ? t227 : ((uint64_t)1ULL));
                            uint64_t t229 = gm_l22;
                            gm_teqsi7sd08g9f t230;
                            GM_ST(gm_b16_8, ((uint8_t *)&t230 + 0), t225);
                            GM_ST(gm_b16_8, ((uint8_t *)&t230 + 16), t226);
                            GM_ST(uint64_t, ((uint8_t *)&t230 + 32), t228);
                            GM_ST(uint64_t, ((uint8_t *)&t230 + 40), t229);
                            gm_tftb1odc739t5 t231 = gm_f_write_lket48d(t224, &t230);
                            gm_l71 = t231;
                            uint8_t t232 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                            if (!t232) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 1, gm_srcuro7l7pd15c2, 99, 9);
                            gm_trqfvmjtt4sij t233;
                            gm_f_list_uvfu1k1((void *)&t233, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l26 = t233;
                            uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 48));
                            if (t234) {
                                void * t235 = (void *)((uint8_t *)&F.l26);
                                gm_l72 = t235;
                            } else {
                                void * t236 = (void *)((uint8_t *)&F.l2);
                                gm_l72 = t236;
                            }
                            void * t237 = gm_l72;
                            gm_l27 = t237;
                            {
                                {
                                    uint8_t t238 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l19) + 96)) != 0);
                                    if (!t238) {
                                        goto gm_lb25;
                                    }
                                    void * t239 = GM_LD(void *, (((uint8_t *)&F.l19) + 96));
                                    gm_l28 = t239;
                                    gm_trqfvmjtt4sij t240;
                                    gm_f_list_uvfu1k1((void *)&t240, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l29 = t240;
                                    gm_l31 = ((uint64_t)0ULL);
                                    uint64_t t241 = gm_l21;
                                    gm_l30 = t241;
                                    {
                                        for (;;) {
                                            uint64_t t242 = gm_l31;
                                            uint64_t t243 = gm_l30;
                                            uint8_t t244 = (t242 < t243);
                                            if (!t244) {
                                                goto gm_lb26;
                                            }
                                            {
                                                {
                                                    {
                                                        void * t245 = (void *)((uint8_t *)&F.l29);
                                                        gm_tftb1odc739t5 t246 = gm_f_push_t83rg2m(t245, ((uint8_t)0ULL));
                                                        gm_l73 = t246;
                                                        uint8_t t247 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                                                        if (!t247) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 1, gm_srcuro7l7pd15c2, 106, 32);
                                                        goto gm_lb29;
                                                    }
gm_lb30:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 106, 13);
                                                }
gm_lb29:;
                                            }
gm_lb28:;
                                            uint64_t t248 = gm_l31;
                                            uint64_t r249; bool o250 = __builtin_add_overflow(t248, ((uint64_t)1ULL), &r249);
                                            uint64_t t251 = r249;
                                            gm_l31 = t251;
                                        }
                                    }
gm_lb26:;
                                    gm_trqfvmjtt4sij t252;
                                    gm_f_list_opmir2c((void *)&t252, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l32 = t252;
                                    void * t253 = gm_l0;
                                    void * t254 = gm_l28;
                                    gm_t88rcucg1b7uk t255 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 24));
                                    gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&F.l29));
                                    void * t257 = (void *)((uint8_t *)&F.l32);
                                    gm_t6evimt84lndq t258 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t254));
                                    gm_f_value_bytes_qm8egmj((void *)&gm_l74, (void *)NULL, cx, NULL, t253, &t258, t255, t256, ((uint64_t)0ULL), t257);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t259 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                                    if (t259) {
                                    } else {
                                        gm_t08olq97rscn3 t260 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l74) + 8));
                                        gm_t9t4u07j77scv t261;
                                        GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
                                        ((uint8_t *)&t261)[0] = 1;
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t261 + 8), t260);
                                        memmove(gm_out, ((uint8_t *)&t261), 40);
                                        goto gm_fdone;
                                    }
                                    uint64_t t262 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 8));
                                    uint8_t t263 = (t262 > ((uint64_t)0ULL));
                                    if (t263) {
                                        void * t264 = gm_l0;
                                        gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&F.l29));
                                        gm_b16_8 t266 = GM_LD(gm_b16_8, ((uint8_t *)&t265));
                                        gm_b16_8 t267 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t264, t266);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l33 = t267;
                                        void * t268 = gm_l27;
                                        gm_b16_8 t269 = gm_l20;
                                        gm_b16_8 t270 = gm_l33;
                                        uint64_t t271 = GM_LD(uint64_t, (((uint8_t *)&F.l29) + 8));
                                        gm_tftksr4op114e t272;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t272 + 0), t269);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t272 + 16), t270);
                                        GM_ST(uint64_t, ((uint8_t *)&t272 + 32), t271);
                                        gm_tftb1odc739t5 t273 = gm_f_write_dhp21l9(t268, &t272);
                                        gm_l75 = t273;
                                        uint8_t t274 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                                        if (!t274) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 1, gm_srcuro7l7pd15c2, 111, 17);
                                    }
                                    gm_b16_8 t275 = GM_LD(gm_b16_8, ((uint8_t *)&F.l32));
                                    gm_l34 = t275;
                                    gm_l35 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t276 = gm_l35;
                                            uint64_t t277 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                            uint8_t t278 = (t276 < t277);
                                            if (!t278) {
                                                goto gm_lb31;
                                            }
                                            {
                                                void * t279 = GM_LD(void *, ((uint8_t *)&gm_l34));
                                                uint64_t t280 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                                uint64_t t281 = gm_l35;
                                                {
                                                    {
                                                        memmove(((uint8_t *)&gm_l36), (((uint8_t *)t279) + (uintptr_t)t281 * 32), 32);
                                                        void * t282 = gm_l27;
                                                        gm_b16_8 t283 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                                                        uint64_t t284 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 24));
                                                        gm_b16_8 t285 = gm_l20;
                                                        uint64_t t286 = GM_LD(uint64_t, ((uint8_t *)&gm_l36));
                                                        gm_thddbqb6snabm t287;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t287 + 0), t283);
                                                        GM_ST(uint64_t, ((uint8_t *)&t287 + 16), t284);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t287 + 24), t285);
                                                        GM_ST(uint64_t, ((uint8_t *)&t287 + 40), t286);
                                                        gm_tftb1odc739t5 t288 = gm_f_write_vlrefbu(t282, &t287);
                                                        gm_l76 = t288;
                                                        uint8_t t289 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                                                        if (!t289) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 1, gm_srcuro7l7pd15c2, 114, 17);
                                                        goto gm_lb34;
                                                    }
gm_lb35:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 113, 13);
                                                }
gm_lb34:;
                                            }
gm_lb33:;
                                            uint64_t t290 = gm_l35;
                                            uint64_t r291; bool o292 = __builtin_add_overflow(t290, ((uint64_t)1ULL), &r291);
                                            uint64_t t293 = r291;
                                            gm_l35 = t293;
                                        }
                                    }
gm_lb31:;
                                    goto gm_lb24;
                                }
gm_lb25:;
                            }
gm_lb24:;
                            uint8_t t294 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 48));
                            if (t294) {
                                void * t295 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t296 = gm_l20;
                                gm_b16_8 t297 = gm_l20;
                                gm_b16_8 t298 = gm_l20;
                                gm_b16_8 t299 = gm_l20;
                                gm_b16_8 t300 = gm_l20;
                                gm_b16_8 t301 = gm_l20;
                                gm_b16_8 t302 = gm_l20;
                                gm_th2d9c7aju8hp t303;
                                GM_ST(gm_b16_8, ((uint8_t *)&t303 + 0), t296);
                                GM_ST(gm_b16_8, ((uint8_t *)&t303 + 16), t297);
                                GM_ST(gm_b16_8, ((uint8_t *)&t303 + 32), t298);
                                GM_ST(gm_b16_8, ((uint8_t *)&t303 + 48), t299);
                                GM_ST(gm_b16_8, ((uint8_t *)&t303 + 64), t300);
                                GM_ST(gm_b16_8, ((uint8_t *)&t303 + 80), t301);
                                GM_ST(gm_b16_8, ((uint8_t *)&t303 + 96), t302);
                                gm_tftb1odc739t5 t304 = gm_f_write_qbs7gku(t295, &t303);
                                gm_l77 = t304;
                                uint8_t t305 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                                if (!t305) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 1, gm_srcuro7l7pd15c2, 118, 13);
                                void * t306 = gm_l0;
                                void * t307 = (void *)(((uint8_t *)t306) + 1984);
                                gm_b16_8 t308 = gm_l20;
                                gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
                                gm_t6j6smj7qoctu t310;
                                GM_ST(gm_b16_8, ((uint8_t *)&t310 + 0), t308);
                                GM_ST(gm_b16_8, ((uint8_t *)&t310 + 16), t309);
                                gm_tftb1odc739t5 t311 = gm_f_write_ufh35j8(t307, &t310);
                                gm_l78 = t311;
                                uint8_t t312 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                                if (!t312) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 1, gm_srcuro7l7pd15c2, 119, 13);
                            }
                            goto gm_lb20;
                        }
gm_lb21:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 82, 5);
                    }
gm_lb20:;
                }
gm_lb19:;
                uint64_t t313 = gm_l17;
                uint64_t r314; bool o315 = __builtin_add_overflow(t313, ((uint64_t)1ULL), &r314);
                uint64_t t316 = r314;
                gm_l17 = t316;
            }
        }
gm_lb17:;
    }
    {
        {
            for (;;) {
                uint64_t t317 = gm_l3;
                void * t318 = gm_l0;
                uint64_t t319 = GM_LD(uint64_t, ((((uint8_t *)t318) + 3488) + 8));
                uint8_t t320 = (t317 < t319);
                if (!t320) {
                    goto gm_lb36;
                }
                {
                    void * t321 = gm_l0;
                    void * t322 = GM_LD(void *, (((uint8_t *)t321) + 3488));
                    uint64_t t323 = GM_LD(uint64_t, ((((uint8_t *)t321) + 3488) + 8));
                    uint64_t t324 = gm_l3;
                    if ((uintptr_t)t324 >= (uintptr_t)(t323)) gm_trap_index((uintptr_t)t324, t323, gm_srcuro7l7pd15c2, 124, 15);
                    memmove(((uint8_t *)&gm_l37), (((uint8_t *)t322) + (uintptr_t)t324 * 32), 32);
                    gm_b16_8 t325 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 16));
                    gm_l38 = t325;
                    void * t326 = gm_l0;
                    gm_b16_8 t327 = gm_l38;
                    GM_ST(gm_b16_8, (((uint8_t *)t326) + 3584), t327);
                    uint64_t t328 = gm_l3;
                    uint64_t r329; bool o330 = __builtin_add_overflow(t328, ((uint64_t)1ULL), &r329);
                    if (o330) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 127, 9);
                    uint64_t t331 = r329;
                    gm_l3 = t331;
                    void * t332 = gm_l0;
                    void * t333 = (void *)(((uint8_t *)t332) + 8);
                    gm_t88rcucg1b7uk t334 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l37) + 8));
                    uint64_t t335 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t333, t334);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l39 = t335;
                    void * t336 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t337 = gm_l38;
                    uint64_t t338 = gm_l39;
                    uint64_t t339 = (t338 > ((uint64_t)1ULL) ? t338 : ((uint64_t)1ULL));
                    void * t340 = gm_l0;
                    void * t341 = (void *)(((uint8_t *)t340) + 8);
                    gm_t88rcucg1b7uk t342 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l37) + 8));
                    uint64_t t343 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t341, t342);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t344 = (t343 > ((uint64_t)1ULL) ? t343 : ((uint64_t)1ULL));
                    gm_tgbgti82mf879 t345;
                    GM_ST(gm_b16_8, ((uint8_t *)&t345 + 0), t337);
                    GM_ST(uint64_t, ((uint8_t *)&t345 + 16), t339);
                    GM_ST(uint64_t, ((uint8_t *)&t345 + 24), t344);
                    gm_tftb1odc739t5 t346 = gm_f_write_60kg76i(t336, &t345);
                    gm_l79 = t346;
                    uint8_t t347 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                    if (!t347) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 1, gm_srcuro7l7pd15c2, 129, 9);
                    gm_trqfvmjtt4sij t348;
                    gm_f_list_uvfu1k1((void *)&t348, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l40 = t348;
                    gm_l42 = ((uint64_t)0ULL);
                    uint64_t t349 = gm_l39;
                    gm_l41 = t349;
                    {
                        for (;;) {
                            uint64_t t350 = gm_l42;
                            uint64_t t351 = gm_l41;
                            uint8_t t352 = (t350 < t351);
                            if (!t352) {
                                goto gm_lb39;
                            }
                            {
                                {
                                    {
                                        void * t353 = (void *)((uint8_t *)&F.l40);
                                        gm_tftb1odc739t5 t354 = gm_f_push_t83rg2m(t353, ((uint8_t)0ULL));
                                        gm_l80 = t354;
                                        uint8_t t355 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                                        if (!t355) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 1, gm_srcuro7l7pd15c2, 131, 28);
                                        goto gm_lb42;
                                    }
gm_lb43:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 131, 9);
                                }
gm_lb42:;
                            }
gm_lb41:;
                            uint64_t t356 = gm_l42;
                            uint64_t r357; bool o358 = __builtin_add_overflow(t356, ((uint64_t)1ULL), &r357);
                            uint64_t t359 = r357;
                            gm_l42 = t359;
                        }
                    }
gm_lb39:;
                    gm_trqfvmjtt4sij t360;
                    gm_f_list_opmir2c((void *)&t360, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l43 = t360;
                    void * t361 = gm_l0;
                    void * t362 = GM_LD(void *, ((uint8_t *)&gm_l37));
                    gm_t88rcucg1b7uk t363 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l37) + 8));
                    gm_b16_8 t364 = GM_LD(gm_b16_8, ((uint8_t *)&F.l40));
                    void * t365 = (void *)((uint8_t *)&F.l43);
                    gm_t6evimt84lndq t366 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t362));
                    gm_f_value_bytes_qm8egmj((void *)&gm_l81, (void *)NULL, cx, NULL, t361, &t366, t363, t364, ((uint64_t)0ULL), t365);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t367 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                    if (t367) {
                    } else {
                        gm_t08olq97rscn3 t368 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l81) + 8));
                        gm_t9t4u07j77scv t369;
                        GM_ST(uint8_t, (uint8_t *)&t369 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t369 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t369 + 4, 0);
                        ((uint8_t *)&t369)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t369 + 8), t368);
                        memmove(gm_out, ((uint8_t *)&t369), 40);
                        goto gm_fdone;
                    }
                    uint64_t t370 = GM_LD(uint64_t, (((uint8_t *)&F.l40) + 8));
                    uint8_t t371 = (t370 > ((uint64_t)0ULL));
                    if (t371) {
                        void * t372 = gm_l0;
                        gm_b16_8 t373 = GM_LD(gm_b16_8, ((uint8_t *)&F.l40));
                        gm_b16_8 t374 = GM_LD(gm_b16_8, ((uint8_t *)&t373));
                        gm_b16_8 t375 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t372, t374);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l44 = t375;
                        void * t376 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t377 = gm_l38;
                        gm_b16_8 t378 = gm_l44;
                        uint64_t t379 = GM_LD(uint64_t, (((uint8_t *)&F.l40) + 8));
                        gm_tftksr4op114e t380;
                        GM_ST(gm_b16_8, ((uint8_t *)&t380 + 0), t377);
                        GM_ST(gm_b16_8, ((uint8_t *)&t380 + 16), t378);
                        GM_ST(uint64_t, ((uint8_t *)&t380 + 32), t379);
                        gm_tftb1odc739t5 t381 = gm_f_write_snsbmln(t376, &t380);
                        gm_l82 = t381;
                        uint8_t t382 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
                        if (!t382) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l82) + 1, gm_srcuro7l7pd15c2, 136, 13);
                    }
                    gm_b16_8 t383 = GM_LD(gm_b16_8, ((uint8_t *)&F.l43));
                    gm_l45 = t383;
                    gm_l46 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t384 = gm_l46;
                            uint64_t t385 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                            uint8_t t386 = (t384 < t385);
                            if (!t386) {
                                goto gm_lb44;
                            }
                            {
                                void * t387 = GM_LD(void *, ((uint8_t *)&gm_l45));
                                uint64_t t388 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                                uint64_t t389 = gm_l46;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l47), (((uint8_t *)t387) + (uintptr_t)t389 * 32), 32);
                                        void * t390 = (void *)((uint8_t *)&F.l2);
                                        gm_b16_8 t391 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                                        uint64_t t392 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 24));
                                        gm_b16_8 t393 = gm_l38;
                                        uint64_t t394 = GM_LD(uint64_t, ((uint8_t *)&gm_l47));
                                        gm_thddbqb6snabm t395;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t395 + 0), t391);
                                        GM_ST(uint64_t, ((uint8_t *)&t395 + 16), t392);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t395 + 24), t393);
                                        GM_ST(uint64_t, ((uint8_t *)&t395 + 40), t394);
                                        gm_tftb1odc739t5 t396 = gm_f_write_th5g0ba(t390, &t395);
                                        gm_l83 = t396;
                                        uint8_t t397 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
                                        if (!t397) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l83) + 1, gm_srcuro7l7pd15c2, 139, 13);
                                        goto gm_lb47;
                                    }
gm_lb48:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 138, 9);
                                }
gm_lb47:;
                            }
gm_lb46:;
                            uint64_t t398 = gm_l46;
                            uint64_t r399; bool o400 = __builtin_add_overflow(t398, ((uint64_t)1ULL), &r399);
                            uint64_t t401 = r399;
                            gm_l46 = t401;
                        }
                    }
gm_lb44:;
                }
gm_lb38:;
            }
        }
gm_lb36:;
    }
    {
        void * t402 = gm_l0;
        gm_f_drain_leoibma((void *)&gm_l84, (void *)NULL, cx, NULL, t402);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t403 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
        if (t403) {
        } else {
            gm_t08olq97rscn3 t404 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l84) + 8));
            gm_t9t4u07j77scv t405;
            GM_ST(uint8_t, (uint8_t *)&t405 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t405 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t405 + 4, 0);
            ((uint8_t *)&t405)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t405 + 8), t404);
            memmove(gm_out, ((uint8_t *)&t405), 40);
            goto gm_fdone;
        }
        uint64_t t406 = gm_l3;
        void * t407 = gm_l0;
        uint64_t t408 = GM_LD(uint64_t, ((((uint8_t *)t407) + 3488) + 8));
        uint8_t t409 = (!(t406 == t408));
        if (t409) {
            gm_tl874t2atnlh3 t410 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
            gm_b16_8 t411;
            GM_ST(void *, (uint8_t *)&t411, (void *)(gm_s2uimijqjekbd));
            GM_ST(uintptr_t, (uint8_t *)&t411 + 8, (uintptr_t)(66));
            gm_t08olq97rscn3 t412;
            gm_f_unsupported_531q51r((void *)&t412, (void *)NULL, cx, NULL, t410, t411);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9t4u07j77scv t413;
            GM_ST(uint8_t, (uint8_t *)&t413 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t413 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t413 + 4, 0);
            ((uint8_t *)&t413)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t413 + 8), t412);
            memmove(gm_out, ((uint8_t *)&t413), 40);
            goto gm_fdone;
        }
        void * t414 = gm_l0;
        gm_b16_8 t415 = GM_LD(gm_b16_8, (((uint8_t *)t414) + 2552));
        gm_l48 = t415;
        gm_l49 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t416 = gm_l49;
                uint64_t t417 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                uint8_t t418 = (t416 < t417);
                if (!t418) {
                    goto gm_lb49;
                }
                {
                    void * t419 = GM_LD(void *, ((uint8_t *)&gm_l48));
                    uint64_t t420 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                    uint64_t t421 = gm_l49;
                    {
                        {
                            memmove(((uint8_t *)&F.l50), (((uint8_t *)t419) + (uintptr_t)t421 * 104), 104);
                            gm_b16_8 t422 = GM_LD(gm_b16_8, (((uint8_t *)&F.l50) + 88));
                            gm_l51 = t422;
                            void * t423 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t424 = gm_l51;
                            void * t425 = gm_l0;
                            void * t426 = (void *)(((uint8_t *)t425) + 8);
                            gm_t88rcucg1b7uk t427 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&F.l50));
                            uint64_t t428 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t426, t427);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t429 = (t428 > ((uint64_t)1ULL) ? t428 : ((uint64_t)1ULL));
                            void * t430 = gm_l0;
                            void * t431 = (void *)(((uint8_t *)t430) + 8);
                            gm_t88rcucg1b7uk t432 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&F.l50));
                            uint64_t t433 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t431, t432);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t434 = (t433 > ((uint64_t)1ULL) ? t433 : ((uint64_t)1ULL));
                            gm_tgbgti82mf879 t435;
                            GM_ST(gm_b16_8, ((uint8_t *)&t435 + 0), t424);
                            GM_ST(uint64_t, ((uint8_t *)&t435 + 16), t429);
                            GM_ST(uint64_t, ((uint8_t *)&t435 + 24), t434);
                            gm_tftb1odc739t5 t436 = gm_f_write_60kg76i(t423, &t435);
                            gm_l85 = t436;
                            uint8_t t437 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
                            if (!t437) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l85) + 1, gm_srcuro7l7pd15c2, 147, 9);
                            uint64_t t438 = GM_LD(uint64_t, ((((uint8_t *)&F.l50) + 8) + 8));
                            uint8_t t439 = (t438 > ((uint64_t)0ULL));
                            if (t439) {
                                void * t440 = gm_l0;
                                gm_b16_8 t441 = GM_LD(gm_b16_8, (((uint8_t *)&F.l50) + 8));
                                gm_b16_8 t442 = GM_LD(gm_b16_8, ((uint8_t *)&t441));
                                gm_b16_8 t443 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t440, t442);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l52 = t443;
                                void * t444 = (void *)((uint8_t *)&F.l2);
                                gm_b16_8 t445 = gm_l51;
                                gm_b16_8 t446 = gm_l52;
                                uint64_t t447 = GM_LD(uint64_t, ((((uint8_t *)&F.l50) + 8) + 8));
                                gm_tftksr4op114e t448;
                                GM_ST(gm_b16_8, ((uint8_t *)&t448 + 0), t445);
                                GM_ST(gm_b16_8, ((uint8_t *)&t448 + 16), t446);
                                GM_ST(uint64_t, ((uint8_t *)&t448 + 32), t447);
                                gm_tftb1odc739t5 t449 = gm_f_write_snsbmln(t444, &t448);
                                gm_l86 = t449;
                                uint8_t t450 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
                                if (!t450) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l86) + 1, gm_srcuro7l7pd15c2, 150, 13);
                            }
                            gm_b16_8 t451 = GM_LD(gm_b16_8, (((uint8_t *)&F.l50) + 48));
                            gm_l53 = t451;
                            gm_l54 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t452 = gm_l54;
                                    uint64_t t453 = GM_LD(uint64_t, (((uint8_t *)&gm_l53) + 8));
                                    uint8_t t454 = (t452 < t453);
                                    if (!t454) {
                                        goto gm_lb54;
                                    }
                                    {
                                        void * t455 = GM_LD(void *, ((uint8_t *)&gm_l53));
                                        uint64_t t456 = GM_LD(uint64_t, (((uint8_t *)&gm_l53) + 8));
                                        uint64_t t457 = gm_l54;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l55), (((uint8_t *)t455) + (uintptr_t)t457 * 24), 24);
                                                void * t458 = gm_l0;
                                                gm_b16_8 t459 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
                                                gm_b16_8 t460 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t458, t459);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l56 = t460;
                                                void * t461 = (void *)((uint8_t *)&F.l2);
                                                gm_b16_8 t462 = gm_l56;
                                                gm_b16_8 t463 = gm_l51;
                                                uint64_t t464 = GM_LD(uint64_t, ((uint8_t *)&gm_l55));
                                                gm_tftksr4op114e t465;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t465 + 0), t462);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t465 + 16), t463);
                                                GM_ST(uint64_t, ((uint8_t *)&t465 + 32), t464);
                                                gm_tftb1odc739t5 t466 = gm_f_write_t09mk5p(t461, &t465);
                                                gm_l87 = t466;
                                                uint8_t t467 = (uint8_t)!((((uint8_t *)&gm_l87))[0] != 0);
                                                if (!t467) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l87) + 1, gm_srcuro7l7pd15c2, 154, 13);
                                                goto gm_lb57;
                                            }
gm_lb58:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 152, 9);
                                        }
gm_lb57:;
                                    }
gm_lb56:;
                                    uint64_t t468 = gm_l54;
                                    uint64_t r469; bool o470 = __builtin_add_overflow(t468, ((uint64_t)1ULL), &r469);
                                    uint64_t t471 = r469;
                                    gm_l54 = t471;
                                }
                            }
gm_lb54:;
                            goto gm_lb52;
                        }
gm_lb53:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 145, 5);
                    }
gm_lb52:;
                }
gm_lb51:;
                uint64_t t472 = gm_l49;
                uint64_t r473; bool o474 = __builtin_add_overflow(t472, ((uint64_t)1ULL), &r473);
                uint64_t t475 = r473;
                gm_l49 = t475;
            }
        }
gm_lb49:;
    }
    {
        void * t476 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t477;
        GM_ST(void *, (uint8_t *)&t477, (void *)(gm_sikpqtja8pqpv));
        GM_ST(uintptr_t, (uint8_t *)&t477 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t478 = gm_f_extend_0cs85gs(t476, t477);
        gm_l88 = t478;
        uint8_t t479 = (uint8_t)!((((uint8_t *)&gm_l88))[0] != 0);
        if (!t479) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l88) + 1, gm_srcuro7l7pd15c2, 157, 5);
        gm_b16_8 t480 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
        gm_b16_8 t481 = GM_LD(gm_b16_8, ((uint8_t *)&t480));
        gm_b16_8 t482 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        gm_b16_8 t483 = GM_LD(gm_b16_8, ((uint8_t *)&t482));
        gm_t6j6smj7qoctu t484;
        GM_ST(gm_b16_8, ((uint8_t *)&t484 + 0), t481);
        GM_ST(gm_b16_8, ((uint8_t *)&t484 + 16), t483);
        gm_t9t4u07j77scv t485;
        GM_ST(uint8_t, (uint8_t *)&t485 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t485 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t485 + 4, 0);
        ((uint8_t *)&t485)[0] = 0;
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t485 + 8), t484);
        memmove(gm_out, ((uint8_t *)&t485), 40);
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
__attribute__((aligned(4))) void gm_f_place_reloc_blfij5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, uint64_t a3) {
    struct gm_fr_place_reloc_blfij5d F;
    void * gm_l0; /* g */
    gm_tkaapcii86udk gm_l1; /* pl */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    uint64_t gm_l3; /* off */
    gm_t88rcucg1b7uk gm_l4; /* t */
    void * gm_l5; /* a */
    gm_t88rcucg1b7uk gm_l6; /* root */
    uint64_t gm_l7; /* addend */
    gm_t88rcucg1b7uk gm_l8; /* cur */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    uint64_t gm_l12; /* len */
    gm_t88rcucg1b7uk gm_l13; /* elem */
    gm_b16_8 gm_l14; /* elems */
    uint32_t gm_l15; /* id */
    GM_STACK_CHECK(&F, 1440u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t88rcucg1b7uk gm_l17;
    gm_tkn42u7na3t81 gm_l18;
    gm_t88rcucg1b7uk gm_l19;
    gm_t88rcucg1b7uk gm_l20;
    gm_t53at80pfdus3 gm_l21;
    gm_b16_8 gm_l22;
    gm_b16_8 gm_l23;
    gm_tabb710d9mq2e gm_l24;
    {
        {
            void * t18 = gm_l0;
            void * t19 = GM_LD(void *, ((uint8_t *)a1));
            gm_tkn42u7na3t81 t20 = gm_f_cell_type_0jgf3ue(t18, t19);
            gm_l18 = t20;
            uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l18))[4] != 0);
            if (!t21) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l18));
            gm_l4 = t22;
            gm_t88rcucg1b7uk t23 = gm_l4;
            gm_l17 = t23;
            goto gm_lb1;
        }
gm_lb2:;
        uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 8));
        uint8_t t25 = ((t24 == ((uint64_t)0ULL)));
        if (t25) {
            gm_t88rcucg1b7uk t26 = gm_l2;
            gm_l19 = t26;
        } else {
            {
                {
                    uint64_t t27 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 8));
                    uint8_t t28 = ((t27 == ((uint64_t)1ULL)));
                    if (!t28) {
                        goto gm_lb4;
                    }
                    void * t29 = GM_LD(void *, ((uint8_t *)a1));
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t29) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)3ULL));
                    if (!t31) {
                        goto gm_lb4;
                    }
                    void * t32 = GM_LD(void *, (((uint8_t *)t29) + 16));
                    gm_l5 = t32;
                    void * t33 = gm_l0;
                    void * t34 = GM_LD(void *, ((uint8_t *)t33));
                    void * t35 = GM_LD(void *, ((uint8_t *)t34));
                    void * t36 = (void *)(((uint8_t *)t35) + 8);
                    void * t37 = gm_l5;
                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 8));
                    uint64_t t39 = (uint64_t)t38;
                    gm_t88rcucg1b7uk t40 = gm_l2;
                    gm_t88rcucg1b7uk t41 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t36, t39, t40);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l20 = t41;
                    goto gm_lb3;
                }
gm_lb4:;
                gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                gm_b16_8 t43;
                GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sqrl3kgomorsv));
                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(38));
                gm_t08olq97rscn3 t44;
                gm_f_unsupported_531q51r((void *)&t44, (void *)NULL, cx, NULL, t42, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tt9fudfifskca t45;
                GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                ((uint8_t *)&t45)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t45 + 8), t44);
                memmove(gm_out, ((uint8_t *)&t45), 40);
                goto gm_fdone;
                gm_l20 = GM_ZERO(gm_t88rcucg1b7uk);
            }
gm_lb3:;
            gm_t88rcucg1b7uk t46 = gm_l20;
            gm_l19 = t46;
        }
        gm_t88rcucg1b7uk t47 = gm_l19;
        gm_l17 = t47;
    }
gm_lb1:;
    gm_t88rcucg1b7uk t48 = gm_l17;
    gm_l6 = t48;
    gm_l7 = ((uint64_t)0ULL);
    gm_t88rcucg1b7uk t49 = gm_l6;
    gm_l8 = t49;
    gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_l9 = t50;
    gm_l10 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t51 = gm_l10;
            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            uint8_t t53 = (t51 < t52);
            if (!t53) {
                goto gm_lb5;
            }
            {
                void * t54 = GM_LD(void *, ((uint8_t *)&gm_l9));
                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint64_t t56 = gm_l10;
                {
                    {
                        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t54) + (uintptr_t)t56 * 8));
                        gm_l11 = t57;
                        void * t58 = gm_l0;
                        void * t59 = (void *)(((uint8_t *)t58) + 8);
                        gm_t88rcucg1b7uk t60 = gm_l8;
                        gm_f_kind_ghvav22((void *)&gm_l21, t59, GM_CONV(gm_csguankko4lmhd, t60));
                        {
                            {
                                uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                uint8_t t62 = (uint8_t)(t61 == ((uint8_t)11ULL));
                                if (!t62) {
                                    goto gm_lb11;
                                }
                                uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                gm_l12 = t63;
                                gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l21) + 8) + 8));
                                gm_l13 = t64;
                                uint64_t t65 = gm_l7;
                                uint64_t t66 = gm_l11;
                                void * t67 = gm_l0;
                                void * t68 = (void *)(((uint8_t *)t67) + 8);
                                gm_t88rcucg1b7uk t69 = gm_l13;
                                uint64_t t70 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t68, t69);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint64_t r71; bool o72 = __builtin_mul_overflow(t66, t70, &r71);
                                if (o72) gm_trap("integer overflow in `*`", gm_srcuro7l7pd15c2, 290, 27);
                                uint64_t t73 = r71;
                                uint64_t r74; bool o75 = __builtin_add_overflow(t65, t73, &r74);
                                if (o75) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 290, 17);
                                uint64_t t76 = r74;
                                gm_l7 = t76;
                                uint64_t t77 = gm_l11;
                                uint64_t t78 = gm_l12;
                                uint8_t t79 = (t77 < t78);
                                if (t79) {
                                    gm_t88rcucg1b7uk t80 = gm_l13;
                                    gm_l8 = t80;
                                }
                                goto gm_lb10;
                            }
gm_lb11:;
                            {
                                uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                uint8_t t82 = (uint8_t)(t81 == ((uint8_t)12ULL));
                                if (!t82) {
                                    goto gm_lb12;
                                }
                                gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                                gm_l14 = t83;
                                uint64_t t84 = gm_l7;
                                void * t85 = gm_l0;
                                void * t86 = (void *)(((uint8_t *)t85) + 8);
                                gm_t88rcucg1b7uk t87 = gm_l8;
                                gm_b16_8 t88 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t86, t87);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l22 = t88;
                                void * t89 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                uint64_t t91 = gm_l11;
                                uint64_t t92 = (uint64_t)t91;
                                if ((uintptr_t)t92 >= (uintptr_t)(t90)) gm_trap_index((uintptr_t)t92, t90, gm_srcuro7l7pd15c2, 294, 27);
                                uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)t89) + (uintptr_t)t92 * 8));
                                uint64_t r94; bool o95 = __builtin_add_overflow(t84, t93, &r94);
                                if (o95) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 294, 17);
                                uint64_t t96 = r94;
                                gm_l7 = t96;
                                void * t97 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                uint64_t t99 = gm_l11;
                                uint64_t t100 = (uint64_t)t99;
                                if ((uintptr_t)t100 >= (uintptr_t)(t98)) gm_trap_index((uintptr_t)t100, t98, gm_srcuro7l7pd15c2, 295, 23);
                                gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t97) + (uintptr_t)t100 * 4));
                                gm_l8 = t101;
                                goto gm_lb10;
                            }
gm_lb12:;
                            {
                                uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                uint8_t t103 = (uint8_t)(t102 == ((uint8_t)17ULL));
                                if (!t103) {
                                    goto gm_lb13;
                                }
                                uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 8));
                                gm_l15 = t104;
                                {
                                    {
                                        void * t105 = gm_l0;
                                        void * t106 = GM_LD(void *, ((uint8_t *)t105));
                                        uint32_t t107 = gm_l15;
                                        void * t108 = gm_f_adt_mpqv8qe(t106, t107);
                                        uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t108) + 24) + 0));
                                        uint8_t t110 = (uint8_t)(t109 == ((uint8_t)0ULL));
                                        if (!t110) {
                                            goto gm_lb15;
                                        }
                                        memmove(((uint8_t *)&F.l16), ((((uint8_t *)t108) + 24) + 8), 48);
                                        uint64_t t111 = gm_l7;
                                        void * t112 = gm_l0;
                                        void * t113 = (void *)(((uint8_t *)t112) + 8);
                                        gm_t88rcucg1b7uk t114 = gm_l8;
                                        gm_b16_8 t115 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t113, t114);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l23 = t115;
                                        void * t116 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                        uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                        uint64_t t118 = gm_l11;
                                        uint64_t t119 = (uint64_t)t118;
                                        if ((uintptr_t)t119 >= (uintptr_t)(t117)) gm_trap_index((uintptr_t)t119, t117, gm_srcuro7l7pd15c2, 299, 27);
                                        uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)t116) + (uintptr_t)t119 * 8));
                                        uint64_t r121; bool o122 = __builtin_add_overflow(t111, t120, &r121);
                                        if (o122) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 299, 17);
                                        uint64_t t123 = r121;
                                        gm_l7 = t123;
                                        void * t124 = GM_LD(void *, ((uint8_t *)&F.l16));
                                        uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                        uint64_t t126 = gm_l11;
                                        uint64_t t127 = (uint64_t)t126;
                                        if ((uintptr_t)t127 >= (uintptr_t)(t125)) gm_trap_index((uintptr_t)t127, t125, gm_srcuro7l7pd15c2, 300, 23);
                                        gm_t88rcucg1b7uk t128 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t124) + (uintptr_t)t127 * 40) + 16));
                                        gm_l8 = t128;
                                        goto gm_lb14;
                                    }
gm_lb15:;
                                    gm_tl874t2atnlh3 t129 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                                    gm_b16_8 t130;
                                    GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sqrl3kgomorsv));
                                    GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(38));
                                    gm_t08olq97rscn3 t131;
                                    gm_f_unsupported_531q51r((void *)&t131, (void *)NULL, cx, NULL, t129, t130);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tt9fudfifskca t132;
                                    GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
                                    ((uint8_t *)&t132)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t132 + 8), t131);
                                    memmove(gm_out, ((uint8_t *)&t132), 40);
                                    goto gm_fdone;
                                }
gm_lb14:;
                                goto gm_lb10;
                            }
gm_lb13:;
                            {
                                gm_tl874t2atnlh3 t133 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                                gm_b16_8 t134;
                                GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sqrl3kgomorsv));
                                GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(38));
                                gm_t08olq97rscn3 t135;
                                gm_f_unsupported_531q51r((void *)&t135, (void *)NULL, cx, NULL, t133, t134);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tt9fudfifskca t136;
                                GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                                ((uint8_t *)&t136)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t136 + 8), t135);
                                memmove(gm_out, ((uint8_t *)&t136), 40);
                                goto gm_fdone;
                            }
gm_lb16:;
                            gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 288, 9);
                        }
gm_lb10:;
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 287, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t137 = gm_l10;
            uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
            uint64_t t140 = r138;
            gm_l10 = t140;
        }
    }
gm_lb5:;
    uint64_t t141 = gm_l3;
    void * t142 = gm_l0;
    void * t143 = GM_LD(void *, ((uint8_t *)a1));
    gm_t88rcucg1b7uk t144 = gm_l6;
    void * t145 = gm_l0;
    gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)t145) + 3584));
    uint64_t t147 = gm_l3;
    gm_t3877pk7lv748 t148;
    GM_ST(gm_b16_8, ((uint8_t *)&t148 + 0), t146);
    GM_ST(uint64_t, ((uint8_t *)&t148 + 16), t147);
    gm_f_format_317bju7((void *)&gm_l24, (void *)NULL, cx, NULL, &t148);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t149 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
    if (!t149) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_srcuro7l7pd15c2, 305, 69);
    gm_b16_8 t150 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
    gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
    gm_b16_8 t152 = gm_f_cell_storage_1rtp280((void *)NULL, cx, NULL, t142, t143, t144, t151);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t153 = gm_l7;
    gm_tob1snuii5bme t154;
    GM_ST(uint64_t, ((uint8_t *)&t154 + 0), t141);
    GM_ST(gm_b16_8, ((uint8_t *)&t154 + 8), t152);
    GM_ST(uint64_t, ((uint8_t *)&t154 + 24), t153);
    gm_tt9fudfifskca t155;
    GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
    ((uint8_t *)&t155)[0] = 0;
    GM_ST(gm_tob1snuii5bme, ((uint8_t *)&t155 + 8), t154);
    memmove(gm_out, ((uint8_t *)&t155), 40);
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
inline __attribute__((aligned(4))) void gm_f_store_int_mfkaqk5(void * a0, gm_b16_8 a1, uint64_t a2, unsigned __int128 a3, uint64_t a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_store_int_mfkaqk5 F;
    void * gm_l0; /* g */
    gm_b16_8 gm_l1; /* out */
    uint64_t gm_l2; /* off */
    unsigned __int128 gm_l3; /* v */
    uint64_t gm_l4; /* n */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    uint64_t gm_l8; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 315u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint64_t gm_l9;
    gm_l6 = ((uint64_t)0ULL);
    uint64_t t7 = gm_l4;
    gm_l5 = t7;
    {
        for (;;) {
            uint64_t t8 = gm_l6;
            uint64_t t9 = gm_l5;
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t11 = gm_l6;
                        gm_l7 = t11;
                        void * t12 = gm_l0;
                        uint8_t t13 = GM_LD(uint8_t, (((((uint8_t *)t12) + 8) + 8) + 8));
                        if (t13) {
                            uint64_t t14 = gm_l7;
                            gm_l9 = t14;
                        } else {
                            uint64_t t15 = gm_l4;
                            uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
                            if (o17) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 317, 61);
                            uint64_t t18 = r16;
                            uint64_t t19 = gm_l7;
                            uint64_t r20; bool o21 = __builtin_sub_overflow(t18, t19, &r20);
                            if (o21) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 317, 61);
                            uint64_t t22 = r20;
                            gm_l9 = t22;
                        }
                        uint64_t t23 = gm_l9;
                        gm_l8 = t23;
                        void * t24 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t26 = gm_l2;
                        uint64_t t27 = gm_l8;
                        uint64_t r28; bool o29 = __builtin_add_overflow(t26, t27, &r28);
                        if (o29) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 318, 14);
                        uint64_t t30 = r28;
                        uint64_t t31 = (uint64_t)t30;
                        if ((uintptr_t)t31 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t31, t25, gm_srcuro7l7pd15c2, 318, 9);
                        unsigned __int128 t32 = gm_l3;
                        uint64_t t33 = gm_l7;
                        uint64_t r34; bool o35 = __builtin_mul_overflow(((uint64_t)8ULL), t33, &r34);
                        if (o35) gm_trap("integer overflow in `*`", gm_srcuro7l7pd15c2, 318, 51);
                        uint64_t t36 = r34;
                        unsigned __int128 t37 = (unsigned __int128)t36;
                        unsigned __int128 n38 = (unsigned __int128)t37;
                        if (n38 >= 128) gm_trap_shift((uint64_t)n38, "u128", gm_srcuro7l7pd15c2, 318, 41);
                        unsigned __int128 t39 = (unsigned __int128)(t32 >> (int)n38);
                        uint8_t t40 = (uint8_t)t39;
                        GM_ST(uint8_t, (((uint8_t *)t24) + (uintptr_t)t31 * 1), t40);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 316, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t41 = gm_l6;
            uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
            uint64_t t44 = r42;
            gm_l6 = t44;
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
__attribute__((aligned(4))) void gm_f_value_bytes_qm8egmj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint64_t a4, void * a5) {
    struct gm_fr_value_bytes_qm8egmj F;
    void * gm_l0; /* g */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* out */
    uint64_t gm_l4; /* off */
    void * gm_l5; /* relocs */
    unsigned __int128 gm_l6; /* bits */
    uint64_t gm_l7; /* n */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* k */
    uint64_t gm_l11; /* at */
    double gm_l12; /* x */
    uint8_t gm_l13; /* b */
    void * gm_l14; /* x */
    gm_t88rcucg1b7uk gm_l15; /* inner */
    void * gm_l16; /* x */
    gm_t88rcucg1b7uk gm_l17; /* ok */
    gm_b16_8 gm_l18; /* offs */
    void * gm_l19; /* x */
    gm_t88rcucg1b7uk gm_l20; /* err */
    gm_b16_8 gm_l21; /* offs */
    uint32_t gm_l22; /* index */
    void * gm_l23; /* payload */
    uint32_t gm_l24; /* id */
    gm_t88rcucg1b7uk gm_l25; /* tag_ty */
    gm_b16_8 gm_l26; /* variants */
    gm_b16_8 gm_l28; /* offs */
    __int128 gm_l29; /* tag */
    void * gm_l30; /* x */
    gm_t88rcucg1b7uk gm_l31; /* pt */
    void * gm_l32; /* a */
    gm_t88rcucg1b7uk gm_l33; /* elem */
    uint64_t gm_l34; /* esz */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* k */
    gm_b16_8 gm_l39; /* elems */
    gm_b16_8 gm_l40; /* offs */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* k */
    uint32_t gm_l45; /* id */
    gm_t88rcucg1b7uk gm_l47; /* u */
    unsigned __int128 gm_l48; /* raw */
    gm_b16_8 gm_l49; /* offs */
    gm_b16_8 gm_l50; /* <tmp> */
    uint64_t gm_l51; /* <tmp> */
    uint64_t gm_l52; /* k */
    gm_tkaapcii86udk gm_l54; /* pl */
    gm_t88rcucg1b7uk gm_l55; /* p */
    gm_t88rcucg1b7uk gm_l56; /* p */
    gm_t88rcucg1b7uk gm_l57; /* inner */
    gm_t88rcucg1b7uk gm_l58; /* p */
    gm_t88rcucg1b7uk gm_l59; /* p */
    gm_t88rcucg1b7uk gm_l60; /* pointee */
    gm_tob1snuii5bme gm_l61; /* r */
    gm_tkaapcii86udk gm_l62; /* start */
    uint64_t gm_l63; /* len */
    gm_t88rcucg1b7uk gm_l64; /* elem */
    uint64_t gm_l65; /* p */
    gm_tob1snuii5bme gm_l66; /* r */
    uint32_t gm_l67; /* f */
    gm_b16_8 gm_l68; /* target */
    GM_STACK_CHECK(&F, 8138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    uint64_t gm_l69;
    gm_t53at80pfdus3 gm_l70;
    uint8_t gm_l71;
    gm_t53at80pfdus3 gm_l72;
    gm_tqgtd5gtgj65s gm_l73;
    gm_b16_8 gm_l74;
    gm_t53at80pfdus3 gm_l75;
    gm_tqgtd5gtgj65s gm_l76;
    gm_t53at80pfdus3 gm_l77;
    gm_tqgtd5gtgj65s gm_l78;
    gm_t53at80pfdus3 gm_l79;
    gm_ta24rd13gupia gm_l80;
    __int128 gm_l81;
    gm_tqgtd5gtgj65s gm_l82;
    gm_t88rcucg1b7uk gm_l83;
    gm_tqgtd5gtgj65s gm_l84;
    gm_t53at80pfdus3 gm_l85;
    gm_thf8as07kj3uj gm_l86;
    gm_tqgtd5gtgj65s gm_l87;
    gm_thf8as07kj3uj gm_l88;
    gm_tqgtd5gtgj65s gm_l89;
    gm_t3d480iurj7vi gm_l90;
    gm_thf8as07kj3uj gm_l91;
    gm_tqgtd5gtgj65s gm_l92;
    gm_t88rcucg1b7uk gm_l93;
    gm_t53at80pfdus3 gm_l94;
    gm_t88rcucg1b7uk gm_l95;
    gm_t53at80pfdus3 gm_l96;
    gm_tt9fudfifskca gm_l97;
    gm_tftb1odc739t5 gm_l98;
    gm_t53at80pfdus3 gm_l99;
    uint8_t gm_l100;
    gm_tt9fudfifskca gm_l101;
    gm_tftb1odc739t5 gm_l102;
    uint8_t gm_l103;
    gm_t53at80pfdus3 gm_l104;
    gm_b16_8 gm_l105;
    gm_tabb710d9mq2e gm_l106;
    gm_tabb710d9mq2e gm_l107;
    gm_tftb1odc739t5 gm_l108;
    {
        {
            {
                {
                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)0ULL));
                    if (!t83) {
                        goto gm_lb2;
                    }
                    unsigned __int128 t84 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
                    gm_l6 = t84;
                    void * t85 = gm_l0;
                    void * t86 = (void *)(((uint8_t *)t85) + 8);
                    gm_t88rcucg1b7uk t87 = gm_l2;
                    uint64_t t88 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t86, t87);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l7 = t88;
                    gm_l9 = ((uint64_t)0ULL);
                    uint64_t t89 = gm_l7;
                    gm_l8 = t89;
                    {
                        for (;;) {
                            uint64_t t90 = gm_l9;
                            uint64_t t91 = gm_l8;
                            uint8_t t92 = (t90 < t91);
                            if (!t92) {
                                goto gm_lb3;
                            }
                            {
                                {
                                    {
                                        uint64_t t93 = gm_l9;
                                        gm_l10 = t93;
                                        void * t94 = gm_l0;
                                        uint8_t t95 = GM_LD(uint8_t, (((((uint8_t *)t94) + 8) + 8) + 8));
                                        if (t95) {
                                            uint64_t t96 = gm_l10;
                                            gm_l69 = t96;
                                        } else {
                                            uint64_t t97 = gm_l7;
                                            uint64_t r98; bool o99 = __builtin_sub_overflow(t97, ((uint64_t)1ULL), &r98);
                                            if (o99) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 167, 69);
                                            uint64_t t100 = r98;
                                            uint64_t t101 = gm_l10;
                                            uint64_t r102; bool o103 = __builtin_sub_overflow(t100, t101, &r102);
                                            if (o103) gm_trap("integer overflow in `-`", gm_srcuro7l7pd15c2, 167, 69);
                                            uint64_t t104 = r102;
                                            gm_l69 = t104;
                                        }
                                        uint64_t t105 = gm_l69;
                                        gm_l11 = t105;
                                        void * t106 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint64_t t108 = gm_l4;
                                        uint64_t t109 = gm_l11;
                                        uint64_t r110; bool o111 = __builtin_add_overflow(t108, t109, &r110);
                                        if (o111) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 168, 22);
                                        uint64_t t112 = r110;
                                        uint64_t t113 = (uint64_t)t112;
                                        if ((uintptr_t)t113 >= (uintptr_t)(t107)) gm_trap_index((uintptr_t)t113, t107, gm_srcuro7l7pd15c2, 168, 17);
                                        unsigned __int128 t114 = gm_l6;
                                        uint64_t t115 = gm_l10;
                                        uint64_t r116; bool o117 = __builtin_mul_overflow(((uint64_t)8ULL), t115, &r116);
                                        if (o117) gm_trap("integer overflow in `*`", gm_srcuro7l7pd15c2, 168, 62);
                                        uint64_t t118 = r116;
                                        unsigned __int128 t119 = (unsigned __int128)t118;
                                        unsigned __int128 n120 = (unsigned __int128)t119;
                                        if (n120 >= 128) gm_trap_shift((uint64_t)n120, "u128", gm_srcuro7l7pd15c2, 168, 49);
                                        unsigned __int128 t121 = (unsigned __int128)(t114 >> (int)n120);
                                        uint8_t t122 = (uint8_t)t121;
                                        GM_ST(uint8_t, (((uint8_t *)t106) + (uintptr_t)t113 * 1), t122);
                                        goto gm_lb6;
                                    }
gm_lb7:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 166, 13);
                                }
gm_lb6:;
                            }
gm_lb5:;
                            uint64_t t123 = gm_l9;
                            uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                            uint64_t t126 = r124;
                            gm_l9 = t126;
                        }
                    }
gm_lb3:;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t128 = (uint8_t)(t127 == ((uint8_t)1ULL));
                    if (!t128) {
                        goto gm_lb8;
                    }
                    double t129 = GM_LD(double, (((uint8_t *)&F.l1) + 16));
                    gm_l12 = t129;
                    void * t130 = gm_l0;
                    void * t131 = (void *)(((uint8_t *)t130) + 8);
                    gm_t88rcucg1b7uk t132 = gm_l2;
                    gm_f_kind_ghvav22((void *)&gm_l70, t131, GM_CONV(gm_csguankko4lmhd, t132));
                    {
                        {
                            uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l70) + 0));
                            uint8_t t134 = (uint8_t)(t133 == ((uint8_t)3ULL));
                            if (!t134) {
                                goto gm_lb10;
                            }
                            uint16_t t135 = GM_LD(uint16_t, (((uint8_t *)&gm_l70) + 8));
                            uint8_t t136 = ((t135 == ((uint16_t)64ULL)));
                            if (!t136) {
                                goto gm_lb10;
                            }
                            void * t137 = gm_l0;
                            gm_b16_8 t138 = gm_l3;
                            uint64_t t139 = gm_l4;
                            double t140 = gm_l12;
                            uint64_t t141;
                            memset(&t141, 0, sizeof t141); memcpy(&t141, ((uint8_t *)&t140), sizeof t141 < 8 ? sizeof t141 : 8);
                            unsigned __int128 t142 = (unsigned __int128)t141;
                            gm_f_store_int_mfkaqk5(t137, t138, t139, t142, ((uint64_t)8ULL));
                            goto gm_lb9;
                        }
gm_lb10:;
                        {
                            uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l70) + 0));
                            uint8_t t144 = (uint8_t)(t143 == ((uint8_t)3ULL));
                            if (!t144) {
                                goto gm_lb11;
                            }
                            uint16_t t145 = GM_LD(uint16_t, (((uint8_t *)&gm_l70) + 8));
                            uint8_t t146 = ((t145 == ((uint16_t)32ULL)));
                            if (!t146) {
                                goto gm_lb11;
                            }
                            void * t147 = gm_l0;
                            gm_b16_8 t148 = gm_l3;
                            uint64_t t149 = gm_l4;
                            double t150 = gm_l12;
                            float t151 = (float)((double)t150);
                            uint32_t t152;
                            memset(&t152, 0, sizeof t152); memcpy(&t152, ((uint8_t *)&t151), sizeof t152 < 4 ? sizeof t152 : 4);
                            unsigned __int128 t153 = (unsigned __int128)t152;
                            gm_f_store_int_mfkaqk5(t147, t148, t149, t153, ((uint64_t)4ULL));
                            goto gm_lb9;
                        }
gm_lb11:;
                        {
                            gm_tl874t2atnlh3 t154 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                            gm_b16_8 t155;
                            GM_ST(void *, (uint8_t *)&t155, (void *)(gm_skff6pclobvnu));
                            GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(39));
                            gm_t08olq97rscn3 t156;
                            gm_f_unsupported_531q51r((void *)&t156, (void *)NULL, cx, NULL, t154, t155);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tqgtd5gtgj65s t157;
                            GM_ST(uint8_t, (uint8_t *)&t157 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t157 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t157 + 4, 0);
                            ((uint8_t *)&t157)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t157 + 8), t156);
                            memmove(gm_out, ((uint8_t *)&t157), 40);
                            goto gm_fdone;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 172, 13);
                    }
gm_lb9:;
                    goto gm_lb1;
                }
gm_lb8:;
            }
            {
                {
                    uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t159 = (uint8_t)(t158 == ((uint8_t)2ULL));
                    if (!t159) {
                        goto gm_lb13;
                    }
                    uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
                    gm_l13 = t160;
                    void * t161 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t t163 = gm_l4;
                    uint64_t t164 = (uint64_t)t163;
                    if ((uintptr_t)t164 >= (uintptr_t)(t162)) gm_trap_index((uintptr_t)t164, t162, gm_srcuro7l7pd15c2, 178, 23);
                    uint8_t t165 = gm_l13;
                    if (t165) {
                        gm_l71 = ((uint8_t)1ULL);
                    } else {
                        gm_l71 = ((uint8_t)0ULL);
                    }
                    uint8_t t166 = gm_l71;
                    GM_ST(uint8_t, (((uint8_t *)t161) + (uintptr_t)t164 * 1), t166);
                    goto gm_lb1;
                }
gm_lb13:;
                {
                    uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t168 = (uint8_t)(t167 == ((uint8_t)6ULL));
                    if (!t168) {
                        goto gm_lb14;
                    }
                    goto gm_lb1;
                }
gm_lb14:;
                {
                    uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t170 = (uint8_t)(t169 == ((uint8_t)7ULL));
                    if (!t170) {
                        goto gm_lb15;
                    }
                    void * t171 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                    gm_l14 = t171;
                    {
                        {
                            void * t172 = gm_l0;
                            void * t173 = (void *)(((uint8_t *)t172) + 8);
                            gm_t88rcucg1b7uk t174 = gm_l2;
                            gm_f_kind_ghvav22((void *)&gm_l72, t173, GM_CONV(gm_csguankko4lmhd, t174));
                            uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l72) + 0));
                            uint8_t t176 = (uint8_t)(t175 == ((uint8_t)13ULL));
                            if (!t176) {
                                goto gm_lb17;
                            }
                            gm_t88rcucg1b7uk t177 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l72) + 8));
                            gm_l15 = t177;
                            void * t178 = gm_l0;
                            void * t179 = gm_l14;
                            gm_t88rcucg1b7uk t180 = gm_l15;
                            gm_b16_8 t181 = gm_l3;
                            uint64_t t182 = gm_l4;
                            void * t183 = gm_l5;
                            gm_t6evimt84lndq t184 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t179));
                            gm_f_value_bytes_qm8egmj((void *)&gm_l73, (void *)NULL, cx, NULL, t178, &t184, t180, t181, t182, t183);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t185 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                            if (t185) {
                            } else {
                                gm_t08olq97rscn3 t186 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l73) + 8));
                                gm_tqgtd5gtgj65s t187;
                                GM_ST(uint8_t, (uint8_t *)&t187 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t187 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t187 + 4, 0);
                                ((uint8_t *)&t187)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t187 + 8), t186);
                                memmove(gm_out, ((uint8_t *)&t187), 40);
                                goto gm_fdone;
                            }
                            void * t188 = gm_l0;
                            void * t189 = GM_LD(void *, ((uint8_t *)t188));
                            void * t190 = GM_LD(void *, ((uint8_t *)t189));
                            gm_t88rcucg1b7uk t191 = gm_l15;
                            uint8_t t192 = gm_f_nullable_pointer_7qfmn6o(t190, GM_CONV(gm_csguankko4lmhd, t191));
                            uint8_t t193 = (uint8_t)!t192;
                            if (t193) {
                                void * t194 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                uint64_t t195 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint64_t t196 = gm_l4;
                                void * t197 = gm_l0;
                                void * t198 = (void *)(((uint8_t *)t197) + 8);
                                gm_t88rcucg1b7uk t199 = gm_l2;
                                gm_b16_8 t200 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t198, t199);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l74 = t200;
                                void * t201 = GM_LD(void *, ((uint8_t *)&gm_l74));
                                uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)&gm_l74) + 8));
                                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t202)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t202, gm_srcuro7l7pd15c2, 183, 67);
                                uint64_t t203 = GM_LD(uint64_t, (((uint8_t *)t201) + (uintptr_t)((uint64_t)1ULL) * 8));
                                uint64_t r204; bool o205 = __builtin_add_overflow(t196, t203, &r204);
                                if (o205) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 183, 61);
                                uint64_t t206 = r204;
                                uint64_t t207 = (uint64_t)t206;
                                if ((uintptr_t)t207 >= (uintptr_t)(t195)) gm_trap_index((uintptr_t)t207, t195, gm_srcuro7l7pd15c2, 183, 56);
                                GM_ST(uint8_t, (((uint8_t *)t194) + (uintptr_t)t207 * 1), ((uint8_t)1ULL));
                            }
                            goto gm_lb16;
                        }
gm_lb17:;
                        gm_tl874t2atnlh3 t208 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                        gm_b16_8 t209;
                        GM_ST(void *, (uint8_t *)&t209, (void *)(gm_sg1uf2uigo01q));
                        GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(26));
                        gm_t08olq97rscn3 t210;
                        gm_f_unsupported_531q51r((void *)&t210, (void *)NULL, cx, NULL, t208, t209);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tqgtd5gtgj65s t211;
                        GM_ST(uint8_t, (uint8_t *)&t211 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t211 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t211 + 4, 0);
                        ((uint8_t *)&t211)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t211 + 8), t210);
                        memmove(gm_out, ((uint8_t *)&t211), 40);
                        goto gm_fdone;
                    }
gm_lb16:;
                    goto gm_lb1;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t213 = (uint8_t)(t212 == ((uint8_t)8ULL));
                    if (!t213) {
                        goto gm_lb18;
                    }
                    void * t214 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                    gm_l16 = t214;
                    {
                        {
                            void * t215 = gm_l0;
                            void * t216 = (void *)(((uint8_t *)t215) + 8);
                            gm_t88rcucg1b7uk t217 = gm_l2;
                            gm_f_kind_ghvav22((void *)&gm_l75, t216, GM_CONV(gm_csguankko4lmhd, t217));
                            uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&gm_l75) + 0));
                            uint8_t t219 = (uint8_t)(t218 == ((uint8_t)14ULL));
                            if (!t219) {
                                goto gm_lb20;
                            }
                            gm_t88rcucg1b7uk t220 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l75) + 8) + 4));
                            gm_l17 = t220;
                            void * t221 = gm_l0;
                            void * t222 = (void *)(((uint8_t *)t221) + 8);
                            gm_t88rcucg1b7uk t223 = gm_l2;
                            gm_b16_8 t224 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t222, t223);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l18 = t224;
                            void * t225 = gm_l0;
                            void * t226 = gm_l16;
                            gm_t88rcucg1b7uk t227 = gm_l17;
                            gm_b16_8 t228 = gm_l3;
                            uint64_t t229 = gm_l4;
                            void * t230 = GM_LD(void *, ((uint8_t *)&gm_l18));
                            uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t231)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t231, gm_srcuro7l7pd15c2, 188, 50);
                            uint64_t t232 = GM_LD(uint64_t, (((uint8_t *)t230) + (uintptr_t)((uint64_t)1ULL) * 8));
                            uint64_t r233; bool o234 = __builtin_add_overflow(t229, t232, &r233);
                            if (o234) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 188, 44);
                            uint64_t t235 = r233;
                            void * t236 = gm_l5;
                            gm_t6evimt84lndq t237 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t226));
                            gm_f_value_bytes_qm8egmj((void *)&gm_l76, (void *)NULL, cx, NULL, t225, &t237, t227, t228, t235, t236);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t238 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                            if (t238) {
                            } else {
                                gm_t08olq97rscn3 t239 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l76) + 8));
                                gm_tqgtd5gtgj65s t240;
                                GM_ST(uint8_t, (uint8_t *)&t240 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t240 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t240 + 4, 0);
                                ((uint8_t *)&t240)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t240 + 8), t239);
                                memmove(gm_out, ((uint8_t *)&t240), 40);
                                goto gm_fdone;
                            }
                            goto gm_lb19;
                        }
gm_lb20:;
                        gm_tl874t2atnlh3 t241 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                        gm_b16_8 t242;
                        GM_ST(void *, (uint8_t *)&t242, (void *)(gm_sb8jvk2vbau9p));
                        GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(24));
                        gm_t08olq97rscn3 t243;
                        gm_f_unsupported_531q51r((void *)&t243, (void *)NULL, cx, NULL, t241, t242);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tqgtd5gtgj65s t244;
                        GM_ST(uint8_t, (uint8_t *)&t244 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t244 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t244 + 4, 0);
                        ((uint8_t *)&t244)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t244 + 8), t243);
                        memmove(gm_out, ((uint8_t *)&t244), 40);
                        goto gm_fdone;
                    }
gm_lb19:;
                    goto gm_lb1;
                }
gm_lb18:;
                {
                    uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t246 = (uint8_t)(t245 == ((uint8_t)9ULL));
                    if (!t246) {
                        goto gm_lb21;
                    }
                    void * t247 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                    gm_l19 = t247;
                    {
                        {
                            void * t248 = gm_l0;
                            void * t249 = (void *)(((uint8_t *)t248) + 8);
                            gm_t88rcucg1b7uk t250 = gm_l2;
                            gm_f_kind_ghvav22((void *)&gm_l77, t249, GM_CONV(gm_csguankko4lmhd, t250));
                            uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&gm_l77) + 0));
                            uint8_t t252 = (uint8_t)(t251 == ((uint8_t)14ULL));
                            if (!t252) {
                                goto gm_lb23;
                            }
                            gm_t88rcucg1b7uk t253 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l77) + 8));
                            gm_l20 = t253;
                            void * t254 = gm_l0;
                            void * t255 = (void *)(((uint8_t *)t254) + 8);
                            gm_t88rcucg1b7uk t256 = gm_l2;
                            gm_b16_8 t257 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t255, t256);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l21 = t257;
                            void * t258 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t259 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t260 = gm_l4;
                            void * t261 = GM_LD(void *, ((uint8_t *)&gm_l21));
                            uint64_t t262 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t262)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t262, gm_srcuro7l7pd15c2, 193, 24);
                            uint64_t t263 = GM_LD(uint64_t, (((uint8_t *)t261) + (uintptr_t)((uint64_t)0ULL) * 8));
                            uint64_t r264; bool o265 = __builtin_add_overflow(t260, t263, &r264);
                            if (o265) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 193, 18);
                            uint64_t t266 = r264;
                            uint64_t t267 = (uint64_t)t266;
                            if ((uintptr_t)t267 >= (uintptr_t)(t259)) gm_trap_index((uintptr_t)t267, t259, gm_srcuro7l7pd15c2, 193, 13);
                            GM_ST(uint8_t, (((uint8_t *)t258) + (uintptr_t)t267 * 1), ((uint8_t)1ULL));
                            void * t268 = gm_l0;
                            void * t269 = gm_l19;
                            gm_t88rcucg1b7uk t270 = gm_l20;
                            gm_b16_8 t271 = gm_l3;
                            uint64_t t272 = gm_l4;
                            void * t273 = GM_LD(void *, ((uint8_t *)&gm_l21));
                            uint64_t t274 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t274)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t274, gm_srcuro7l7pd15c2, 194, 51);
                            uint64_t t275 = GM_LD(uint64_t, (((uint8_t *)t273) + (uintptr_t)((uint64_t)1ULL) * 8));
                            uint64_t r276; bool o277 = __builtin_add_overflow(t272, t275, &r276);
                            if (o277) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 194, 45);
                            uint64_t t278 = r276;
                            void * t279 = gm_l5;
                            gm_t6evimt84lndq t280 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t269));
                            gm_f_value_bytes_qm8egmj((void *)&gm_l78, (void *)NULL, cx, NULL, t268, &t280, t270, t271, t278, t279);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t281 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                            if (t281) {
                            } else {
                                gm_t08olq97rscn3 t282 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l78) + 8));
                                gm_tqgtd5gtgj65s t283;
                                GM_ST(uint8_t, (uint8_t *)&t283 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t283 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t283 + 4, 0);
                                ((uint8_t *)&t283)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t283 + 8), t282);
                                memmove(gm_out, ((uint8_t *)&t283), 40);
                                goto gm_fdone;
                            }
                            goto gm_lb22;
                        }
gm_lb23:;
                        gm_tl874t2atnlh3 t284 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                        gm_b16_8 t285;
                        GM_ST(void *, (uint8_t *)&t285, (void *)(gm_sb8jvk2vbau9p));
                        GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(24));
                        gm_t08olq97rscn3 t286;
                        gm_f_unsupported_531q51r((void *)&t286, (void *)NULL, cx, NULL, t284, t285);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tqgtd5gtgj65s t287;
                        GM_ST(uint8_t, (uint8_t *)&t287 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t287 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t287 + 4, 0);
                        ((uint8_t *)&t287)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t287 + 8), t286);
                        memmove(gm_out, ((uint8_t *)&t287), 40);
                        goto gm_fdone;
                    }
gm_lb22:;
                    goto gm_lb1;
                }
gm_lb21:;
            }
            {
                {
                    uint8_t t288 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t289 = (uint8_t)(t288 == ((uint8_t)10ULL));
                    if (!t289) {
                        goto gm_lb24;
                    }
                    uint32_t t290 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
                    gm_l22 = t290;
                    void * t291 = GM_LD(void *, ((((uint8_t *)&F.l1) + 16) + 8));
                    gm_l23 = t291;
                    {
                        {
                            void * t292 = gm_l0;
                            void * t293 = (void *)(((uint8_t *)t292) + 8);
                            gm_t88rcucg1b7uk t294 = gm_l2;
                            gm_f_kind_ghvav22((void *)&gm_l79, t293, GM_CONV(gm_csguankko4lmhd, t294));
                            uint8_t t295 = GM_LD(uint8_t, (((uint8_t *)&gm_l79) + 0));
                            uint8_t t296 = (uint8_t)(t295 == ((uint8_t)17ULL));
                            if (!t296) {
                                goto gm_lb26;
                            }
                            uint32_t t297 = GM_LD(uint32_t, (((uint8_t *)&gm_l79) + 8));
                            gm_l24 = t297;
                            {
                                {
                                    void * t298 = gm_l0;
                                    void * t299 = GM_LD(void *, ((uint8_t *)t298));
                                    uint32_t t300 = gm_l24;
                                    void * t301 = gm_f_adt_mpqv8qe(t299, t300);
                                    uint8_t t302 = GM_LD(uint8_t, ((((uint8_t *)t301) + 24) + 0));
                                    uint8_t t303 = (uint8_t)(t302 == ((uint8_t)1ULL));
                                    if (!t303) {
                                        goto gm_lb28;
                                    }
                                    gm_t88rcucg1b7uk t304 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t301) + 24) + 8));
                                    gm_l25 = t304;
                                    gm_b16_8 t305 = GM_LD(gm_b16_8, (((((uint8_t *)t301) + 24) + 8) + 8));
                                    gm_l26 = t305;
                                    void * t306 = GM_LD(void *, ((uint8_t *)&gm_l26));
                                    uint64_t t307 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                    uint32_t t308 = gm_l22;
                                    uint64_t t309 = (uint64_t)t308;
                                    if ((uintptr_t)t309 >= (uintptr_t)(t307)) gm_trap_index((uintptr_t)t309, t307, gm_srcuro7l7pd15c2, 199, 19);
                                    memmove(((uint8_t *)&F.l27), (((uint8_t *)t306) + (uintptr_t)t309 * 72), 72);
                                    void * t310 = gm_l0;
                                    void * t311 = (void *)(((uint8_t *)t310) + 8);
                                    gm_t88rcucg1b7uk t312 = gm_l2;
                                    gm_b16_8 t313 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t311, t312);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l28 = t313;
                                    gm_tbci5ospk5nnn t314 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l27) + 24));
                                    gm_f_to_i128_o7ud1ue((void *)&gm_l80, &t314);
                                    uint8_t t315 = (uint8_t)((((uint8_t *)&gm_l80))[16] != 0);
                                    if (t315) {
                                        __int128 t316 = GM_LD(__int128, ((uint8_t *)&gm_l80));
                                        gm_l81 = t316;
                                    } else {
                                        gm_l81 = ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                                    }
                                    __int128 t317 = gm_l81;
                                    gm_l29 = t317;
                                    void * t318 = gm_l0;
                                    __int128 t319 = gm_l29;
                                    unsigned __int128 t320 = (unsigned __int128)t319;
                                    gm_t88rcucg1b7uk t321 = gm_l25;
                                    gm_b16_8 t322 = gm_l3;
                                    uint64_t t323 = gm_l4;
                                    void * t324 = GM_LD(void *, ((uint8_t *)&gm_l28));
                                    uint64_t t325 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t325)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t325, gm_srcuro7l7pd15c2, 202, 79);
                                    uint64_t t326 = GM_LD(uint64_t, (((uint8_t *)t324) + (uintptr_t)((uint64_t)0ULL) * 8));
                                    uint64_t r327; bool o328 = __builtin_add_overflow(t323, t326, &r327);
                                    if (o328) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 202, 73);
                                    uint64_t t329 = r327;
                                    void * t330 = gm_l5;
                                    gm_t6evimt84lndq t331;
                                    GM_ST(uint8_t, (uint8_t *)&t331 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t331 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t331 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t331 + 8, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t331 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t331 + 40, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t331 + 0), ((uint8_t)0ULL));
                                    GM_ST(unsigned __int128, ((uint8_t *)&t331 + 16), t320);
                                    gm_f_value_bytes_qm8egmj((void *)&gm_l82, (void *)NULL, cx, NULL, t318, &t331, t321, t322, t329, t330);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t332 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
                                    if (t332) {
                                    } else {
                                        gm_t08olq97rscn3 t333 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l82) + 8));
                                        gm_tqgtd5gtgj65s t334;
                                        GM_ST(uint8_t, (uint8_t *)&t334 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t334 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t334 + 4, 0);
                                        ((uint8_t *)&t334)[0] = 1;
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t334 + 8), t333);
                                        memmove(gm_out, ((uint8_t *)&t334), 40);
                                        goto gm_fdone;
                                    }
                                    {
                                        {
                                            uint8_t t335 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l23)) != 0);
                                            if (!t335) {
                                                goto gm_lb30;
                                            }
                                            void * t336 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                            gm_l30 = t336;
                                            uint8_t t337 = (uint8_t)(((((uint8_t *)&F.l27) + 16))[4] != 0);
                                            if (t337) {
                                                gm_t88rcucg1b7uk t338 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                                                gm_l83 = t338;
                                            } else {
                                                gm_tl874t2atnlh3 t339 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                                                gm_b16_8 t340;
                                                GM_ST(void *, (uint8_t *)&t340, (void *)(gm_sa6mes8ijqmmk));
                                                GM_ST(uintptr_t, (uint8_t *)&t340 + 8, (uintptr_t)(29));
                                                gm_t08olq97rscn3 t341;
                                                gm_f_unsupported_531q51r((void *)&t341, (void *)NULL, cx, NULL, t339, t340);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_tqgtd5gtgj65s t342;
                                                GM_ST(uint8_t, (uint8_t *)&t342 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t342 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t342 + 4, 0);
                                                ((uint8_t *)&t342)[0] = 1;
                                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t342 + 8), t341);
                                                memmove(gm_out, ((uint8_t *)&t342), 40);
                                                goto gm_fdone;
                                            }
                                            gm_t88rcucg1b7uk t343 = gm_l83;
                                            gm_l31 = t343;
                                            void * t344 = gm_l0;
                                            void * t345 = gm_l30;
                                            gm_t88rcucg1b7uk t346 = gm_l31;
                                            gm_b16_8 t347 = gm_l3;
                                            uint64_t t348 = gm_l4;
                                            void * t349 = GM_LD(void *, ((uint8_t *)&gm_l28));
                                            uint64_t t350 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t350)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t350, gm_srcuro7l7pd15c2, 205, 54);
                                            uint64_t t351 = GM_LD(uint64_t, (((uint8_t *)t349) + (uintptr_t)((uint64_t)1ULL) * 8));
                                            uint64_t r352; bool o353 = __builtin_add_overflow(t348, t351, &r352);
                                            if (o353) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 205, 48);
                                            uint64_t t354 = r352;
                                            void * t355 = gm_l5;
                                            gm_t6evimt84lndq t356 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t345));
                                            gm_f_value_bytes_qm8egmj((void *)&gm_l84, (void *)NULL, cx, NULL, t344, &t356, t346, t347, t354, t355);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t357 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
                                            if (t357) {
                                            } else {
                                                gm_t08olq97rscn3 t358 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l84) + 8));
                                                gm_tqgtd5gtgj65s t359;
                                                GM_ST(uint8_t, (uint8_t *)&t359 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t359 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t359 + 4, 0);
                                                ((uint8_t *)&t359)[0] = 1;
                                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t359 + 8), t358);
                                                memmove(gm_out, ((uint8_t *)&t359), 40);
                                                goto gm_fdone;
                                            }
                                            goto gm_lb29;
                                        }
gm_lb30:;
                                    }
gm_lb29:;
                                    goto gm_lb27;
                                }
gm_lb28:;
                                gm_tl874t2atnlh3 t360 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                                gm_b16_8 t361;
                                GM_ST(void *, (uint8_t *)&t361, (void *)(gm_sa6mes8ijqmmk));
                                GM_ST(uintptr_t, (uint8_t *)&t361 + 8, (uintptr_t)(29));
                                gm_t08olq97rscn3 t362;
                                gm_f_unsupported_531q51r((void *)&t362, (void *)NULL, cx, NULL, t360, t361);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tqgtd5gtgj65s t363;
                                GM_ST(uint8_t, (uint8_t *)&t363 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t363 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t363 + 4, 0);
                                ((uint8_t *)&t363)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t363 + 8), t362);
                                memmove(gm_out, ((uint8_t *)&t363), 40);
                                goto gm_fdone;
                            }
gm_lb27:;
                            goto gm_lb25;
                        }
gm_lb26:;
                        gm_tl874t2atnlh3 t364 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                        gm_b16_8 t365;
                        GM_ST(void *, (uint8_t *)&t365, (void *)(gm_sa6mes8ijqmmk));
                        GM_ST(uintptr_t, (uint8_t *)&t365 + 8, (uintptr_t)(29));
                        gm_t08olq97rscn3 t366;
                        gm_f_unsupported_531q51r((void *)&t366, (void *)NULL, cx, NULL, t364, t365);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tqgtd5gtgj65s t367;
                        GM_ST(uint8_t, (uint8_t *)&t367 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t367 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t367 + 4, 0);
                        ((uint8_t *)&t367)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t367 + 8), t366);
                        memmove(gm_out, ((uint8_t *)&t367), 40);
                        goto gm_fdone;
                    }
gm_lb25:;
                    goto gm_lb1;
                }
gm_lb24:;
            }
            {
                {
                    uint8_t t368 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t369 = (uint8_t)(t368 == ((uint8_t)3ULL));
                    if (!t369) {
                        goto gm_lb31;
                    }
                    void * t370 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
                    gm_l32 = t370;
                    void * t371 = gm_l0;
                    void * t372 = (void *)(((uint8_t *)t371) + 8);
                    gm_t88rcucg1b7uk t373 = gm_l2;
                    gm_f_kind_ghvav22((void *)&gm_l85, t372, GM_CONV(gm_csguankko4lmhd, t373));
                    {
                        {
                            uint8_t t374 = GM_LD(uint8_t, (((uint8_t *)&gm_l85) + 0));
                            uint8_t t375 = (uint8_t)(t374 == ((uint8_t)11ULL));
                            if (!t375) {
                                goto gm_lb33;
                            }
                            gm_t88rcucg1b7uk t376 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l85) + 8) + 8));
                            gm_l33 = t376;
                            void * t377 = gm_l0;
                            void * t378 = (void *)(((uint8_t *)t377) + 8);
                            gm_t88rcucg1b7uk t379 = gm_l33;
                            uint64_t t380 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t378, t379);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l34 = t380;
                            void * t381 = gm_l32;
                            gm_b16_8 t382 = GM_LD(gm_b16_8, ((uint8_t *)t381));
                            gm_b16_8 t383 = GM_LD(gm_b16_8, ((uint8_t *)&t382));
                            gm_l35 = t383;
                            gm_l36 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t384 = gm_l36;
                                    uint64_t t385 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                    uint8_t t386 = (t384 < t385);
                                    if (!t386) {
                                        goto gm_lb34;
                                    }
                                    {
                                        uint64_t t387 = gm_l36;
                                        void * t388 = GM_LD(void *, ((uint8_t *)&gm_l35));
                                        uint64_t t389 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                        uint64_t t390 = gm_l36;
                                        gm_t6evimt84lndq t391 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t388) + (uintptr_t)t390 * 48));
                                        gm_thf8as07kj3uj t392;
                                        GM_ST(uint64_t, (uint8_t *)&t392 + 8, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t392 + 0), t387);
                                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t392 + 16), t391);
                                        gm_l86 = t392;
                                        {
                                            {
                                                uint64_t t393 = GM_LD(uint64_t, ((uint8_t *)&gm_l86));
                                                gm_l37 = t393;
                                                memmove(((uint8_t *)&F.l38), (((uint8_t *)&gm_l86) + 16), 48);
                                                void * t394 = gm_l0;
                                                gm_t88rcucg1b7uk t395 = gm_l33;
                                                gm_b16_8 t396 = gm_l3;
                                                uint64_t t397 = gm_l4;
                                                uint64_t t398 = gm_l37;
                                                gm_t6evimt84lndq t399 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l38));
                                                uint64_t t400 = (uint64_t)t398;
                                                uint64_t t401 = gm_l34;
                                                uint64_t r402; bool o403 = __builtin_mul_overflow(t400, t401, &r402);
                                                if (o403) gm_trap("integer overflow in `*`", gm_srcuro7l7pd15c2, 212, 92);
                                                uint64_t t404 = r402;
                                                uint64_t r405; bool o406 = __builtin_add_overflow(t397, t404, &r405);
                                                if (o406) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 212, 86);
                                                uint64_t t407 = r405;
                                                void * t408 = gm_l5;
                                                gm_f_value_bytes_qm8egmj((void *)&gm_l87, (void *)NULL, cx, NULL, t394, &t399, t395, t396, t407, t408);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t409 = (uint8_t)!((((uint8_t *)&gm_l87))[0] != 0);
                                                if (t409) {
                                                } else {
                                                    gm_t08olq97rscn3 t410 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l87) + 8));
                                                    gm_tqgtd5gtgj65s t411;
                                                    GM_ST(uint8_t, (uint8_t *)&t411 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t411 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t411 + 4, 0);
                                                    ((uint8_t *)&t411)[0] = 1;
                                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t411 + 8), t410);
                                                    memmove(gm_out, ((uint8_t *)&t411), 40);
                                                    goto gm_fdone;
                                                }
                                                goto gm_lb37;
                                            }
gm_lb38:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 212, 21);
                                        }
gm_lb37:;
                                    }
gm_lb36:;
                                    uint64_t t412 = gm_l36;
                                    uint64_t r413; bool o414 = __builtin_add_overflow(t412, ((uint64_t)1ULL), &r413);
                                    uint64_t t415 = r413;
                                    gm_l36 = t415;
                                }
                            }
gm_lb34:;
                            goto gm_lb32;
                        }
gm_lb33:;
                        {
                            uint8_t t416 = GM_LD(uint8_t, (((uint8_t *)&gm_l85) + 0));
                            uint8_t t417 = (uint8_t)(t416 == ((uint8_t)12ULL));
                            if (!t417) {
                                goto gm_lb39;
                            }
                            gm_b16_8 t418 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l85) + 8));
                            gm_l39 = t418;
                            void * t419 = gm_l0;
                            void * t420 = (void *)(((uint8_t *)t419) + 8);
                            gm_t88rcucg1b7uk t421 = gm_l2;
                            gm_b16_8 t422 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t420, t421);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l40 = t422;
                            void * t423 = gm_l32;
                            gm_b16_8 t424 = GM_LD(gm_b16_8, ((uint8_t *)t423));
                            gm_b16_8 t425 = GM_LD(gm_b16_8, ((uint8_t *)&t424));
                            gm_l41 = t425;
                            gm_l42 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t426 = gm_l42;
                                    uint64_t t427 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                                    uint8_t t428 = (t426 < t427);
                                    if (!t428) {
                                        goto gm_lb40;
                                    }
                                    {
                                        uint64_t t429 = gm_l42;
                                        void * t430 = GM_LD(void *, ((uint8_t *)&gm_l41));
                                        uint64_t t431 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                                        uint64_t t432 = gm_l42;
                                        gm_t6evimt84lndq t433 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t430) + (uintptr_t)t432 * 48));
                                        gm_thf8as07kj3uj t434;
                                        GM_ST(uint64_t, (uint8_t *)&t434 + 8, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t434 + 0), t429);
                                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t434 + 16), t433);
                                        gm_l88 = t434;
                                        {
                                            {
                                                uint64_t t435 = GM_LD(uint64_t, ((uint8_t *)&gm_l88));
                                                gm_l43 = t435;
                                                memmove(((uint8_t *)&F.l44), (((uint8_t *)&gm_l88) + 16), 48);
                                                void * t436 = gm_l0;
                                                void * t437 = GM_LD(void *, ((uint8_t *)&gm_l39));
                                                uint64_t t438 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                                                uint64_t t439 = gm_l43;
                                                if ((uintptr_t)t439 >= (uintptr_t)(t438)) gm_trap_index((uintptr_t)t439, t438, gm_srcuro7l7pd15c2, 216, 75);
                                                gm_t88rcucg1b7uk t440 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t437) + (uintptr_t)t439 * 4));
                                                gm_b16_8 t441 = gm_l3;
                                                uint64_t t442 = gm_l4;
                                                void * t443 = GM_LD(void *, ((uint8_t *)&gm_l40));
                                                uint64_t t444 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                                uint64_t t445 = gm_l43;
                                                if ((uintptr_t)t445 >= (uintptr_t)(t444)) gm_trap_index((uintptr_t)t445, t444, gm_srcuro7l7pd15c2, 216, 96);
                                                uint64_t t446 = GM_LD(uint64_t, (((uint8_t *)t443) + (uintptr_t)t445 * 8));
                                                uint64_t r447; bool o448 = __builtin_add_overflow(t442, t446, &r447);
                                                if (o448) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 216, 90);
                                                uint64_t t449 = r447;
                                                void * t450 = gm_l5;
                                                gm_t6evimt84lndq t451 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l44));
                                                gm_f_value_bytes_qm8egmj((void *)&gm_l89, (void *)NULL, cx, NULL, t436, &t451, t440, t441, t449, t450);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t452 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                                                if (t452) {
                                                } else {
                                                    gm_t08olq97rscn3 t453 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l89) + 8));
                                                    gm_tqgtd5gtgj65s t454;
                                                    GM_ST(uint8_t, (uint8_t *)&t454 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t454 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t454 + 4, 0);
                                                    ((uint8_t *)&t454)[0] = 1;
                                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t454 + 8), t453);
                                                    memmove(gm_out, ((uint8_t *)&t454), 40);
                                                    goto gm_fdone;
                                                }
                                                goto gm_lb43;
                                            }
gm_lb44:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 216, 21);
                                        }
gm_lb43:;
                                    }
gm_lb42:;
                                    uint64_t t455 = gm_l42;
                                    uint64_t r456; bool o457 = __builtin_add_overflow(t455, ((uint64_t)1ULL), &r456);
                                    uint64_t t458 = r456;
                                    gm_l42 = t458;
                                }
                            }
gm_lb40:;
                            goto gm_lb32;
                        }
gm_lb39:;
                        {
                            uint8_t t459 = GM_LD(uint8_t, (((uint8_t *)&gm_l85) + 0));
                            uint8_t t460 = (uint8_t)(t459 == ((uint8_t)17ULL));
                            if (!t460) {
                                goto gm_lb45;
                            }
                            uint32_t t461 = GM_LD(uint32_t, (((uint8_t *)&gm_l85) + 8));
                            gm_l45 = t461;
                            void * t462 = gm_l0;
                            void * t463 = GM_LD(void *, ((uint8_t *)t462));
                            uint32_t t464 = gm_l45;
                            void * t465 = gm_f_adt_mpqv8qe(t463, t464);
                            {
                                {
                                    uint8_t t466 = GM_LD(uint8_t, ((((uint8_t *)t465) + 24) + 0));
                                    uint8_t t467 = (uint8_t)(t466 == ((uint8_t)0ULL));
                                    if (!t467) {
                                        goto gm_lb47;
                                    }
                                    memmove(((uint8_t *)&F.l46), ((((uint8_t *)t465) + 24) + 8), 48);
                                    {
                                        {
                                            uint8_t t468 = (uint8_t)(((((uint8_t *)&F.l46) + 40))[4] != 0);
                                            if (!t468) {
                                                goto gm_lb49;
                                            }
                                            gm_t88rcucg1b7uk t469 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l46) + 40));
                                            gm_l47 = t469;
                                            void * t470 = gm_l0;
                                            gm_t88rcucg1b7uk t471 = gm_l2;
                                            gm_t6evimt84lndq t472 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l1));
                                            gm_f_bits_of_ilgn83m((void *)&gm_l90, (void *)NULL, cx, NULL, t470, &t472, t471);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t473 = (uint8_t)!((((uint8_t *)&gm_l90))[0] != 0);
                                            if (t473) {
                                            } else {
                                                gm_t08olq97rscn3 t474 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l90) + 16));
                                                gm_tqgtd5gtgj65s t475;
                                                GM_ST(uint8_t, (uint8_t *)&t475 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t475 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t475 + 4, 0);
                                                ((uint8_t *)&t475)[0] = 1;
                                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t475 + 8), t474);
                                                memmove(gm_out, ((uint8_t *)&t475), 40);
                                                goto gm_fdone;
                                            }
                                            unsigned __int128 t476 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l90) + 16));
                                            gm_l48 = t476;
                                            void * t477 = gm_l0;
                                            unsigned __int128 t478 = gm_l48;
                                            gm_t88rcucg1b7uk t479 = gm_l47;
                                            gm_b16_8 t480 = gm_l3;
                                            uint64_t t481 = gm_l4;
                                            void * t482 = gm_l5;
                                            gm_t6evimt84lndq t483;
                                            GM_ST(uint8_t, (uint8_t *)&t483 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t483 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t483 + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t483 + 8, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t483 + 32, 0);
                                            GM_ST(uint64_t, (uint8_t *)&t483 + 40, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t483 + 0), ((uint8_t)0ULL));
                                            GM_ST(unsigned __int128, ((uint8_t *)&t483 + 16), t478);
                                            gm_f_value_bytes_qm8egmj((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t477, &t483, t479, t480, t481, t482);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_fdone;
                                        }
gm_lb49:;
                                    }
gm_lb48:;
                                    void * t484 = gm_l0;
                                    void * t485 = (void *)(((uint8_t *)t484) + 8);
                                    gm_t88rcucg1b7uk t486 = gm_l2;
                                    gm_b16_8 t487 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t485, t486);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l49 = t487;
                                    void * t488 = gm_l32;
                                    gm_b16_8 t489 = GM_LD(gm_b16_8, ((uint8_t *)t488));
                                    gm_b16_8 t490 = GM_LD(gm_b16_8, ((uint8_t *)&t489));
                                    gm_l50 = t490;
                                    gm_l51 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t491 = gm_l51;
                                            uint64_t t492 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                                            uint8_t t493 = (t491 < t492);
                                            if (!t493) {
                                                goto gm_lb50;
                                            }
                                            {
                                                uint64_t t494 = gm_l51;
                                                void * t495 = GM_LD(void *, ((uint8_t *)&gm_l50));
                                                uint64_t t496 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                                                uint64_t t497 = gm_l51;
                                                gm_t6evimt84lndq t498 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t495) + (uintptr_t)t497 * 48));
                                                gm_thf8as07kj3uj t499;
                                                GM_ST(uint64_t, (uint8_t *)&t499 + 8, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t499 + 0), t494);
                                                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t499 + 16), t498);
                                                gm_l91 = t499;
                                                {
                                                    {
                                                        uint64_t t500 = GM_LD(uint64_t, ((uint8_t *)&gm_l91));
                                                        gm_l52 = t500;
                                                        memmove(((uint8_t *)&F.l53), (((uint8_t *)&gm_l91) + 16), 48);
                                                        void * t501 = gm_l0;
                                                        void * t502 = GM_LD(void *, ((uint8_t *)&F.l46));
                                                        uint64_t t503 = GM_LD(uint64_t, (((uint8_t *)&F.l46) + 8));
                                                        uint64_t t504 = gm_l52;
                                                        if ((uintptr_t)t504 >= (uintptr_t)(t503)) gm_trap_index((uintptr_t)t504, t503, gm_srcuro7l7pd15c2, 225, 79);
                                                        gm_t88rcucg1b7uk t505 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t502) + (uintptr_t)t504 * 40) + 16));
                                                        gm_b16_8 t506 = gm_l3;
                                                        uint64_t t507 = gm_l4;
                                                        void * t508 = GM_LD(void *, ((uint8_t *)&gm_l49));
                                                        uint64_t t509 = GM_LD(uint64_t, (((uint8_t *)&gm_l49) + 8));
                                                        uint64_t t510 = gm_l52;
                                                        if ((uintptr_t)t510 >= (uintptr_t)(t509)) gm_trap_index((uintptr_t)t510, t509, gm_srcuro7l7pd15c2, 225, 107);
                                                        uint64_t t511 = GM_LD(uint64_t, (((uint8_t *)t508) + (uintptr_t)t510 * 8));
                                                        uint64_t r512; bool o513 = __builtin_add_overflow(t507, t511, &r512);
                                                        if (o513) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 225, 101);
                                                        uint64_t t514 = r512;
                                                        void * t515 = gm_l5;
                                                        gm_t6evimt84lndq t516 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l53));
                                                        gm_f_value_bytes_qm8egmj((void *)&gm_l92, (void *)NULL, cx, NULL, t501, &t516, t505, t506, t514, t515);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t517 = (uint8_t)!((((uint8_t *)&gm_l92))[0] != 0);
                                                        if (t517) {
                                                        } else {
                                                            gm_t08olq97rscn3 t518 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l92) + 8));
                                                            gm_tqgtd5gtgj65s t519;
                                                            GM_ST(uint8_t, (uint8_t *)&t519 + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t519 + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t519 + 4, 0);
                                                            ((uint8_t *)&t519)[0] = 1;
                                                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t519 + 8), t518);
                                                            memmove(gm_out, ((uint8_t *)&t519), 40);
                                                            goto gm_fdone;
                                                        }
                                                        goto gm_lb53;
                                                    }
gm_lb54:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 225, 25);
                                                }
gm_lb53:;
                                            }
gm_lb52:;
                                            uint64_t t520 = gm_l51;
                                            uint64_t r521; bool o522 = __builtin_add_overflow(t520, ((uint64_t)1ULL), &r521);
                                            uint64_t t523 = r521;
                                            gm_l51 = t523;
                                        }
                                    }
gm_lb50:;
                                    goto gm_lb46;
                                }
gm_lb47:;
                                {
                                    gm_tl874t2atnlh3 t524 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                                    gm_b16_8 t525;
                                    GM_ST(void *, (uint8_t *)&t525, (void *)(gm_s67fd8s6hf84e));
                                    GM_ST(uintptr_t, (uint8_t *)&t525 + 8, (uintptr_t)(23));
                                    gm_t08olq97rscn3 t526;
                                    gm_f_unsupported_531q51r((void *)&t526, (void *)NULL, cx, NULL, t524, t525);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tqgtd5gtgj65s t527;
                                    GM_ST(uint8_t, (uint8_t *)&t527 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t527 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t527 + 4, 0);
                                    ((uint8_t *)&t527)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t527 + 8), t526);
                                    memmove(gm_out, ((uint8_t *)&t527), 40);
                                    goto gm_fdone;
                                }
gm_lb55:;
                                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 218, 32);
                            }
gm_lb46:;
                            goto gm_lb32;
                        }
gm_lb45:;
                        {
                            gm_tl874t2atnlh3 t528 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                            gm_b16_8 t529;
                            GM_ST(void *, (uint8_t *)&t529, (void *)(gm_s67fd8s6hf84e));
                            GM_ST(uintptr_t, (uint8_t *)&t529 + 8, (uintptr_t)(23));
                            gm_t08olq97rscn3 t530;
                            gm_f_unsupported_531q51r((void *)&t530, (void *)NULL, cx, NULL, t528, t529);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tqgtd5gtgj65s t531;
                            GM_ST(uint8_t, (uint8_t *)&t531 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t531 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t531 + 4, 0);
                            ((uint8_t *)&t531)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t531 + 8), t530);
                            memmove(gm_out, ((uint8_t *)&t531), 40);
                            goto gm_fdone;
                        }
gm_lb56:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 209, 13);
                    }
gm_lb32:;
                    goto gm_lb1;
                }
gm_lb31:;
            }
            {
                {
                    uint8_t t532 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t533 = (uint8_t)(t532 == ((uint8_t)4ULL));
                    if (!t533) {
                        goto gm_lb57;
                    }
                    memmove(((uint8_t *)&gm_l54), (((uint8_t *)&F.l1) + 16), 24);
                    void * t534 = gm_l0;
                    void * t535 = (void *)(((uint8_t *)t534) + 8);
                    gm_t88rcucg1b7uk t536 = gm_l2;
                    gm_f_kind_ghvav22((void *)&gm_l94, t535, GM_CONV(gm_csguankko4lmhd, t536));
                    {
                        {
                            uint8_t t537 = GM_LD(uint8_t, (((uint8_t *)&gm_l94) + 0));
                            uint8_t t538 = (uint8_t)(t537 == ((uint8_t)8ULL));
                            if (!t538) {
                                goto gm_lb59;
                            }
                            gm_t88rcucg1b7uk t539 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l94) + 8) + 4));
                            gm_l55 = t539;
                            gm_t88rcucg1b7uk t540 = gm_l55;
                            gm_l93 = t540;
                            goto gm_lb58;
                        }
gm_lb59:;
                        {
                            uint8_t t541 = GM_LD(uint8_t, (((uint8_t *)&gm_l94) + 0));
                            uint8_t t542 = (uint8_t)(t541 == ((uint8_t)9ULL));
                            if (!t542) {
                                goto gm_lb60;
                            }
                            gm_t88rcucg1b7uk t543 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l94) + 8) + 4));
                            gm_l56 = t543;
                            gm_t88rcucg1b7uk t544 = gm_l56;
                            gm_l93 = t544;
                            goto gm_lb58;
                        }
gm_lb60:;
                        {
                            uint8_t t545 = GM_LD(uint8_t, (((uint8_t *)&gm_l94) + 0));
                            uint8_t t546 = (uint8_t)(t545 == ((uint8_t)13ULL));
                            if (!t546) {
                                goto gm_lb61;
                            }
                            gm_t88rcucg1b7uk t547 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l94) + 8));
                            gm_l57 = t547;
                            void * t548 = gm_l0;
                            void * t549 = (void *)(((uint8_t *)t548) + 8);
                            gm_t88rcucg1b7uk t550 = gm_l57;
                            gm_f_kind_ghvav22((void *)&gm_l96, t549, GM_CONV(gm_csguankko4lmhd, t550));
                            {
                                {
                                    uint8_t t551 = GM_LD(uint8_t, (((uint8_t *)&gm_l96) + 0));
                                    uint8_t t552 = (uint8_t)(t551 == ((uint8_t)8ULL));
                                    if (!t552) {
                                        goto gm_lb63;
                                    }
                                    gm_t88rcucg1b7uk t553 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l96) + 8) + 4));
                                    gm_l58 = t553;
                                    gm_t88rcucg1b7uk t554 = gm_l58;
                                    gm_l95 = t554;
                                    goto gm_lb62;
                                }
gm_lb63:;
                                {
                                    uint8_t t555 = GM_LD(uint8_t, (((uint8_t *)&gm_l96) + 0));
                                    uint8_t t556 = (uint8_t)(t555 == ((uint8_t)9ULL));
                                    if (!t556) {
                                        goto gm_lb64;
                                    }
                                    gm_t88rcucg1b7uk t557 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l96) + 8) + 4));
                                    gm_l59 = t557;
                                    gm_t88rcucg1b7uk t558 = gm_l59;
                                    gm_l95 = t558;
                                    goto gm_lb62;
                                }
gm_lb64:;
                                {
                                    gm_tl874t2atnlh3 t559 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                                    gm_b16_8 t560;
                                    GM_ST(void *, (uint8_t *)&t560, (void *)(gm_sgiqpj781s1a9));
                                    GM_ST(uintptr_t, (uint8_t *)&t560 + 8, (uintptr_t)(35));
                                    gm_t08olq97rscn3 t561;
                                    gm_f_unsupported_531q51r((void *)&t561, (void *)NULL, cx, NULL, t559, t560);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tqgtd5gtgj65s t562;
                                    GM_ST(uint8_t, (uint8_t *)&t562 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t562 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t562 + 4, 0);
                                    ((uint8_t *)&t562)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t562 + 8), t561);
                                    memmove(gm_out, ((uint8_t *)&t562), 40);
                                    goto gm_fdone;
                                }
gm_lb65:;
                                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 236, 37);
                            }
gm_lb62:;
                            gm_t88rcucg1b7uk t563 = gm_l95;
                            gm_l93 = t563;
                            goto gm_lb58;
                        }
gm_lb61:;
                        {
                            gm_tl874t2atnlh3 t564 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                            gm_b16_8 t565;
                            GM_ST(void *, (uint8_t *)&t565, (void *)(gm_sgiqpj781s1a9));
                            GM_ST(uintptr_t, (uint8_t *)&t565 + 8, (uintptr_t)(35));
                            gm_t08olq97rscn3 t566;
                            gm_f_unsupported_531q51r((void *)&t566, (void *)NULL, cx, NULL, t564, t565);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tqgtd5gtgj65s t567;
                            GM_ST(uint8_t, (uint8_t *)&t567 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t567 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t567 + 4, 0);
                            ((uint8_t *)&t567)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t567 + 8), t566);
                            memmove(gm_out, ((uint8_t *)&t567), 40);
                            goto gm_fdone;
                        }
gm_lb66:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 233, 27);
                    }
gm_lb58:;
                    gm_t88rcucg1b7uk t568 = gm_l93;
                    gm_l60 = t568;
                    void * t569 = gm_l0;
                    gm_t88rcucg1b7uk t570 = gm_l60;
                    uint64_t t571 = gm_l4;
                    gm_tkaapcii86udk t572 = GM_LD(gm_tkaapcii86udk, ((uint8_t *)&gm_l54));
                    gm_f_place_reloc_blfij5d((void *)&gm_l97, (void *)NULL, cx, NULL, t569, &t572, t570, t571);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t573 = (uint8_t)!((((uint8_t *)&gm_l97))[0] != 0);
                    if (t573) {
                    } else {
                        gm_t08olq97rscn3 t574 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l97) + 8));
                        gm_tqgtd5gtgj65s t575;
                        GM_ST(uint8_t, (uint8_t *)&t575 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t575 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t575 + 4, 0);
                        ((uint8_t *)&t575)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t575 + 8), t574);
                        memmove(gm_out, ((uint8_t *)&t575), 40);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&gm_l61), (((uint8_t *)&gm_l97) + 8), 32);
                    void * t576 = gm_l5;
                    gm_tftb1odc739t5 t577 = gm_f_push_vgtfcjf(t576, &(*(gm_tob1snuii5bme *)((uint8_t *)&gm_l61)));
                    gm_l98 = t577;
                    uint8_t t578 = (uint8_t)!((((uint8_t *)&gm_l98))[0] != 0);
                    if (!t578) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l98) + 1, gm_srcuro7l7pd15c2, 244, 13);
                    goto gm_lb1;
                }
gm_lb57:;
            }
            {
                {
                    uint8_t t579 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t580 = (uint8_t)(t579 == ((uint8_t)5ULL));
                    if (!t580) {
                        goto gm_lb67;
                    }
                    memmove(((uint8_t *)&gm_l62), (((uint8_t *)&F.l1) + 16), 24);
                    uint64_t t581 = GM_LD(uint64_t, ((((uint8_t *)&F.l1) + 16) + 24));
                    gm_l63 = t581;
                    {
                        {
                            void * t582 = gm_l0;
                            void * t583 = (void *)(((uint8_t *)t582) + 8);
                            gm_t88rcucg1b7uk t584 = gm_l2;
                            gm_f_kind_ghvav22((void *)&gm_l99, t583, GM_CONV(gm_csguankko4lmhd, t584));
                            uint8_t t585 = GM_LD(uint8_t, (((uint8_t *)&gm_l99) + 0));
                            uint8_t t586 = (uint8_t)(t585 == ((uint8_t)10ULL));
                            if (!t586) {
                                goto gm_lb69;
                            }
                            gm_t88rcucg1b7uk t587 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l99) + 8) + 4));
                            gm_l64 = t587;
                            void * t588 = gm_l0;
                            uint64_t t589 = GM_LD(uint64_t, ((((uint8_t *)t588) + 8) + 8));
                            gm_l65 = t589;
                            uint64_t t590 = gm_l63;
                            uint8_t t591 = (t590 > ((uint64_t)0ULL));
                            if (t591) {
                                gm_l100 = ((uint8_t)1);
                            } else {
                                uint64_t t592 = GM_LD(uint64_t, ((((uint8_t *)&gm_l62) + 8) + 8));
                                uint8_t t593 = (t592 > ((uint64_t)0ULL));
                                gm_l100 = t593;
                            }
                            uint8_t t594 = gm_l100;
                            if (t594) {
                                void * t595 = gm_l0;
                                gm_t88rcucg1b7uk t596 = gm_l64;
                                uint64_t t597 = gm_l4;
                                gm_tkaapcii86udk t598 = GM_LD(gm_tkaapcii86udk, ((uint8_t *)&gm_l62));
                                gm_f_place_reloc_blfij5d((void *)&gm_l101, (void *)NULL, cx, NULL, t595, &t598, t596, t597);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t599 = (uint8_t)!((((uint8_t *)&gm_l101))[0] != 0);
                                if (t599) {
                                } else {
                                    gm_t08olq97rscn3 t600 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l101) + 8));
                                    gm_tqgtd5gtgj65s t601;
                                    GM_ST(uint8_t, (uint8_t *)&t601 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t601 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t601 + 4, 0);
                                    ((uint8_t *)&t601)[0] = 1;
                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t601 + 8), t600);
                                    memmove(gm_out, ((uint8_t *)&t601), 40);
                                    goto gm_fdone;
                                }
                                memmove(((uint8_t *)&gm_l66), (((uint8_t *)&gm_l101) + 8), 32);
                                void * t602 = gm_l5;
                                gm_tftb1odc739t5 t603 = gm_f_push_vgtfcjf(t602, &(*(gm_tob1snuii5bme *)((uint8_t *)&gm_l66)));
                                gm_l102 = t603;
                                uint8_t t604 = (uint8_t)!((((uint8_t *)&gm_l102))[0] != 0);
                                if (!t604) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l102) + 1, gm_srcuro7l7pd15c2, 251, 17);
                            }
                            void * t605 = gm_l0;
                            gm_b16_8 t606 = gm_l3;
                            uint64_t t607 = gm_l4;
                            uint64_t t608 = gm_l65;
                            uint64_t r609; bool o610 = __builtin_add_overflow(t607, t608, &r609);
                            if (o610) gm_trap("integer overflow in `+`", gm_srcuro7l7pd15c2, 253, 30);
                            uint64_t t611 = r609;
                            uint64_t t612 = gm_l63;
                            unsigned __int128 t613 = (unsigned __int128)t612;
                            uint64_t t614 = gm_l65;
                            gm_f_store_int_mfkaqk5(t605, t606, t611, t613, t614);
                            goto gm_lb68;
                        }
gm_lb69:;
                        gm_tl874t2atnlh3 t615 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                        gm_b16_8 t616;
                        GM_ST(void *, (uint8_t *)&t616, (void *)(gm_svlcdvcv2bp94));
                        GM_ST(uintptr_t, (uint8_t *)&t616 + 8, (uintptr_t)(23));
                        gm_t08olq97rscn3 t617;
                        gm_f_unsupported_531q51r((void *)&t617, (void *)NULL, cx, NULL, t615, t616);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tqgtd5gtgj65s t618;
                        GM_ST(uint8_t, (uint8_t *)&t618 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t618 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t618 + 4, 0);
                        ((uint8_t *)&t618)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t618 + 8), t617);
                        memmove(gm_out, ((uint8_t *)&t618), 40);
                        goto gm_fdone;
                    }
gm_lb68:;
                    goto gm_lb1;
                }
gm_lb67:;
            }
            {
                {
                    uint8_t t619 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                    uint8_t t620 = (uint8_t)(t619 == ((uint8_t)11ULL));
                    if (!t620) {
                        goto gm_lb70;
                    }
                    uint32_t t621 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
                    gm_l67 = t621;
                    void * t622 = gm_l0;
                    uint32_t t623 = gm_l67;
                    gm_b0_1 t624 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t622, t623);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t625 = gm_l0;
                    void * t626 = (void *)(((uint8_t *)t625) + 8);
                    gm_t88rcucg1b7uk t627 = gm_l2;
                    gm_f_kind_ghvav22((void *)&gm_l104, t626, GM_CONV(gm_csguankko4lmhd, t627));
                    {
                        {
                            uint8_t t628 = GM_LD(uint8_t, (((uint8_t *)&gm_l104) + 0));
                            uint8_t t629 = (uint8_t)(t628 == ((uint8_t)16ULL));
                            if (!t629) {
                                goto gm_lb72;
                            }
                            gm_l103 = ((uint8_t)0);
                            goto gm_lb71;
                        }
gm_lb72:;
                        {
                            uint8_t t630 = GM_LD(uint8_t, (((uint8_t *)&gm_l104) + 0));
                            uint8_t t631 = (uint8_t)(t630 == ((uint8_t)13ULL));
                            if (!t631) {
                                goto gm_lb73;
                            }
                            gm_l103 = ((uint8_t)0);
                            goto gm_lb71;
                        }
gm_lb73:;
                        {
                            gm_l103 = ((uint8_t)1);
                            goto gm_lb71;
                        }
gm_lb74:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 257, 16);
                    }
gm_lb71:;
                    uint8_t t632 = gm_l103;
                    if (t632) {
                        gm_tl874t2atnlh3 t633 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                        gm_b16_8 t634;
                        GM_ST(void *, (uint8_t *)&t634, (void *)(gm_sbe7f0bkvca75));
                        GM_ST(uintptr_t, (uint8_t *)&t634 + 8, (uintptr_t)(42));
                        gm_t08olq97rscn3 t635;
                        gm_f_unsupported_531q51r((void *)&t635, (void *)NULL, cx, NULL, t633, t634);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tqgtd5gtgj65s t636;
                        GM_ST(uint8_t, (uint8_t *)&t636 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t636 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t636 + 4, 0);
                        ((uint8_t *)&t636)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t636 + 8), t635);
                        memmove(gm_out, ((uint8_t *)&t636), 40);
                        goto gm_fdone;
                    }
                    {
                        {
                            void * t637 = gm_l0;
                            uint32_t t638 = gm_l67;
                            uint8_t t639 = gm_f_is_plain_qr08i40((void *)NULL, cx, NULL, t637, t638);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (!t639) {
                                goto gm_lb76;
                            }
                            void * t640 = gm_l0;
                            gm_t88rcucg1b7uk t641 = gm_l2;
                            uint8_t t642 = gm_f_fn_ptr_type_plain_bi8eu4l((void *)NULL, cx, NULL, t640, t641);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t643 = (uint8_t)!t642;
                            if (!t643) {
                                goto gm_lb76;
                            }
                            void * t644 = gm_l0;
                            uint32_t t645 = gm_l67;
                            gm_b16_8 t646 = gm_f_addr_name_g5lrlt3((void *)NULL, cx, NULL, t644, t645);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t647;
                            GM_ST(gm_b16_8, ((uint8_t *)&t647 + 0), t646);
                            gm_f_format_8sjjbd9((void *)&gm_l106, (void *)NULL, cx, NULL, t647);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t648 = (uint8_t)!((((uint8_t *)&gm_l106))[0] != 0);
                            if (!t648) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l106) + 8, gm_srcuro7l7pd15c2, 262, 17);
                            gm_b16_8 t649 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l106) + 8));
                            gm_b16_8 t650 = GM_LD(gm_b16_8, ((uint8_t *)&t649));
                            gm_l105 = t650;
                            goto gm_lb75;
                        }
gm_lb76:;
                        void * t651 = gm_l0;
                        uint32_t t652 = gm_l67;
                        gm_b16_8 t653 = gm_f_addr_name_g5lrlt3((void *)NULL, cx, NULL, t651, t652);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t654;
                        GM_ST(gm_b16_8, ((uint8_t *)&t654 + 0), t653);
                        gm_f_format_be3fee1((void *)&gm_l107, (void *)NULL, cx, NULL, t654);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t655 = (uint8_t)!((((uint8_t *)&gm_l107))[0] != 0);
                        if (!t655) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l107) + 8, gm_srcuro7l7pd15c2, 264, 17);
                        gm_b16_8 t656 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l107) + 8));
                        gm_b16_8 t657 = GM_LD(gm_b16_8, ((uint8_t *)&t656));
                        gm_l105 = t657;
                    }
gm_lb75:;
                    gm_b16_8 t658 = gm_l105;
                    gm_l68 = t658;
                    void * t659 = gm_l5;
                    uint64_t t660 = gm_l4;
                    gm_b16_8 t661 = gm_l68;
                    gm_tob1snuii5bme t662;
                    GM_ST(uint64_t, ((uint8_t *)&t662 + 0), t660);
                    GM_ST(gm_b16_8, ((uint8_t *)&t662 + 8), t661);
                    GM_ST(uint64_t, ((uint8_t *)&t662 + 24), ((uint64_t)0ULL));
                    gm_tftb1odc739t5 t663 = gm_f_push_vgtfcjf(t659, &t662);
                    gm_l108 = t663;
                    uint8_t t664 = (uint8_t)!((((uint8_t *)&gm_l108))[0] != 0);
                    if (!t664) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l108) + 1, gm_srcuro7l7pd15c2, 266, 13);
                    goto gm_lb1;
                }
gm_lb70:;
            }
            {
                {
                    {
                        {
                            uint8_t t665 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t666 = (uint8_t)(t665 == ((uint8_t)12ULL));
                            if (!t666) {
                                goto gm_lb79;
                            }
                            goto gm_lb78;
                        }
gm_lb79:;
                        {
                            uint8_t t667 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
                            uint8_t t668 = (uint8_t)(t667 == ((uint8_t)13ULL));
                            if (!t668) {
                                goto gm_lb80;
                            }
                            goto gm_lb78;
                        }
gm_lb80:;
                        goto gm_lb77;
                    }
gm_lb78:;
                    gm_tl874t2atnlh3 t669 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                    gm_b16_8 t670;
                    GM_ST(void *, (uint8_t *)&t670, (void *)(gm_sbbavs945cjhp));
                    GM_ST(uintptr_t, (uint8_t *)&t670 + 8, (uintptr_t)(31));
                    gm_t08olq97rscn3 t671;
                    gm_f_unsupported_531q51r((void *)&t671, (void *)NULL, cx, NULL, t669, t670);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqgtd5gtgj65s t672;
                    GM_ST(uint8_t, (uint8_t *)&t672 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t672 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t672 + 4, 0);
                    ((uint8_t *)&t672)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t672 + 8), t671);
                    memmove(gm_out, ((uint8_t *)&t672), 40);
                    goto gm_fdone;
                }
gm_lb77:;
                gm_trap("no match arm matched the scrutinee", gm_srcuro7l7pd15c2, 163, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_tqgtd5gtgj65s t673;
        memset(&t673, 0, sizeof t673);
        ((uint8_t *)&t673)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t673 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t673), 40);
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
