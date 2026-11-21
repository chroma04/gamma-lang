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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint16_t f0; } gm_tu0pangbbaeec;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; } gm_tbfdi36rjh0el;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; } gm_temv198mna2jp;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; gm_tf5srh4mp4b3i f3; uint64_t f4; } gm_tml3m6miq9l2k;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; } gm_tov4mtrfp5e7v;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_t88rcucg1b7uk a[2]; } gm_tqsk8dt6ib3qq;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tu0pangbbaeec) == 2 && _Alignof(gm_tu0pangbbaeec) == 2 && offsetof(gm_tu0pangbbaeec, f0) == 0, "layout of `(u16,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint64_t f0; gm_th2lqbo77pvsc f1; } gm_t13urt52695sh;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t0rfe1ou14h21 f0; void * f1; } gm_t4grvdhp6dkf1;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { gm_tgbgti82mf879 f0; uint8_t f1; uint8_t pe[7]; } gm_t8ldkt09h6492;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbfdi36rjh0el) == 40 && _Alignof(gm_tbfdi36rjh0el) == 8 && offsetof(gm_tbfdi36rjh0el, f0) == 0 && offsetof(gm_tbfdi36rjh0el, f1) == 8 && offsetof(gm_tbfdi36rjh0el, f2) == 16 && offsetof(gm_tbfdi36rjh0el, f3) == 24, "layout of `(u64, u64, u64, string)`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_t4c7ipjj9s3fp f0; gm_trqfvmjtt4sij f1; uint8_t f2; uint8_t p3[7]; gm_trqfvmjtt4sij f3; gm_t4c7ipjj9s3fp f4; gm_t4c7ipjj9s3fp f5; } gm_tbpdf5ej15iof;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_temv198mna2jp) == 40 && _Alignof(gm_temv198mna2jp) == 8 && offsetof(gm_temv198mna2jp, f0) == 0 && offsetof(gm_temv198mna2jp, f1) == 16 && offsetof(gm_temv198mna2jp, f2) == 24, "layout of `(string, u32, string)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
typedef struct { gm_th2lqbo77pvsc f0; uint8_t f1; uint8_t pe[7]; } gm_ti5ramhglnvdt;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tml3m6miq9l2k) == 56 && _Alignof(gm_tml3m6miq9l2k) == 8 && offsetof(gm_tml3m6miq9l2k, f0) == 0 && offsetof(gm_tml3m6miq9l2k, f1) == 16 && offsetof(gm_tml3m6miq9l2k, f2) == 24 && offsetof(gm_tml3m6miq9l2k, f3) == 32 && offsetof(gm_tml3m6miq9l2k, f4) == 48, "layout of `Member`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tov4mtrfp5e7v) == 12 && _Alignof(gm_tov4mtrfp5e7v) == 4 && offsetof(gm_tov4mtrfp5e7v, f0) == 0 && offsetof(gm_tov4mtrfp5e7v, f1) == 4 && offsetof(gm_tov4mtrfp5e7v, f2) == 8, "layout of `BitField`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_tqsk8dt6ib3qq) == 8 && _Alignof(gm_tqsk8dt6ib3qq) == 4 && offsetof(gm_tqsk8dt6ib3qq, a) == 0, "layout of `[2]Ty`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t4c7ipjj9s3fp f0; } gm_ttiitul0263ld;
typedef struct { uint64_t f0; gm_tml3m6miq9l2k f1; } gm_ttos9gukqrqer;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { uint64_t f0; uint64_t f1; gm_tl572d7579hns f2; gm_b16_8 f3; } gm_tulpvg6gm4fqa;
_Static_assert(sizeof(gm_t13urt52695sh) == 32 && _Alignof(gm_t13urt52695sh) == 8 && offsetof(gm_t13urt52695sh, f0) == 0 && offsetof(gm_t13urt52695sh, f1) == 8, "layout of `(i: usize, x: ?string)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4grvdhp6dkf1) == 72 && _Alignof(gm_t4grvdhp6dkf1) == 8 && offsetof(gm_t4grvdhp6dkf1, f0) == 0 && offsetof(gm_t4grvdhp6dkf1, f1) == 64, "layout of `Layouts`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t8ldkt09h6492) == 40 && _Alignof(gm_t8ldkt09h6492) == 8 && offsetof(gm_t8ldkt09h6492, f0) == 0 && offsetof(gm_t8ldkt09h6492, f1) == 32, "layout of `?PayloadUnion`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbpdf5ej15iof) == 256 && _Alignof(gm_tbpdf5ej15iof) == 8 && offsetof(gm_tbpdf5ej15iof, f0) == 0 && offsetof(gm_tbpdf5ej15iof, f1) == 56 && offsetof(gm_tbpdf5ej15iof, f2) == 96 && offsetof(gm_tbpdf5ej15iof, f3) == 104 && offsetof(gm_tbpdf5ej15iof, f4) == 144 && offsetof(gm_tbpdf5ej15iof, f5) == 200, "layout of `Views`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_ti5ramhglnvdt) == 32 && _Alignof(gm_ti5ramhglnvdt) == 8 && offsetof(gm_ti5ramhglnvdt, f0) == 0 && offsetof(gm_ti5ramhglnvdt, f1) == 24, "layout of `??string`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
typedef struct { void * f0; gm_t4grvdhp6dkf1 f1; gm_trqfvmjtt4sij f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; gm_t4c7ipjj9s3fp f9; gm_trqfvmjtt4sij f10; gm_t4c7ipjj9s3fp f11; gm_trqfvmjtt4sij f12; gm_t4c7ipjj9s3fp f13; gm_t4c7ipjj9s3fp f14; gm_trqfvmjtt4sij f15; gm_t4c7ipjj9s3fp f16; uint8_t f17; uint8_t p18[7]; gm_t4c7ipjj9s3fp f18; gm_tbpdf5ej15iof f19; } gm_trg1s7erfs461;
_Static_assert(sizeof(gm_ttiitul0263ld) == 56 && _Alignof(gm_ttiitul0263ld) == 8 && offsetof(gm_ttiitul0263ld, f0) == 0, "layout of `Namer`");
_Static_assert(sizeof(gm_ttos9gukqrqer) == 64 && _Alignof(gm_ttos9gukqrqer) == 8 && offsetof(gm_ttos9gukqrqer, f0) == 0 && offsetof(gm_ttos9gukqrqer, f1) == 8, "layout of `(i: usize, x: Member)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tulpvg6gm4fqa) == 56 && _Alignof(gm_tulpvg6gm4fqa) == 8 && offsetof(gm_tulpvg6gm4fqa, f0) == 0 && offsetof(gm_tulpvg6gm4fqa, f1) == 8 && offsetof(gm_tulpvg6gm4fqa, f2) == 16 && offsetof(gm_tulpvg6gm4fqa, f3) == 40, "layout of `BlobDef`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_trg1s7erfs461) == 1128 && _Alignof(gm_trg1s7erfs461) == 8 && offsetof(gm_trg1s7erfs461, f0) == 0 && offsetof(gm_trg1s7erfs461, f1) == 8 && offsetof(gm_trg1s7erfs461, f2) == 80 && offsetof(gm_trg1s7erfs461, f3) == 120 && offsetof(gm_trg1s7erfs461, f4) == 176 && offsetof(gm_trg1s7erfs461, f5) == 232 && offsetof(gm_trg1s7erfs461, f6) == 288 && offsetof(gm_trg1s7erfs461, f7) == 328 && offsetof(gm_trg1s7erfs461, f8) == 368 && offsetof(gm_trg1s7erfs461, f9) == 408 && offsetof(gm_trg1s7erfs461, f10) == 464 && offsetof(gm_trg1s7erfs461, f11) == 504 && offsetof(gm_trg1s7erfs461, f12) == 560 && offsetof(gm_trg1s7erfs461, f13) == 600 && offsetof(gm_trg1s7erfs461, f14) == 656 && offsetof(gm_trg1s7erfs461, f15) == 712 && offsetof(gm_trg1s7erfs461, f16) == 752 && offsetof(gm_trg1s7erfs461, f17) == 808 && offsetof(gm_trg1s7erfs461, f18) == 816 && offsetof(gm_trg1s7erfs461, f19) == 872, "layout of `CTypes`");
struct gm_fr_add_member_name_j3i0eje {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adt_of_gej99vn {
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
struct gm_fr_assert_message_6bd9l6u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_bit_width_u86m8ir {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_bits_layout_23rect9 {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_blob_before_9p1c704 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_blob_kind_kp13j9k {
    gm_frame h;
    gm_tl572d7579hns ret;
};
struct gm_fr_blob_name_sjpa1ub {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* lanes */
    gm_b16_8 ret;
};
struct gm_fr_blob_typedef_8ta3rbi {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* members */
    gm_b16_8 ret;
};
struct gm_fr_blob_typedefs_4nfn4l8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_trqfvmjtt4sij l2; /* sorted */
    gm_tulpvg6gm4fqa l5; /* b */
    gm_b16_8 ret;
};
struct gm_fr_bool_ty_kotvo8t {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_by_ref_fi0o860 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_c_string_n7h27b3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_content_name_83mn5uf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_cty_66ee0kf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ctypes_j6au0st {
    gm_frame h;
    gm_t0rfe1ou14h21 l1; /* t */
    gm_trg1s7erfs461 ret;
};
struct gm_fr_define_typed_m3mqpm3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_desc_0cag2kq {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* f */
    gm_trqfvmjtt4sij l16; /* parts */
    gm_tpautr4br7h3l l32; /* sd */
    gm_trqfvmjtt4sij l34; /* parts */
    gm_t0mglksrkfa5e l38; /* fd */
    gm_trqfvmjtt4sij l41; /* parts */
    gm_t0mglksrkfa5e l45; /* fd */
    gm_trqfvmjtt4sij l51; /* parts */
    gm_tckdobunaulee l54; /* v */
    gm_trqfvmjtt4sij l57; /* fields */
    gm_b16_8 ret;
};
struct gm_fr_descriptors_fnnm3h1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_08mdrht {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_2g727l2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ab79bmt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hm6i8si {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jckt05t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ose1ba9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ppl3r45 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qsq36pn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vu26gaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_glhj3bs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_field_cty_nfj8rcv {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_fields_array_tikg33t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6gkpg0v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_0h3rgr6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_19e6va7 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_19ftgop {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1cgbgnn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1dfo4pq {
    gm_frame h;
    gm_tftksr4op114e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1il5da4 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_38m1q8c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3buamil {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4v9fh5b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6a91mh8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8n626pi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9117rt8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a4r7k06 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aiomp5b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aqb9tiu {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c19sabv {
    gm_frame h;
    gm_tbfdi36rjh0el l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbfdi36rjh0el l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ch41dev {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_df9v4jq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dtmr1c2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ef193cn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ef6t3dr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f6fn313 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_g0ibdjv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gkjg5ld {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h6b2ofo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_io38sjt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_irg2n6e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jpvhgp3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ka5ced8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kl8i1sq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_km55tit {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ktg7ckd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lkeo172 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mfspujt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ms4f9aj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n3gtmrf {
    gm_frame h;
    gm_temv198mna2jp l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_temv198mna2jp l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nbg44fl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o1q76rb {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_onn3qvb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p144a0o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qc4s0m5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s44fp1q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s51nisc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s9gvae7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tbvfjdi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tc7tn98 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tjkla39 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tra8sop {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_udu99n6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vgl5p2e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vsbf5l9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_p217beu {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_hash_map_1s9eugt {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_m04clhi {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_p3t5tj8 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_tl7apid {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_cty_s62af0i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_int_ty_7nma1b7 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_float_gthejtb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_eo6jkc5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_never_in5ftma {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_signed_integer_ige9b74 {
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
struct gm_fr_is_vector_fm7q9br {
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
struct gm_fr_kind_rank_g1kdlst {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_layouts_au8nfbt {
    gm_frame h;
    gm_t0rfe1ou14h21 l1; /* t */
    gm_t4grvdhp6dkf1 ret;
};
struct gm_fr_list_0c2v515 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_792g2go {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_cfvjofe {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nvrrj0t {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_cty_3kvgdd5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_make_mirror_rudvl6p {
    gm_frame h;
    gm_tpautr4br7h3l l7; /* def */
    gm_trqfvmjtt4sij l10; /* names */
    gm_trqfvmjtt4sij l11; /* field_names */
    gm_t0mglksrkfa5e l14; /* f */
    gm_t4c7ipjj9s3fp l15; /* taken */
    gm_trqfvmjtt4sij l23; /* members */
    gm_t0mglksrkfa5e l27; /* f */
    gm_trqfvmjtt4sij l34; /* attrs */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_make_typed_2bh0dpj {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_trqfvmjtt4sij l7; /* named */
    gm_tpautr4br7h3l l22; /* def */
    gm_t0mglksrkfa5e l27; /* f */
    gm_trqfvmjtt4sij l31; /* payloads */
    gm_tckdobunaulee l34; /* v */
    gm_trqfvmjtt4sij l37; /* members */
    gm_trqfvmjtt4sij l38; /* named */
    gm_tml3m6miq9l2k l44; /* m */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_member_names_17el54k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_mirror_lcjegsf {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_mirror_text_cr5muc9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_new_namer_5q88o9g {
    gm_frame h;
    gm_ttiitul0263ld ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_param_cty_e8tvhrm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_payload_union_feshns2 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* members */
    gm_t8ldkt09h6492 ret;
};
struct gm_fr_plain_cty_q9lhlci {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d0271rf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_m55rf90 {
    gm_frame h;
    gm_tml3m6miq9l2k l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pmmlu39 {
    gm_frame h;
    gm_tulpvg6gm4fqa l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_q9fsb22 {
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
struct gm_fr_regrow_2m6a5d4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_e5frfkp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ocvi9r0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_p9v2h8s {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_repr_53quqki {
    gm_frame h;
    gm_t5v8n6qrn4rh4 ret;
};
struct gm_fr_reserve_d1qo8mf {
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_sort_by_55adsi9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sort_skeree7 {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_typed_stkcgvr {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unique_members_mrojmjf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ty_vgceilu {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_9mijhtc {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_vector_typedef_vpgf6u8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_write_1h8ac6i {
    gm_frame h;
    gm_tftksr4op114e l1; /* args */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_2h6ucjl {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_2hik1mg {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_3f2sd55 {
    gm_frame h;
    gm_tftksr4op114e l1; /* args */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_3mmgun5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_712qp7n {
    gm_frame h;
    gm_tud7oovfvnf43 l1; /* args */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_cscq5c8 {
    gm_frame h;
    gm_thddbqb6snabm l1; /* args */
    gm_thddbqb6snabm l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_f1h65c6 {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_fle5ipv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_pecnce5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_r7au1ks {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_trmu0m5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_uf08lpv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_assert_message_6bd9l6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_blob_before_9p1c704(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_blob_kind_kp13j9k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_name_sjpa1ub(void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedef_8ta3rbi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedefs_4nfn4l8(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_content_name_83mn5uf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_ctypes_j6au0st(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_define_typed_m3mqpm3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_desc_0cag2kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_descriptors_fnnm3h1(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_08mdrht(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2g727l2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hm6i8si(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_glhj3bs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_field_cty_nfj8rcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_array_tikg33t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0h3rgr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_13o7g7i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_15426cv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_19e6va7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_19ftgop(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_1cgbgnn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_1dfo4pq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_1il5da4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_38m1q8c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
inline __attribute__((aligned(4))) void gm_f_format_3buamil(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3n8di5m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_4ggj4mf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_4v9fh5b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_6a91mh8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6ti8an5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tu0pangbbaeec a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_74pd3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_9117rt8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_a4r7k06(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_aiomp5b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_aqb9tiu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bqgl4hq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_c19sabv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c97t5s6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ch41dev(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_cp3hiv2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_df9v4jq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_format_dtmr1c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ef193cn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_ef6t3dr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
inline __attribute__((aligned(4))) void gm_f_format_f6fn313(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fjqj2pj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_g0ibdjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_gkjg5ld(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_he6jnp0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ig8f8pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_io38sjt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_irg2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_jpvhgp3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_jqrpfot(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ka5ced8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_kl8i1sq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_km55tit(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_ktg7ckd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_lkeo172(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_mfspujt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_mkltt0a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ms4f9aj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tu0pangbbaeec a0);
inline __attribute__((aligned(4))) void gm_f_format_n3gtmrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_nbg44fl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_o1q76rb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_onn3qvb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_format_p144a0o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qc4s0m5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s44fp1q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_s51nisc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_s9gvae7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tbupn9l(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_tbvfjdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_tc7tn98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_tjkla39(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_to7a34u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_tra8sop(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_udu99n6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_vgl5p2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vsbf5l9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_get_9k2rdij(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_gnmase9(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_p217beu(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_1s9eugt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_cty_s62af0i(uint8_t a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ty_7nma1b7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_shnrbt7(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_gthejtb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_vector_fm7q9br(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_keys_qc5bjlr_s31qjug(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t53at80pfdus3 gm_f_kind_ghvav22_c(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst_c(gm_tl572d7579hns a0);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layouts_au8nfbt(void *gm_out, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_792g2go(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_cfvjofe(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nvrrj0t(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_cty_3kvgdd5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_make_mirror_rudvl6p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_make_typed_2bh0dpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_may_hold_pointers_iuorcah(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_member_name_mj2bvs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_member_names_17el54k(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_mirror_lcjegsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mirror_text_cr5muc9(void * a0);
inline __attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_payload_union_feshns2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_pointer_words_ljph5no(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d0271rf(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_m55rf90(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pmmlu39(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_q9fsb22(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_dm7u1sg(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_po09vud(void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2m6a5d4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e5frfkp(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_p9v2h8s(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_sort_by_55adsi9(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_typed_stkcgvr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_fba8sj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_members_mrojmjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_with_mffff7e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_vector_typedef_vpgf6u8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint64_t a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_views_bc1b3r4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0mj6rce(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_1h8ac6i(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2h6ucjl(void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2hik1mg(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3f2sd55(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3mmgun5(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_712qp7n(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_9bp3v08(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_av01663(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cscq5c8(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e6bqrb9(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f1h65c6(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f98j2rj(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fle5ipv(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_glpclq4(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hqo1u7h(void * a0, gm_t4ce0jd3tdro4 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_pecnce5(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r7au1ks(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s3v41br(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_trmu0m5(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uf08lpv(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uqn5uk3(void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_b16_8 a3);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern void *const gm_hlak2jvvbqg4[];
extern uint8_t gm_kr85ffi32os[24] __attribute__((aligned(8)));
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s0f7epcithn0r[18];
extern const uint8_t gm_s0u0p8jkqpsv1[8];
extern const uint8_t gm_s15i3qlq36ep1[16];
extern const uint8_t gm_s1a1r1qe3ssot[9];
extern const uint8_t gm_s2ph9f020upd2[7];
extern const uint8_t gm_s3erfe82rop7u[5];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s3pj34lce6rqp[10];
extern const uint8_t gm_s44ek1gecbajs[7];
extern const uint8_t gm_s48hq6ubqo1rd[10];
extern const uint8_t gm_s4be485tkfn41[15];
extern const uint8_t gm_s4cess8gum6i7[6];
extern const uint8_t gm_s58f7dj21gtf9[18];
extern const uint8_t gm_s592b618tctb1[15];
extern const uint8_t gm_s5bvhb1j14g3q[9];
extern const uint8_t gm_s5hiiva1kjs0f[27];
extern const uint8_t gm_s5llvatkc5h5i[14];
extern const uint8_t gm_s5qpqp0tch0l9[7];
extern const uint8_t gm_s6g2mpli2mu72[11];
extern const uint8_t gm_s6kvnpuo6o71f[26];
extern const uint8_t gm_s6r94a2k1goos[9];
extern const uint8_t gm_s6uo8vuok1rk4[10];
extern const uint8_t gm_s76d71nockpip[17];
extern const uint8_t gm_s7jvhfqq2rri8[30];
extern const uint8_t gm_s7qhcf46f682d[21];
extern const uint8_t gm_s81dvppjuktle[9];
extern const uint8_t gm_s82anase9fhnd[8];
extern const uint8_t gm_s866ofni9sb8u[7];
extern const uint8_t gm_s8ivf1umlv72j[42];
extern const uint8_t gm_s8jc0a1p1kvtf[73];
extern const uint8_t gm_s8lvo4deu27ek[16];
extern const uint8_t gm_s8oho6fnunkdm[14];
extern const uint8_t gm_s8p1h96ji9kh0[6];
extern const uint8_t gm_s9grtrvtmi962[8];
extern const uint8_t gm_s9ob1jv3b8md9[10];
extern const uint8_t gm_s9vtro0rvol4u[17];
extern const uint8_t gm_sa23gppmgki7j[11];
extern const uint8_t gm_sa9ktdbn49iui[16];
extern const uint8_t gm_sajo3kdi51jpa[10];
extern const uint8_t gm_sb2k9e71jsraf[7];
extern const uint8_t gm_sbec11r2rv4ti[23];
extern const uint8_t gm_sbeoq5iu4cto8[11];
extern const uint8_t gm_sc0vbtqan26pc[18];
extern const uint8_t gm_sc3pq8972q5i0[15];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_scduvjunh0oei[9];
extern const uint8_t gm_scj0sm2kpivk4[9];
extern const uint8_t gm_scl69k9ct9m19[16];
extern const uint8_t gm_scttf7jtr4fn9[6];
extern const uint8_t gm_sdam23bqvafqn[18];
extern const uint8_t gm_sdq31tldc1ff5[20];
extern const uint8_t gm_sdubr1na84odv[12];
extern const uint8_t gm_se71q7absh67v[12];
extern const uint8_t gm_sern92gk4dtsm[13];
extern const uint8_t gm_sf1p9u1vovjso[5];
extern const uint8_t gm_sf37jkpot59cu[54];
extern const uint8_t gm_sf3uq7o37v8ks[13];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg5uvbf14q54s[21];
extern const uint8_t gm_sg6dqv3rmh13r[59];
extern const uint8_t gm_sgjmsnv8ccaed[49];
extern const uint8_t gm_shn8mjp9kc2n2[9];
extern const uint8_t gm_shq9t6lutffbf[4];
extern const uint8_t gm_shqvr1vn78f0b[54];
extern const uint8_t gm_shrq42dntcbrd[19];
extern const uint8_t gm_siblunnp4nahm[10];
extern const uint8_t gm_simqpapdqf8iv[40];
extern const uint8_t gm_sir2n5292lks8[8];
extern const uint8_t gm_ska87i9f7924f[8];
extern const uint8_t gm_skagf3vj30re5[17];
extern const uint8_t gm_skr00pujn28ln[29];
extern const uint8_t gm_sktjfeu2a0ava[15];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_slad86501rp5c[19];
extern const uint8_t gm_slntkh69kcfmm[8];
extern const uint8_t gm_sm6ioruobq064[6];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_smiavg3q7ucd9[8];
extern const uint8_t gm_smn3oh253jlna[10];
extern const uint8_t gm_snokcvhtidt97[6];
extern const uint8_t gm_so31m6feg820p[4];
extern const uint8_t gm_so3qrsvnd4euf[12];
extern const uint8_t gm_so4g53vn8c2d4[8];
extern const uint8_t gm_so4p5vea9o316[10];
extern const uint8_t gm_sp24uj950qpkl[16];
extern const uint8_t gm_sp2du31gjaojf[8];
extern const uint8_t gm_spgnoteg4vcil[11];
extern const uint8_t gm_spi406027g4jo[19];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sr2oeunjjbqtf[14];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srckfibkpt681aa[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sre51cnt7pim3[35];
extern const uint8_t gm_srge6muj16eos[31];
extern const uint8_t gm_sropo1941pvlt[16];
extern const uint8_t gm_srv5rbhjb8ns5[15];
extern const uint8_t gm_st53sho4849cg[18];
extern const uint8_t gm_st9g0aublffth[18];
extern const uint8_t gm_std3g89n1j9ln[8];
extern const uint8_t gm_stj45lnsahel3[30];
extern const uint8_t gm_stuj0mv5nl55t[4];
extern const uint8_t gm_suggn3kp9dfmg[9];
extern const uint8_t gm_suuq26jjqhul4[7];
extern const uint8_t gm_suvf0cqih9u7i[17];
extern const gm_tmpl gm_tpl00cra9g0k45s;
extern const gm_tmpl gm_tpl1t0vlt7tq67r;
extern const gm_tmpl gm_tpl2u97uspakqd9;
extern const gm_tmpl gm_tpl313mu5t4735i;
extern const gm_tmpl gm_tpl3bnq2anocte9;
extern const gm_tmpl gm_tpl4g1h0tr3apfi;
extern const gm_tmpl gm_tpl52cv2e94esne;
extern const gm_tmpl gm_tpl6gp425a13qgr;
extern const gm_tmpl gm_tpl6lldue1mm65c;
extern const gm_tmpl gm_tpl6salfcvi63n2;
extern const gm_tmpl gm_tpl75d11no80fsi;
extern const gm_tmpl gm_tpl91b7sjgnmine;
extern const gm_tmpl gm_tpla0dc0a9b9i8b;
extern const gm_tmpl gm_tpla74als5inv2i;
extern const gm_tmpl gm_tplaa2bqbq660if;
extern const gm_tmpl gm_tplabinq26b6unj;
extern const gm_tmpl gm_tplad88kt6677ij;
extern const gm_tmpl gm_tplar04pgor24ut;
extern const gm_tmpl gm_tplba0kefu5gu2c;
extern const gm_tmpl gm_tplbckos3fl05qe;
extern const gm_tmpl gm_tplbeuo81m3df3c;
extern const gm_tmpl gm_tplc70rik6173ge;
extern const gm_tmpl gm_tplc9nrcb798jhl;
extern const gm_tmpl gm_tplcb5rgmp3lpr2;
extern const gm_tmpl gm_tpldofkl5m8hqug;
extern const gm_tmpl gm_tpldpi521pvp06p;
extern const gm_tmpl gm_tpldrlq7uo0cbrm;
extern const gm_tmpl gm_tpldueenbh2sgd4;
extern const gm_tmpl gm_tplec2d6tt24j0q;
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplennc1kv0529b;
extern const gm_tmpl gm_tplf3en286s8fa5;
extern const gm_tmpl gm_tplfqr816g9de0k;
extern const gm_tmpl gm_tplgfhhtcka09o4;
extern const gm_tmpl gm_tplgm9vv1vtdqi9;
extern const gm_tmpl gm_tplgnobkmf75b9i;
extern const gm_tmpl gm_tplgq3upqu7s79r;
extern const gm_tmpl gm_tplivckk5jvcluc;
extern const gm_tmpl gm_tplj2nt5hsf4u73;
extern const gm_tmpl gm_tpljg9idve5dgca;
extern const gm_tmpl gm_tpljgv7fkm5ld25;
extern const gm_tmpl gm_tplkfag0fic60vv;
extern const gm_tmpl gm_tplls1lesito750;
extern const gm_tmpl gm_tplmnlel5aohei6;
extern const gm_tmpl gm_tpln2j7s58m8uan;
extern const gm_tmpl gm_tplnbsaptme3355;
extern const gm_tmpl gm_tplnodn319tb55r;
extern const gm_tmpl gm_tplodl7i2apj0bs;
extern const gm_tmpl gm_tplorj2ck0ovl7t;
extern const gm_tmpl gm_tplrof4shrhlnf8;
extern const gm_tmpl gm_tplsa4s74k8e6ci;
extern const gm_tmpl gm_tplsdvt6aocmtuo;
extern const gm_tmpl gm_tplt5ks894658kl;
extern const gm_tmpl gm_tpltc1l980uvcep;
extern const gm_tmpl gm_tpltd7vnsp01mvt;
extern const gm_tmpl gm_tpltfirffnkp263;
extern const gm_tmpl gm_tpludt9aee4n14r;
extern const gm_tmpl gm_tpluuts50bm11md;
extern const gm_tmpl gm_tplv516qd7l6o3q;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_add_member_name_j3i0eje F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 816);
    gm_b16_8 t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_put_5c6qgui(t3, t4, ((uint8_t)1));
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srckfibkpt681aa, 450, 5);
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_kotvo8t F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_content_name_83mn5uf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_content_name_83mn5uf F;
    gm_b16_8 gm_l0; /* prefix */
    gm_b16_8 gm_l1; /* content */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t3, ((uint64_t)12ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t6j6smj7qoctu t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 16), t4);
    gm_f_format_8n626pi((void *)&gm_l2, (void *)NULL, cx, NULL, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcht195tfrfuqo, 86, 82);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_descriptors_fnnm3h1(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_descriptors_fnnm3h1 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 217u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 288));
    void * t4 = gm_l0;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 328));
    void * t6 = gm_l0;
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 368));
    gm_tfeab279c16rr t8;
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 16), t5);
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 32), t7);
    gm_f_format_o1q76rb((void *)&gm_l1, (void *)NULL, cx, NULL, &t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_srckfibkpt681aa, 627, 63);
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_08mdrht(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_08mdrht F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2g727l2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2g727l2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ab79bmt F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hm6i8si(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hm6i8si F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jckt05t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jckt05t F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ose1ba9 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ppl3r45 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qsq36pn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qsq36pn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vu26gaj F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_glhj3bs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_glhj3bs F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_2m6a5d4(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 56)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 56);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_6gkpg0v F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    void * t4 = gm_l1;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)t4));
    uint8_t t6 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t3), ((uint8_t *)&t5));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0h3rgr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_0h3rgr6 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l2;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            void * t5 = (void *)((uint8_t *)&((struct gm_fr_member_names_17el54k *)((&F.h)->up))->l1);
            gm_b16_8 t6 = gm_l1;
            gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
            gm_l2 = t7;
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
            if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srckfibkpt681aa, 456, 37);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 456, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_19e6va7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_19e6va7 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sp2du31gjaojf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(7));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_15426cv((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplc9nrcb798jhl), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_19ftgop(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_19ftgop F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s48hq6ubqo1rd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpljg9idve5dgca), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_1cgbgnn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1cgbgnn F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s592b618tctb1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6lldue1mm65c), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_1dfo4pq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1dfo4pq F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s9ob1jv3b8md9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_tftksr4op114e t3 = GM_LD(gm_tftksr4op114e, ((uint8_t *)a0));
    gm_f_format_mkltt0a((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplv516qd7l6o3q), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_1il5da4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1il5da4 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_srv5rbhjb8ns5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_tq3m5qgvkpje8 t3 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
    gm_f_format_tbupn9l((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2u97uspakqd9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_38m1q8c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_38m1q8c F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skagf3vj30re5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_3n8di5m((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpltfirffnkp263), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3buamil(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3buamil F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sir2n5292lks8));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(7));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl75d11no80fsi), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_4v9fh5b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_4v9fh5b F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scl69k9ct9m19));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplt5ks894658kl), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_6a91mh8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6a91mh8 F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tf1bjg81u0glv gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5qpqp0tch0l9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_bqgl4hq((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl6gp425a13qgr), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_9117rt8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_9117rt8 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9vtro0rvol4u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplivckk5jvcluc), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a4r7k06(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_a4r7k06 F;
    gm_tbccvikg6b96e gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tbccvikg6b96e gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shqvr1vn78f0b));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(53));
    gm_tbccvikg6b96e t3 = GM_LD(gm_tbccvikg6b96e, ((uint8_t *)a0));
    gm_f_format_cp3hiv2((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl91b7sjgnmine), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_aiomp5b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_aiomp5b F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf3uq7o37v8ks));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplba0kefu5gu2c), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_aqb9tiu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_aqb9tiu F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_srv5rbhjb8ns5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_he6jnp0((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2u97uspakqd9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c19sabv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c19sabv F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8jc0a1p1kvtf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(72));
    gm_tbfdi36rjh0el t3 = GM_LD(gm_tbfdi36rjh0el, ((uint8_t *)a0));
    gm_f_format_4ggj4mf((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1t0vlt7tq67r), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ch41dev(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_ch41dev F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sajo3kdi51jpa));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4g1h0tr3apfi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_df9v4jq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_df9v4jq F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbeoq5iu4cto8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_c97t5s6((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj2nt5hsf4u73), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_dtmr1c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dtmr1c2 F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tf1bjg81u0glv gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s81dvppjuktle));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_bqgl4hq((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl6salfcvi63n2), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ef193cn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_ef193cn F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdubr1na84odv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsa4s74k8e6ci), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ef6t3dr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_ef6t3dr F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so4p5vea9o316));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_ig8f8pl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplgq3upqu7s79r), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_f6fn313(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_f6fn313 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3pj34lce6rqp));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_ig8f8pl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbckos3fl05qe), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gkjg5ld(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_gkjg5ld F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3erfe82rop7u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplodl7i2apj0bs), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_io38sjt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_io38sjt F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdq31tldc1ff5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldpi521pvp06p), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_irg2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_irg2n6e F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8oho6fnunkdm));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3bnq2anocte9), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_jpvhgp3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_jpvhgp3 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa23gppmgki7j));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplar04pgor24ut), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ka5ced8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ka5ced8 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp24uj950qpkl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkfag0fic60vv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_kl8i1sq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_kl8i1sq F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s15i3qlq36ep1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpla74als5inv2i), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_km55tit(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_km55tit F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scduvjunh0oei));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl313mu5t4735i), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ktg7ckd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_ktg7ckd F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4be485tkfn41));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpluuts50bm11md), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lkeo172(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_lkeo172 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc3pq8972q5i0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_c97t5s6((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcb5rgmp3lpr2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_mfspujt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_mfspujt F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se71q7absh67v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplgm9vv1vtdqi9), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ms4f9aj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tu0pangbbaeec a0) {
    struct gm_fr_format_ms4f9aj F;
    gm_tu0pangbbaeec gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tu0pangbbaeec gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tu0pangbbaeec t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sajo3kdi51jpa));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_6ti8an5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4g1h0tr3apfi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n3gtmrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n3gtmrf F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_srv5rbhjb8ns5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_temv198mna2jp t3 = GM_LD(gm_temv198mna2jp, ((uint8_t *)a0));
    gm_f_format_13o7g7i((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2u97uspakqd9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_nbg44fl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nbg44fl F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6uo8vuok1rk4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl52cv2e94esne), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_o1q76rb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_o1q76rb F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb2k9e71jsraf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_jqrpfot((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfqr816g9de0k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_onn3qvb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_onn3qvb F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8lvo4deu27ek));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_c97t5s6((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrof4shrhlnf8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_p144a0o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_p144a0o F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg5uvbf14q54s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplls1lesito750), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qc4s0m5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_qc4s0m5 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stj45lnsahel3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplorj2ck0ovl7t), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s44fp1q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s44fp1q F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5bvhb1j14g3q));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmnlel5aohei6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s51nisc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_s51nisc F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr2oeunjjbqtf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplad88kt6677ij), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s9gvae7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_s9gvae7 F;
    gm_tbccvikg6b96e gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tbccvikg6b96e gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sg6dqv3rmh13r));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(58));
    gm_tbccvikg6b96e t3 = GM_LD(gm_tbccvikg6b96e, ((uint8_t *)a0));
    gm_f_format_cp3hiv2((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplaa2bqbq660if), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_tbvfjdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_tbvfjdi F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5llvatkc5h5i));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsdvt6aocmtuo), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_tc7tn98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_tc7tn98 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so31m6feg820p));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldofkl5m8hqug), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_tjkla39(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tjkla39 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_skr00pujn28ln));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(28));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_74pd3in((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpltd7vnsp01mvt), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_tra8sop(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tra8sop F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spgnoteg4vcil));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplgnobkmf75b9i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_udu99n6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_udu99n6 F;
    gm_tbccvikg6b96e gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tbccvikg6b96e gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sern92gk4dtsm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
    gm_tbccvikg6b96e t3 = GM_LD(gm_tbccvikg6b96e, ((uint8_t *)a0));
    gm_f_format_to7a34u((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplabinq26b6unj), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_vgl5p2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vgl5p2e F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sktjfeu2a0ava));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpla0dc0a9b9i8b), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vsbf5l9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vsbf5l9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smn3oh253jlna));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnodn319tb55r), t3);
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_p217beu(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_p217beu F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    uint64_t gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_722uua4(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l4 = t6;
    } else {
        gm_tqs5p4n2tq19v t7;
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
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t10) + (uintptr_t)t12 * 32) + 24));
    gm_tqs5p4n2tq19v t14;
    GM_ST(uint8_t, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[1] = 1;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_1s9eugt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_1s9eugt F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_08mdrht();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_cuh4fs9 F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_k21v01c();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_m04clhi(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_m04clhi F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jckt05t();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_p3t5tj8 F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vu26gaj();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_tl7apid F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qsq36pn();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ty_7nma1b7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ty_7nma1b7 F;
    void * gm_l0; /* ty */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 50u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    uint8_t t5 = gm_l1;
    uint16_t t6 = gm_l2;
    gm_t88rcucg1b7uk t7 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t4, t5, t6);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_gthejtb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_float_gthejtb F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    gm_t53at80pfdus3 gm_l3;
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    void * t7 = (void *)(((uint8_t *)t6) + 8);
    gm_t88rcucg1b7uk t8 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l3, t7, GM_CONV(gm_csguankko4lmhd, t8));
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)3ULL));
            if (!t10) {
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
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 112, 53);
    }
gm_lb1:;
    uint8_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_eo6jkc5 F;
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
    uint8_t t6 = gm_f_is_integer_8d4f9ek(t4, GM_CONV(gm_csguankko4lmhd, t5));
    F.ret = t6;
    goto gm_fret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_signed_integer_ige9b74 F;
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
    uint8_t t6 = gm_f_is_signed_integer_s66djm7(t4, GM_CONV(gm_csguankko4lmhd, t5));
    F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_layouts_au8nfbt(void *gm_out, void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_layouts_au8nfbt F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t0rfe1ou14h21 t3 = GM_LD(gm_t0rfe1ou14h21, ((uint8_t *)a1));
    gm_t4grvdhp6dkf1 t4;
    GM_ST(gm_t0rfe1ou14h21, ((uint8_t *)&t4 + 0), t3);
    GM_ST(void *, ((uint8_t *)&t4 + 64), t2);
    memmove(gm_out, ((uint8_t *)&t4), 72);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0c2v515 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ppl3r45();
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
inline __attribute__((aligned(4))) void gm_f_list_792g2go(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_792g2go F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ab79bmt();
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
inline __attribute__((aligned(4))) void gm_f_list_cfvjofe(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_cfvjofe F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_2g727l2();
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
inline __attribute__((aligned(4))) void gm_f_list_nvrrj0t(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nvrrj0t F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_hm6i8si();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_member_names_17el54k(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_member_names_17el54k F;
    void * gm_l0; /* ty */
    void * gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_trqfvmjtt4sij t3;
    gm_f_list_epvducg((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t3;
    void * t4 = gm_l0;
    void * t5 = (void *)(((uint8_t *)t4) + 816);
    gm_l2 = t5;
    {
        gm_node wn6 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlak2jvvbqg4, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        void * t9 = gm_l2;
        gm_b0_1 t10 = gm_f_keys_qc5bjlr_s31qjug((void *)NULL, (&wn6), NULL, t9);
        if (gm_unw.kind) {
            goto gm_ll8;
        }
    }
    goto gm_ls11;
gm_ll8:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we7; }
    goto gm_lad;
gm_ls11:;
gm_lb1:;
we7:;
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_f_sort_skeree7(t12);
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    F.ret = t13;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mirror_text_cr5muc9(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mirror_text_cr5muc9 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 712));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_namer_5q88o9g F;
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t4c7ipjj9s3fp t2;
    gm_f_hash_map_m04clhi((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_ttiitul0263ld t3;
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t3 + 0), t2);
    memmove(gm_out, ((uint8_t *)&t3), 56);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d0271rf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d0271rf F;
    void * gm_l0; /* l */
    gm_th2lqbo77pvsc gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_ppl3r45();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_e5frfkp(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_m55rf90(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_m55rf90 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_ab79bmt();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ocvi9r0(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 56), ((uint8_t *)a1), 56);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pmmlu39(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pmmlu39 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_hm6i8si();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_2m6a5d4(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 56), ((uint8_t *)a1), 56);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_q9fsb22(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_q9fsb22 F;
    void * gm_l0; /* l */
    gm_t3877pk7lv748 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_2g727l2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_p9v2h8s(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 24), ((uint8_t *)a1), 24);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2m6a5d4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2m6a5d4 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_e5frfkp(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_e5frfkp F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ocvi9r0(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ocvi9r0 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_p9v2h8s(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_p9v2h8s F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reserve_d1qo8mf F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l2 = t3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
    uint64_t t6 = gm_l2;
    uint64_t r7; bool o8 = __builtin_sub_overflow(t5, t6, &r7);
    if (o8) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 18, 8);
    uint64_t t9 = r7;
    uint64_t t10 = gm_l1;
    uint8_t t11 = (t9 >= t10);
    if (t11) {
        gm_tftb1odc739t5 t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
        ((uint8_t *)&t12)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t12 + 1), GM_ZERO(gm_b0_1));
        F.ret = t12;
        goto gm_fret;
    }
    void * t13 = gm_l0;
    uint64_t t14 = gm_l1;
    gm_b16_8 t15 = gm_f_empty_slice_ijtp9lo();
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
    gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t13, t14, t16);
    F.ret = t17;
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
inline __attribute__((aligned(4))) void gm_f_sort_by_55adsi9(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_55adsi9 F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = gm_l1;
    gm_f_introsort_shnrbt7(t2, t3);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_skeree7 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6gkpg0v), NULL, cx});
    gm_f_introsort_r6p6ckl(t2, t3);
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
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_members_mrojmjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_unique_members_mrojmjf F;
    gm_b16_8 gm_l0; /* names */
    void * gm_l1; /* taken */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    void * t3 = gm_l1;
    gm_b16_8 t4 = gm_f_unique_with_mffff7e((void *)NULL, cx, NULL, t2, t3, ((uint8_t)0));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_9mijhtc F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_1h8ac6i(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_1h8ac6i F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgjmsnv8ccaed));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(48));
    gm_tftb1odc739t5 t4 = gm_f_write_e6bqrb9(t2, &(*(gm_tftksr4op114e *)((uint8_t *)a1)), ((void *)&gm_tpljgv7fkm5ld25), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2h6ucjl(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_2h6ucjl F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 63u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s8oho6fnunkdm));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(13));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tpl3bnq2anocte9), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2hik1mg(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_2hik1mg F;
    void * gm_l0; /* l */
    gm_t3877pk7lv748 gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s82anase9fhnd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_tftb1odc739t5 t4 = gm_f_write_glpclq4(t2, &(*(gm_t3877pk7lv748 *)((uint8_t *)a1)), ((void *)&gm_tplennc1kv0529b), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3f2sd55(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_3f2sd55 F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5hiiva1kjs0f));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
    gm_tftb1odc739t5 t4 = gm_f_write_9bp3v08(t2, &(*(gm_tftksr4op114e *)((uint8_t *)a1)), ((void *)&gm_tplc70rik6173ge), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3mmgun5(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_3mmgun5 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_simqpapdqf8iv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_tftb1odc739t5 t4 = gm_f_write_av01663(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpldueenbh2sgd4), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_712qp7n(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_712qp7n F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7jvhfqq2rri8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_tftb1odc739t5 t4 = gm_f_write_0mj6rce(t2, &(*(gm_tud7oovfvnf43 *)((uint8_t *)a1)), ((void *)&gm_tpln2j7s58m8uan), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cscq5c8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_cscq5c8 F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf37jkpot59cu));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_tftb1odc739t5 t4 = gm_f_write_f98j2rj(t2, &(*(gm_thddbqb6snabm *)((uint8_t *)a1)), ((void *)&gm_tpludt9aee4n14r), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f1h65c6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_f1h65c6 F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srge6muj16eos));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
    gm_tftb1odc739t5 t4 = gm_f_write_s3v41br(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplgfhhtcka09o4), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fle5ipv(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_fle5ipv F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_slntkh69kcfmm));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(7));
    gm_tftb1odc739t5 t5 = gm_f_write_uqn5uk3(t2, t3, ((void *)&gm_tplec2d6tt24j0q), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_pecnce5(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_pecnce5 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s0u0p8jkqpsv1));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(7));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplnbsaptme3355), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r7au1ks(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_r7au1ks F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8ivf1umlv72j));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
    gm_tftb1odc739t5 t4 = gm_f_write_av01663(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpltc1l980uvcep), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_trmu0m5(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_trmu0m5 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s6kvnpuo6o71f));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(25));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplbeuo81m3df3c), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uf08lpv(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_uf08lpv F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sre51cnt7pim3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_tftb1odc739t5 t4 = gm_f_write_av01663(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpl00cra9g0k45s), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_of_gej99vn F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 174u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l3;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            void * t6 = (void *)(((uint8_t *)t5) + 8);
            gm_t88rcucg1b7uk t7 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l3, t6, GM_CONV(gm_csguankko4lmhd, t7));
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)17ULL));
            if (!t9) {
                goto gm_lb2;
            }
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
            gm_l2 = t10;
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, ((uint8_t *)t11));
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 1088));
            uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 1088) + 8));
            uint32_t t15 = gm_l2;
            uint64_t t16 = (uint64_t)t15;
            if ((uintptr_t)t16 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t16, t14, gm_srckfibkpt681aa, 130, 13);
            void * t17 = (void *)(((uint8_t *)t13) + (uintptr_t)t16 * 184);
            F.ret = t17;
            goto gm_fret;
        }
gm_lb2:;
        gm_trap("reached `@unreachable()`", gm_srckfibkpt681aa, 129, 42);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_assert_message_6bd9l6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_assert_message_6bd9l6u F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 293u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_trqfvmjtt4sij t14;
    gm_f_list_uvfu1k1((void *)&t14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t14;
    void * t15 = (void *)((uint8_t *)&F.l1);
    gm_tftb1odc739t5 t16 = gm_f_push_t83rg2m(t15, ((uint8_t)34ULL));
    gm_l5 = t16;
    uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srckfibkpt681aa, 734, 5);
    gm_b16_8 t18 = gm_l0;
    gm_l2 = t18;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t19 = gm_l3;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                void * t22 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t24 = gm_l3;
                {
                    {
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t22) + (uintptr_t)t24 * 1));
                        gm_l4 = t25;
                        {
                            {
                                {
                                    {
                                        uint8_t t26 = gm_l4;
                                        uint8_t t27 = ((t26 == ((uint8_t)34ULL)));
                                        if (!t27) {
                                            goto gm_lb9;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        uint8_t t28 = gm_l4;
                                        uint8_t t29 = ((t28 == ((uint8_t)92ULL)));
                                        if (!t29) {
                                            goto gm_lb10;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb10:;
                                    goto gm_lb7;
                                }
gm_lb8:;
                                void * t30 = (void *)((uint8_t *)&F.l1);
                                gm_tftb1odc739t5 t31 = gm_f_push_t83rg2m(t30, ((uint8_t)92ULL));
                                gm_l6 = t31;
                                uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                                if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srckfibkpt681aa, 738, 17);
                                void * t33 = (void *)((uint8_t *)&F.l1);
                                uint8_t t34 = gm_l4;
                                gm_tftb1odc739t5 t35 = gm_f_push_t83rg2m(t33, t34);
                                gm_l7 = t35;
                                uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                                if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srckfibkpt681aa, 739, 17);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t37 = gm_l4;
                                uint8_t t38 = (t37 >= ((uint8_t)32ULL));
                                if (!t38) {
                                    goto gm_lb11;
                                }
                                uint8_t t39 = (t37 <= ((uint8_t)126ULL));
                                if (!t39) {
                                    goto gm_lb11;
                                }
                                void * t40 = (void *)((uint8_t *)&F.l1);
                                uint8_t t41 = gm_l4;
                                gm_tftb1odc739t5 t42 = gm_f_push_t83rg2m(t40, t41);
                                gm_l8 = t42;
                                uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                                if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srckfibkpt681aa, 741, 26);
                                goto gm_lb6;
                            }
gm_lb11:;
                            {
                                goto gm_lb6;
                            }
gm_lb12:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 736, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 735, 5);
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
    void * t48 = (void *)((uint8_t *)&F.l1);
    gm_tftb1odc739t5 t49 = gm_f_push_t83rg2m(t48, ((uint8_t)34ULL));
    gm_l9 = t49;
    uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srckfibkpt681aa, 745, 5);
    gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
    F.ret = t52;
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
extern __attribute__((aligned(4))) uint32_t gm_f_bit_width_u86m8ir(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_blob_before_9p1c704(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_blob_before_9p1c704 F;
    void * gm_l0; /* x */
    void * gm_l1; /* y */
    uint8_t gm_l2; /* rx */
    uint8_t gm_l3; /* ry */
    gm_b16_8 gm_l4; /* a */
    gm_b16_8 gm_l5; /* b */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 442u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t9 = gm_l0;
    uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)t9));
    void * t11 = gm_l1;
    uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)t11));
    uint8_t t13 = (!(t10 == t12));
    if (t13) {
        void * t14 = gm_l0;
        uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)t14));
        void * t16 = gm_l1;
        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)t16));
        uint8_t t18 = (t15 < t17);
        F.ret = t18;
        goto gm_fret;
    }
    void * t19 = gm_l0;
    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t19) + 8));
    void * t21 = gm_l1;
    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 8));
    uint8_t t23 = (!(t20 == t22));
    if (t23) {
        void * t24 = gm_l0;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 8));
        void * t26 = gm_l1;
        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 8));
        uint8_t t28 = (t25 < t27);
        F.ret = t28;
        goto gm_fret;
    }
    void * t29 = gm_l0;
    uint8_t t30 = gm_f_kind_rank_g1kdlst(&(*(gm_tl572d7579hns *)(((uint8_t *)t29) + 16)));
    gm_l2 = t30;
    void * t31 = gm_l1;
    uint8_t t32 = gm_f_kind_rank_g1kdlst(&(*(gm_tl572d7579hns *)(((uint8_t *)t31) + 16)));
    gm_l3 = t32;
    uint8_t t33 = gm_l2;
    uint8_t t34 = gm_l3;
    uint8_t t35 = (!(t33 == t34));
    if (t35) {
        uint8_t t36 = gm_l2;
        uint8_t t37 = gm_l3;
        uint8_t t38 = (t36 < t37);
        F.ret = t38;
        goto gm_fret;
    }
    {
        {
            void * t39 = gm_l0;
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t39) + 16) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)3ULL));
            if (!t41) {
                goto gm_lb2;
            }
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((((uint8_t *)t39) + 16) + 8));
            gm_l4 = t42;
            void * t43 = gm_l1;
            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t43) + 16) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)3ULL));
            if (!t45) {
                goto gm_lb2;
            }
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((((uint8_t *)t43) + 16) + 8));
            gm_l5 = t46;
            gm_l7 = ((uint64_t)0ULL);
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint64_t t49 = (t47 < t48 ? t47 : t48);
            gm_l6 = t49;
            {
                for (;;) {
                    uint64_t t50 = gm_l7;
                    uint64_t t51 = gm_l6;
                    uint8_t t52 = (t50 < t51);
                    if (!t52) {
                        goto gm_lb3;
                    }
                    {
                        {
                            {
                                uint64_t t53 = gm_l7;
                                gm_l8 = t53;
                                void * t54 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                uint64_t t56 = gm_l8;
                                if ((uintptr_t)t56 >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)t56, t55, gm_srckfibkpt681aa, 684, 16);
                                uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t54) + (uintptr_t)t56 * 1));
                                void * t58 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                uint64_t t60 = gm_l8;
                                if ((uintptr_t)t60 >= (uintptr_t)(t59)) gm_trap_index((uintptr_t)t60, t59, gm_srckfibkpt681aa, 684, 28);
                                uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)t58) + (uintptr_t)t60 * 1));
                                uint8_t t62 = (!(t57 == t61));
                                if (t62) {
                                    void * t63 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                    uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                    uint64_t t65 = gm_l8;
                                    if ((uintptr_t)t65 >= (uintptr_t)(t64)) gm_trap_index((uintptr_t)t65, t64, gm_srckfibkpt681aa, 684, 42);
                                    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t63) + (uintptr_t)t65 * 1));
                                    F.ret = t66;
                                    goto gm_fret;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 683, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t67 = gm_l7;
                    uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                    uint64_t t70 = r68;
                    gm_l7 = t70;
                }
            }
gm_lb3:;
            uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t73 = (t71 < t72);
            F.ret = t73;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_blob_kind_kp13j9k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, uint64_t a3) {
    struct gm_fr_blob_kind_kp13j9k F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* s */
    uint64_t gm_l3; /* a */
    uint64_t gm_l4; /* pb */
    gm_b16_8 gm_l5; /* lanes */
    GM_STACK_CHECK(&F, 354u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    gm_th2lqbo77pvsc gm_l9;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
    gm_l4 = t5;
    void * t6 = gm_l0;
    uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 56));
    uint8_t t8 = (uint8_t)!t7;
    if (t8) {
        gm_l8 = ((uint8_t)1);
    } else {
        uint64_t t9 = gm_l3;
        uint64_t t10 = gm_l4;
        uint8_t t11 = (t9 < t10);
        gm_l8 = t11;
    }
    uint8_t t12 = gm_l8;
    if (t12) {
        gm_l7 = ((uint8_t)1);
    } else {
        uint64_t t13 = gm_l2;
        uint64_t t14 = gm_l4;
        uint8_t t15 = (t13 < t14);
        gm_l7 = t15;
    }
    uint8_t t16 = gm_l7;
    if (t16) {
        gm_l6 = ((uint8_t)1);
    } else {
        uint64_t t17 = gm_l2;
        uint64_t t18 = gm_l4;
        if (t18 == 0) gm_trap("division by zero", gm_srckfibkpt681aa, 701, 54);
        uint64_t t19 = (uint64_t)(t17 % t18);
        uint8_t t20 = (!(t19 == ((uint64_t)0ULL)));
        gm_l6 = t20;
    }
    uint8_t t21 = gm_l6;
    if (t21) {
        gm_tl572d7579hns t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t22 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t22 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
        memmove(gm_out, ((uint8_t *)&t22), 24);
        goto gm_fdone;
    }
    void * t23 = gm_l0;
    void * t24 = (void *)(((uint8_t *)t23) + 8);
    gm_t88rcucg1b7uk t25 = gm_l1;
    uint8_t t26 = gm_f_may_hold_pointers_iuorcah((void *)NULL, cx, NULL, t24, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t27 = (uint8_t)!t26;
    if (t27) {
        gm_tl572d7579hns t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t28 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t28 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)2ULL));
        memmove(gm_out, ((uint8_t *)&t28), 24);
        goto gm_fdone;
    }
    {
        {
            void * t29 = gm_l0;
            void * t30 = (void *)(((uint8_t *)t29) + 8);
            gm_t88rcucg1b7uk t31 = gm_l1;
            gm_f_pointer_words_ljph5no((void *)&gm_l9, (void *)NULL, cx, NULL, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t32 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
            if (!t32) {
                goto gm_lb2;
            }
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
            gm_l5 = t33;
            gm_b16_8 t34 = gm_l5;
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
            gm_tl572d7579hns t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)3ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t36 + 8), t35);
            memmove(gm_out, ((uint8_t *)&t36), 24);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_tl572d7579hns t37;
    GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t37 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t37 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)1ULL));
    memmove(gm_out, ((uint8_t *)&t37), 24);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_blob_name_sjpa1ub(void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1, const void * a2) {
    struct gm_fr_blob_name_sjpa1ub F;
    uint64_t gm_l0; /* size */
    uint64_t gm_l1; /* align */
    gm_tl572d7579hns gm_l2; /* kind */
    gm_b16_8 gm_l3; /* ptrs */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* p */
    GM_STACK_CHECK(&F, 594u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    uint8_t gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tabb710d9mq2e gm_l13;
    {
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)2ULL));
            if (!t12) {
                goto gm_lb2;
            }
            uint64_t t13 = gm_l0;
            uint64_t t14 = gm_l1;
            gm_t2lqu8jsj511d t15;
            GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t13);
            GM_ST(uint64_t, ((uint8_t *)&t15 + 8), t14);
            gm_f_format_ef6t3dr((void *)&gm_l9, (void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srckfibkpt681aa, 638, 15);
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            gm_l8 = t18;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)3ULL));
            if (!t20) {
                goto gm_lb3;
            }
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
            gm_l3 = t21;
            gm_trqfvmjtt4sij t22;
            gm_f_list_uvfu1k1((void *)&t22, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l4 = t22;
            gm_b16_8 t23 = gm_l3;
            gm_l5 = t23;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t24 = gm_l6;
                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t26 = (t24 < t25);
                    if (!t26) {
                        goto gm_lb4;
                    }
                    {
                        void * t27 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t29 = gm_l6;
                        {
                            {
                                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t27) + (uintptr_t)t29 * 1));
                                gm_l7 = t30;
                                void * t31 = (void *)((uint8_t *)&F.l4);
                                uint8_t t32 = gm_l7;
                                if (t32) {
                                    gm_l10 = ((uint8_t)112ULL);
                                } else {
                                    gm_l10 = ((uint8_t)119ULL);
                                }
                                uint8_t t33 = gm_l10;
                                gm_tftb1odc739t5 t34 = gm_f_push_t83rg2m(t31, t33);
                                gm_l11 = t34;
                                uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srckfibkpt681aa, 641, 25);
                                goto gm_lb7;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 641, 9);
                        }
gm_lb7:;
                    }
gm_lb6:;
                    uint64_t t36 = gm_l6;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l6 = t39;
                }
            }
gm_lb4:;
            uint64_t t40 = gm_l0;
            uint64_t t41 = gm_l1;
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
            gm_tbccvikg6b96e t43;
            GM_ST(uint64_t, ((uint8_t *)&t43 + 0), t40);
            GM_ST(uint64_t, ((uint8_t *)&t43 + 8), t41);
            GM_ST(gm_b16_8, ((uint8_t *)&t43 + 16), t42);
            gm_f_format_udu99n6((void *)&gm_l12, (void *)NULL, cx, NULL, &t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srckfibkpt681aa, 642, 9);
            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
            gm_l8 = t46;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint64_t t47 = gm_l0;
            uint64_t t48 = gm_l1;
            gm_t2lqu8jsj511d t49;
            GM_ST(uint64_t, ((uint8_t *)&t49 + 0), t47);
            GM_ST(uint64_t, ((uint8_t *)&t49 + 8), t48);
            gm_f_format_f6fn313((void *)&gm_l13, (void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srckfibkpt681aa, 644, 10);
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
            gm_l8 = t52;
            goto gm_lb1;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 637, 81);
    }
gm_lb1:;
    gm_b16_8 t53 = gm_l8;
    F.ret = t53;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedef_8ta3rbi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, gm_b16_8 a4) {
    struct gm_fr_blob_typedef_8ta3rbi F;
    void * gm_l0; /* ty */
    uint64_t gm_l1; /* s */
    uint64_t gm_l2; /* a */
    gm_tl572d7579hns gm_l3; /* kind */
    gm_b16_8 gm_l4; /* name */
    uint64_t gm_l5; /* pb */
    gm_b16_8 gm_l6; /* ptrs */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint8_t gm_l11; /* p */
    GM_STACK_CHECK(&F, 1058u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tf5srh4mp4b3i gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_b16_8 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tabb710d9mq2e gm_l20;
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
    gm_l5 = t13;
    {
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)0ULL));
            if (!t15) {
                goto gm_lb2;
            }
            uint64_t t16 = gm_l2;
            uint64_t t17 = gm_l1;
            gm_b16_8 t18 = gm_l4;
            gm_tbccvikg6b96e t19;
            GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t16);
            GM_ST(uint64_t, ((uint8_t *)&t19 + 8), t17);
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 16), t18);
            gm_f_format_a4r7k06((void *)&gm_l12, (void *)NULL, cx, NULL, &t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srckfibkpt681aa, 714, 26);
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
            F.ret = t22;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)1ULL));
            if (!t24) {
                goto gm_lb3;
            }
            uint64_t t25 = gm_l2;
            uint64_t t26 = gm_l1;
            uint64_t t27 = gm_l1;
            uint64_t t28 = gm_l5;
            if (t28 == 0) gm_trap("division by zero", gm_srckfibkpt681aa, 715, 123);
            uint64_t t29 = (uint64_t)(t27 / t28);
            gm_b16_8 t30 = gm_l4;
            gm_tbfdi36rjh0el t31;
            GM_ST(uint64_t, ((uint8_t *)&t31 + 0), t25);
            GM_ST(uint64_t, ((uint8_t *)&t31 + 8), t26);
            GM_ST(uint64_t, ((uint8_t *)&t31 + 16), t29);
            GM_ST(gm_b16_8, ((uint8_t *)&t31 + 24), t30);
            gm_f_format_c19sabv((void *)&gm_l13, (void *)NULL, cx, NULL, &t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srckfibkpt681aa, 715, 29);
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
            F.ret = t34;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)2ULL));
            if (!t36) {
                goto gm_lb4;
            }
            uint64_t t37 = gm_l2;
            uint64_t t38 = gm_l1;
            uint64_t t39 = gm_l5;
            if (t39 == 0) gm_trap("division by zero", gm_srckfibkpt681aa, 716, 103);
            uint64_t t40 = (uint64_t)(t38 / t39);
            gm_b16_8 t41 = gm_l4;
            gm_tbccvikg6b96e t42;
            GM_ST(uint64_t, ((uint8_t *)&t42 + 0), t37);
            GM_ST(uint64_t, ((uint8_t *)&t42 + 8), t40);
            GM_ST(gm_b16_8, ((uint8_t *)&t42 + 16), t41);
            gm_f_format_s9gvae7((void *)&gm_l14, (void *)NULL, cx, NULL, &t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srckfibkpt681aa, 716, 26);
            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
            F.ret = t45;
            goto gm_fret;
        }
gm_lb4:;
        {
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)3ULL));
            if (!t47) {
                goto gm_lb5;
            }
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)a3) + 8));
            gm_l6 = t48;
            gm_trqfvmjtt4sij t49;
            gm_f_list_uvfu1k1((void *)&t49, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l7 = t49;
            gm_b16_8 t50 = gm_l6;
            gm_l8 = t50;
            gm_l9 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t51 = gm_l9;
                    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint8_t t53 = (t51 < t52);
                    if (!t53) {
                        goto gm_lb6;
                    }
                    {
                        uint64_t t54 = gm_l9;
                        void * t55 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint64_t t57 = gm_l9;
                        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)t57 * 1));
                        gm_tf5srh4mp4b3i t59;
                        GM_ST(uint8_t, (uint8_t *)&t59 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t59 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t59 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t59 + 0), t54);
                        GM_ST(uint8_t, ((uint8_t *)&t59 + 8), t58);
                        gm_l15 = t59;
                        {
                            {
                                uint64_t t60 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                gm_l10 = t60;
                                uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 8));
                                gm_l11 = t61;
                                uint64_t t62 = gm_l10;
                                uint8_t t63 = (t62 > ((uint64_t)0ULL));
                                if (t63) {
                                    void * t64 = (void *)((uint8_t *)&F.l7);
                                    gm_tftb1odc739t5 t65 = gm_f_push_t83rg2m(t64, ((uint8_t)32ULL));
                                    gm_l16 = t65;
                                    uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                    if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srckfibkpt681aa, 720, 28);
                                }
                                uint64_t t67 = gm_l10;
                                uint8_t t68 = ((t67 == ((uint64_t)0ULL)));
                                if (t68) {
                                    void * t69 = (void *)((uint8_t *)&F.l7);
                                    uint64_t t70 = gm_l2;
                                    gm_t4ce0jd3tdro4 t71;
                                    GM_ST(uint64_t, ((uint8_t *)&t71 + 0), t70);
                                    gm_tftb1odc739t5 t72 = gm_f_write_2h6ucjl(t69, t71);
                                    gm_l17 = t72;
                                    uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                    if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srckfibkpt681aa, 721, 29);
                                }
                                void * t74 = (void *)((uint8_t *)&F.l7);
                                uint8_t t75 = gm_l11;
                                if (t75) {
                                    gm_b16_8 t76;
                                    GM_ST(void *, (uint8_t *)&t76, (void *)(gm_s44ek1gecbajs));
                                    GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(6));
                                    gm_l18 = t76;
                                } else {
                                    gm_b16_8 t77;
                                    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_siblunnp4nahm));
                                    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(9));
                                    gm_l18 = t77;
                                }
                                gm_b16_8 t78 = gm_l18;
                                uint64_t t79 = gm_l10;
                                gm_t3877pk7lv748 t80;
                                GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t78);
                                GM_ST(uint64_t, ((uint8_t *)&t80 + 16), t79);
                                gm_tftb1odc739t5 t81 = gm_f_write_2hik1mg(t74, &t80);
                                gm_l19 = t81;
                                uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srckfibkpt681aa, 722, 17);
                                goto gm_lb9;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 719, 13);
                        }
gm_lb9:;
                    }
gm_lb8:;
                    uint64_t t83 = gm_l9;
                    uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                    uint64_t t86 = r84;
                    gm_l9 = t86;
                }
            }
gm_lb6:;
            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
            gm_b16_8 t88 = gm_l4;
            gm_t6j6smj7qoctu t89;
            GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t87);
            GM_ST(gm_b16_8, ((uint8_t *)&t89 + 16), t88);
            gm_f_format_tjkla39((void *)&gm_l20, (void *)NULL, cx, NULL, &t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t90) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srckfibkpt681aa, 724, 20);
            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
            gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
            F.ret = t92;
            goto gm_fret;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 713, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_blob_typedefs_4nfn4l8(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_blob_typedefs_4nfn4l8 F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 424u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_trqfvmjtt4sij t7;
    gm_f_list_uvfu1k1((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t7;
    gm_trqfvmjtt4sij t8;
    gm_f_list_nvrrj0t((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t8;
    void * t9 = (void *)((uint8_t *)&F.l2);
    void * t10 = gm_l0;
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 80));
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
    gm_tftb1odc739t5 t13 = gm_f_extend_glhj3bs(t9, t12);
    gm_l6 = t13;
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srckfibkpt681aa, 469, 5);
    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_closure t16 = ((gm_closure){GM_PTR_TAG(((void *)gm_f_blob_before_9p1c704)), NULL, cx});
    gm_f_sort_by_55adsi9(t15, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_l3 = t17;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t18 = gm_l4;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t20 = (t18 < t19);
            if (!t20) {
                goto gm_lb1;
            }
            {
                void * t21 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t23 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t21) + (uintptr_t)t23 * 56), 56);
                        void * t24 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                        gm_tftb1odc739t5 t26 = gm_f_extend_0cs85gs(t24, t25);
                        gm_l7 = t26;
                        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srckfibkpt681aa, 471, 29);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 471, 5);
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
    void * t32 = (void *)((uint8_t *)&F.l1);
    void * t33 = gm_l0;
    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t33) + 560));
    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
    gm_tftb1odc739t5 t36 = gm_f_extend_0cs85gs(t32, t35);
    gm_l8 = t36;
    uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srckfibkpt681aa, 472, 5);
    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
    F.ret = t39;
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_by_ref_fi0o860 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t5v8n6qrn4rh4 t4;
    gm_f_repr_53quqki((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    uint8_t t6 = (uint8_t)((t5 == ((uint8_t)6ULL)));
    gm_t5v8n6qrn4rh4 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)6ULL));
    if (t6) {
        void * t8 = gm_l0;
        gm_t88rcucg1b7uk t9 = gm_l1;
        uint64_t t10 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = (t10 > ((uint64_t)16ULL));
        gm_l2 = t11;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t12 = gm_l2;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_c_string_n7h27b3 F;
    gm_b16_8 gm_l0; /* bytes */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* b */
    GM_STACK_CHECK(&F, 370u);
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
    gm_tftb1odc739t5 gm_l11;
    gm_trqfvmjtt4sij t12;
    gm_f_list_uvfu1k1((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t12;
    void * t13 = (void *)((uint8_t *)&F.l1);
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)2ULL), &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_srckfibkpt681aa, 752, 17);
    uint64_t t17 = r15;
    gm_tftb1odc739t5 t18 = gm_f_reserve_d1qo8mf(t13, t17);
    gm_l5 = t18;
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srckfibkpt681aa, 752, 5);
    void * t20 = (void *)((uint8_t *)&F.l1);
    gm_tftb1odc739t5 t21 = gm_f_push_t83rg2m(t20, ((uint8_t)34ULL));
    gm_l6 = t21;
    uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srckfibkpt681aa, 753, 5);
    gm_b16_8 t23 = gm_l0;
    gm_l2 = t23;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t24 = gm_l3;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t26 = (t24 < t25);
            if (!t26) {
                goto gm_lb1;
            }
            {
                void * t27 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t29 = gm_l3;
                {
                    {
                        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t27) + (uintptr_t)t29 * 1));
                        gm_l4 = t30;
                        {
                            {
                                uint8_t t31 = gm_l4;
                                uint8_t t32 = ((t31 == ((uint8_t)34ULL)));
                                if (!t32) {
                                    goto gm_lb7;
                                }
                                void * t33 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t34;
                                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_smee1janbl2t9));
                                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t35 = gm_f_extend_0cs85gs(t33, t34);
                                gm_l7 = t35;
                                uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                                if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srckfibkpt681aa, 756, 20);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t37 = gm_l4;
                                uint8_t t38 = ((t37 == ((uint8_t)92ULL)));
                                if (!t38) {
                                    goto gm_lb8;
                                }
                                void * t39 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t40;
                                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s3p8gug7bvl1p));
                                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t41 = gm_f_extend_0cs85gs(t39, t40);
                                gm_l8 = t41;
                                uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                                if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srckfibkpt681aa, 757, 21);
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t43 = gm_l4;
                                uint8_t t44 = (t43 >= ((uint8_t)32ULL));
                                if (!t44) {
                                    goto gm_lb9;
                                }
                                uint8_t t45 = (t43 <= ((uint8_t)126ULL));
                                if (!t45) {
                                    goto gm_lb9;
                                }
                                uint8_t t46 = gm_l4;
                                uint8_t t47 = (!(t46 == ((uint8_t)63ULL)));
                                if (!t47) {
                                    goto gm_lb9;
                                }
                                void * t48 = (void *)((uint8_t *)&F.l1);
                                uint8_t t49 = gm_l4;
                                gm_tftb1odc739t5 t50 = gm_f_push_t83rg2m(t48, t49);
                                gm_l9 = t50;
                                uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srckfibkpt681aa, 758, 44);
                                goto gm_lb6;
                            }
gm_lb9:;
                            {
                                void * t52 = (void *)((uint8_t *)&F.l1);
                                uint8_t t53 = gm_l4;
                                gm_t9qkh0rsooc8d t54;
                                GM_ST(uint8_t, ((uint8_t *)&t54 + 0), t53);
                                gm_tftb1odc739t5 t55 = gm_f_write_fle5ipv(t52, t54);
                                gm_l10 = t55;
                                uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srckfibkpt681aa, 759, 18);
                                goto gm_lb6;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 755, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 754, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t57 = gm_l3;
            uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
            uint64_t t60 = r58;
            gm_l3 = t60;
        }
    }
gm_lb1:;
    void * t61 = (void *)((uint8_t *)&F.l1);
    gm_tftb1odc739t5 t62 = gm_f_push_t83rg2m(t61, ((uint8_t)34ULL));
    gm_l11 = t62;
    uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srckfibkpt681aa, 762, 5);
    gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_cty_66ee0kf F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* c */
    gm_b16_8 gm_l4; /* c */
    GM_STACK_CHECK(&F, 372u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l5;
    uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint64_t t8 = (uint64_t)t7;
    gm_l2 = t8;
    uint64_t t9 = gm_l2;
    void * t10 = gm_l0;
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t10) + 464) + 8));
    uint8_t t12 = (t9 < t11);
    if (t12) {
        {
            {
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, (((uint8_t *)t13) + 464));
                uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 464) + 8));
                uint64_t t16 = gm_l2;
                if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srckfibkpt681aa, 160, 17);
                uint8_t t17 = (uint8_t)(((((uint8_t *)t14) + (uintptr_t)t16 * 24))[16] != 0);
                if (!t17) {
                    goto gm_lb2;
                }
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 24));
                gm_l3 = t18;
                gm_b16_8 t19 = gm_l3;
                F.ret = t19;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    void * t20 = gm_l0;
    gm_t88rcucg1b7uk t21 = gm_l1;
    gm_b16_8 t22 = gm_f_make_cty_3kvgdd5((void *)NULL, cx, NULL, t20, t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t22;
    {
        for (;;) {
            void * t23 = gm_l0;
            uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t23) + 464) + 8));
            uint64_t t25 = gm_l2;
            uint8_t t26 = (t24 <= t25);
            if (!t26) {
                goto gm_lb3;
            }
            {
                void * t27 = gm_l0;
                void * t28 = (void *)(((uint8_t *)t27) + 464);
                gm_th2lqbo77pvsc t29;
                memset(&t29, 0, sizeof t29);
                gm_tftb1odc739t5 t30 = gm_f_push_d0271rf(t28, &t29);
                gm_l5 = t30;
                uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srckfibkpt681aa, 163, 41);
            }
gm_lb5:;
        }
    }
gm_lb3:;
    void * t32 = gm_l0;
    void * t33 = GM_LD(void *, (((uint8_t *)t32) + 464));
    uint64_t t34 = GM_LD(uint64_t, ((((uint8_t *)t32) + 464) + 8));
    uint64_t t35 = gm_l2;
    if ((uintptr_t)t35 >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)t35, t34, gm_srckfibkpt681aa, 164, 5);
    gm_b16_8 t36 = gm_l4;
    gm_th2lqbo77pvsc t37;
    GM_ST(uint8_t, (uint8_t *)&t37 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t37 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t37 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t37), t36);
    ((uint8_t *)&t37)[16] = 1;
    GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)t33) + (uintptr_t)t35 * 24), t37);
    gm_b16_8 t38 = gm_l4;
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
inline __attribute__((aligned(4))) void gm_f_ctypes_j6au0st(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_ctypes_j6au0st F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 2264u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    gm_t4grvdhp6dkf1 t4;
    gm_f_layouts_au8nfbt((void *)&t4, t3, &(*(gm_t0rfe1ou14h21 *)((uint8_t *)&F.l1)));
    gm_trqfvmjtt4sij t5;
    gm_f_list_nvrrj0t((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t6;
    gm_f_hash_map_cuh4fs9((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t7;
    gm_f_hash_map_tl7apid((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_ttiitul0263ld t8;
    gm_f_new_namer_5q88o9g((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t9;
    gm_f_list_uvfu1k1((void *)&t9, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t10;
    gm_f_list_uvfu1k1((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t11;
    gm_f_list_uvfu1k1((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t12;
    gm_f_hash_map_1s9eugt((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t13;
    gm_f_list_0c2v515((void *)&t13, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t14;
    gm_f_hash_map_p3t5tj8((void *)&t14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t15;
    gm_f_list_uvfu1k1((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t16;
    gm_f_hash_map_1s9eugt((void *)&t16, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t17;
    gm_f_hash_map_p3t5tj8((void *)&t17, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t18;
    gm_f_list_uvfu1k1((void *)&t18, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t19;
    gm_f_hash_map_cuh4fs9((void *)&t19, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t20;
    gm_f_hash_map_cuh4fs9((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbpdf5ej15iof t21;
    gm_f_views_bc1b3r4((void *)&t21, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trg1s7erfs461 t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 809, 0);
    GM_ST(uint16_t, (uint8_t *)&t22 + 810, 0);
    GM_ST(uint32_t, (uint8_t *)&t22 + 812, 0);
    GM_ST(void *, ((uint8_t *)&t22 + 0), t2);
    GM_ST(gm_t4grvdhp6dkf1, ((uint8_t *)&t22 + 8), t4);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t22 + 80), t5);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t22 + 120), t6);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t22 + 176), t7);
    GM_ST(gm_ttiitul0263ld, ((uint8_t *)&t22 + 232), t8);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t22 + 288), t9);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t22 + 328), t10);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t22 + 368), t11);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t22 + 408), t12);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t22 + 464), t13);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t22 + 504), t14);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t22 + 560), t15);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t22 + 600), t16);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t22 + 656), t17);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t22 + 712), t18);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t22 + 752), t19);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 808), ((uint8_t)0));
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t22 + 816), t20);
    GM_ST(gm_tbpdf5ej15iof, ((uint8_t *)&t22 + 872), t21);
    memmove(gm_out, ((uint8_t *)&t22), 1128);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_define_typed_m3mqpm3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4) {
    struct gm_fr_define_typed_m3mqpm3 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* members */
    gm_b16_8 gm_l3; /* named */
    uint64_t gm_l4; /* natural */
    uint64_t gm_l5; /* size */
    uint64_t gm_l6; /* al */
    gm_b16_8 gm_l7; /* attr */
    gm_b16_8 gm_l8; /* shape */
    gm_b16_8 gm_l9; /* n */
    gm_b16_8 gm_l10; /* name */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t3877pk7lv748 gm_l13; /* m */
    GM_STACK_CHECK(&F, 1311u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_b16_8 gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_th2lqbo77pvsc gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    void * t9 = gm_l0;
    gm_t88rcucg1b7uk t10 = gm_l1;
    uint64_t t11 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t11;
    void * t12 = gm_l0;
    gm_t88rcucg1b7uk t13 = gm_l1;
    uint64_t t14 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t14;
    uint64_t t15 = gm_l6;
    uint64_t t16 = gm_l4;
    uint8_t t17 = (t15 > t16);
    if (t17) {
        uint64_t t18 = gm_l6;
        gm_t4ce0jd3tdro4 t19;
        GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t18);
        gm_f_format_qc4s0m5((void *)&gm_l15, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srckfibkpt681aa, 313, 38);
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_l14 = t22;
    } else {
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(0));
        gm_l14 = t23;
    }
    gm_b16_8 t24 = gm_l14;
    gm_l7 = t24;
    gm_b16_8 t25 = gm_l7;
    gm_b16_8 t26 = gm_l2;
    gm_t6j6smj7qoctu t27;
    GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t25);
    GM_ST(gm_b16_8, ((uint8_t *)&t27 + 16), t26);
    gm_f_format_8n626pi((void *)&gm_l16, (void *)NULL, cx, NULL, &t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srckfibkpt681aa, 314, 14);
    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
    gm_l8 = t29;
    {
        {
            void * t30 = gm_l0;
            void * t31 = (void *)(((uint8_t *)t30) + 504);
            gm_b16_8 t32 = gm_l8;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            gm_f_get_e3joqcc((void *)&gm_l17, t31, t33);
            uint8_t t34 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
            if (!t34) {
                goto gm_lb2;
            }
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
            gm_l9 = t35;
            gm_b16_8 t36 = gm_l9;
            F.ret = t36;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t37;
    GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sf1p9u1vovjso));
    GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(4));
    gm_b16_8 t38 = gm_l8;
    gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
    gm_b16_8 t40 = gm_f_content_name_83mn5uf((void *)NULL, cx, NULL, t37, t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t40;
    void * t41 = gm_l0;
    void * t42 = (void *)(((uint8_t *)t41) + 560);
    gm_b16_8 t43 = gm_l7;
    gm_b16_8 t44 = gm_l2;
    gm_b16_8 t45 = gm_l10;
    gm_tfeab279c16rr t46;
    GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t43);
    GM_ST(gm_b16_8, ((uint8_t *)&t46 + 16), t44);
    GM_ST(gm_b16_8, ((uint8_t *)&t46 + 32), t45);
    gm_tftb1odc739t5 t47 = gm_f_write_f1h65c6(t42, &t46);
    gm_l18 = t47;
    uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srckfibkpt681aa, 317, 5);
    void * t49 = gm_l0;
    void * t50 = (void *)(((uint8_t *)t49) + 560);
    gm_b16_8 t51 = gm_l10;
    uint64_t t52 = gm_l5;
    gm_b16_8 t53 = gm_l10;
    uint64_t t54 = gm_l6;
    gm_thddbqb6snabm t55;
    GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t51);
    GM_ST(uint64_t, ((uint8_t *)&t55 + 16), t52);
    GM_ST(gm_b16_8, ((uint8_t *)&t55 + 24), t53);
    GM_ST(uint64_t, ((uint8_t *)&t55 + 40), t54);
    gm_tftb1odc739t5 t56 = gm_f_write_cscq5c8(t50, &t55);
    gm_l19 = t56;
    uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
    if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srckfibkpt681aa, 318, 5);
    gm_b16_8 t58 = gm_l3;
    gm_l11 = t58;
    gm_l12 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t59 = gm_l12;
            uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
            uint8_t t61 = (t59 < t60);
            if (!t61) {
                goto gm_lb3;
            }
            {
                void * t62 = GM_LD(void *, ((uint8_t *)&gm_l11));
                uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint64_t t64 = gm_l12;
                {
                    {
                        memmove(((uint8_t *)&gm_l13), (((uint8_t *)t62) + (uintptr_t)t64 * 24), 24);
                        void * t65 = gm_l0;
                        void * t66 = (void *)(((uint8_t *)t65) + 560);
                        gm_b16_8 t67 = gm_l10;
                        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                        uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 16));
                        gm_tftksr4op114e t70;
                        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t67);
                        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 16), t68);
                        GM_ST(uint64_t, ((uint8_t *)&t70 + 32), t69);
                        gm_tftb1odc739t5 t71 = gm_f_write_3f2sd55(t66, &t70);
                        gm_l20 = t71;
                        uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                        if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srckfibkpt681aa, 319, 22);
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 319, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t73 = gm_l12;
            uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
            uint64_t t76 = r74;
            gm_l12 = t76;
        }
    }
gm_lb3:;
    void * t77 = gm_l0;
    void * t78 = (void *)(((uint8_t *)t77) + 560);
    void * t79 = gm_l0;
    gm_t88rcucg1b7uk t80 = gm_l1;
    gm_b16_8 t81 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t79, t80);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t82;
    GM_ST(gm_b16_8, ((uint8_t *)&t82 + 0), t81);
    gm_f_format_1cgbgnn((void *)&gm_l21, (void *)NULL, cx, NULL, t82);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
    if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_srckfibkpt681aa, 320, 52);
    gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
    gm_b16_8 t86 = gm_f_assert_message_6bd9l6u((void *)NULL, cx, NULL, t85);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t87;
    GM_ST(gm_b16_8, ((uint8_t *)&t87 + 0), t86);
    gm_tftb1odc739t5 t88 = gm_f_write_pecnce5(t78, t87);
    gm_l22 = t88;
    uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
    if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srckfibkpt681aa, 320, 5);
    void * t90 = gm_l0;
    void * t91 = (void *)(((uint8_t *)t90) + 504);
    gm_b16_8 t92 = gm_l8;
    gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
    gm_b16_8 t94 = gm_l10;
    gm_tftb1odc739t5 t95 = gm_f_put_7shfu9u(t91, t93, t94);
    gm_l23 = t95;
    uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srckfibkpt681aa, 321, 5);
    gm_b16_8 t97 = gm_l10;
    F.ret = t97;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_desc_0cag2kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_desc_0cag2kq F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* i */
    gm_b16_8 gm_l3; /* i */
    uint8_t gm_l5; /* signed */
    uint16_t gm_l6; /* bits */
    uint16_t gm_l7; /* bits */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    gm_b16_8 gm_l9; /* e */
    uint64_t gm_l10; /* len */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    gm_b16_8 gm_l12; /* e */
    gm_b16_8 gm_l13; /* elems */
    gm_b16_8 gm_l14; /* names */
    gm_b16_8 gm_l15; /* offs */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* k */
    gm_t88rcucg1b7uk gm_l20; /* et */
    gm_b16_8 gm_l21; /* d */
    gm_b16_8 gm_l22; /* name */
    gm_t88rcucg1b7uk gm_l23; /* inner */
    gm_b16_8 gm_l24; /* e */
    gm_t88rcucg1b7uk gm_l25; /* err */
    gm_t88rcucg1b7uk gm_l26; /* ok */
    gm_b16_8 gm_l27; /* e */
    gm_b16_8 gm_l28; /* o */
    gm_b16_8 gm_l29; /* offs */
    uint32_t gm_l30; /* id */
    void * gm_l31; /* def */
    gm_b16_8 gm_l33; /* offs */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* k */
    gm_b16_8 gm_l39; /* d */
    gm_tcmkompbl09il gm_l40; /* bl */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    uint64_t gm_l44; /* k */
    gm_tov4mtrfp5e7v gm_l46; /* bf */
    gm_b16_8 gm_l47; /* d */
    gm_t88rcucg1b7uk gm_l48; /* tag */
    gm_b16_8 gm_l49; /* variants */
    gm_b16_8 gm_l50; /* offs */
    gm_b16_8 gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    gm_t88rcucg1b7uk gm_l55; /* pt */
    gm_b16_8 gm_l56; /* p */
    gm_b16_8 gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    gm_b16_8 gm_l60; /* x */
    GM_STACK_CHECK(&F, 7814u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l61;
    gm_tabb710d9mq2e gm_l62;
    gm_tftb1odc739t5 gm_l63;
    gm_tftb1odc739t5 gm_l64;
    gm_tabb710d9mq2e gm_l65;
    gm_tftb1odc739t5 gm_l66;
    gm_tabb710d9mq2e gm_l67;
    gm_tftb1odc739t5 gm_l68;
    gm_t53at80pfdus3 gm_l69;
    gm_tftb1odc739t5 gm_l70;
    gm_tabb710d9mq2e gm_l71;
    gm_tftb1odc739t5 gm_l72;
    gm_tabb710d9mq2e gm_l73;
    gm_tftb1odc739t5 gm_l74;
    gm_tftb1odc739t5 gm_l75;
    gm_tabb710d9mq2e gm_l76;
    gm_tftb1odc739t5 gm_l77;
    gm_tabb710d9mq2e gm_l78;
    gm_tftb1odc739t5 gm_l79;
    gm_tftb1odc739t5 gm_l80;
    gm_tabb710d9mq2e gm_l81;
    gm_tftb1odc739t5 gm_l82;
    gm_tftb1odc739t5 gm_l83;
    gm_tftb1odc739t5 gm_l84;
    gm_tftb1odc739t5 gm_l85;
    gm_tabb710d9mq2e gm_l86;
    gm_tftb1odc739t5 gm_l87;
    gm_tftb1odc739t5 gm_l88;
    gm_tabb710d9mq2e gm_l89;
    gm_tftb1odc739t5 gm_l90;
    gm_tabb710d9mq2e gm_l91;
    gm_tftb1odc739t5 gm_l92;
    gm_tlcm4lvu3g1a8 gm_l93;
    gm_b16_8 gm_l94;
    gm_tabb710d9mq2e gm_l95;
    gm_tftb1odc739t5 gm_l96;
    gm_tftb1odc739t5 gm_l97;
    gm_tabb710d9mq2e gm_l98;
    gm_tftb1odc739t5 gm_l99;
    gm_tftb1odc739t5 gm_l100;
    gm_tabb710d9mq2e gm_l101;
    gm_tftb1odc739t5 gm_l102;
    gm_tftb1odc739t5 gm_l103;
    gm_b16_8 gm_l104;
    gm_tabb710d9mq2e gm_l105;
    gm_tftb1odc739t5 gm_l106;
    gm_tftb1odc739t5 gm_l107;
    gm_tabb710d9mq2e gm_l108;
    gm_tftb1odc739t5 gm_l109;
    gm_tabb710d9mq2e gm_l110;
    gm_tftb1odc739t5 gm_l111;
    gm_tabb710d9mq2e gm_l112;
    gm_tftb1odc739t5 gm_l113;
    gm_tabb710d9mq2e gm_l114;
    gm_tftb1odc739t5 gm_l115;
    gm_tabb710d9mq2e gm_l116;
    gm_tftb1odc739t5 gm_l117;
    uint8_t gm_l118;
    gm_t8ta482bl6be0 gm_l119;
    gm_tabb710d9mq2e gm_l120;
    gm_tftb1odc739t5 gm_l121;
    gm_tftb1odc739t5 gm_l122;
    gm_t57euh612ligr gm_l123;
    gm_t8ta482bl6be0 gm_l124;
    gm_tabb710d9mq2e gm_l125;
    gm_tftb1odc739t5 gm_l126;
    gm_tftb1odc739t5 gm_l127;
    gm_tabb710d9mq2e gm_l128;
    gm_tftb1odc739t5 gm_l129;
    gm_b16_8 gm_l130;
    gm_tabb710d9mq2e gm_l131;
    gm_tftb1odc739t5 gm_l132;
    gm_tabb710d9mq2e gm_l133;
    gm_tftb1odc739t5 gm_l134;
    gm_tftb1odc739t5 gm_l135;
    gm_tabb710d9mq2e gm_l136;
    gm_tftb1odc739t5 gm_l137;
    gm_tabb710d9mq2e gm_l138;
    gm_tftb1odc739t5 gm_l139;
    gm_tabb710d9mq2e gm_l140;
    gm_tftb1odc739t5 gm_l141;
    uint64_t gm_l142;
    gm_tabb710d9mq2e gm_l143;
    gm_tftb1odc739t5 gm_l144;
    gm_tabb710d9mq2e gm_l145;
    gm_tftb1odc739t5 gm_l146;
    gm_tabb710d9mq2e gm_l147;
    gm_tftb1odc739t5 gm_l148;
    gm_tabb710d9mq2e gm_l149;
    gm_tftb1odc739t5 gm_l150;
    gm_tftb1odc739t5 gm_l151;
    gm_tabb710d9mq2e gm_l152;
    gm_tabb710d9mq2e gm_l153;
    gm_tftb1odc739t5 gm_l154;
    gm_tftb1odc739t5 gm_l155;
    gm_tabb710d9mq2e gm_l156;
    gm_tabb710d9mq2e gm_l157;
    gm_tftb1odc739t5 gm_l158;
    gm_b16_8 gm_l159;
    gm_tabb710d9mq2e gm_l160;
    gm_tftb1odc739t5 gm_l161;
    gm_tabb710d9mq2e gm_l162;
    gm_tftb1odc739t5 gm_l163;
    gm_tabb710d9mq2e gm_l164;
    {
        {
            {
                void * t59 = gm_l0;
                void * t60 = (void *)(((uint8_t *)t59) + 176);
                uint32_t t61 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
                gm_f_get_gnmase9((void *)&gm_l61, t60, t61);
                uint8_t t62 = (uint8_t)((((uint8_t *)&gm_l61))[16] != 0);
                if (!t62) {
                    goto gm_lb2;
                }
                gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l61));
                gm_l2 = t63;
                gm_b16_8 t64 = gm_l2;
                gm_tnpifv20irnph t65;
                GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t64);
                gm_f_format_h6b2ofo((void *)&gm_l62, (void *)NULL, cx, NULL, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 8, gm_srckfibkpt681aa, 482, 44);
                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l62) + 8));
                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                F.ret = t68;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t69 = gm_l0;
        void * t70 = (void *)(((uint8_t *)t69) + 232);
        gm_b16_8 t71;
        GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sm6ioruobq064));
        GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(5));
        void * t72 = gm_l0;
        gm_t88rcucg1b7uk t73 = gm_l1;
        gm_b16_8 t74 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t72, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t75 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t70, t71, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t75;
        void * t76 = gm_l0;
        void * t77 = (void *)(((uint8_t *)t76) + 176);
        uint32_t t78 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
        gm_b16_8 t79 = gm_l3;
        gm_tftb1odc739t5 t80 = gm_f_put_po09vud(t77, t78, t79);
        gm_l63 = t80;
        uint8_t t81 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
        if (!t81) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 1, gm_srckfibkpt681aa, 484, 5);
        void * t82 = gm_l0;
        void * t83 = (void *)(((uint8_t *)t82) + 288);
        gm_b16_8 t84 = gm_l3;
        gm_tnpifv20irnph t85;
        GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t84);
        gm_tftb1odc739t5 t86 = gm_f_write_trmu0m5(t83, t85);
        gm_l64 = t86;
        uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
        if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_srckfibkpt681aa, 485, 5);
        gm_trqfvmjtt4sij t88;
        gm_f_list_epvducg((void *)&t88, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t88;
        void * t89 = (void *)((uint8_t *)&F.l4);
        void * t90 = gm_l0;
        gm_t88rcucg1b7uk t91 = gm_l1;
        uint64_t t92 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t90, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4ce0jd3tdro4 t93;
        GM_ST(uint64_t, ((uint8_t *)&t93 + 0), t92);
        gm_f_format_jpvhgp3((void *)&gm_l65, (void *)NULL, cx, NULL, t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
        if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_srckfibkpt681aa, 487, 12);
        gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
        gm_tftb1odc739t5 t97 = gm_f_push_3ef746b(t89, t96);
        gm_l66 = t97;
        uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
        if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_srckfibkpt681aa, 487, 5);
        void * t99 = (void *)((uint8_t *)&F.l4);
        void * t100 = gm_l0;
        gm_t88rcucg1b7uk t101 = gm_l1;
        uint64_t t102 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t100, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4ce0jd3tdro4 t103;
        GM_ST(uint64_t, ((uint8_t *)&t103 + 0), t102);
        gm_f_format_ef193cn((void *)&gm_l67, (void *)NULL, cx, NULL, t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t104 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
        if (!t104) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_srckfibkpt681aa, 488, 12);
        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
        gm_tftb1odc739t5 t107 = gm_f_push_3ef746b(t99, t106);
        gm_l68 = t107;
    }
    {
        uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
        if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_srckfibkpt681aa, 488, 5);
        void * t109 = gm_l0;
        gm_t88rcucg1b7uk t110 = gm_l1;
        gm_f_kind_ghvav22((void *)&gm_l69, t109, GM_CONV(gm_csguankko4lmhd, t110));
        {
            {
                {
                    uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                    uint8_t t112 = (uint8_t)(t111 == ((uint8_t)0ULL));
                    if (!t112) {
                        goto gm_lb4;
                    }
                    uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 8));
                    gm_l5 = t113;
                    uint16_t t114 = GM_LD(uint16_t, ((((uint8_t *)&gm_l69) + 8) + 2));
                    gm_l6 = t114;
                    void * t115 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t116;
                    GM_ST(void *, (uint8_t *)&t116, (void *)(gm_sropo1941pvlt));
                    GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(15));
                    gm_tftb1odc739t5 t117 = gm_f_push_3ef746b(t115, t116);
                    gm_l70 = t117;
                    uint8_t t118 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                    if (!t118) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 1, gm_srckfibkpt681aa, 491, 13);
                    void * t119 = (void *)((uint8_t *)&F.l4);
                    uint16_t t120 = gm_l6;
                    gm_tu0pangbbaeec t121;
                    GM_ST(uint16_t, ((uint8_t *)&t121 + 0), t120);
                    gm_f_format_ms4f9aj((void *)&gm_l71, (void *)NULL, cx, NULL, t121);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                    if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 8, gm_srckfibkpt681aa, 492, 20);
                    gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 8));
                    gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
                    gm_tftb1odc739t5 t125 = gm_f_push_3ef746b(t119, t124);
                    gm_l72 = t125;
                    uint8_t t126 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                    if (!t126) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 1, gm_srckfibkpt681aa, 492, 13);
                    void * t127 = (void *)((uint8_t *)&F.l4);
                    uint8_t t128 = gm_l5;
                    gm_t9qkh0rsooc8d t129;
                    GM_ST(uint8_t, ((uint8_t *)&t129 + 0), t128);
                    gm_f_format_lkeo172((void *)&gm_l73, (void *)NULL, cx, NULL, t129);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                    if (!t130) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 8, gm_srckfibkpt681aa, 493, 20);
                    gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l73) + 8));
                    gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
                    gm_tftb1odc739t5 t133 = gm_f_push_3ef746b(t127, t132);
                    gm_l74 = t133;
                    uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                    if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 1, gm_srckfibkpt681aa, 493, 13);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    {
                        {
                            uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                            uint8_t t136 = (uint8_t)(t135 == ((uint8_t)1ULL));
                            if (!t136) {
                                goto gm_lb7;
                            }
                            goto gm_lb6;
                        }
gm_lb7:;
                        {
                            uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                            uint8_t t138 = (uint8_t)(t137 == ((uint8_t)2ULL));
                            if (!t138) {
                                goto gm_lb8;
                            }
                            goto gm_lb6;
                        }
gm_lb8:;
                        goto gm_lb5;
                    }
gm_lb6:;
                    void * t139 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t140;
                    GM_ST(void *, (uint8_t *)&t140, (void *)(gm_sropo1941pvlt));
                    GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(15));
                    gm_tftb1odc739t5 t141 = gm_f_push_3ef746b(t139, t140);
                    gm_l75 = t141;
                    uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                    if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 1, gm_srckfibkpt681aa, 496, 13);
                    void * t143 = (void *)((uint8_t *)&F.l4);
                    void * t144 = gm_l0;
                    void * t145 = (void *)(((uint8_t *)t144) + 8);
                    uint32_t t146 = gm_f_ptr_bits_glps5i7(t145);
                    gm_t88rcucg1b7uk t147;
                    GM_ST(uint32_t, ((uint8_t *)&t147 + 0), t146);
                    gm_f_format_ch41dev((void *)&gm_l76, (void *)NULL, cx, NULL, t147);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t148 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                    if (!t148) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 8, gm_srckfibkpt681aa, 497, 20);
                    gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l76) + 8));
                    gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                    gm_tftb1odc739t5 t151 = gm_f_push_3ef746b(t143, t150);
                    gm_l77 = t151;
                    uint8_t t152 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                    if (!t152) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 1, gm_srckfibkpt681aa, 497, 13);
                    void * t153 = (void *)((uint8_t *)&F.l4);
                    void * t154 = gm_l0;
                    gm_t88rcucg1b7uk t155 = gm_l1;
                    gm_t53at80pfdus3 t156;
                    gm_f_kind_ghvav22((void *)&t156, t154, GM_CONV(gm_csguankko4lmhd, t155));
                    uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&t156) + 0));
                    uint8_t t158 = (uint8_t)((t157 == ((uint8_t)2ULL)));
                    gm_t9qkh0rsooc8d t159;
                    GM_ST(uint8_t, ((uint8_t *)&t159 + 0), t158);
                    gm_t53at80pfdus3 t160;
                    memset(&t160, 0, sizeof t160);
                    GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)2ULL));
                    gm_f_format_lkeo172((void *)&gm_l78, (void *)NULL, cx, NULL, t159);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t161 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                    if (!t161) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 8, gm_srckfibkpt681aa, 498, 20);
                    gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l78) + 8));
                    gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
                    gm_tftb1odc739t5 t164 = gm_f_push_3ef746b(t153, t163);
                    gm_l79 = t164;
                    uint8_t t165 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                    if (!t165) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 1, gm_srckfibkpt681aa, 498, 13);
                    goto gm_lb3;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                    uint8_t t167 = (uint8_t)(t166 == ((uint8_t)3ULL));
                    if (!t167) {
                        goto gm_lb9;
                    }
                    uint16_t t168 = GM_LD(uint16_t, (((uint8_t *)&gm_l69) + 8));
                    gm_l7 = t168;
                    void * t169 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t170;
                    GM_ST(void *, (uint8_t *)&t170, (void *)(gm_st53sho4849cg));
                    GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(17));
                    gm_tftb1odc739t5 t171 = gm_f_push_3ef746b(t169, t170);
                    gm_l80 = t171;
                    uint8_t t172 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                    if (!t172) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 1, gm_srckfibkpt681aa, 501, 13);
                    void * t173 = (void *)((uint8_t *)&F.l4);
                    uint16_t t174 = gm_l7;
                    gm_tu0pangbbaeec t175;
                    GM_ST(uint16_t, ((uint8_t *)&t175 + 0), t174);
                    gm_f_format_ms4f9aj((void *)&gm_l81, (void *)NULL, cx, NULL, t175);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t176 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                    if (!t176) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 8, gm_srckfibkpt681aa, 502, 20);
                    gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l81) + 8));
                    gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
                    gm_tftb1odc739t5 t179 = gm_f_push_3ef746b(t173, t178);
                    gm_l82 = t179;
                    uint8_t t180 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
                    if (!t180) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l82) + 1, gm_srckfibkpt681aa, 502, 13);
                    goto gm_lb3;
                }
gm_lb9:;
                {
                    uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                    uint8_t t182 = (uint8_t)(t181 == ((uint8_t)4ULL));
                    if (!t182) {
                        goto gm_lb10;
                    }
                    void * t183 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t184;
                    GM_ST(void *, (uint8_t *)&t184, (void *)(gm_suvf0cqih9u7i));
                    GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(16));
                    gm_tftb1odc739t5 t185 = gm_f_push_3ef746b(t183, t184);
                    gm_l83 = t185;
                    uint8_t t186 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
                    if (!t186) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l83) + 1, gm_srckfibkpt681aa, 504, 18);
                    goto gm_lb3;
                }
gm_lb10:;
                {
                    {
                        {
                            uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                            uint8_t t188 = (uint8_t)(t187 == ((uint8_t)8ULL));
                            if (!t188) {
                                goto gm_lb13;
                            }
                            goto gm_lb12;
                        }
gm_lb13:;
                        {
                            uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                            uint8_t t190 = (uint8_t)(t189 == ((uint8_t)9ULL));
                            if (!t190) {
                                goto gm_lb14;
                            }
                            goto gm_lb12;
                        }
gm_lb14:;
                        {
                            uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                            uint8_t t192 = (uint8_t)(t191 == ((uint8_t)7ULL));
                            if (!t192) {
                                goto gm_lb15;
                            }
                            goto gm_lb12;
                        }
gm_lb15:;
                        {
                            uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                            uint8_t t194 = (uint8_t)(t193 == ((uint8_t)16ULL));
                            if (!t194) {
                                goto gm_lb16;
                            }
                            goto gm_lb12;
                        }
gm_lb16:;
                        goto gm_lb11;
                    }
gm_lb12:;
                    void * t195 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t196;
                    GM_ST(void *, (uint8_t *)&t196, (void *)(gm_sa9ktdbn49iui));
                    GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(15));
                    gm_tftb1odc739t5 t197 = gm_f_push_3ef746b(t195, t196);
                    gm_l84 = t197;
                    uint8_t t198 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
                    if (!t198) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l84) + 1, gm_srckfibkpt681aa, 505, 53);
                    goto gm_lb3;
                }
gm_lb11:;
                {
                    uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                    uint8_t t200 = (uint8_t)(t199 == ((uint8_t)10ULL));
                    if (!t200) {
                        goto gm_lb17;
                    }
                    gm_t88rcucg1b7uk t201 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l69) + 8) + 4));
                    gm_l8 = t201;
                    void * t202 = gm_l0;
                    gm_t88rcucg1b7uk t203 = gm_l8;
                    gm_b16_8 t204 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t202, t203);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l9 = t204;
                    void * t205 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t206;
                    GM_ST(void *, (uint8_t *)&t206, (void *)(gm_sdam23bqvafqn));
                    GM_ST(uintptr_t, (uint8_t *)&t206 + 8, (uintptr_t)(17));
                    gm_tftb1odc739t5 t207 = gm_f_push_3ef746b(t205, t206);
                    gm_l85 = t207;
                    uint8_t t208 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
                    if (!t208) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l85) + 1, gm_srckfibkpt681aa, 508, 13);
                    void * t209 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t210 = gm_l9;
                    gm_tnpifv20irnph t211;
                    GM_ST(gm_b16_8, ((uint8_t *)&t211 + 0), t210);
                    gm_f_format_vsbf5l9((void *)&gm_l86, (void *)NULL, cx, NULL, t211);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t212 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
                    if (!t212) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l86) + 8, gm_srckfibkpt681aa, 509, 20);
                    gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l86) + 8));
                    gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&t213));
                    gm_tftb1odc739t5 t215 = gm_f_push_3ef746b(t209, t214);
                    gm_l87 = t215;
                    uint8_t t216 = (uint8_t)!((((uint8_t *)&gm_l87))[0] != 0);
                    if (!t216) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l87) + 1, gm_srckfibkpt681aa, 509, 13);
                    goto gm_lb3;
                }
gm_lb17:;
            }
            {
                {
                    uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                    uint8_t t218 = (uint8_t)(t217 == ((uint8_t)11ULL));
                    if (!t218) {
                        goto gm_lb18;
                    }
                    uint64_t t219 = GM_LD(uint64_t, (((uint8_t *)&gm_l69) + 8));
                    gm_l10 = t219;
                    gm_t88rcucg1b7uk t220 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l69) + 8) + 8));
                    gm_l11 = t220;
                    void * t221 = gm_l0;
                    gm_t88rcucg1b7uk t222 = gm_l11;
                    gm_b16_8 t223 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t221, t222);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l12 = t223;
                    void * t224 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t225;
                    GM_ST(void *, (uint8_t *)&t225, (void *)(gm_s0f7epcithn0r));
                    GM_ST(uintptr_t, (uint8_t *)&t225 + 8, (uintptr_t)(17));
                    gm_tftb1odc739t5 t226 = gm_f_push_3ef746b(t224, t225);
                    gm_l88 = t226;
                    uint8_t t227 = (uint8_t)!((((uint8_t *)&gm_l88))[0] != 0);
                    if (!t227) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l88) + 1, gm_srckfibkpt681aa, 513, 13);
                    void * t228 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t229 = gm_l12;
                    gm_tnpifv20irnph t230;
                    GM_ST(gm_b16_8, ((uint8_t *)&t230 + 0), t229);
                    gm_f_format_vsbf5l9((void *)&gm_l89, (void *)NULL, cx, NULL, t230);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t231 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                    if (!t231) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l89) + 8, gm_srckfibkpt681aa, 514, 20);
                    gm_b16_8 t232 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l89) + 8));
                    gm_b16_8 t233 = GM_LD(gm_b16_8, ((uint8_t *)&t232));
                    gm_tftb1odc739t5 t234 = gm_f_push_3ef746b(t228, t233);
                    gm_l90 = t234;
                    uint8_t t235 = (uint8_t)!((((uint8_t *)&gm_l90))[0] != 0);
                    if (!t235) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l90) + 1, gm_srckfibkpt681aa, 514, 13);
                    void * t236 = (void *)((uint8_t *)&F.l4);
                    uint64_t t237 = gm_l10;
                    gm_t4ce0jd3tdro4 t238;
                    GM_ST(uint64_t, ((uint8_t *)&t238 + 0), t237);
                    gm_f_format_km55tit((void *)&gm_l91, (void *)NULL, cx, NULL, t238);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t239 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                    if (!t239) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 8, gm_srckfibkpt681aa, 515, 20);
                    gm_b16_8 t240 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l91) + 8));
                    gm_b16_8 t241 = GM_LD(gm_b16_8, ((uint8_t *)&t240));
                    gm_tftb1odc739t5 t242 = gm_f_push_3ef746b(t236, t241);
                    gm_l92 = t242;
                    uint8_t t243 = (uint8_t)!((((uint8_t *)&gm_l92))[0] != 0);
                    if (!t243) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l92) + 1, gm_srckfibkpt681aa, 515, 13);
                    goto gm_lb3;
                }
gm_lb18:;
                {
                    uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                    uint8_t t245 = (uint8_t)(t244 == ((uint8_t)12ULL));
                    if (!t245) {
                        goto gm_lb19;
                    }
                    gm_b16_8 t246 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l69) + 8));
                    gm_l13 = t246;
                    gm_b16_8 t247 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l69) + 8) + 16));
                    gm_l14 = t247;
                    void * t248 = gm_l0;
                    gm_t88rcucg1b7uk t249 = gm_l1;
                    gm_b16_8 t250 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t248, t249);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t250;
                    gm_trqfvmjtt4sij t251;
                    gm_f_list_epvducg((void *)&t251, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l16 = t251;
                    gm_b16_8 t252 = gm_l13;
                    gm_l17 = t252;
                    gm_l18 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t253 = gm_l18;
                            uint64_t t254 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                            uint8_t t255 = (t253 < t254);
                            if (!t255) {
                                goto gm_lb20;
                            }
                            {
                                uint64_t t256 = gm_l18;
                                void * t257 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                uint64_t t258 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                uint64_t t259 = gm_l18;
                                gm_t88rcucg1b7uk t260 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t257) + (uintptr_t)t259 * 4));
                                gm_tlcm4lvu3g1a8 t261;
                                GM_ST(uint32_t, (uint8_t *)&t261 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t261 + 0), t256);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t261 + 8), t260);
                                gm_l93 = t261;
                                {
                                    {
                                        uint64_t t262 = GM_LD(uint64_t, ((uint8_t *)&gm_l93));
                                        gm_l19 = t262;
                                        gm_t88rcucg1b7uk t263 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l93) + 8));
                                        gm_l20 = t263;
                                        void * t264 = gm_l0;
                                        gm_t88rcucg1b7uk t265 = gm_l20;
                                        gm_b16_8 t266 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t264, t265);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l21 = t266;
                                        uint64_t t267 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                        uint8_t t268 = (t267 > ((uint64_t)0ULL));
                                        if (t268) {
                                            void * t269 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                            uint64_t t270 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                            uint64_t t271 = gm_l19;
                                            if ((uintptr_t)t271 >= (uintptr_t)(t270)) gm_trap_index((uintptr_t)t271, t270, gm_srckfibkpt681aa, 522, 60);
                                            gm_b16_8 t272 = GM_LD(gm_b16_8, (((uint8_t *)t269) + (uintptr_t)t271 * 16));
                                            gm_b16_8 t273 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t272);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l94 = t273;
                                        } else {
                                            gm_b16_8 t274;
                                            GM_ST(void *, (uint8_t *)&t274, (void *)(gm_s04qus0luoga5));
                                            GM_ST(uintptr_t, (uint8_t *)&t274 + 8, (uintptr_t)(4));
                                            gm_l94 = t274;
                                        }
                                        gm_b16_8 t275 = gm_l94;
                                        gm_l22 = t275;
                                        void * t276 = (void *)((uint8_t *)&F.l16);
                                        gm_b16_8 t277 = gm_l22;
                                        void * t278 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                        uint64_t t279 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                        uint64_t t280 = gm_l19;
                                        if ((uintptr_t)t280 >= (uintptr_t)(t279)) gm_trap_index((uintptr_t)t280, t279, gm_srckfibkpt681aa, 523, 63);
                                        uint64_t t281 = GM_LD(uint64_t, (((uint8_t *)t278) + (uintptr_t)t280 * 8));
                                        gm_b16_8 t282 = gm_l21;
                                        gm_tq3m5qgvkpje8 t283;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t283 + 0), t277);
                                        GM_ST(uint64_t, ((uint8_t *)&t283 + 16), t281);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t283 + 24), t282);
                                        gm_f_format_1il5da4((void *)&gm_l95, (void *)NULL, cx, NULL, &t283);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t284 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
                                        if (!t284) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l95) + 8, gm_srckfibkpt681aa, 523, 28);
                                        gm_b16_8 t285 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l95) + 8));
                                        gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&t285));
                                        gm_tftb1odc739t5 t287 = gm_f_push_3ef746b(t276, t286);
                                        gm_l96 = t287;
                                        uint8_t t288 = (uint8_t)!((((uint8_t *)&gm_l96))[0] != 0);
                                        if (!t288) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l96) + 1, gm_srckfibkpt681aa, 523, 17);
                                        goto gm_lb23;
                                    }
gm_lb24:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 520, 13);
                                }
gm_lb23:;
                            }
gm_lb22:;
                            uint64_t t289 = gm_l18;
                            uint64_t r290; bool o291 = __builtin_add_overflow(t289, ((uint64_t)1ULL), &r290);
                            uint64_t t292 = r290;
                            gm_l18 = t292;
                        }
                    }
gm_lb20:;
                    void * t293 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t294;
                    GM_ST(void *, (uint8_t *)&t294, (void *)(gm_sc0vbtqan26pc));
                    GM_ST(uintptr_t, (uint8_t *)&t294 + 8, (uintptr_t)(17));
                    gm_tftb1odc739t5 t295 = gm_f_push_3ef746b(t293, t294);
                    gm_l97 = t295;
                    uint8_t t296 = (uint8_t)!((((uint8_t *)&gm_l97))[0] != 0);
                    if (!t296) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l97) + 1, gm_srckfibkpt681aa, 525, 13);
                    void * t297 = (void *)((uint8_t *)&F.l4);
                    uint64_t t298 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint8_t t299 = (t298 > ((uint64_t)0ULL));
                    gm_t9qkh0rsooc8d t300;
                    GM_ST(uint8_t, ((uint8_t *)&t300 + 0), t299);
                    gm_f_format_df9v4jq((void *)&gm_l98, (void *)NULL, cx, NULL, t300);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t301 = (uint8_t)!((((uint8_t *)&gm_l98))[0] != 0);
                    if (!t301) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l98) + 8, gm_srckfibkpt681aa, 526, 20);
                    gm_b16_8 t302 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l98) + 8));
                    gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
                    gm_tftb1odc739t5 t304 = gm_f_push_3ef746b(t297, t303);
                    gm_l99 = t304;
                    uint8_t t305 = (uint8_t)!((((uint8_t *)&gm_l99))[0] != 0);
                    if (!t305) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l99) + 1, gm_srckfibkpt681aa, 526, 13);
                    void * t306 = gm_l0;
                    gm_b16_8 t307 = gm_l3;
                    gm_b16_8 t308 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
                    gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
                    void * t310 = (void *)((uint8_t *)&F.l4);
                    gm_b0_1 t311 = gm_f_fields_array_tikg33t((void *)NULL, cx, NULL, t306, t307, t309, t310);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb19:;
            }
            {
                {
                    uint8_t t312 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                    uint8_t t313 = (uint8_t)(t312 == ((uint8_t)13ULL));
                    if (!t313) {
                        goto gm_lb25;
                    }
                    gm_t88rcucg1b7uk t314 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l69) + 8));
                    gm_l23 = t314;
                    void * t315 = gm_l0;
                    gm_t88rcucg1b7uk t316 = gm_l23;
                    gm_b16_8 t317 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t315, t316);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l24 = t317;
                    void * t318 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t319;
                    GM_ST(void *, (uint8_t *)&t319, (void *)(gm_s7qhcf46f682d));
                    GM_ST(uintptr_t, (uint8_t *)&t319 + 8, (uintptr_t)(20));
                    gm_tftb1odc739t5 t320 = gm_f_push_3ef746b(t318, t319);
                    gm_l100 = t320;
                    uint8_t t321 = (uint8_t)!((((uint8_t *)&gm_l100))[0] != 0);
                    if (!t321) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l100) + 1, gm_srckfibkpt681aa, 531, 13);
                    void * t322 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t323 = gm_l24;
                    gm_tnpifv20irnph t324;
                    GM_ST(gm_b16_8, ((uint8_t *)&t324 + 0), t323);
                    gm_f_format_vsbf5l9((void *)&gm_l101, (void *)NULL, cx, NULL, t324);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t325 = (uint8_t)!((((uint8_t *)&gm_l101))[0] != 0);
                    if (!t325) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l101) + 8, gm_srckfibkpt681aa, 532, 20);
                    gm_b16_8 t326 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l101) + 8));
                    gm_b16_8 t327 = GM_LD(gm_b16_8, ((uint8_t *)&t326));
                    gm_tftb1odc739t5 t328 = gm_f_push_3ef746b(t322, t327);
                    gm_l102 = t328;
                    uint8_t t329 = (uint8_t)!((((uint8_t *)&gm_l102))[0] != 0);
                    if (!t329) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l102) + 1, gm_srckfibkpt681aa, 532, 13);
                    void * t330 = gm_l0;
                    void * t331 = GM_LD(void *, ((uint8_t *)t330));
                    gm_t88rcucg1b7uk t332 = gm_l23;
                    uint8_t t333 = gm_f_nullable_pointer_7qfmn6o(t331, GM_CONV(gm_csguankko4lmhd, t332));
                    if (t333) {
                        void * t334 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t335;
                        GM_ST(void *, (uint8_t *)&t335, (void *)(gm_sbec11r2rv4ti));
                        GM_ST(uintptr_t, (uint8_t *)&t335 + 8, (uintptr_t)(22));
                        gm_tftb1odc739t5 t336 = gm_f_push_3ef746b(t334, t335);
                        gm_l103 = t336;
                        uint8_t t337 = (uint8_t)!((((uint8_t *)&gm_l103))[0] != 0);
                        if (!t337) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l103) + 1, gm_srckfibkpt681aa, 534, 17);
                    } else {
                        void * t338 = (void *)((uint8_t *)&F.l4);
                        void * t339 = gm_l0;
                        gm_t88rcucg1b7uk t340 = gm_l1;
                        gm_b16_8 t341 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t339, t340);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l104 = t341;
                        void * t342 = GM_LD(void *, ((uint8_t *)&gm_l104));
                        uint64_t t343 = GM_LD(uint64_t, (((uint8_t *)&gm_l104) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t343)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t343, gm_srckfibkpt681aa, 536, 55);
                        uint64_t t344 = GM_LD(uint64_t, (((uint8_t *)t342) + (uintptr_t)((uint64_t)1ULL) * 8));
                        gm_t4ce0jd3tdro4 t345;
                        GM_ST(uint64_t, ((uint8_t *)&t345 + 0), t344);
                        gm_f_format_9117rt8((void *)&gm_l105, (void *)NULL, cx, NULL, t345);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t346 = (uint8_t)!((((uint8_t *)&gm_l105))[0] != 0);
                        if (!t346) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l105) + 8, gm_srckfibkpt681aa, 536, 24);
                        gm_b16_8 t347 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l105) + 8));
                        gm_b16_8 t348 = GM_LD(gm_b16_8, ((uint8_t *)&t347));
                        gm_tftb1odc739t5 t349 = gm_f_push_3ef746b(t338, t348);
                        gm_l106 = t349;
                        uint8_t t350 = (uint8_t)!((((uint8_t *)&gm_l106))[0] != 0);
                        if (!t350) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l106) + 1, gm_srckfibkpt681aa, 536, 17);
                    }
                    goto gm_lb3;
                }
gm_lb25:;
                {
                    uint8_t t351 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                    uint8_t t352 = (uint8_t)(t351 == ((uint8_t)14ULL));
                    if (!t352) {
                        goto gm_lb26;
                    }
                    gm_t88rcucg1b7uk t353 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l69) + 8));
                    gm_l25 = t353;
                    gm_t88rcucg1b7uk t354 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l69) + 8) + 4));
                    gm_l26 = t354;
                    void * t355 = gm_l0;
                    gm_t88rcucg1b7uk t356 = gm_l25;
                    gm_b16_8 t357 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t355, t356);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l27 = t357;
                    void * t358 = gm_l0;
                    gm_t88rcucg1b7uk t359 = gm_l26;
                    gm_b16_8 t360 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t358, t359);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l28 = t360;
                    void * t361 = gm_l0;
                    gm_t88rcucg1b7uk t362 = gm_l1;
                    gm_b16_8 t363 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t361, t362);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l29 = t363;
                    void * t364 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t365;
                    GM_ST(void *, (uint8_t *)&t365, (void *)(gm_shrq42dntcbrd));
                    GM_ST(uintptr_t, (uint8_t *)&t365 + 8, (uintptr_t)(18));
                    gm_tftb1odc739t5 t366 = gm_f_push_3ef746b(t364, t365);
                    gm_l107 = t366;
                    uint8_t t367 = (uint8_t)!((((uint8_t *)&gm_l107))[0] != 0);
                    if (!t367) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l107) + 1, gm_srckfibkpt681aa, 543, 13);
                    void * t368 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t369 = gm_l28;
                    gm_tnpifv20irnph t370;
                    GM_ST(gm_b16_8, ((uint8_t *)&t370 + 0), t369);
                    gm_f_format_vsbf5l9((void *)&gm_l108, (void *)NULL, cx, NULL, t370);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t371 = (uint8_t)!((((uint8_t *)&gm_l108))[0] != 0);
                    if (!t371) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l108) + 8, gm_srckfibkpt681aa, 544, 20);
                    gm_b16_8 t372 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l108) + 8));
                    gm_b16_8 t373 = GM_LD(gm_b16_8, ((uint8_t *)&t372));
                    gm_tftb1odc739t5 t374 = gm_f_push_3ef746b(t368, t373);
                    gm_l109 = t374;
                    uint8_t t375 = (uint8_t)!((((uint8_t *)&gm_l109))[0] != 0);
                    if (!t375) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l109) + 1, gm_srckfibkpt681aa, 544, 13);
                    void * t376 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t377 = gm_l27;
                    gm_tnpifv20irnph t378;
                    GM_ST(gm_b16_8, ((uint8_t *)&t378 + 0), t377);
                    gm_f_format_s44fp1q((void *)&gm_l110, (void *)NULL, cx, NULL, t378);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t379 = (uint8_t)!((((uint8_t *)&gm_l110))[0] != 0);
                    if (!t379) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l110) + 8, gm_srckfibkpt681aa, 545, 20);
                    gm_b16_8 t380 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l110) + 8));
                    gm_b16_8 t381 = GM_LD(gm_b16_8, ((uint8_t *)&t380));
                    gm_tftb1odc739t5 t382 = gm_f_push_3ef746b(t376, t381);
                    gm_l111 = t382;
                    uint8_t t383 = (uint8_t)!((((uint8_t *)&gm_l111))[0] != 0);
                    if (!t383) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l111) + 1, gm_srckfibkpt681aa, 545, 13);
                    void * t384 = (void *)((uint8_t *)&F.l4);
                    void * t385 = GM_LD(void *, ((uint8_t *)&gm_l29));
                    uint64_t t386 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t386)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t386, gm_srckfibkpt681aa, 546, 50);
                    uint64_t t387 = GM_LD(uint64_t, (((uint8_t *)t385) + (uintptr_t)((uint64_t)0ULL) * 8));
                    gm_t4ce0jd3tdro4 t388;
                    GM_ST(uint64_t, ((uint8_t *)&t388 + 0), t387);
                    gm_f_format_4v9fh5b((void *)&gm_l112, (void *)NULL, cx, NULL, t388);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t389 = (uint8_t)!((((uint8_t *)&gm_l112))[0] != 0);
                    if (!t389) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l112) + 8, gm_srckfibkpt681aa, 546, 20);
                    gm_b16_8 t390 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l112) + 8));
                    gm_b16_8 t391 = GM_LD(gm_b16_8, ((uint8_t *)&t390));
                    gm_tftb1odc739t5 t392 = gm_f_push_3ef746b(t384, t391);
                    gm_l113 = t392;
                    uint8_t t393 = (uint8_t)!((((uint8_t *)&gm_l113))[0] != 0);
                    if (!t393) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l113) + 1, gm_srckfibkpt681aa, 546, 13);
                    void * t394 = (void *)((uint8_t *)&F.l4);
                    void * t395 = GM_LD(void *, ((uint8_t *)&gm_l29));
                    uint64_t t396 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t396)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t396, gm_srckfibkpt681aa, 547, 54);
                    uint64_t t397 = GM_LD(uint64_t, (((uint8_t *)t395) + (uintptr_t)((uint64_t)1ULL) * 8));
                    gm_t4ce0jd3tdro4 t398;
                    GM_ST(uint64_t, ((uint8_t *)&t398 + 0), t397);
                    gm_f_format_io38sjt((void *)&gm_l114, (void *)NULL, cx, NULL, t398);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t399 = (uint8_t)!((((uint8_t *)&gm_l114))[0] != 0);
                    if (!t399) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l114) + 8, gm_srckfibkpt681aa, 547, 20);
                    gm_b16_8 t400 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l114) + 8));
                    gm_b16_8 t401 = GM_LD(gm_b16_8, ((uint8_t *)&t400));
                    gm_tftb1odc739t5 t402 = gm_f_push_3ef746b(t394, t401);
                    gm_l115 = t402;
                    uint8_t t403 = (uint8_t)!((((uint8_t *)&gm_l115))[0] != 0);
                    if (!t403) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l115) + 1, gm_srckfibkpt681aa, 547, 13);
                    goto gm_lb3;
                }
gm_lb26:;
            }
            {
                {
                    {
                        uint8_t t404 = GM_LD(uint8_t, (((uint8_t *)&gm_l69) + 0));
                        uint8_t t405 = (uint8_t)(t404 == ((uint8_t)17ULL));
                        if (!t405) {
                            goto gm_lb27;
                        }
                        uint32_t t406 = GM_LD(uint32_t, (((uint8_t *)&gm_l69) + 8));
                        gm_l30 = t406;
                        void * t407 = gm_l0;
                        void * t408 = GM_LD(void *, ((uint8_t *)t407));
                        void * t409 = GM_LD(void *, (((uint8_t *)t408) + 1088));
                        uint64_t t410 = GM_LD(uint64_t, ((((uint8_t *)t408) + 1088) + 8));
                        uint32_t t411 = gm_l30;
                        uint64_t t412 = (uint64_t)t411;
                        if ((uintptr_t)t412 >= (uintptr_t)(t410)) gm_trap_index((uintptr_t)t412, t410, gm_srckfibkpt681aa, 550, 21);
                        void * t413 = (void *)(((uint8_t *)t409) + (uintptr_t)t412 * 184);
                        gm_l31 = t413;
                        void * t414 = (void *)((uint8_t *)&F.l4);
                        void * t415 = gm_l31;
                        gm_b16_8 t416 = GM_LD(gm_b16_8, ((uint8_t *)t415));
                        gm_b16_8 t417 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t416);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t418;
                        GM_ST(gm_b16_8, ((uint8_t *)&t418 + 0), t417);
                        gm_f_format_nbg44fl((void *)&gm_l116, (void *)NULL, cx, NULL, t418);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t419 = (uint8_t)!((((uint8_t *)&gm_l116))[0] != 0);
                        if (!t419) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l116) + 8, gm_srckfibkpt681aa, 551, 20);
                        gm_b16_8 t420 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l116) + 8));
                        gm_b16_8 t421 = GM_LD(gm_b16_8, ((uint8_t *)&t420));
                        gm_tftb1odc739t5 t422 = gm_f_push_3ef746b(t414, t421);
                        gm_l117 = t422;
                        uint8_t t423 = (uint8_t)!((((uint8_t *)&gm_l117))[0] != 0);
                        if (!t423) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l117) + 1, gm_srckfibkpt681aa, 551, 13);
                        void * t424 = gm_l31;
                        {
                            {
                                {
                                    uint8_t t425 = GM_LD(uint8_t, ((((uint8_t *)t424) + 24) + 0));
                                    uint8_t t426 = (uint8_t)(t425 == ((uint8_t)0ULL));
                                    if (!t426) {
                                        goto gm_lb29;
                                    }
                                    memmove(((uint8_t *)&F.l32), ((((uint8_t *)t424) + 24) + 8), 48);
                                    {
                                        {
                                            uint8_t t427 = (uint8_t)(((((uint8_t *)&F.l32) + 40))[4] != 0);
                                            if (t427) {
                                                goto gm_lb31;
                                            }
                                            gm_l118 = ((uint8_t)1);
                                            goto gm_lb30;
                                        }
gm_lb31:;
                                        {
                                            gm_l118 = ((uint8_t)0);
                                            goto gm_lb30;
                                        }
gm_lb32:;
                                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 554, 24);
                                    }
gm_lb30:;
                                    uint8_t t428 = gm_l118;
                                    if (t428) {
                                        void * t429 = gm_l0;
                                        gm_t88rcucg1b7uk t430 = gm_l1;
                                        gm_b16_8 t431 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t429, t430);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l33 = t431;
                                        gm_trqfvmjtt4sij t432;
                                        gm_f_list_epvducg((void *)&t432, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l34 = t432;
                                        gm_b16_8 t433 = GM_LD(gm_b16_8, ((uint8_t *)&F.l32));
                                        gm_b16_8 t434 = GM_LD(gm_b16_8, ((uint8_t *)&t433));
                                        gm_l35 = t434;
                                        gm_l36 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t435 = gm_l36;
                                                uint64_t t436 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                                uint8_t t437 = (t435 < t436);
                                                if (!t437) {
                                                    goto gm_lb33;
                                                }
                                                {
                                                    uint64_t t438 = gm_l36;
                                                    void * t439 = GM_LD(void *, ((uint8_t *)&gm_l35));
                                                    uint64_t t440 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                                    uint64_t t441 = gm_l36;
                                                    gm_t0mglksrkfa5e t442 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t439) + (uintptr_t)t441 * 40));
                                                    gm_t8ta482bl6be0 t443;
                                                    GM_ST(uint64_t, ((uint8_t *)&t443 + 0), t438);
                                                    GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&t443 + 8), t442);
                                                    gm_l119 = t443;
                                                    {
                                                        {
                                                            uint64_t t444 = GM_LD(uint64_t, ((uint8_t *)&gm_l119));
                                                            gm_l37 = t444;
                                                            memmove(((uint8_t *)&F.l38), (((uint8_t *)&gm_l119) + 8), 40);
                                                            void * t445 = gm_l0;
                                                            gm_t88rcucg1b7uk t446 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l38) + 16));
                                                            gm_b16_8 t447 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t445, t446);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            gm_l39 = t447;
                                                            void * t448 = (void *)((uint8_t *)&F.l34);
                                                            gm_b16_8 t449 = GM_LD(gm_b16_8, ((uint8_t *)&F.l38));
                                                            gm_b16_8 t450 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t449);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            void * t451 = GM_LD(void *, ((uint8_t *)&gm_l33));
                                                            uint64_t t452 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                                            uint64_t t453 = gm_l37;
                                                            if ((uintptr_t)t453 >= (uintptr_t)(t452)) gm_trap_index((uintptr_t)t453, t452, gm_srckfibkpt681aa, 559, 88);
                                                            uint64_t t454 = GM_LD(uint64_t, (((uint8_t *)t451) + (uintptr_t)t453 * 8));
                                                            gm_b16_8 t455 = gm_l39;
                                                            gm_tq3m5qgvkpje8 t456;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t456 + 0), t450);
                                                            GM_ST(uint64_t, ((uint8_t *)&t456 + 16), t454);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t456 + 24), t455);
                                                            gm_f_format_1il5da4((void *)&gm_l120, (void *)NULL, cx, NULL, &t456);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint8_t t457 = (uint8_t)!((((uint8_t *)&gm_l120))[0] != 0);
                                                            if (!t457) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l120) + 8, gm_srckfibkpt681aa, 559, 40);
                                                            gm_b16_8 t458 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l120) + 8));
                                                            gm_b16_8 t459 = GM_LD(gm_b16_8, ((uint8_t *)&t458));
                                                            gm_tftb1odc739t5 t460 = gm_f_push_3ef746b(t448, t459);
                                                            gm_l121 = t460;
                                                            uint8_t t461 = (uint8_t)!((((uint8_t *)&gm_l121))[0] != 0);
                                                            if (!t461) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l121) + 1, gm_srckfibkpt681aa, 559, 29);
                                                            goto gm_lb36;
                                                        }
gm_lb37:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 557, 25);
                                                    }
gm_lb36:;
                                                }
gm_lb35:;
                                                uint64_t t462 = gm_l36;
                                                uint64_t r463; bool o464 = __builtin_add_overflow(t462, ((uint64_t)1ULL), &r463);
                                                uint64_t t465 = r463;
                                                gm_l36 = t465;
                                            }
                                        }
gm_lb33:;
                                        void * t466 = (void *)((uint8_t *)&F.l4);
                                        gm_b16_8 t467;
                                        GM_ST(void *, (uint8_t *)&t467, (void *)(gm_slad86501rp5c));
                                        GM_ST(uintptr_t, (uint8_t *)&t467 + 8, (uintptr_t)(18));
                                        gm_tftb1odc739t5 t468 = gm_f_push_3ef746b(t466, t467);
                                        gm_l122 = t468;
                                        uint8_t t469 = (uint8_t)!((((uint8_t *)&gm_l122))[0] != 0);
                                        if (!t469) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l122) + 1, gm_srckfibkpt681aa, 561, 25);
                                        void * t470 = gm_l0;
                                        gm_b16_8 t471 = gm_l3;
                                        gm_b16_8 t472 = GM_LD(gm_b16_8, ((uint8_t *)&F.l34));
                                        gm_b16_8 t473 = GM_LD(gm_b16_8, ((uint8_t *)&t472));
                                        void * t474 = (void *)((uint8_t *)&F.l4);
                                        gm_b0_1 t475 = gm_f_fields_array_tikg33t((void *)NULL, cx, NULL, t470, t471, t473, t474);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    } else {
                                        void * t476 = gm_l0;
                                        gm_t88rcucg1b7uk t477 = gm_l1;
                                        gm_f_bits_layout_23rect9((void *)&gm_l123, (void *)NULL, cx, NULL, t476, t477);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t478 = (uint8_t)((((uint8_t *)&gm_l123))[24] != 0);
                                        if (!t478) gm_trap("unwrap of null", gm_srckfibkpt681aa, 564, 31);
                                        memmove(((uint8_t *)&gm_l40), ((uint8_t *)&gm_l123), 24);
                                        gm_trqfvmjtt4sij t479;
                                        gm_f_list_epvducg((void *)&t479, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l41 = t479;
                                        gm_b16_8 t480 = GM_LD(gm_b16_8, ((uint8_t *)&F.l32));
                                        gm_b16_8 t481 = GM_LD(gm_b16_8, ((uint8_t *)&t480));
                                        gm_l42 = t481;
                                        gm_l43 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t482 = gm_l43;
                                                uint64_t t483 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                                                uint8_t t484 = (t482 < t483);
                                                if (!t484) {
                                                    goto gm_lb38;
                                                }
                                                {
                                                    uint64_t t485 = gm_l43;
                                                    void * t486 = GM_LD(void *, ((uint8_t *)&gm_l42));
                                                    uint64_t t487 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                                                    uint64_t t488 = gm_l43;
                                                    gm_t0mglksrkfa5e t489 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t486) + (uintptr_t)t488 * 40));
                                                    gm_t8ta482bl6be0 t490;
                                                    GM_ST(uint64_t, ((uint8_t *)&t490 + 0), t485);
                                                    GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&t490 + 8), t489);
                                                    gm_l124 = t490;
                                                    {
                                                        {
                                                            uint64_t t491 = GM_LD(uint64_t, ((uint8_t *)&gm_l124));
                                                            gm_l44 = t491;
                                                            memmove(((uint8_t *)&F.l45), (((uint8_t *)&gm_l124) + 8), 40);
                                                            void * t492 = GM_LD(void *, (((uint8_t *)&gm_l40) + 8));
                                                            uint64_t t493 = GM_LD(uint64_t, ((((uint8_t *)&gm_l40) + 8) + 8));
                                                            uint64_t t494 = gm_l44;
                                                            if ((uintptr_t)t494 >= (uintptr_t)(t493)) gm_trap_index((uintptr_t)t494, t493, gm_srckfibkpt681aa, 567, 35);
                                                            gm_tov4mtrfp5e7v t495 = GM_LD(gm_tov4mtrfp5e7v, (((uint8_t *)t492) + (uintptr_t)t494 * 12));
                                                            gm_l46 = t495;
                                                            void * t496 = gm_l0;
                                                            gm_t88rcucg1b7uk t497 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l46) + 8));
                                                            gm_b16_8 t498 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t496, t497);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            gm_l47 = t498;
                                                            void * t499 = (void *)((uint8_t *)&F.l41);
                                                            gm_b16_8 t500 = GM_LD(gm_b16_8, ((uint8_t *)&F.l45));
                                                            gm_b16_8 t501 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t500);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint32_t t502 = GM_LD(uint32_t, ((uint8_t *)&gm_l46));
                                                            gm_b16_8 t503 = gm_l47;
                                                            gm_temv198mna2jp t504;
                                                            GM_ST(uint32_t, (uint8_t *)&t504 + 20, 0);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t504 + 0), t501);
                                                            GM_ST(uint32_t, ((uint8_t *)&t504 + 16), t502);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t504 + 24), t503);
                                                            gm_f_format_n3gtmrf((void *)&gm_l125, (void *)NULL, cx, NULL, &t504);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint8_t t505 = (uint8_t)!((((uint8_t *)&gm_l125))[0] != 0);
                                                            if (!t505) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l125) + 8, gm_srckfibkpt681aa, 569, 40);
                                                            gm_b16_8 t506 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l125) + 8));
                                                            gm_b16_8 t507 = GM_LD(gm_b16_8, ((uint8_t *)&t506));
                                                            gm_tftb1odc739t5 t508 = gm_f_push_3ef746b(t499, t507);
                                                            gm_l126 = t508;
                                                            uint8_t t509 = (uint8_t)!((((uint8_t *)&gm_l126))[0] != 0);
                                                            if (!t509) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l126) + 1, gm_srckfibkpt681aa, 569, 29);
                                                            goto gm_lb41;
                                                        }
gm_lb42:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 566, 25);
                                                    }
gm_lb41:;
                                                }
gm_lb40:;
                                                uint64_t t510 = gm_l43;
                                                uint64_t r511; bool o512 = __builtin_add_overflow(t510, ((uint64_t)1ULL), &r511);
                                                uint64_t t513 = r511;
                                                gm_l43 = t513;
                                            }
                                        }
gm_lb38:;
                                        void * t514 = (void *)((uint8_t *)&F.l4);
                                        gm_b16_8 t515;
                                        GM_ST(void *, (uint8_t *)&t515, (void *)(gm_s76d71nockpip));
                                        GM_ST(uintptr_t, (uint8_t *)&t515 + 8, (uintptr_t)(16));
                                        gm_tftb1odc739t5 t516 = gm_f_push_3ef746b(t514, t515);
                                        gm_l127 = t516;
                                        uint8_t t517 = (uint8_t)!((((uint8_t *)&gm_l127))[0] != 0);
                                        if (!t517) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l127) + 1, gm_srckfibkpt681aa, 571, 25);
                                        void * t518 = (void *)((uint8_t *)&F.l4);
                                        void * t519 = gm_l0;
                                        gm_t88rcucg1b7uk t520 = gm_l1;
                                        uint32_t t521 = gm_f_bit_width_u86m8ir((void *)NULL, cx, NULL, t519, t520);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_t88rcucg1b7uk t522;
                                        GM_ST(uint32_t, ((uint8_t *)&t522 + 0), t521);
                                        gm_f_format_ch41dev((void *)&gm_l128, (void *)NULL, cx, NULL, t522);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t523 = (uint8_t)!((((uint8_t *)&gm_l128))[0] != 0);
                                        if (!t523) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l128) + 8, gm_srckfibkpt681aa, 572, 32);
                                        gm_b16_8 t524 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l128) + 8));
                                        gm_b16_8 t525 = GM_LD(gm_b16_8, ((uint8_t *)&t524));
                                        gm_tftb1odc739t5 t526 = gm_f_push_3ef746b(t518, t525);
                                        gm_l129 = t526;
                                        uint8_t t527 = (uint8_t)!((((uint8_t *)&gm_l129))[0] != 0);
                                        if (!t527) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l129) + 1, gm_srckfibkpt681aa, 572, 25);
                                        void * t528 = gm_l0;
                                        gm_b16_8 t529 = gm_l3;
                                        gm_b16_8 t530 = GM_LD(gm_b16_8, ((uint8_t *)&F.l41));
                                        gm_b16_8 t531 = GM_LD(gm_b16_8, ((uint8_t *)&t530));
                                        void * t532 = (void *)((uint8_t *)&F.l4);
                                        gm_b0_1 t533 = gm_f_fields_array_tikg33t((void *)NULL, cx, NULL, t528, t529, t531, t532);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    goto gm_lb28;
                                }
gm_lb29:;
                            }
                            {
                                {
                                    uint8_t t534 = GM_LD(uint8_t, ((((uint8_t *)t424) + 24) + 0));
                                    uint8_t t535 = (uint8_t)(t534 == ((uint8_t)1ULL));
                                    if (!t535) {
                                        goto gm_lb43;
                                    }
                                    gm_t88rcucg1b7uk t536 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t424) + 24) + 8));
                                    gm_l48 = t536;
                                    gm_b16_8 t537 = GM_LD(gm_b16_8, (((((uint8_t *)t424) + 24) + 8) + 8));
                                    gm_l49 = t537;
                                    void * t538 = gm_l0;
                                    gm_t88rcucg1b7uk t539 = gm_l1;
                                    gm_b16_8 t540 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t538, t539);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l50 = t540;
                                    gm_trqfvmjtt4sij t541;
                                    gm_f_list_epvducg((void *)&t541, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l51 = t541;
                                    gm_b16_8 t542 = gm_l49;
                                    gm_l52 = t542;
                                    gm_l53 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t543 = gm_l53;
                                            uint64_t t544 = GM_LD(uint64_t, (((uint8_t *)&gm_l52) + 8));
                                            uint8_t t545 = (t543 < t544);
                                            if (!t545) {
                                                goto gm_lb44;
                                            }
                                            {
                                                void * t546 = GM_LD(void *, ((uint8_t *)&gm_l52));
                                                uint64_t t547 = GM_LD(uint64_t, (((uint8_t *)&gm_l52) + 8));
                                                uint64_t t548 = gm_l53;
                                                {
                                                    {
                                                        memmove(((uint8_t *)&F.l54), (((uint8_t *)t546) + (uintptr_t)t548 * 72), 72);
                                                        {
                                                            {
                                                                uint8_t t549 = (uint8_t)(((((uint8_t *)&F.l54) + 16))[4] != 0);
                                                                if (!t549) {
                                                                    goto gm_lb50;
                                                                }
                                                                gm_t88rcucg1b7uk t550 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l54) + 16));
                                                                gm_l55 = t550;
                                                                void * t551 = gm_l0;
                                                                gm_t88rcucg1b7uk t552 = gm_l55;
                                                                gm_b16_8 t553 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t551, t552);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                gm_l130 = t553;
                                                                goto gm_lb49;
                                                            }
gm_lb50:;
                                                            gm_b16_8 t554;
                                                            GM_ST(void *, (uint8_t *)&t554, (void *)(gm_s04qus0luoga5));
                                                            GM_ST(uintptr_t, (uint8_t *)&t554 + 8, (uintptr_t)(4));
                                                            gm_l130 = t554;
                                                        }
gm_lb49:;
                                                        gm_b16_8 t555 = gm_l130;
                                                        gm_l56 = t555;
                                                        void * t556 = (void *)((uint8_t *)&F.l51);
                                                        gm_b16_8 t557 = GM_LD(gm_b16_8, ((uint8_t *)&F.l54));
                                                        gm_b16_8 t558 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t557);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_b16_8 t559 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l54) + 24)));
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_b16_8 t560 = gm_l56;
                                                        gm_tfeab279c16rr t561;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t561 + 0), t558);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t561 + 16), t559);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t561 + 32), t560);
                                                        gm_f_format_aqb9tiu((void *)&gm_l131, (void *)NULL, cx, NULL, &t561);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t562 = (uint8_t)!((((uint8_t *)&gm_l131))[0] != 0);
                                                        if (!t562) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l131) + 8, gm_srckfibkpt681aa, 581, 36);
                                                        gm_b16_8 t563 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l131) + 8));
                                                        gm_b16_8 t564 = GM_LD(gm_b16_8, ((uint8_t *)&t563));
                                                        gm_tftb1odc739t5 t565 = gm_f_push_3ef746b(t556, t564);
                                                        gm_l132 = t565;
                                                        uint8_t t566 = (uint8_t)!((((uint8_t *)&gm_l132))[0] != 0);
                                                        if (!t566) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l132) + 1, gm_srckfibkpt681aa, 581, 25);
                                                        goto gm_lb47;
                                                    }
gm_lb48:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 579, 21);
                                                }
gm_lb47:;
                                            }
gm_lb46:;
                                            uint64_t t567 = gm_l53;
                                            uint64_t r568; bool o569 = __builtin_add_overflow(t567, ((uint64_t)1ULL), &r568);
                                            uint64_t t570 = r568;
                                            gm_l53 = t570;
                                        }
                                    }
gm_lb44:;
                                    void * t571 = gm_l0;
                                    void * t572 = (void *)(((uint8_t *)t571) + 328);
                                    gm_b16_8 t573 = gm_l3;
                                    gm_b16_8 t574 = GM_LD(gm_b16_8, ((uint8_t *)&F.l51));
                                    gm_b16_8 t575 = GM_LD(gm_b16_8, ((uint8_t *)&t574));
                                    gm_b16_8 t576;
                                    GM_ST(void *, (uint8_t *)&t576, (void *)(gm_sg5gdnrith2i6));
                                    GM_ST(uintptr_t, (uint8_t *)&t576 + 8, (uintptr_t)(2));
                                    gm_f_join_sfknljb((void *)&gm_l133, (void *)NULL, cx, NULL, t575, t576);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t577 = (uint8_t)!((((uint8_t *)&gm_l133))[0] != 0);
                                    if (!t577) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l133) + 8, gm_srckfibkpt681aa, 583, 90);
                                    gm_b16_8 t578 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l133) + 8));
                                    gm_t6j6smj7qoctu t579;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t579 + 0), t573);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t579 + 16), t578);
                                    gm_tftb1odc739t5 t580 = gm_f_write_r7au1ks(t572, &t579);
                                    gm_l134 = t580;
                                    uint8_t t581 = (uint8_t)!((((uint8_t *)&gm_l134))[0] != 0);
                                    if (!t581) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l134) + 1, gm_srckfibkpt681aa, 583, 21);
                                    void * t582 = (void *)((uint8_t *)&F.l4);
                                    gm_b16_8 t583;
                                    GM_ST(void *, (uint8_t *)&t583, (void *)(gm_s58f7dj21gtf9));
                                    GM_ST(uintptr_t, (uint8_t *)&t583 + 8, (uintptr_t)(17));
                                    gm_tftb1odc739t5 t584 = gm_f_push_3ef746b(t582, t583);
                                    gm_l135 = t584;
                                    uint8_t t585 = (uint8_t)!((((uint8_t *)&gm_l135))[0] != 0);
                                    if (!t585) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l135) + 1, gm_srckfibkpt681aa, 584, 21);
                                    void * t586 = (void *)((uint8_t *)&F.l4);
                                    uint64_t t587 = GM_LD(uint64_t, (((uint8_t *)&gm_l49) + 8));
                                    gm_t4ce0jd3tdro4 t588;
                                    GM_ST(uint64_t, ((uint8_t *)&t588 + 0), t587);
                                    gm_f_format_ktg7ckd((void *)&gm_l136, (void *)NULL, cx, NULL, t588);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t589 = (uint8_t)!((((uint8_t *)&gm_l136))[0] != 0);
                                    if (!t589) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l136) + 8, gm_srckfibkpt681aa, 585, 28);
                                    gm_b16_8 t590 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l136) + 8));
                                    gm_b16_8 t591 = GM_LD(gm_b16_8, ((uint8_t *)&t590));
                                    gm_tftb1odc739t5 t592 = gm_f_push_3ef746b(t586, t591);
                                    gm_l137 = t592;
                                    uint8_t t593 = (uint8_t)!((((uint8_t *)&gm_l137))[0] != 0);
                                    if (!t593) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l137) + 1, gm_srckfibkpt681aa, 585, 21);
                                    void * t594 = (void *)((uint8_t *)&F.l4);
                                    gm_b16_8 t595 = gm_l3;
                                    gm_tnpifv20irnph t596;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t596 + 0), t595);
                                    gm_f_format_ka5ced8((void *)&gm_l138, (void *)NULL, cx, NULL, t596);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t597 = (uint8_t)!((((uint8_t *)&gm_l138))[0] != 0);
                                    if (!t597) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l138) + 8, gm_srckfibkpt681aa, 586, 28);
                                    gm_b16_8 t598 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l138) + 8));
                                    gm_b16_8 t599 = GM_LD(gm_b16_8, ((uint8_t *)&t598));
                                    gm_tftb1odc739t5 t600 = gm_f_push_3ef746b(t594, t599);
                                    gm_l139 = t600;
                                    uint8_t t601 = (uint8_t)!((((uint8_t *)&gm_l139))[0] != 0);
                                    if (!t601) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l139) + 1, gm_srckfibkpt681aa, 586, 21);
                                    void * t602 = (void *)((uint8_t *)&F.l4);
                                    void * t603 = GM_LD(void *, ((uint8_t *)&gm_l50));
                                    uint64_t t604 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t604)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t604, gm_srckfibkpt681aa, 587, 58);
                                    uint64_t t605 = GM_LD(uint64_t, (((uint8_t *)t603) + (uintptr_t)((uint64_t)0ULL) * 8));
                                    gm_t4ce0jd3tdro4 t606;
                                    GM_ST(uint64_t, ((uint8_t *)&t606 + 0), t605);
                                    gm_f_format_4v9fh5b((void *)&gm_l140, (void *)NULL, cx, NULL, t606);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t607 = (uint8_t)!((((uint8_t *)&gm_l140))[0] != 0);
                                    if (!t607) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l140) + 8, gm_srckfibkpt681aa, 587, 28);
                                    gm_b16_8 t608 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l140) + 8));
                                    gm_b16_8 t609 = GM_LD(gm_b16_8, ((uint8_t *)&t608));
                                    gm_tftb1odc739t5 t610 = gm_f_push_3ef746b(t602, t609);
                                    gm_l141 = t610;
                                    uint8_t t611 = (uint8_t)!((((uint8_t *)&gm_l141))[0] != 0);
                                    if (!t611) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l141) + 1, gm_srckfibkpt681aa, 587, 21);
                                    void * t612 = (void *)((uint8_t *)&F.l4);
                                    uint64_t t613 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                                    uint8_t t614 = (t613 > ((uint64_t)1ULL));
                                    if (t614) {
                                        void * t615 = GM_LD(void *, ((uint8_t *)&gm_l50));
                                        uint64_t t616 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                                        uint64_t t617 = GM_LD(uint64_t, (((uint8_t *)t615) + (uintptr_t)((uint64_t)1ULL) * 8));
                                        gm_l142 = t617;
                                    } else {
                                        gm_l142 = ((uint64_t)0ULL);
                                    }
                                    uint64_t t618 = gm_l142;
                                    gm_t4ce0jd3tdro4 t619;
                                    GM_ST(uint64_t, ((uint8_t *)&t619 + 0), t618);
                                    gm_f_format_io38sjt((void *)&gm_l143, (void *)NULL, cx, NULL, t619);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t620 = (uint8_t)!((((uint8_t *)&gm_l143))[0] != 0);
                                    if (!t620) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l143) + 8, gm_srckfibkpt681aa, 588, 28);
                                    gm_b16_8 t621 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l143) + 8));
                                    gm_b16_8 t622 = GM_LD(gm_b16_8, ((uint8_t *)&t621));
                                    gm_tftb1odc739t5 t623 = gm_f_push_3ef746b(t612, t622);
                                    gm_l144 = t623;
                                    uint8_t t624 = (uint8_t)!((((uint8_t *)&gm_l144))[0] != 0);
                                    if (!t624) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l144) + 1, gm_srckfibkpt681aa, 588, 21);
                                    void * t625 = (void *)((uint8_t *)&F.l4);
                                    void * t626 = gm_l0;
                                    gm_t88rcucg1b7uk t627 = gm_l48;
                                    uint64_t t628 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t626, t627);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t4ce0jd3tdro4 t629;
                                    GM_ST(uint64_t, ((uint8_t *)&t629 + 0), t628);
                                    gm_f_format_tbvfjdi((void *)&gm_l145, (void *)NULL, cx, NULL, t629);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t630 = (uint8_t)!((((uint8_t *)&gm_l145))[0] != 0);
                                    if (!t630) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l145) + 8, gm_srckfibkpt681aa, 589, 28);
                                    gm_b16_8 t631 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l145) + 8));
                                    gm_b16_8 t632 = GM_LD(gm_b16_8, ((uint8_t *)&t631));
                                    gm_tftb1odc739t5 t633 = gm_f_push_3ef746b(t625, t632);
                                    gm_l146 = t633;
                                    uint8_t t634 = (uint8_t)!((((uint8_t *)&gm_l146))[0] != 0);
                                    if (!t634) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l146) + 1, gm_srckfibkpt681aa, 589, 21);
                                    void * t635 = (void *)((uint8_t *)&F.l4);
                                    void * t636 = gm_l0;
                                    gm_t88rcucg1b7uk t637 = gm_l48;
                                    uint32_t t638 = gm_f_bit_width_u86m8ir((void *)NULL, cx, NULL, t636, t637);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t88rcucg1b7uk t639;
                                    GM_ST(uint32_t, ((uint8_t *)&t639 + 0), t638);
                                    gm_f_format_ch41dev((void *)&gm_l147, (void *)NULL, cx, NULL, t639);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t640 = (uint8_t)!((((uint8_t *)&gm_l147))[0] != 0);
                                    if (!t640) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l147) + 8, gm_srckfibkpt681aa, 590, 28);
                                    gm_b16_8 t641 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l147) + 8));
                                    gm_b16_8 t642 = GM_LD(gm_b16_8, ((uint8_t *)&t641));
                                    gm_tftb1odc739t5 t643 = gm_f_push_3ef746b(t635, t642);
                                    gm_l148 = t643;
                                    uint8_t t644 = (uint8_t)!((((uint8_t *)&gm_l148))[0] != 0);
                                    if (!t644) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l148) + 1, gm_srckfibkpt681aa, 590, 21);
                                    void * t645 = (void *)((uint8_t *)&F.l4);
                                    void * t646 = gm_l0;
                                    gm_t88rcucg1b7uk t647 = gm_l48;
                                    uint8_t t648 = gm_f_is_signed_integer_ige9b74(t646, GM_CONV(gm_csguankko4lmhd, t647));
                                    gm_t9qkh0rsooc8d t649;
                                    GM_ST(uint8_t, ((uint8_t *)&t649 + 0), t648);
                                    gm_f_format_onn3qvb((void *)&gm_l149, (void *)NULL, cx, NULL, t649);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t650 = (uint8_t)!((((uint8_t *)&gm_l149))[0] != 0);
                                    if (!t650) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l149) + 8, gm_srckfibkpt681aa, 591, 28);
                                    gm_b16_8 t651 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l149) + 8));
                                    gm_b16_8 t652 = GM_LD(gm_b16_8, ((uint8_t *)&t651));
                                    gm_tftb1odc739t5 t653 = gm_f_push_3ef746b(t645, t652);
                                    gm_l150 = t653;
                                    uint8_t t654 = (uint8_t)!((((uint8_t *)&gm_l150))[0] != 0);
                                    if (!t654) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l150) + 1, gm_srckfibkpt681aa, 591, 21);
                                    goto gm_lb28;
                                }
gm_lb43:;
                            }
                            {
                                {
                                    uint8_t t655 = GM_LD(uint8_t, ((((uint8_t *)t424) + 24) + 0));
                                    uint8_t t656 = (uint8_t)(t655 == ((uint8_t)2ULL));
                                    if (!t656) {
                                        goto gm_lb51;
                                    }
                                    void * t657 = (void *)((uint8_t *)&F.l4);
                                    gm_b16_8 t658;
                                    GM_ST(void *, (uint8_t *)&t658, (void *)(gm_spi406027g4jo));
                                    GM_ST(uintptr_t, (uint8_t *)&t658 + 8, (uintptr_t)(18));
                                    gm_tftb1odc739t5 t659 = gm_f_push_3ef746b(t657, t658);
                                    gm_l151 = t659;
                                    uint8_t t660 = (uint8_t)!((((uint8_t *)&gm_l151))[0] != 0);
                                    if (!t660) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l151) + 1, gm_srckfibkpt681aa, 594, 21);
                                    void * t661 = (void *)((uint8_t *)&F.l4);
                                    void * t662 = gm_l31;
                                    gm_b16_8 t663 = GM_LD(gm_b16_8, ((uint8_t *)t662));
                                    gm_tnpifv20irnph t664;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t664 + 0), t663);
                                    gm_f_format_gkjg5ld((void *)&gm_l152, (void *)NULL, cx, NULL, t664);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t665 = (uint8_t)!((((uint8_t *)&gm_l152))[0] != 0);
                                    if (!t665) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l152) + 8, gm_srckfibkpt681aa, 595, 61);
                                    gm_b16_8 t666 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l152) + 8));
                                    gm_b16_8 t667 = GM_LD(gm_b16_8, ((uint8_t *)&t666));
                                    gm_b16_8 t668 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t667);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tnpifv20irnph t669;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t669 + 0), t668);
                                    gm_f_format_nbg44fl((void *)&gm_l153, (void *)NULL, cx, NULL, t669);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t670 = (uint8_t)!((((uint8_t *)&gm_l153))[0] != 0);
                                    if (!t670) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l153) + 8, gm_srckfibkpt681aa, 595, 28);
                                    gm_b16_8 t671 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l153) + 8));
                                    gm_b16_8 t672 = GM_LD(gm_b16_8, ((uint8_t *)&t671));
                                    gm_tftb1odc739t5 t673 = gm_f_push_3ef746b(t661, t672);
                                    gm_l154 = t673;
                                    uint8_t t674 = (uint8_t)!((((uint8_t *)&gm_l154))[0] != 0);
                                    if (!t674) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l154) + 1, gm_srckfibkpt681aa, 595, 21);
                                    goto gm_lb28;
                                }
gm_lb51:;
                                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 552, 13);
                            }
                        }
gm_lb28:;
                    }
                    {
                        goto gm_lb3;
                    }
                }
gm_lb27:;
            }
            {
                {
                    void * t675 = (void *)((uint8_t *)&F.l4);
                    gm_b16_8 t676;
                    GM_ST(void *, (uint8_t *)&t676, (void *)(gm_spi406027g4jo));
                    GM_ST(uintptr_t, (uint8_t *)&t676 + 8, (uintptr_t)(18));
                    gm_tftb1odc739t5 t677 = gm_f_push_3ef746b(t675, t676);
                    gm_l155 = t677;
                    uint8_t t678 = (uint8_t)!((((uint8_t *)&gm_l155))[0] != 0);
                    if (!t678) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l155) + 1, gm_srckfibkpt681aa, 600, 13);
                    void * t679 = (void *)((uint8_t *)&F.l4);
                    void * t680 = gm_l0;
                    gm_t88rcucg1b7uk t681 = gm_l1;
                    gm_b16_8 t682 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t680, t681);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t683;
                    GM_ST(gm_b16_8, ((uint8_t *)&t683 + 0), t682);
                    gm_f_format_gkjg5ld((void *)&gm_l156, (void *)NULL, cx, NULL, t683);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t684 = (uint8_t)!((((uint8_t *)&gm_l156))[0] != 0);
                    if (!t684) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l156) + 8, gm_srckfibkpt681aa, 601, 53);
                    gm_b16_8 t685 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l156) + 8));
                    gm_b16_8 t686 = GM_LD(gm_b16_8, ((uint8_t *)&t685));
                    gm_b16_8 t687 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t686);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t688;
                    GM_ST(gm_b16_8, ((uint8_t *)&t688 + 0), t687);
                    gm_f_format_nbg44fl((void *)&gm_l157, (void *)NULL, cx, NULL, t688);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t689 = (uint8_t)!((((uint8_t *)&gm_l157))[0] != 0);
                    if (!t689) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l157) + 8, gm_srckfibkpt681aa, 601, 20);
                    gm_b16_8 t690 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l157) + 8));
                    gm_b16_8 t691 = GM_LD(gm_b16_8, ((uint8_t *)&t690));
                    gm_tftb1odc739t5 t692 = gm_f_push_3ef746b(t679, t691);
                    gm_l158 = t692;
                    uint8_t t693 = (uint8_t)!((((uint8_t *)&gm_l158))[0] != 0);
                    if (!t693) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l158) + 1, gm_srckfibkpt681aa, 601, 13);
                    goto gm_lb3;
                }
gm_lb52:;
                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 489, 5);
            }
        }
gm_lb3:;
    }
    {
        gm_trqfvmjtt4sij t694;
        gm_f_list_epvducg((void *)&t694, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l57 = t694;
        gm_b16_8 t695 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_l58 = t695;
        gm_l59 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t696 = gm_l59;
                uint64_t t697 = GM_LD(uint64_t, (((uint8_t *)&gm_l58) + 8));
                uint8_t t698 = (t696 < t697);
                if (!t698) {
                    goto gm_lb53;
                }
                {
                    void * t699 = GM_LD(void *, ((uint8_t *)&gm_l58));
                    uint64_t t700 = GM_LD(uint64_t, (((uint8_t *)&gm_l58) + 8));
                    uint64_t t701 = gm_l59;
                    {
                        {
                            gm_b16_8 t702 = GM_LD(gm_b16_8, (((uint8_t *)t699) + (uintptr_t)t701 * 16));
                            gm_l60 = t702;
                            void * t703 = (void *)((uint8_t *)&F.l57);
                            gm_b16_8 t704 = gm_l60;
                            gm_b16_8 t705;
                            GM_ST(void *, (uint8_t *)&t705, (void *)(gm_sl3q52ep6aa53));
                            GM_ST(uintptr_t, (uint8_t *)&t705 + 8, (uintptr_t)(1));
                            uint8_t t706 = gm_f_starts_with_6vvt01e(t704, t705);
                            if (t706) {
                                gm_b16_8 t707 = gm_l60;
                                gm_l159 = t707;
                            } else {
                                gm_b16_8 t708 = gm_l60;
                                gm_tnpifv20irnph t709;
                                GM_ST(gm_b16_8, ((uint8_t *)&t709 + 0), t708);
                                gm_f_format_g0ibdjv((void *)&gm_l160, (void *)NULL, cx, NULL, t709);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t710 = (uint8_t)!((((uint8_t *)&gm_l160))[0] != 0);
                                if (!t710) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l160) + 8, gm_srckfibkpt681aa, 605, 71);
                                gm_b16_8 t711 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l160) + 8));
                                gm_b16_8 t712 = GM_LD(gm_b16_8, ((uint8_t *)&t711));
                                gm_l159 = t712;
                            }
                            gm_b16_8 t713 = gm_l159;
                            gm_tftb1odc739t5 t714 = gm_f_push_3ef746b(t703, t713);
                            gm_l161 = t714;
                            uint8_t t715 = (uint8_t)!((((uint8_t *)&gm_l161))[0] != 0);
                            if (!t715) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l161) + 1, gm_srckfibkpt681aa, 605, 24);
                            goto gm_lb56;
                        }
gm_lb57:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 605, 5);
                    }
gm_lb56:;
                }
gm_lb55:;
                uint64_t t716 = gm_l59;
                uint64_t r717; bool o718 = __builtin_add_overflow(t716, ((uint64_t)1ULL), &r717);
                uint64_t t719 = r717;
                gm_l59 = t719;
            }
        }
gm_lb53:;
        void * t720 = gm_l0;
        void * t721 = (void *)(((uint8_t *)t720) + 368);
        gm_b16_8 t722 = gm_l3;
        gm_b16_8 t723 = GM_LD(gm_b16_8, ((uint8_t *)&F.l57));
        gm_b16_8 t724 = GM_LD(gm_b16_8, ((uint8_t *)&t723));
        gm_b16_8 t725;
        GM_ST(void *, (uint8_t *)&t725, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t725 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l162, (void *)NULL, cx, NULL, t724, t725);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t726 = (uint8_t)!((((uint8_t *)&gm_l162))[0] != 0);
        if (!t726) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l162) + 8, gm_srckfibkpt681aa, 606, 66);
        gm_b16_8 t727 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l162) + 8));
        gm_t6j6smj7qoctu t728;
        GM_ST(gm_b16_8, ((uint8_t *)&t728 + 0), t722);
        GM_ST(gm_b16_8, ((uint8_t *)&t728 + 16), t727);
        gm_tftb1odc739t5 t729 = gm_f_write_uf08lpv(t721, &t728);
        gm_l163 = t729;
        uint8_t t730 = (uint8_t)!((((uint8_t *)&gm_l163))[0] != 0);
        if (!t730) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l163) + 1, gm_srckfibkpt681aa, 606, 5);
        gm_b16_8 t731 = gm_l3;
        gm_tnpifv20irnph t732;
        GM_ST(gm_b16_8, ((uint8_t *)&t732 + 0), t731);
        gm_f_format_h6b2ofo((void *)&gm_l164, (void *)NULL, cx, NULL, t732);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t733 = (uint8_t)!((((uint8_t *)&gm_l164))[0] != 0);
        if (!t733) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l164) + 8, gm_srckfibkpt681aa, 607, 12);
        gm_b16_8 t734 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l164) + 8));
        gm_b16_8 t735 = GM_LD(gm_b16_8, ((uint8_t *)&t734));
        F.ret = t735;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_descriptors_fnnm3h1(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_field_cty_nfj8rcv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_field_cty_nfj8rcv F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* len */
    gm_t88rcucg1b7uk gm_l3; /* elem */
    gm_t6j6smj7qoctu gm_l4; /* inner */
    gm_b16_8 gm_l5; /* m */
    GM_STACK_CHECK(&F, 1102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t5v8n6qrn4rh4 gm_l6;
    gm_t53at80pfdus3 gm_l7;
    gm_ttu09lij36f8c gm_l8;
    gm_t6j6smj7qoctu gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_th2lqbo77pvsc gm_l11;
    gm_b16_8 gm_l12;
    void * t18 = gm_l0;
    gm_t88rcucg1b7uk t19 = gm_l1;
    gm_f_repr_53quqki((void *)&gm_l6, (void *)NULL, cx, NULL, t18, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)2ULL));
            if (!t21) {
                goto gm_lb2;
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_snokcvhtidt97));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(5));
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(0));
            gm_t6j6smj7qoctu t24;
            GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t22);
            GM_ST(gm_b16_8, ((uint8_t *)&t24 + 16), t23);
            gm_ttu09lij36f8c t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t25 + 36, 0);
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t25), t24);
            ((uint8_t *)&t25)[32] = 1;
            memmove(gm_out, ((uint8_t *)&t25), 40);
            goto gm_fdone;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
                    if (!t27) {
                        goto gm_lb5;
                    }
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
                    if (!t29) {
                        goto gm_lb6;
                    }
                    goto gm_lb4;
                }
gm_lb6:;
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)3ULL));
                    if (!t31) {
                        goto gm_lb7;
                    }
                    goto gm_lb4;
                }
gm_lb7:;
                goto gm_lb3;
            }
gm_lb4:;
            void * t32 = gm_l0;
            gm_t88rcucg1b7uk t33 = gm_l1;
            gm_b16_8 t34 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(0));
            gm_t6j6smj7qoctu t36;
            GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t34);
            GM_ST(gm_b16_8, ((uint8_t *)&t36 + 16), t35);
            gm_ttu09lij36f8c t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 36, 0);
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t37), t36);
            ((uint8_t *)&t37)[32] = 1;
            memmove(gm_out, ((uint8_t *)&t37), 40);
            goto gm_fdone;
        }
gm_lb3:;
        {
            {
                {
                    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t39 = (uint8_t)(t38 == ((uint8_t)4ULL));
                    if (!t39) {
                        goto gm_lb10;
                    }
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)5ULL));
                    if (!t41) {
                        goto gm_lb11;
                    }
                    goto gm_lb9;
                }
gm_lb11:;
                goto gm_lb8;
            }
gm_lb9:;
            gm_ttu09lij36f8c t42;
            memset(&t42, 0, sizeof t42);
            memmove(gm_out, ((uint8_t *)&t42), 40);
            goto gm_fdone;
        }
gm_lb8:;
        {
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)6ULL));
            if (!t44) {
                goto gm_lb12;
            }
            goto gm_lb1;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 414, 5);
    }
gm_lb1:;
    void * t45 = gm_l0;
    gm_t88rcucg1b7uk t46 = gm_l1;
    gm_f_kind_ghvav22((void *)&gm_l7, t45, GM_CONV(gm_csguankko4lmhd, t46));
    {
        {
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)11ULL));
            if (!t48) {
                goto gm_lb14;
            }
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            gm_l2 = t49;
            gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l7) + 8) + 8));
            gm_l3 = t50;
            uint64_t t51 = gm_l2;
            uint8_t t52 = ((t51 == ((uint64_t)0ULL)));
            if (t52) {
                gm_ttu09lij36f8c t53;
                memset(&t53, 0, sizeof t53);
                memmove(gm_out, ((uint8_t *)&t53), 40);
                goto gm_fdone;
            }
            void * t54 = gm_l0;
            gm_t88rcucg1b7uk t55 = gm_l3;
            gm_f_field_cty_nfj8rcv((void *)&gm_l8, (void *)NULL, cx, NULL, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t56 = (uint8_t)((((uint8_t *)&gm_l8))[32] != 0);
            if (t56) {
                memmove(((uint8_t *)&gm_l9), ((uint8_t *)&gm_l8), 32);
            } else {
                gm_ttu09lij36f8c t57;
                memset(&t57, 0, sizeof t57);
                memmove(gm_out, ((uint8_t *)&t57), 40);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l9), 32);
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            uint64_t t59 = gm_l2;
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
            gm_tf1bjg81u0glv t61;
            GM_ST(uint64_t, ((uint8_t *)&t61 + 0), t59);
            GM_ST(gm_b16_8, ((uint8_t *)&t61 + 8), t60);
            gm_f_format_6a91mh8((void *)&gm_l10, (void *)NULL, cx, NULL, &t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t62) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srckfibkpt681aa, 424, 51);
            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
            gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
            gm_t6j6smj7qoctu t65;
            GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t58);
            GM_ST(gm_b16_8, ((uint8_t *)&t65 + 16), t64);
            gm_ttu09lij36f8c t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t66 + 36, 0);
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t66), t65);
            ((uint8_t *)&t66)[32] = 1;
            memmove(gm_out, ((uint8_t *)&t66), 40);
            goto gm_fdone;
        }
gm_lb14:;
        {
            uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t68 = (uint8_t)(t67 == ((uint8_t)17ULL));
            if (!t68) {
                goto gm_lb15;
            }
            void * t69 = gm_l0;
            gm_t88rcucg1b7uk t70 = gm_l1;
            gm_f_mirror_lcjegsf((void *)&gm_l11, (void *)NULL, cx, NULL, t69, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t71 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
            if (t71) {
                gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                gm_l12 = t72;
            } else {
                gm_ttu09lij36f8c t73;
                memset(&t73, 0, sizeof t73);
                memmove(gm_out, ((uint8_t *)&t73), 40);
                goto gm_fdone;
            }
            gm_b16_8 t74 = gm_l12;
            gm_l5 = t74;
            gm_b16_8 t75 = gm_l5;
            gm_b16_8 t76;
            GM_ST(void *, (uint8_t *)&t76, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(0));
            gm_t6j6smj7qoctu t77;
            GM_ST(gm_b16_8, ((uint8_t *)&t77 + 0), t75);
            GM_ST(gm_b16_8, ((uint8_t *)&t77 + 16), t76);
            gm_ttu09lij36f8c t78;
            GM_ST(uint8_t, (uint8_t *)&t78 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t78 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t78 + 36, 0);
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t78), t77);
            ((uint8_t *)&t78)[32] = 1;
            memmove(gm_out, ((uint8_t *)&t78), 40);
            goto gm_fdone;
        }
gm_lb15:;
        {
            gm_ttu09lij36f8c t79;
            memset(&t79, 0, sizeof t79);
            memmove(gm_out, ((uint8_t *)&t79), 40);
            goto gm_fdone;
        }
gm_lb16:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 420, 5);
    }
gm_lb13:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fields_array_tikg33t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_fields_array_tikg33t F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* i */
    gm_b16_8 gm_l2; /* parts */
    void * gm_l3; /* f */
    GM_STACK_CHECK(&F, 416u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l4;
    gm_tabb710d9mq2e gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tabb710d9mq2e gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tftb1odc739t5 gm_l10;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        void * t4 = gm_l3;
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_so3qrsvnd4euf));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(11));
        gm_tftb1odc739t5 t6 = gm_f_push_3ef746b(t4, t5);
        gm_l4 = t6;
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srckfibkpt681aa, 612, 9);
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = (void *)(((uint8_t *)t8) + 328);
    gm_b16_8 t10 = gm_l1;
    gm_b16_8 t11 = gm_l2;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sg5gdnrith2i6));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(2));
    gm_f_join_sfknljb((void *)&gm_l5, (void *)NULL, cx, NULL, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srckfibkpt681aa, 615, 72);
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_t6j6smj7qoctu t15;
    GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t10);
    GM_ST(gm_b16_8, ((uint8_t *)&t15 + 16), t14);
    gm_tftb1odc739t5 t16 = gm_f_write_3mmgun5(t9, &t15);
    gm_l6 = t16;
    uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srckfibkpt681aa, 615, 5);
    void * t18 = gm_l3;
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_t4ce0jd3tdro4 t20;
    GM_ST(uint64_t, ((uint8_t *)&t20 + 0), t19);
    gm_f_format_aiomp5b((void *)&gm_l7, (void *)NULL, cx, NULL, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srckfibkpt681aa, 616, 12);
    gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
    gm_tftb1odc739t5 t24 = gm_f_push_3ef746b(t18, t23);
    gm_l8 = t24;
    uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srckfibkpt681aa, 616, 5);
    void * t26 = gm_l3;
    gm_b16_8 t27 = gm_l1;
    gm_tnpifv20irnph t28;
    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t27);
    gm_f_format_s51nisc((void *)&gm_l9, (void *)NULL, cx, NULL, t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srckfibkpt681aa, 617, 12);
    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
    gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
    gm_tftb1odc739t5 t32 = gm_f_push_3ef746b(t26, t31);
    gm_l10 = t32;
    uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srckfibkpt681aa, 617, 5);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0h3rgr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_int_cty_s62af0i(uint8_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_cty_s62af0i F;
    uint8_t gm_l0; /* signed */
    uint64_t gm_l1; /* storage */
    GM_STACK_CHECK_SHALLOW(&F, 434u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l2;
    gm_b16_8 gm_l3;
    gm_b16_8 gm_l4;
    gm_b16_8 gm_l5;
    gm_b16_8 gm_l6;
    gm_b16_8 gm_l7;
    {
        {
            uint64_t t8 = gm_l1;
            uint8_t t9 = ((t8 == ((uint64_t)1ULL)));
            if (!t9) {
                goto gm_lb2;
            }
            uint8_t t10 = gm_l0;
            if (t10) {
                gm_b16_8 t11;
                GM_ST(void *, (uint8_t *)&t11, (void *)(gm_suuq26jjqhul4));
                GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(6));
                gm_l3 = t11;
            } else {
                gm_b16_8 t12;
                GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s9grtrvtmi962));
                GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(7));
                gm_l3 = t12;
            }
            gm_b16_8 t13 = gm_l3;
            gm_l2 = t13;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint64_t t14 = gm_l1;
            uint8_t t15 = ((t14 == ((uint64_t)2ULL)));
            if (!t15) {
                goto gm_lb3;
            }
            uint8_t t16 = gm_l0;
            if (t16) {
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(gm_std3g89n1j9ln));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(7));
                gm_l4 = t17;
            } else {
                gm_b16_8 t18;
                GM_ST(void *, (uint8_t *)&t18, (void *)(gm_suggn3kp9dfmg));
                GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(8));
                gm_l4 = t18;
            }
            gm_b16_8 t19 = gm_l4;
            gm_l2 = t19;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint64_t t20 = gm_l1;
            uint8_t t21 = ((t20 == ((uint64_t)4ULL)));
            if (!t21) {
                goto gm_lb4;
            }
            uint8_t t22 = gm_l0;
            if (t22) {
                gm_b16_8 t23;
                GM_ST(void *, (uint8_t *)&t23, (void *)(gm_ska87i9f7924f));
                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(7));
                gm_l5 = t23;
            } else {
                gm_b16_8 t24;
                GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s6r94a2k1goos));
                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(8));
                gm_l5 = t24;
            }
            gm_b16_8 t25 = gm_l5;
            gm_l2 = t25;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint64_t t26 = gm_l1;
            uint8_t t27 = ((t26 == ((uint64_t)8ULL)));
            if (!t27) {
                goto gm_lb5;
            }
            uint8_t t28 = gm_l0;
            if (t28) {
                gm_b16_8 t29;
                GM_ST(void *, (uint8_t *)&t29, (void *)(gm_so4g53vn8c2d4));
                GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(7));
                gm_l6 = t29;
            } else {
                gm_b16_8 t30;
                GM_ST(void *, (uint8_t *)&t30, (void *)(gm_shn8mjp9kc2n2));
                GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(8));
                gm_l6 = t30;
            }
            gm_b16_8 t31 = gm_l6;
            gm_l2 = t31;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t32 = gm_l0;
            if (t32) {
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s1a1r1qe3ssot));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(8));
                gm_l7 = t33;
            } else {
                gm_b16_8 t34;
                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_st9g0aublffth));
                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(17));
                gm_l7 = t34;
            }
            gm_b16_8 t35 = gm_l7;
            gm_l2 = t35;
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 629, 56);
    }
gm_lb1:;
    gm_b16_8 t36 = gm_l2;
    F.ret = t36;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ty_7nma1b7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
extern __attribute__((aligned(4))) uint8_t gm_f_is_float_gthejtb(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_ige9b74(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_fm7q9br(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_vector_fm7q9br F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    gm_t53at80pfdus3 gm_l3;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = (void *)(((uint8_t *)t7) + 8);
    gm_t88rcucg1b7uk t9 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l3, t8, GM_CONV(gm_csguankko4lmhd, t9));
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)11ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint8_t t12 = GM_LD(uint8_t, (((((uint8_t *)&gm_l3) + 8) + 12) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)2ULL));
            if (!t13) {
                goto gm_lb2;
            }
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)11ULL));
            if (!t15) {
                goto gm_lb3;
            }
            uint8_t t16 = GM_LD(uint8_t, (((((uint8_t *)&gm_l3) + 8) + 12) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)3ULL));
            if (!t17) {
                goto gm_lb3;
            }
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 115, 54);
    }
gm_lb1:;
    uint8_t t18 = gm_l2;
    F.ret = t18;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1);
extern __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t53at80pfdus3 gm_f_kind_ghvav22_c(void * a0, gm_csguankko4lmhd a1) { gm_t53at80pfdus3 gm_r; gm_f_kind_ghvav22(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_rank_g1kdlst F;
    gm_tl572d7579hns gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l1;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)0ULL));
            if (!t8) {
                goto gm_lb2;
            }
            gm_l1 = ((uint8_t)0ULL);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)1ULL));
            if (!t10) {
                goto gm_lb3;
            }
            gm_l1 = ((uint8_t)1ULL);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)2ULL));
            if (!t12) {
                goto gm_lb4;
            }
            gm_l1 = ((uint8_t)2ULL);
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)3ULL));
            if (!t14) {
                goto gm_lb5;
            }
            gm_l1 = ((uint8_t)3ULL);
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 691, 39);
    }
gm_lb1:;
    uint8_t t15 = gm_l1;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_kind_rank_g1kdlst_c(gm_tl572d7579hns a0) { return gm_f_kind_rank_g1kdlst(&a0); }
__attribute__((aligned(4))) gm_b16_8 gm_f_make_cty_3kvgdd5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_make_cty_3kvgdd5 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* signed */
    uint64_t gm_l3; /* storage */
    gm_b16_8 gm_l4; /* n */
    uint64_t gm_l5; /* s */
    uint64_t gm_l6; /* a */
    gm_tl572d7579hns gm_l7; /* kind */
    gm_b16_8 gm_l8; /* name */
    GM_STACK_CHECK(&F, 773u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t5v8n6qrn4rh4 gm_l9;
    gm_th2lqbo77pvsc gm_l10;
    uint8_t gm_l11;
    gm_tqs5p4n2tq19v gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    void * t17 = gm_l0;
    gm_t88rcucg1b7uk t18 = gm_l1;
    gm_f_repr_53quqki((void *)&gm_l9, (void *)NULL, cx, NULL, t17, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)0ULL));
            if (!t20) {
                goto gm_lb2;
            }
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 8));
            gm_l2 = t21;
            uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)&gm_l9) + 8) + 8));
            gm_l3 = t22;
            uint8_t t23 = gm_l2;
            uint64_t t24 = gm_l3;
            gm_b16_8 t25 = gm_f_int_cty_s62af0i(t23, t24);
            F.ret = t25;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)1ULL));
            if (!t27) {
                goto gm_lb3;
            }
            uint16_t t28 = GM_LD(uint16_t, (((uint8_t *)&gm_l9) + 8));
            uint8_t t29 = ((t28 == ((uint16_t)16ULL)));
            if (!t29) {
                goto gm_lb3;
            }
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_scj0sm2kpivk4));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(8));
            F.ret = t30;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)1ULL));
            if (!t32) {
                goto gm_lb4;
            }
            uint16_t t33 = GM_LD(uint16_t, (((uint8_t *)&gm_l9) + 8));
            uint8_t t34 = ((t33 == ((uint16_t)32ULL)));
            if (!t34) {
                goto gm_lb4;
            }
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s4cess8gum6i7));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(5));
            F.ret = t35;
            goto gm_fret;
        }
gm_lb4:;
        {
            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)1ULL));
            if (!t37) {
                goto gm_lb5;
            }
            gm_b16_8 t38;
            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s866ofni9sb8u));
            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(6));
            F.ret = t38;
            goto gm_fret;
        }
gm_lb5:;
        {
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)2ULL));
            if (!t40) {
                goto gm_lb6;
            }
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s9grtrvtmi962));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(7));
            F.ret = t41;
            goto gm_fret;
        }
gm_lb6:;
        {
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)3ULL));
            if (!t43) {
                goto gm_lb7;
            }
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s44ek1gecbajs));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(6));
            F.ret = t44;
            goto gm_fret;
        }
gm_lb7:;
        {
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)4ULL));
            if (!t46) {
                goto gm_lb8;
            }
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_s6g2mpli2mu72));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(10));
            F.ret = t47;
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t49 = (uint8_t)(t48 == ((uint8_t)5ULL));
            if (!t49) {
                goto gm_lb9;
            }
            gm_b16_8 t50;
            GM_ST(void *, (uint8_t *)&t50, (void *)(gm_smiavg3q7ucd9));
            GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(7));
            F.ret = t50;
            goto gm_fret;
        }
gm_lb9:;
        {
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)6ULL));
            if (!t52) {
                goto gm_lb10;
            }
            {
                {
                    void * t53 = gm_l0;
                    gm_t88rcucg1b7uk t54 = gm_l1;
                    gm_f_typed_stkcgvr((void *)&gm_l10, (void *)NULL, cx, NULL, t53, t54);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t55 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
                    if (!t55) {
                        goto gm_lb12;
                    }
                    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
                    gm_l4 = t56;
                    gm_b16_8 t57 = gm_l4;
                    F.ret = t57;
                    goto gm_fret;
                }
gm_lb12:;
            }
gm_lb11:;
            void * t58 = gm_l0;
            gm_t88rcucg1b7uk t59 = gm_l1;
            uint64_t t60 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t60;
            void * t61 = gm_l0;
            gm_t88rcucg1b7uk t62 = gm_l1;
            uint64_t t63 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t64 = (t63 > ((uint64_t)1ULL) ? t63 : ((uint64_t)1ULL));
            gm_l6 = t64;
            void * t65 = gm_l0;
            gm_t88rcucg1b7uk t66 = gm_l1;
            uint64_t t67 = gm_l5;
            uint64_t t68 = gm_l6;
            gm_f_blob_kind_kp13j9k((void *)&gm_l7, (void *)NULL, cx, NULL, t65, t66, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t69 = gm_l5;
            uint64_t t70 = gm_l6;
            gm_b16_8 t71 = gm_f_blob_name_sjpa1ub((void *)NULL, cx, NULL, t69, t70, &(*(gm_tl572d7579hns *)((uint8_t *)&gm_l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t71;
            void * t72 = gm_l0;
            void * t73 = (void *)(((uint8_t *)t72) + 120);
            gm_b16_8 t74 = gm_l8;
            gm_tqs5p4n2tq19v t75 = gm_f_get_p217beu(t73, t74);
            gm_l12 = t75;
            {
                {
                    uint8_t t76 = (uint8_t)((((uint8_t *)&gm_l12))[1] != 0);
                    if (t76) {
                        goto gm_lb14;
                    }
                    gm_l11 = ((uint8_t)1);
                    goto gm_lb13;
                }
gm_lb14:;
                {
                    gm_l11 = ((uint8_t)0);
                    goto gm_lb13;
                }
gm_lb15:;
                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 184, 16);
            }
gm_lb13:;
            uint8_t t77 = gm_l11;
            if (t77) {
                void * t78 = gm_l0;
                void * t79 = (void *)(((uint8_t *)t78) + 120);
                gm_b16_8 t80 = gm_l8;
                gm_tftb1odc739t5 t81 = gm_f_put_5c6qgui(t79, t80, ((uint8_t)1));
                gm_l13 = t81;
                uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srckfibkpt681aa, 185, 17);
                void * t83 = gm_l0;
                void * t84 = (void *)(((uint8_t *)t83) + 80);
                uint64_t t85 = gm_l5;
                uint64_t t86 = gm_l6;
                void * t87 = gm_l0;
                uint64_t t88 = gm_l5;
                uint64_t t89 = gm_l6;
                gm_b16_8 t90 = gm_l8;
                gm_tl572d7579hns t91 = GM_LD(gm_tl572d7579hns, ((uint8_t *)&gm_l7));
                gm_b16_8 t92 = gm_f_blob_typedef_8ta3rbi((void *)NULL, cx, NULL, t87, t88, t89, &(*(gm_tl572d7579hns *)((uint8_t *)&gm_l7)), t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tulpvg6gm4fqa t93;
                GM_ST(uint64_t, ((uint8_t *)&t93 + 0), t85);
                GM_ST(uint64_t, ((uint8_t *)&t93 + 8), t86);
                GM_ST(gm_tl572d7579hns, ((uint8_t *)&t93 + 16), t91);
                GM_ST(gm_b16_8, ((uint8_t *)&t93 + 40), t92);
                gm_tftb1odc739t5 t94 = gm_f_push_pmmlu39(t84, &t93);
                gm_l14 = t94;
                uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srckfibkpt681aa, 186, 17);
            }
            gm_b16_8 t96 = gm_l8;
            F.ret = t96;
            goto gm_fret;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 169, 5);
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
__attribute__((aligned(4))) void gm_f_make_mirror_rudvl6p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_make_mirror_rudvl6p F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    gm_b16_8 gm_l3; /* keyword */
    gm_b16_8 gm_l4; /* fields */
    uint8_t gm_l5; /* packed */
    gm_tf5srh4mp4b3i gm_l6; /* explicit */
    gm_t88rcucg1b7uk gm_l8; /* u */
    gm_b16_8 gm_l9; /* fs */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_th2lqbo77pvsc gm_l19; /* n */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    gm_t6j6smj7qoctu gm_l28; /* fc */
    uint64_t gm_l29; /* a */
    gm_b16_8 gm_l30; /* al */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    gm_b16_8 gm_l33; /* n */
    uint64_t gm_l35; /* a */
    gm_b16_8 gm_l36; /* attr_text */
    gm_b16_8 gm_l37; /* content */
    gm_b16_8 gm_l38; /* n */
    gm_b16_8 gm_l39; /* name */
    gm_b16_8 gm_l40; /* offs */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* i */
    GM_STACK_CHECK(&F, 4220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l44;
    uint8_t gm_l45;
    gm_tftb1odc739t5 gm_l46;
    gm_t13urt52695sh gm_l47;
    gm_b16_8 gm_l48;
    gm_tabb710d9mq2e gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tabb710d9mq2e gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_t8ta482bl6be0 gm_l53;
    gm_ttu09lij36f8c gm_l54;
    gm_t6j6smj7qoctu gm_l55;
    gm_b16_8 gm_l56;
    gm_tabb710d9mq2e gm_l57;
    gm_tabb710d9mq2e gm_l58;
    gm_tftb1odc739t5 gm_l59;
    uint8_t gm_l60;
    gm_th2lqbo77pvsc gm_l61;
    gm_tftb1odc739t5 gm_l62;
    gm_tabb710d9mq2e gm_l63;
    gm_tftb1odc739t5 gm_l64;
    gm_b16_8 gm_l65;
    gm_tabb710d9mq2e gm_l66;
    gm_tabb710d9mq2e gm_l67;
    gm_tabb710d9mq2e gm_l68;
    gm_tabb710d9mq2e gm_l69;
    gm_th2lqbo77pvsc gm_l70;
    gm_tftb1odc739t5 gm_l71;
    gm_tabb710d9mq2e gm_l72;
    gm_tftb1odc739t5 gm_l73;
    gm_tftb1odc739t5 gm_l74;
    gm_tftb1odc739t5 gm_l75;
    gm_tabb710d9mq2e gm_l76;
    gm_tftb1odc739t5 gm_l77;
    {
        {
            {
                {
                    void * t49 = gm_l0;
                    gm_t88rcucg1b7uk t50 = gm_l1;
                    gm_f_kind_ghvav22((void *)&gm_l44, t49, GM_CONV(gm_csguankko4lmhd, t50));
                    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l44) + 0));
                    uint8_t t52 = (uint8_t)(t51 == ((uint8_t)17ULL));
                    if (!t52) {
                        goto gm_lb2;
                    }
                    uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)&gm_l44) + 8));
                    gm_l2 = t53;
                    gm_b16_8 t54;
                    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sc49g68j2cekh));
                    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(6));
                    gm_l3 = t54;
                    gm_b16_8 t55 = gm_f_empty_slice_ose1ba9();
                    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                    gm_l4 = t56;
                    gm_l5 = ((uint8_t)0);
                    gm_tf5srh4mp4b3i t57;
                    memset(&t57, 0, sizeof t57);
                    gm_l6 = t57;
                    void * t58 = gm_l0;
                    void * t59 = GM_LD(void *, ((uint8_t *)t58));
                    void * t60 = GM_LD(void *, (((uint8_t *)t59) + 1088));
                    uint64_t t61 = GM_LD(uint64_t, ((((uint8_t *)t59) + 1088) + 8));
                    uint32_t t62 = gm_l2;
                    uint64_t t63 = (uint64_t)t62;
                    if ((uintptr_t)t63 >= (uintptr_t)(t61)) gm_trap_index((uintptr_t)t63, t61, gm_srckfibkpt681aa, 351, 11);
                    {
                        {
                            uint8_t t64 = GM_LD(uint8_t, (((((uint8_t *)t60) + (uintptr_t)t63 * 184) + 24) + 0));
                            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)0ULL));
                            if (!t65) {
                                goto gm_lb4;
                            }
                            memmove(((uint8_t *)&F.l7), (((((uint8_t *)t60) + (uintptr_t)t63 * 184) + 24) + 8), 48);
                            {
                                {
                                    uint8_t t66 = (uint8_t)(((((uint8_t *)&F.l7) + 40))[4] != 0);
                                    if (!t66) {
                                        goto gm_lb6;
                                    }
                                    gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 40));
                                    gm_l8 = t67;
                                    void * t68 = gm_l0;
                                    gm_t88rcucg1b7uk t69 = gm_l8;
                                    gm_b16_8 t70 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t68, t69);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_th2lqbo77pvsc t71;
                                    GM_ST(uint8_t, (uint8_t *)&t71 + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t71 + 18, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t71 + 20, 0);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t71), t70);
                                    ((uint8_t *)&t71)[16] = 1;
                                    memmove(gm_out, ((uint8_t *)&t71), 24);
                                    goto gm_fdone;
                                }
gm_lb6:;
                            }
gm_lb5:;
                            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                            gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                            gm_l4 = t73;
                            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 16));
                            gm_l5 = t74;
                            gm_tf5srh4mp4b3i t75 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l7) + 24));
                            gm_l6 = t75;
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t76 = GM_LD(uint8_t, (((((uint8_t *)t60) + (uintptr_t)t63 * 184) + 24) + 0));
                            uint8_t t77 = (uint8_t)(t76 == ((uint8_t)2ULL));
                            if (!t77) {
                                goto gm_lb7;
                            }
                            gm_b16_8 t78 = GM_LD(gm_b16_8, (((((uint8_t *)t60) + (uintptr_t)t63 * 184) + 24) + 8));
                            gm_l9 = t78;
                            gm_b16_8 t79;
                            GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s8p1h96ji9kh0));
                            GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(5));
                            gm_l3 = t79;
                            gm_b16_8 t80 = gm_l9;
                            gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
                            gm_l4 = t81;
                            goto gm_lb3;
                        }
gm_lb7:;
                        {
                            uint8_t t82 = GM_LD(uint8_t, (((((uint8_t *)t60) + (uintptr_t)t63 * 184) + 24) + 0));
                            uint8_t t83 = (uint8_t)(t82 == ((uint8_t)1ULL));
                            if (!t83) {
                                goto gm_lb8;
                            }
                            gm_th2lqbo77pvsc t84;
                            memset(&t84, 0, sizeof t84);
                            memmove(gm_out, ((uint8_t *)&t84), 24);
                            goto gm_fdone;
                        }
gm_lb8:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 351, 5);
                    }
gm_lb3:;
                }
                {
                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t86 = ((t85 == ((uint64_t)0ULL)));
                    if (t86) {
                        gm_l45 = ((uint8_t)1);
                    } else {
                        void * t87 = gm_l0;
                        gm_t88rcucg1b7uk t88 = gm_l1;
                        uint64_t t89 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t87, t88);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t90 = ((t89 == ((uint64_t)0ULL)));
                        gm_l45 = t90;
                    }
                    uint8_t t91 = gm_l45;
                    if (t91) {
                        gm_th2lqbo77pvsc t92;
                        memset(&t92, 0, sizeof t92);
                        memmove(gm_out, ((uint8_t *)&t92), 24);
                        goto gm_fdone;
                    }
                    gm_trqfvmjtt4sij t93;
                    gm_f_list_epvducg((void *)&t93, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l10 = t93;
                    void * t94 = gm_l0;
                    uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)t94) + 808));
                    if (t95) {
                        gm_trqfvmjtt4sij t96;
                        gm_f_list_epvducg((void *)&t96, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l11 = t96;
                        gm_b16_8 t97 = gm_l4;
                        gm_l12 = t97;
                        gm_l13 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t98 = gm_l13;
                                uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint8_t t100 = (t98 < t99);
                                if (!t100) {
                                    goto gm_lb9;
                                }
                                {
                                    void * t101 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                    uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                    uint64_t t103 = gm_l13;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l14), (((uint8_t *)t101) + (uintptr_t)t103 * 40), 40);
                                            void * t104 = (void *)((uint8_t *)&F.l11);
                                            gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                            gm_tftb1odc739t5 t106 = gm_f_push_3ef746b(t104, t105);
                                            gm_l46 = t106;
                                            uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                            if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_srckfibkpt681aa, 369, 27);
                                            goto gm_lb12;
                                        }
gm_lb13:;
                                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 369, 9);
                                    }
gm_lb12:;
                                }
gm_lb11:;
                                uint64_t t108 = gm_l13;
                                uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                                uint64_t t111 = r109;
                                gm_l13 = t111;
                            }
                        }
gm_lb9:;
                        gm_t4c7ipjj9s3fp t112;
                        gm_f_hash_map_cuh4fs9((void *)&t112, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l15 = t112;
                        gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                        gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                        void * t115 = (void *)((uint8_t *)&F.l15);
                        gm_b16_8 t116 = gm_f_unique_members_mrojmjf((void *)NULL, cx, NULL, t114, t115);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
                        gm_l16 = t117;
                        gm_l17 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t118 = gm_l17;
                                uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                uint8_t t120 = (t118 < t119);
                                if (!t120) {
                                    goto gm_lb14;
                                }
                                {
                                    uint64_t t121 = gm_l17;
                                    void * t122 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                    uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                    uint64_t t124 = gm_l17;
                                    gm_th2lqbo77pvsc t125 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t122) + (uintptr_t)t124 * 24));
                                    gm_t13urt52695sh t126;
                                    GM_ST(uint64_t, ((uint8_t *)&t126 + 0), t121);
                                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t126 + 8), t125);
                                    gm_l47 = t126;
                                    {
                                        {
                                            uint64_t t127 = GM_LD(uint64_t, ((uint8_t *)&gm_l47));
                                            gm_l18 = t127;
                                            memmove(((uint8_t *)&gm_l19), (((uint8_t *)&gm_l47) + 8), 24);
                                            void * t128 = (void *)((uint8_t *)&F.l10);
                                            uint8_t t129 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
                                            if (t129) {
                                                gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
                                                gm_l48 = t130;
                                            } else {
                                                uint64_t t131 = gm_l18;
                                                gm_t4ce0jd3tdro4 t132;
                                                GM_ST(uint64_t, ((uint8_t *)&t132 + 0), t131);
                                                gm_f_format_tc7tn98((void *)&gm_l49, (void *)NULL, cx, NULL, t132);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t133 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                                if (!t133) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 8, gm_srckfibkpt681aa, 371, 94);
                                                gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l49) + 8));
                                                gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                                                gm_l48 = t135;
                                            }
                                            gm_b16_8 t136 = gm_l48;
                                            gm_tftb1odc739t5 t137 = gm_f_push_3ef746b(t128, t136);
                                            gm_l50 = t137;
                                            uint8_t t138 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                                            if (!t138) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srckfibkpt681aa, 371, 76);
                                            goto gm_lb17;
                                        }
gm_lb18:;
                                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 371, 9);
                                    }
gm_lb17:;
                                }
gm_lb16:;
                                uint64_t t139 = gm_l17;
                                uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                                uint64_t t142 = r140;
                                gm_l17 = t142;
                            }
                        }
gm_lb14:;
                    } else {
                        gm_l21 = ((uint64_t)0ULL);
                        uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        gm_l20 = t143;
                        {
                            for (;;) {
                                uint64_t t144 = gm_l21;
                                uint64_t t145 = gm_l20;
                                uint8_t t146 = (t144 < t145);
                                if (!t146) {
                                    goto gm_lb19;
                                }
                                {
                                    {
                                        {
                                            uint64_t t147 = gm_l21;
                                            gm_l22 = t147;
                                            void * t148 = (void *)((uint8_t *)&F.l10);
                                            uint64_t t149 = gm_l22;
                                            gm_t4ce0jd3tdro4 t150;
                                            GM_ST(uint64_t, ((uint8_t *)&t150 + 0), t149);
                                            gm_f_format_tc7tn98((void *)&gm_l51, (void *)NULL, cx, NULL, t150);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t151 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                                            if (!t151) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 8, gm_srckfibkpt681aa, 373, 45);
                                            gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                                            gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
                                            gm_tftb1odc739t5 t154 = gm_f_push_3ef746b(t148, t153);
                                            gm_l52 = t154;
                                            uint8_t t155 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                                            if (!t155) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_srckfibkpt681aa, 373, 34);
                                            goto gm_lb22;
                                        }
gm_lb23:;
                                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 373, 9);
                                    }
gm_lb22:;
                                }
gm_lb21:;
                                uint64_t t156 = gm_l21;
                                uint64_t r157; bool o158 = __builtin_add_overflow(t156, ((uint64_t)1ULL), &r157);
                                uint64_t t159 = r157;
                                gm_l21 = t159;
                            }
                        }
gm_lb19:;
                    }
                }
                {
                    gm_trqfvmjtt4sij t160;
                    gm_f_list_epvducg((void *)&t160, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l23 = t160;
                    gm_b16_8 t161 = gm_l4;
                    gm_l24 = t161;
                    gm_l25 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t162 = gm_l25;
                            uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                            uint8_t t164 = (t162 < t163);
                            if (!t164) {
                                goto gm_lb24;
                            }
                            {
                                uint64_t t165 = gm_l25;
                                void * t166 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                uint64_t t168 = gm_l25;
                                gm_t0mglksrkfa5e t169 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t166) + (uintptr_t)t168 * 40));
                                gm_t8ta482bl6be0 t170;
                                GM_ST(uint64_t, ((uint8_t *)&t170 + 0), t165);
                                GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&t170 + 8), t169);
                                gm_l53 = t170;
                                {
                                    {
                                        uint64_t t171 = GM_LD(uint64_t, ((uint8_t *)&gm_l53));
                                        gm_l26 = t171;
                                        memmove(((uint8_t *)&F.l27), (((uint8_t *)&gm_l53) + 8), 40);
                                        void * t172 = gm_l0;
                                        gm_t88rcucg1b7uk t173 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                                        gm_f_field_cty_nfj8rcv((void *)&gm_l54, (void *)NULL, cx, NULL, t172, t173);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t174 = (uint8_t)((((uint8_t *)&gm_l54))[32] != 0);
                                        if (t174) {
                                            memmove(((uint8_t *)&gm_l55), ((uint8_t *)&gm_l54), 32);
                                        } else {
                                            gm_th2lqbo77pvsc t175;
                                            memset(&t175, 0, sizeof t175);
                                            memmove(gm_out, ((uint8_t *)&t175), 24);
                                            goto gm_fdone;
                                        }
                                        memmove(((uint8_t *)&gm_l28), ((uint8_t *)&gm_l55), 32);
                                        {
                                            {
                                                uint8_t t176 = (uint8_t)(((((uint8_t *)&F.l27) + 24))[8] != 0);
                                                if (!t176) {
                                                    goto gm_lb30;
                                                }
                                                uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&F.l27) + 24));
                                                gm_l29 = t177;
                                                uint64_t t178 = gm_l29;
                                                gm_t4ce0jd3tdro4 t179;
                                                GM_ST(uint64_t, ((uint8_t *)&t179 + 0), t178);
                                                gm_f_format_irg2n6e((void *)&gm_l57, (void *)NULL, cx, NULL, t179);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t180 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                                if (!t180) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 8, gm_srckfibkpt681aa, 378, 40);
                                                gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l57) + 8));
                                                gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
                                                gm_l56 = t182;
                                                goto gm_lb29;
                                            }
gm_lb30:;
                                            gm_b16_8 t183;
                                            GM_ST(void *, (uint8_t *)&t183, (void *)(gm_sr4qbrk6tlgaa));
                                            GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(0));
                                            gm_l56 = t183;
                                        }
gm_lb29:;
                                        gm_b16_8 t184 = gm_l56;
                                        gm_l30 = t184;
                                        void * t185 = (void *)((uint8_t *)&F.l23);
                                        gm_b16_8 t186 = gm_l30;
                                        gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l28));
                                        void * t188 = GM_LD(void *, ((uint8_t *)&F.l10));
                                        uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                        uint64_t t190 = gm_l26;
                                        if ((uintptr_t)t190 >= (uintptr_t)(t189)) gm_trap_index((uintptr_t)t190, t189, gm_srckfibkpt681aa, 379, 59);
                                        gm_b16_8 t191 = GM_LD(gm_b16_8, (((uint8_t *)t188) + (uintptr_t)t190 * 16));
                                        gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 16));
                                        gm_t0uuhaljsf410 t193;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t193 + 0), t186);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t193 + 16), t187);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t193 + 32), t191);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t193 + 48), t192);
                                        gm_f_format_tra8sop((void *)&gm_l58, (void *)NULL, cx, NULL, &t193);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t194 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                        if (!t194) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 8, gm_srckfibkpt681aa, 379, 22);
                                        gm_b16_8 t195 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                                        gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
                                        gm_tftb1odc739t5 t197 = gm_f_push_3ef746b(t185, t196);
                                        gm_l59 = t197;
                                        uint8_t t198 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                        if (!t198) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srckfibkpt681aa, 379, 9);
                                        goto gm_lb27;
                                    }
gm_lb28:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 376, 5);
                                }
gm_lb27:;
                            }
gm_lb26:;
                            uint64_t t199 = gm_l25;
                            uint64_t r200; bool o201 = __builtin_add_overflow(t199, ((uint64_t)1ULL), &r200);
                            uint64_t t202 = r200;
                            gm_l25 = t202;
                        }
                    }
gm_lb24:;
                }
                {
                    void * t203 = gm_l0;
                    uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)t203) + 808));
                    if (t204) {
                        gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                        gm_l31 = t205;
                        gm_l32 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t206 = gm_l32;
                                uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                uint8_t t208 = (t206 < t207);
                                if (!t208) {
                                    goto gm_lb31;
                                }
                                {
                                    void * t209 = GM_LD(void *, ((uint8_t *)&gm_l31));
                                    uint64_t t210 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                    uint64_t t211 = gm_l32;
                                    {
                                        {
                                            gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)t209) + (uintptr_t)t211 * 16));
                                            gm_l33 = t212;
                                            gm_b16_8 t213 = gm_l33;
                                            gm_f_member_name_mj2bvs3((void *)&gm_l61, (void *)NULL, cx, NULL, t213);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            {
                                                {
                                                    uint8_t t214 = (uint8_t)((((uint8_t *)&gm_l61))[16] != 0);
                                                    if (t214) {
                                                        goto gm_lb37;
                                                    }
                                                    gm_l60 = ((uint8_t)0);
                                                    goto gm_lb36;
                                                }
gm_lb37:;
                                                {
                                                    gm_l60 = ((uint8_t)1);
                                                    goto gm_lb36;
                                                }
gm_lb38:;
                                                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 383, 16);
                                            }
gm_lb36:;
                                            uint8_t t215 = gm_l60;
                                            if (t215) {
                                                void * t216 = gm_l0;
                                                gm_b16_8 t217 = gm_l33;
                                                gm_b0_1 t218 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t216, t217);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            goto gm_lb34;
                                        }
gm_lb35:;
                                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 382, 9);
                                    }
gm_lb34:;
                                }
gm_lb33:;
                                uint64_t t219 = gm_l32;
                                uint64_t r220; bool o221 = __builtin_add_overflow(t219, ((uint64_t)1ULL), &r220);
                                uint64_t t222 = r220;
                                gm_l32 = t222;
                            }
                        }
gm_lb31:;
                    }
                    gm_trqfvmjtt4sij t223;
                    gm_f_list_epvducg((void *)&t223, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l34 = t223;
                    uint8_t t224 = gm_l5;
                    if (t224) {
                        void * t225 = (void *)((uint8_t *)&F.l34);
                        gm_b16_8 t226;
                        GM_ST(void *, (uint8_t *)&t226, (void *)(gm_s2ph9f020upd2));
                        GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(6));
                        gm_tftb1odc739t5 t227 = gm_f_push_3ef746b(t225, t226);
                        gm_l62 = t227;
                        uint8_t t228 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                        if (!t228) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_srckfibkpt681aa, 387, 17);
                    }
                    {
                        {
                            uint8_t t229 = (uint8_t)((((uint8_t *)&gm_l6))[8] != 0);
                            if (!t229) {
                                goto gm_lb40;
                            }
                            uint64_t t230 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
                            gm_l35 = t230;
                            void * t231 = (void *)((uint8_t *)&F.l34);
                            uint64_t t232 = gm_l35;
                            gm_t4ce0jd3tdro4 t233;
                            GM_ST(uint64_t, ((uint8_t *)&t233 + 0), t232);
                            gm_f_format_mfspujt((void *)&gm_l63, (void *)NULL, cx, NULL, t233);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t234 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                            if (!t234) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 8, gm_srckfibkpt681aa, 388, 35);
                            gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l63) + 8));
                            gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
                            gm_tftb1odc739t5 t237 = gm_f_push_3ef746b(t231, t236);
                            gm_l64 = t237;
                            uint8_t t238 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                            if (!t238) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_srckfibkpt681aa, 388, 24);
                            goto gm_lb39;
                        }
gm_lb40:;
                    }
gm_lb39:;
                }
                {
                    uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)&F.l34) + 8));
                    uint8_t t240 = ((t239 == ((uint64_t)0ULL)));
                    if (t240) {
                        gm_b16_8 t241;
                        GM_ST(void *, (uint8_t *)&t241, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(0));
                        gm_l65 = t241;
                    } else {
                        gm_b16_8 t242 = GM_LD(gm_b16_8, ((uint8_t *)&F.l34));
                        gm_b16_8 t243 = GM_LD(gm_b16_8, ((uint8_t *)&t242));
                        gm_b16_8 t244;
                        GM_ST(void *, (uint8_t *)&t244, (void *)(gm_sg5gdnrith2i6));
                        GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)(2));
                        gm_f_join_sfknljb((void *)&gm_l66, (void *)NULL, cx, NULL, t243, t244);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t245 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                        if (!t245) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 8, gm_srckfibkpt681aa, 389, 98);
                        gm_b16_8 t246 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l66) + 8));
                        gm_tnpifv20irnph t247;
                        GM_ST(gm_b16_8, ((uint8_t *)&t247 + 0), t246);
                        gm_f_format_p144a0o((void *)&gm_l67, (void *)NULL, cx, NULL, t247);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t248 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                        if (!t248) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_srckfibkpt681aa, 389, 63);
                        gm_b16_8 t249 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                        gm_b16_8 t250 = GM_LD(gm_b16_8, ((uint8_t *)&t249));
                        gm_l65 = t250;
                    }
                    gm_b16_8 t251 = gm_l65;
                    gm_l36 = t251;
                    gm_b16_8 t252 = gm_l3;
                    gm_b16_8 t253 = gm_l36;
                    gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&F.l23));
                    gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                    gm_b16_8 t256;
                    GM_ST(void *, (uint8_t *)&t256, (void *)(gm_s3p0rbs9hc3lg));
                    GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(1));
                    gm_f_join_sfknljb((void *)&gm_l68, (void *)NULL, cx, NULL, t255, t256);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t257 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                    if (!t257) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 8, gm_srckfibkpt681aa, 391, 58);
                    gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 8));
                    gm_tfeab279c16rr t259;
                    GM_ST(gm_b16_8, ((uint8_t *)&t259 + 0), t252);
                    GM_ST(gm_b16_8, ((uint8_t *)&t259 + 16), t253);
                    GM_ST(gm_b16_8, ((uint8_t *)&t259 + 32), t258);
                    gm_f_format_19e6va7((void *)&gm_l69, (void *)NULL, cx, NULL, &t259);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t260 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                    if (!t260) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 8, gm_srckfibkpt681aa, 391, 16);
                    gm_b16_8 t261 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l69) + 8));
                    gm_l37 = t261;
                    {
                        {
                            void * t262 = gm_l0;
                            void * t263 = (void *)(((uint8_t *)t262) + 656);
                            gm_b16_8 t264 = gm_l37;
                            gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
                            gm_f_get_e3joqcc((void *)&gm_l70, t263, t265);
                            uint8_t t266 = (uint8_t)((((uint8_t *)&gm_l70))[16] != 0);
                            if (!t266) {
                                goto gm_lb42;
                            }
                            gm_b16_8 t267 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l70));
                            gm_l38 = t267;
                            gm_b16_8 t268 = gm_l38;
                            gm_th2lqbo77pvsc t269;
                            GM_ST(uint8_t, (uint8_t *)&t269 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t269 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t269 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t269), t268);
                            ((uint8_t *)&t269)[16] = 1;
                            memmove(gm_out, ((uint8_t *)&t269), 24);
                            goto gm_fdone;
                        }
gm_lb42:;
                    }
gm_lb41:;
                    gm_b16_8 t270;
                    GM_ST(void *, (uint8_t *)&t270, (void *)(gm_scttf7jtr4fn9));
                    GM_ST(uintptr_t, (uint8_t *)&t270 + 8, (uintptr_t)(5));
                    gm_b16_8 t271 = gm_l37;
                    gm_b16_8 t272 = GM_LD(gm_b16_8, ((uint8_t *)&t271));
                    gm_b16_8 t273 = gm_f_content_name_83mn5uf((void *)NULL, cx, NULL, t270, t272);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l39 = t273;
                    void * t274 = gm_l0;
                    void * t275 = (void *)(((uint8_t *)t274) + 656);
                    gm_b16_8 t276 = gm_l37;
                    gm_b16_8 t277 = GM_LD(gm_b16_8, ((uint8_t *)&t276));
                    gm_b16_8 t278 = gm_l39;
                    gm_tftb1odc739t5 t279 = gm_f_put_7shfu9u(t275, t277, t278);
                    gm_l71 = t279;
                }
                {
                    uint8_t t280 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                    if (!t280) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 1, gm_srckfibkpt681aa, 394, 5);
                    void * t281 = gm_l0;
                    void * t282 = (void *)(((uint8_t *)t281) + 712);
                    gm_b16_8 t283 = gm_l3;
                    gm_b16_8 t284 = gm_l36;
                    gm_b16_8 t285 = gm_l39;
                    gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&F.l23));
                    gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&t286));
                    gm_b16_8 t288;
                    GM_ST(void *, (uint8_t *)&t288, (void *)(gm_s3p0rbs9hc3lg));
                    GM_ST(uintptr_t, (uint8_t *)&t288 + 8, (uintptr_t)(1));
                    gm_f_join_sfknljb((void *)&gm_l72, (void *)NULL, cx, NULL, t287, t288);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t289 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                    if (!t289) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 8, gm_srckfibkpt681aa, 395, 86);
                    gm_b16_8 t290 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l72) + 8));
                    gm_b16_8 t291 = gm_l39;
                    gm_tud7oovfvnf43 t292;
                    GM_ST(gm_b16_8, ((uint8_t *)&t292 + 0), t283);
                    GM_ST(gm_b16_8, ((uint8_t *)&t292 + 16), t284);
                    GM_ST(gm_b16_8, ((uint8_t *)&t292 + 32), t285);
                    GM_ST(gm_b16_8, ((uint8_t *)&t292 + 48), t290);
                    GM_ST(gm_b16_8, ((uint8_t *)&t292 + 64), t291);
                    gm_tftb1odc739t5 t293 = gm_f_write_712qp7n(t282, &t292);
                    gm_l73 = t293;
                    uint8_t t294 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                    if (!t294) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 1, gm_srckfibkpt681aa, 395, 5);
                    void * t295 = gm_l0;
                    void * t296 = (void *)(((uint8_t *)t295) + 712);
                    gm_b16_8 t297 = gm_l39;
                    void * t298 = gm_l0;
                    gm_t88rcucg1b7uk t299 = gm_l1;
                    uint64_t t300 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t298, t299);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t301 = gm_l39;
                    void * t302 = gm_l0;
                    gm_t88rcucg1b7uk t303 = gm_l1;
                    uint64_t t304 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t302, t303);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_thddbqb6snabm t305;
                    GM_ST(gm_b16_8, ((uint8_t *)&t305 + 0), t297);
                    GM_ST(uint64_t, ((uint8_t *)&t305 + 16), t300);
                    GM_ST(gm_b16_8, ((uint8_t *)&t305 + 24), t301);
                    GM_ST(uint64_t, ((uint8_t *)&t305 + 40), t304);
                    gm_tftb1odc739t5 t306 = gm_f_write_cscq5c8(t296, &t305);
                    gm_l74 = t306;
                    uint8_t t307 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                    if (!t307) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 1, gm_srckfibkpt681aa, 397, 5);
                    void * t308 = gm_l0;
                    gm_t88rcucg1b7uk t309 = gm_l1;
                    gm_b16_8 t310 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t308, t309);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l40 = t310;
                    gm_l42 = ((uint64_t)0ULL);
                    uint64_t t311 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    gm_l41 = t311;
                    {
                        for (;;) {
                            uint64_t t312 = gm_l42;
                            uint64_t t313 = gm_l41;
                            uint8_t t314 = (t312 < t313);
                            if (!t314) {
                                goto gm_lb43;
                            }
                            {
                                {
                                    {
                                        uint64_t t315 = gm_l42;
                                        gm_l43 = t315;
                                        uint64_t t316 = gm_l43;
                                        uint64_t t317 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                        uint8_t t318 = (t316 < t317);
                                        if (t318) {
                                            void * t319 = gm_l0;
                                            void * t320 = (void *)(((uint8_t *)t319) + 712);
                                            gm_b16_8 t321 = gm_l39;
                                            void * t322 = GM_LD(void *, ((uint8_t *)&F.l10));
                                            uint64_t t323 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                            uint64_t t324 = gm_l43;
                                            if ((uintptr_t)t324 >= (uintptr_t)(t323)) gm_trap_index((uintptr_t)t324, t323, gm_srckfibkpt681aa, 400, 84);
                                            gm_b16_8 t325 = GM_LD(gm_b16_8, (((uint8_t *)t322) + (uintptr_t)t324 * 16));
                                            void * t326 = GM_LD(void *, ((uint8_t *)&gm_l40));
                                            uint64_t t327 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                            uint64_t t328 = gm_l43;
                                            uint64_t t329 = GM_LD(uint64_t, (((uint8_t *)t326) + (uintptr_t)t328 * 8));
                                            gm_tftksr4op114e t330;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t330 + 0), t321);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t330 + 16), t325);
                                            GM_ST(uint64_t, ((uint8_t *)&t330 + 32), t329);
                                            gm_tftb1odc739t5 t331 = gm_f_write_3f2sd55(t320, &t330);
                                            gm_l75 = t331;
                                            uint8_t t332 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                                            if (!t332) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 1, gm_srckfibkpt681aa, 400, 27);
                                        }
                                        goto gm_lb46;
                                    }
gm_lb47:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 399, 5);
                                }
gm_lb46:;
                            }
gm_lb45:;
                            uint64_t t333 = gm_l42;
                            uint64_t r334; bool o335 = __builtin_add_overflow(t333, ((uint64_t)1ULL), &r334);
                            uint64_t t336 = r334;
                            gm_l42 = t336;
                        }
                    }
gm_lb43:;
                }
                {
                    void * t337 = gm_l0;
                    void * t338 = (void *)(((uint8_t *)t337) + 712);
                    void * t339 = gm_l0;
                    gm_t88rcucg1b7uk t340 = gm_l1;
                    gm_b16_8 t341 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t339, t340);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t342;
                    GM_ST(gm_b16_8, ((uint8_t *)&t342 + 0), t341);
                    gm_f_format_1cgbgnn((void *)&gm_l76, (void *)NULL, cx, NULL, t342);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t343 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                    if (!t343) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 8, gm_srckfibkpt681aa, 402, 53);
                    gm_b16_8 t344 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l76) + 8));
                    gm_b16_8 t345 = GM_LD(gm_b16_8, ((uint8_t *)&t344));
                    gm_b16_8 t346 = gm_f_assert_message_6bd9l6u((void *)NULL, cx, NULL, t345);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t347;
                    GM_ST(gm_b16_8, ((uint8_t *)&t347 + 0), t346);
                    gm_tftb1odc739t5 t348 = gm_f_write_pecnce5(t338, t347);
                    gm_l77 = t348;
                    uint8_t t349 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                    if (!t349) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 1, gm_srckfibkpt681aa, 402, 5);
                    gm_b16_8 t350 = gm_l39;
                    gm_th2lqbo77pvsc t351;
                    GM_ST(uint8_t, (uint8_t *)&t351 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t351 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t351 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t351), t350);
                    ((uint8_t *)&t351)[16] = 1;
                    memmove(gm_out, ((uint8_t *)&t351), 24);
                    goto gm_fdone;
                }
            }
gm_lb2:;
        }
        {
            gm_th2lqbo77pvsc t352;
            memset(&t352, 0, sizeof t352);
            memmove(gm_out, ((uint8_t *)&t352), 24);
            goto gm_fdone;
        }
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
__attribute__((aligned(4))) void gm_f_make_typed_2bh0dpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_make_typed_2bh0dpj F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l3; /* len */
    gm_t88rcucg1b7uk gm_l4; /* elem */
    gm_b16_8 gm_l5; /* ec */
    gm_b16_8 gm_l6; /* members */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* ft */
    gm_t88rcucg1b7uk gm_l14; /* inner */
    gm_b16_8 gm_l15; /* offs */
    gm_t88rcucg1b7uk gm_l16; /* err */
    gm_t88rcucg1b7uk gm_l17; /* ok */
    gm_b16_8 gm_l18; /* offs */
    gm_tqsk8dt6ib3qq gm_l19; /* pair */
    gm_tgbgti82mf879 gm_l20; /* p */
    uint32_t gm_l21; /* id */
    gm_b16_8 gm_l23; /* offs */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    gm_t88rcucg1b7uk gm_l28; /* tag */
    gm_b16_8 gm_l29; /* variants */
    gm_b16_8 gm_l30; /* offs */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_t88rcucg1b7uk gm_l35; /* pt */
    gm_tgbgti82mf879 gm_l36; /* p */
    uint64_t gm_l39; /* cursor */
    uint64_t gm_l40; /* natural */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* i */
    uint64_t gm_l45; /* x */
    uint64_t gm_l46; /* x */
    gm_b16_8 gm_l47; /* alignas */
    uint64_t gm_l48; /* a */
    GM_STACK_CHECK(&F, 4731u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l49;
    uint8_t gm_l50;
    gm_t53at80pfdus3 gm_l51;
    uint8_t gm_l52;
    gm_tabb710d9mq2e gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_tlcm4lvu3g1a8 gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_tftb1odc739t5 gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_t8ldkt09h6492 gm_l60;
    gm_tftb1odc739t5 gm_l61;
    uint8_t gm_l62;
    uint8_t gm_l63;
    gm_t8ta482bl6be0 gm_l64;
    gm_tftb1odc739t5 gm_l65;
    gm_tftb1odc739t5 gm_l66;
    gm_tftb1odc739t5 gm_l67;
    gm_t8ldkt09h6492 gm_l68;
    gm_tftb1odc739t5 gm_l69;
    gm_ttos9gukqrqer gm_l70;
    gm_tabb710d9mq2e gm_l71;
    gm_tftb1odc739t5 gm_l72;
    uint64_t gm_l73;
    gm_b16_8 gm_l74;
    gm_tabb710d9mq2e gm_l75;
    gm_tabb710d9mq2e gm_l76;
    gm_tftb1odc739t5 gm_l77;
    gm_tabb710d9mq2e gm_l78;
    gm_tftb1odc739t5 gm_l79;
    gm_tabb710d9mq2e gm_l80;
    gm_tftb1odc739t5 gm_l81;
    gm_tabb710d9mq2e gm_l82;
    {
        void * t46 = gm_l0;
        uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 56));
        if (t47) {
            gm_l50 = ((uint8_t)1);
        } else {
            void * t48 = gm_l0;
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)t48) + 8));
            uint8_t t50 = (!(t49 == ((uint64_t)8ULL)));
            gm_l50 = t50;
        }
        uint8_t t51 = gm_l50;
        if (t51) {
            gm_l49 = ((uint8_t)1);
        } else {
            void * t52 = gm_l0;
            gm_t88rcucg1b7uk t53 = gm_l1;
            uint64_t t54 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t55 = ((t54 == ((uint64_t)0ULL)));
            gm_l49 = t55;
        }
        uint8_t t56 = gm_l49;
        if (t56) {
            gm_th2lqbo77pvsc t57;
            memset(&t57, 0, sizeof t57);
            memmove(gm_out, ((uint8_t *)&t57), 24);
            goto gm_fdone;
        }
        gm_trqfvmjtt4sij t58;
        gm_f_list_792g2go((void *)&t58, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t58;
        void * t59 = gm_l0;
        gm_t88rcucg1b7uk t60 = gm_l1;
        gm_f_kind_ghvav22((void *)&gm_l51, t59, GM_CONV(gm_csguankko4lmhd, t60));
        {
            {
                {
                    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l51) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)11ULL));
                    if (!t62) {
                        goto gm_lb2;
                    }
                    uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l51) + 8));
                    gm_l3 = t63;
                    gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l51) + 8) + 8));
                    gm_l4 = t64;
                    uint64_t t65 = gm_l3;
                    uint8_t t66 = ((t65 == ((uint64_t)0ULL)));
                    if (t66) {
                        gm_l52 = ((uint8_t)1);
                    } else {
                        void * t67 = gm_l0;
                        gm_t88rcucg1b7uk t68 = gm_l4;
                        uint64_t t69 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t67, t68);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t70 = ((t69 == ((uint64_t)0ULL)));
                        gm_l52 = t70;
                    }
                    uint8_t t71 = gm_l52;
                    if (t71) {
                        gm_th2lqbo77pvsc t72;
                        memset(&t72, 0, sizeof t72);
                        memmove(gm_out, ((uint8_t *)&t72), 24);
                        goto gm_fdone;
                    }
                    void * t73 = gm_l0;
                    gm_t88rcucg1b7uk t74 = gm_l4;
                    gm_b16_8 t75 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t73, t74);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l5 = t75;
                    gm_b16_8 t76 = gm_l5;
                    uint64_t t77 = gm_l3;
                    gm_t3877pk7lv748 t78;
                    GM_ST(gm_b16_8, ((uint8_t *)&t78 + 0), t76);
                    GM_ST(uint64_t, ((uint8_t *)&t78 + 16), t77);
                    gm_f_format_19ftgop((void *)&gm_l53, (void *)NULL, cx, NULL, &t78);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                    if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 8, gm_srckfibkpt681aa, 225, 24);
                    gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l53) + 8));
                    gm_l6 = t80;
                    gm_trqfvmjtt4sij t81;
                    gm_f_list_cfvjofe((void *)&t81, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l7 = t81;
                    void * t82 = (void *)((uint8_t *)&F.l7);
                    gm_tftb1odc739t5 t83 = gm_f_push_q9fsb22(t82, &(*(gm_t3877pk7lv748 *)((uint8_t *)gm_kr85ffi32os)));
                    gm_l54 = t83;
                    uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                    if (!t84) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_srckfibkpt681aa, 227, 13);
                    void * t85 = gm_l0;
                    gm_t88rcucg1b7uk t86 = gm_l1;
                    gm_b16_8 t87 = gm_l6;
                    gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
                    gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                    gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                    void * t91 = gm_l0;
                    gm_t88rcucg1b7uk t92 = gm_l4;
                    uint64_t t93 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t91, t92);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t94 = gm_f_define_typed_m3mqpm3((void *)NULL, cx, NULL, t85, t86, t88, t90, t93);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_th2lqbo77pvsc t95;
                    GM_ST(uint8_t, (uint8_t *)&t95 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t95 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t95 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t95), t94);
                    ((uint8_t *)&t95)[16] = 1;
                    memmove(gm_out, ((uint8_t *)&t95), 24);
                    goto gm_fdone;
                }
gm_lb2:;
                {
                    uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l51) + 0));
                    uint8_t t97 = (uint8_t)(t96 == ((uint8_t)12ULL));
                    if (!t97) {
                        goto gm_lb3;
                    }
                    gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                    gm_l8 = t98;
                    void * t99 = gm_l0;
                    gm_t88rcucg1b7uk t100 = gm_l1;
                    gm_b16_8 t101 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t99, t100);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l9 = t101;
                    gm_b16_8 t102 = gm_l8;
                    gm_l10 = t102;
                    gm_l11 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t103 = gm_l11;
                            uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint8_t t105 = (t103 < t104);
                            if (!t105) {
                                goto gm_lb4;
                            }
                            {
                                uint64_t t106 = gm_l11;
                                void * t107 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                uint64_t t109 = gm_l11;
                                gm_t88rcucg1b7uk t110 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t107) + (uintptr_t)t109 * 4));
                                gm_tlcm4lvu3g1a8 t111;
                                GM_ST(uint32_t, (uint8_t *)&t111 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t111 + 0), t106);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t111 + 8), t110);
                                gm_l55 = t111;
                                {
                                    {
                                        uint64_t t112 = GM_LD(uint64_t, ((uint8_t *)&gm_l55));
                                        gm_l12 = t112;
                                        gm_t88rcucg1b7uk t113 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l55) + 8));
                                        gm_l13 = t113;
                                        void * t114 = (void *)((uint8_t *)&F.l2);
                                        void * t115 = gm_l0;
                                        gm_t88rcucg1b7uk t116 = gm_l13;
                                        gm_b16_8 t117 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t115, t116);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t118 = gm_l0;
                                        gm_t88rcucg1b7uk t119 = gm_l13;
                                        uint64_t t120 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t118, t119);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t121 = gm_l0;
                                        gm_t88rcucg1b7uk t122 = gm_l13;
                                        uint64_t t123 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t121, t122);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tf5srh4mp4b3i t124;
                                        memset(&t124, 0, sizeof t124);
                                        void * t125 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                        uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                        uint64_t t127 = gm_l12;
                                        if ((uintptr_t)t127 >= (uintptr_t)(t126)) gm_trap_index((uintptr_t)t127, t126, gm_srckfibkpt681aa, 232, 141);
                                        uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)t125) + (uintptr_t)t127 * 8));
                                        gm_tml3m6miq9l2k t129;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t117);
                                        GM_ST(uint64_t, ((uint8_t *)&t129 + 16), t120);
                                        GM_ST(uint64_t, ((uint8_t *)&t129 + 24), t123);
                                        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t129 + 32), t124);
                                        GM_ST(uint64_t, ((uint8_t *)&t129 + 48), t128);
                                        gm_tftb1odc739t5 t130 = gm_f_push_m55rf90(t114, &t129);
                                        gm_l56 = t130;
                                        uint8_t t131 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                        if (!t131) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_srckfibkpt681aa, 232, 45);
                                        goto gm_lb7;
                                    }
gm_lb8:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 232, 13);
                                }
gm_lb7:;
                            }
gm_lb6:;
                            uint64_t t132 = gm_l11;
                            uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                            uint64_t t135 = r133;
                            gm_l11 = t135;
                        }
                    }
gm_lb4:;
                    goto gm_lb1;
                }
gm_lb3:;
            }
            {
                {
                    uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l51) + 0));
                    uint8_t t137 = (uint8_t)(t136 == ((uint8_t)13ULL));
                    if (!t137) {
                        goto gm_lb9;
                    }
                    gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l51) + 8));
                    gm_l14 = t138;
                    void * t139 = gm_l0;
                    gm_t88rcucg1b7uk t140 = gm_l1;
                    gm_b16_8 t141 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t139, t140);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t141;
                    void * t142 = (void *)((uint8_t *)&F.l2);
                    void * t143 = gm_l0;
                    gm_t88rcucg1b7uk t144 = gm_l14;
                    gm_b16_8 t145 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t143, t144);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t146 = gm_l0;
                    gm_t88rcucg1b7uk t147 = gm_l14;
                    uint64_t t148 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t146, t147);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t149 = gm_l0;
                    gm_t88rcucg1b7uk t150 = gm_l14;
                    uint64_t t151 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t149, t150);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tf5srh4mp4b3i t152;
                    memset(&t152, 0, sizeof t152);
                    void * t153 = GM_LD(void *, ((uint8_t *)&gm_l15));
                    uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t154)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t154, gm_srckfibkpt681aa, 236, 118);
                    uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)t153) + (uintptr_t)((uint64_t)0ULL) * 8));
                    gm_tml3m6miq9l2k t156;
                    GM_ST(gm_b16_8, ((uint8_t *)&t156 + 0), t145);
                    GM_ST(uint64_t, ((uint8_t *)&t156 + 16), t148);
                    GM_ST(uint64_t, ((uint8_t *)&t156 + 24), t151);
                    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t156 + 32), t152);
                    GM_ST(uint64_t, ((uint8_t *)&t156 + 48), t155);
                    gm_tftb1odc739t5 t157 = gm_f_push_m55rf90(t142, &t156);
                    gm_l57 = t157;
                    uint8_t t158 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                    if (!t158) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srckfibkpt681aa, 236, 13);
                    void * t159 = (void *)((uint8_t *)&F.l2);
                    gm_b16_8 t160;
                    GM_ST(void *, (uint8_t *)&t160, (void *)(gm_s9grtrvtmi962));
                    GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(7));
                    gm_tf5srh4mp4b3i t161;
                    memset(&t161, 0, sizeof t161);
                    void * t162 = GM_LD(void *, ((uint8_t *)&gm_l15));
                    uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t163)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t163, gm_srckfibkpt681aa, 237, 87);
                    uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)t162) + (uintptr_t)((uint64_t)1ULL) * 8));
                    gm_tml3m6miq9l2k t165;
                    GM_ST(gm_b16_8, ((uint8_t *)&t165 + 0), t160);
                    GM_ST(uint64_t, ((uint8_t *)&t165 + 16), ((uint64_t)1ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t165 + 24), ((uint64_t)1ULL));
                    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t165 + 32), t161);
                    GM_ST(uint64_t, ((uint8_t *)&t165 + 48), t164);
                    gm_tftb1odc739t5 t166 = gm_f_push_m55rf90(t159, &t165);
                    gm_l58 = t166;
                    uint8_t t167 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                    if (!t167) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_srckfibkpt681aa, 237, 13);
                    goto gm_lb1;
                }
gm_lb9:;
                {
                    uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&gm_l51) + 0));
                    uint8_t t169 = (uint8_t)(t168 == ((uint8_t)14ULL));
                    if (!t169) {
                        goto gm_lb10;
                    }
                    gm_t88rcucg1b7uk t170 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l51) + 8));
                    gm_l16 = t170;
                    gm_t88rcucg1b7uk t171 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l51) + 8) + 4));
                    gm_l17 = t171;
                    void * t172 = gm_l0;
                    gm_t88rcucg1b7uk t173 = gm_l1;
                    gm_b16_8 t174 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t172, t173);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l18 = t174;
                    void * t175 = (void *)((uint8_t *)&F.l2);
                    gm_b16_8 t176;
                    GM_ST(void *, (uint8_t *)&t176, (void *)(gm_s9grtrvtmi962));
                    GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(7));
                    gm_tf5srh4mp4b3i t177;
                    memset(&t177, 0, sizeof t177);
                    void * t178 = GM_LD(void *, ((uint8_t *)&gm_l18));
                    uint64_t t179 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t179)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t179, gm_srckfibkpt681aa, 241, 87);
                    uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)t178) + (uintptr_t)((uint64_t)0ULL) * 8));
                    gm_tml3m6miq9l2k t181;
                    GM_ST(gm_b16_8, ((uint8_t *)&t181 + 0), t176);
                    GM_ST(uint64_t, ((uint8_t *)&t181 + 16), ((uint64_t)1ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t181 + 24), ((uint64_t)1ULL));
                    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t181 + 32), t177);
                    GM_ST(uint64_t, ((uint8_t *)&t181 + 48), t180);
                    gm_tftb1odc739t5 t182 = gm_f_push_m55rf90(t175, &t181);
                    gm_l59 = t182;
                    uint8_t t183 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                    if (!t183) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srckfibkpt681aa, 241, 13);
                    gm_t88rcucg1b7uk t184 = gm_l16;
                    gm_t88rcucg1b7uk t185 = gm_l17;
                    gm_tqsk8dt6ib3qq t186;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t186 + 0), t184);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t186 + 4), t185);
                    gm_l19 = t186;
                    {
                        {
                            void * t187 = gm_l0;
                            void * t188 = (void *)((uint8_t *)&gm_l19);
                            uint64_t t189 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t190 = (uintptr_t)((uint64_t)2ULL);
                            if (t189 > t190 || t190 > (uintptr_t)((uint64_t)2ULL)) gm_trap_slice(t189, t190, ((uint64_t)2ULL), gm_srckfibkpt681aa, 243, 38);
                            uint64_t t191 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t192;
                            GM_ST(void *, (uint8_t *)&t192, (void *)(((uint8_t *)t188 + (uintptr_t)((uint64_t)0ULL) * 4)));
                            GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(t191));
                            gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
                            gm_f_payload_union_feshns2((void *)&gm_l60, (void *)NULL, cx, NULL, t187, t193);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t194 = (uint8_t)((((uint8_t *)&gm_l60))[32] != 0);
                            if (!t194) {
                                goto gm_lb12;
                            }
                            memmove(((uint8_t *)&gm_l20), ((uint8_t *)&gm_l60), 32);
                            void * t195 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                            uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 16));
                            uint64_t t198 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 24));
                            gm_tf5srh4mp4b3i t199;
                            memset(&t199, 0, sizeof t199);
                            void * t200 = GM_LD(void *, ((uint8_t *)&gm_l18));
                            uint64_t t201 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t201)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t201, gm_srckfibkpt681aa, 243, 131);
                            uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)t200) + (uintptr_t)((uint64_t)1ULL) * 8));
                            gm_tml3m6miq9l2k t203;
                            GM_ST(gm_b16_8, ((uint8_t *)&t203 + 0), t196);
                            GM_ST(uint64_t, ((uint8_t *)&t203 + 16), t197);
                            GM_ST(uint64_t, ((uint8_t *)&t203 + 24), t198);
                            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t203 + 32), t199);
                            GM_ST(uint64_t, ((uint8_t *)&t203 + 48), t202);
                            gm_tftb1odc739t5 t204 = gm_f_push_m55rf90(t195, &t203);
                            gm_l61 = t204;
                            uint8_t t205 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                            if (!t205) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srckfibkpt681aa, 243, 50);
                            goto gm_lb11;
                        }
gm_lb12:;
                    }
gm_lb11:;
                    goto gm_lb1;
                }
gm_lb10:;
            }
            {
                {
                    uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&gm_l51) + 0));
                    uint8_t t207 = (uint8_t)(t206 == ((uint8_t)17ULL));
                    if (!t207) {
                        goto gm_lb13;
                    }
                    uint32_t t208 = GM_LD(uint32_t, (((uint8_t *)&gm_l51) + 8));
                    gm_l21 = t208;
                    void * t209 = gm_l0;
                    void * t210 = GM_LD(void *, ((uint8_t *)t209));
                    void * t211 = GM_LD(void *, (((uint8_t *)t210) + 1088));
                    uint64_t t212 = GM_LD(uint64_t, ((((uint8_t *)t210) + 1088) + 8));
                    uint32_t t213 = gm_l21;
                    uint64_t t214 = (uint64_t)t213;
                    if ((uintptr_t)t214 >= (uintptr_t)(t212)) gm_trap_index((uintptr_t)t214, t212, gm_srckfibkpt681aa, 245, 30);
                    {
                        {
                            uint8_t t215 = GM_LD(uint8_t, (((((uint8_t *)t211) + (uintptr_t)t214 * 184) + 24) + 0));
                            uint8_t t216 = (uint8_t)(t215 == ((uint8_t)0ULL));
                            if (!t216) {
                                goto gm_lb15;
                            }
                            memmove(((uint8_t *)&F.l22), (((((uint8_t *)t211) + (uintptr_t)t214 * 184) + 24) + 8), 48);
                            uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 16));
                            if (t217) {
                                gm_l62 = ((uint8_t)1);
                            } else {
                                {
                                    {
                                        uint8_t t218 = (uint8_t)(((((uint8_t *)&F.l22) + 40))[4] != 0);
                                        if (t218) {
                                            goto gm_lb17;
                                        }
                                        gm_l63 = ((uint8_t)0);
                                        goto gm_lb16;
                                    }
gm_lb17:;
                                    {
                                        gm_l63 = ((uint8_t)1);
                                        goto gm_lb16;
                                    }
gm_lb18:;
                                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 247, 34);
                                }
gm_lb16:;
                                uint8_t t219 = gm_l63;
                                gm_l62 = t219;
                            }
                            uint8_t t220 = gm_l62;
                            if (t220) {
                                gm_th2lqbo77pvsc t221;
                                memset(&t221, 0, sizeof t221);
                                memmove(gm_out, ((uint8_t *)&t221), 24);
                                goto gm_fdone;
                            }
                            void * t222 = gm_l0;
                            gm_t88rcucg1b7uk t223 = gm_l1;
                            gm_b16_8 t224 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t222, t223);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l23 = t224;
                            gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
                            gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                            gm_l24 = t226;
                            gm_l25 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t227 = gm_l25;
                                    uint64_t t228 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                    uint8_t t229 = (t227 < t228);
                                    if (!t229) {
                                        goto gm_lb19;
                                    }
                                    {
                                        uint64_t t230 = gm_l25;
                                        void * t231 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                        uint64_t t232 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                        uint64_t t233 = gm_l25;
                                        gm_t0mglksrkfa5e t234 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t231) + (uintptr_t)t233 * 40));
                                        gm_t8ta482bl6be0 t235;
                                        GM_ST(uint64_t, ((uint8_t *)&t235 + 0), t230);
                                        GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&t235 + 8), t234);
                                        gm_l64 = t235;
                                        {
                                            {
                                                uint64_t t236 = GM_LD(uint64_t, ((uint8_t *)&gm_l64));
                                                gm_l26 = t236;
                                                memmove(((uint8_t *)&F.l27), (((uint8_t *)&gm_l64) + 8), 40);
                                                void * t237 = (void *)((uint8_t *)&F.l2);
                                                void * t238 = gm_l0;
                                                gm_t88rcucg1b7uk t239 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                                                gm_b16_8 t240 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t238, t239);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t241 = gm_l0;
                                                gm_t88rcucg1b7uk t242 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                                                uint64_t t243 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t241, t242);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t244 = gm_l0;
                                                gm_t88rcucg1b7uk t245 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l27) + 16));
                                                uint64_t t246 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t244, t245);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_tf5srh4mp4b3i t247 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l27) + 24));
                                                void * t248 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                                uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                uint64_t t250 = gm_l26;
                                                if ((uintptr_t)t250 >= (uintptr_t)(t249)) gm_trap_index((uintptr_t)t250, t249, gm_srckfibkpt681aa, 249, 158);
                                                uint64_t t251 = GM_LD(uint64_t, (((uint8_t *)t248) + (uintptr_t)t250 * 8));
                                                gm_tml3m6miq9l2k t252;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t252 + 0), t240);
                                                GM_ST(uint64_t, ((uint8_t *)&t252 + 16), t243);
                                                GM_ST(uint64_t, ((uint8_t *)&t252 + 24), t246);
                                                GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t252 + 32), t247);
                                                GM_ST(uint64_t, ((uint8_t *)&t252 + 48), t251);
                                                gm_tftb1odc739t5 t253 = gm_f_push_m55rf90(t237, &t252);
                                                gm_l65 = t253;
                                                uint8_t t254 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                                                if (!t254) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 1, gm_srckfibkpt681aa, 249, 53);
                                                goto gm_lb22;
                                            }
gm_lb23:;
                                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 249, 17);
                                        }
gm_lb22:;
                                    }
gm_lb21:;
                                    uint64_t t255 = gm_l25;
                                    uint64_t r256; bool o257 = __builtin_add_overflow(t255, ((uint64_t)1ULL), &r256);
                                    uint64_t t258 = r256;
                                    gm_l25 = t258;
                                }
                            }
gm_lb19:;
                            goto gm_lb14;
                        }
gm_lb15:;
                        {
                            uint8_t t259 = GM_LD(uint8_t, (((((uint8_t *)t211) + (uintptr_t)t214 * 184) + 24) + 0));
                            uint8_t t260 = (uint8_t)(t259 == ((uint8_t)1ULL));
                            if (!t260) {
                                goto gm_lb24;
                            }
                            gm_t88rcucg1b7uk t261 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t211) + (uintptr_t)t214 * 184) + 24) + 8));
                            gm_l28 = t261;
                            gm_b16_8 t262 = GM_LD(gm_b16_8, ((((((uint8_t *)t211) + (uintptr_t)t214 * 184) + 24) + 8) + 8));
                            gm_l29 = t262;
                            void * t263 = gm_l0;
                            gm_t88rcucg1b7uk t264 = gm_l1;
                            gm_b16_8 t265 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t263, t264);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l30 = t265;
                            void * t266 = (void *)((uint8_t *)&F.l2);
                            void * t267 = gm_l0;
                            gm_t88rcucg1b7uk t268 = gm_l28;
                            gm_b16_8 t269 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t267, t268);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t270 = gm_l0;
                            gm_t88rcucg1b7uk t271 = gm_l28;
                            uint64_t t272 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t270, t271);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t273 = gm_l0;
                            gm_t88rcucg1b7uk t274 = gm_l28;
                            uint64_t t275 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t273, t274);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tf5srh4mp4b3i t276;
                            memset(&t276, 0, sizeof t276);
                            void * t277 = GM_LD(void *, ((uint8_t *)&gm_l30));
                            uint64_t t278 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t278)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t278, gm_srckfibkpt681aa, 253, 116);
                            uint64_t t279 = GM_LD(uint64_t, (((uint8_t *)t277) + (uintptr_t)((uint64_t)0ULL) * 8));
                            gm_tml3m6miq9l2k t280;
                            GM_ST(gm_b16_8, ((uint8_t *)&t280 + 0), t269);
                            GM_ST(uint64_t, ((uint8_t *)&t280 + 16), t272);
                            GM_ST(uint64_t, ((uint8_t *)&t280 + 24), t275);
                            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t280 + 32), t276);
                            GM_ST(uint64_t, ((uint8_t *)&t280 + 48), t279);
                            gm_tftb1odc739t5 t281 = gm_f_push_m55rf90(t266, &t280);
                            gm_l66 = t281;
                            uint8_t t282 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                            if (!t282) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_srckfibkpt681aa, 253, 17);
                            gm_trqfvmjtt4sij t283;
                            gm_f_list_nof7vfj((void *)&t283, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l31 = t283;
                            gm_b16_8 t284 = gm_l29;
                            gm_l32 = t284;
                            gm_l33 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t285 = gm_l33;
                                    uint64_t t286 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                    uint8_t t287 = (t285 < t286);
                                    if (!t287) {
                                        goto gm_lb25;
                                    }
                                    {
                                        void * t288 = GM_LD(void *, ((uint8_t *)&gm_l32));
                                        uint64_t t289 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                        uint64_t t290 = gm_l33;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l34), (((uint8_t *)t288) + (uintptr_t)t290 * 72), 72);
                                                {
                                                    {
                                                        uint8_t t291 = (uint8_t)(((((uint8_t *)&F.l34) + 16))[4] != 0);
                                                        if (!t291) {
                                                            goto gm_lb31;
                                                        }
                                                        gm_t88rcucg1b7uk t292 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l34) + 16));
                                                        gm_l35 = t292;
                                                        void * t293 = (void *)((uint8_t *)&F.l31);
                                                        gm_t88rcucg1b7uk t294 = gm_l35;
                                                        gm_tftb1odc739t5 t295 = gm_f_push_k3l9nic(t293, GM_CONV(gm_csguankko4lmhd, t294));
                                                        gm_l67 = t295;
                                                        uint8_t t296 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                                                        if (!t296) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 1, gm_srckfibkpt681aa, 256, 42);
                                                        goto gm_lb30;
                                                    }
gm_lb31:;
                                                }
gm_lb30:;
                                                goto gm_lb28;
                                            }
gm_lb29:;
                                            gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 255, 17);
                                        }
gm_lb28:;
                                    }
gm_lb27:;
                                    uint64_t t297 = gm_l33;
                                    uint64_t r298; bool o299 = __builtin_add_overflow(t297, ((uint64_t)1ULL), &r298);
                                    uint64_t t300 = r298;
                                    gm_l33 = t300;
                                }
                            }
gm_lb25:;
                            uint64_t t301 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                            uint8_t t302 = (t301 > ((uint64_t)1ULL));
                            if (t302) {
                                {
                                    {
                                        void * t303 = gm_l0;
                                        gm_b16_8 t304 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
                                        gm_b16_8 t305 = GM_LD(gm_b16_8, ((uint8_t *)&t304));
                                        gm_f_payload_union_feshns2((void *)&gm_l68, (void *)NULL, cx, NULL, t303, t305);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t306 = (uint8_t)((((uint8_t *)&gm_l68))[32] != 0);
                                        if (!t306) {
                                            goto gm_lb33;
                                        }
                                        memmove(((uint8_t *)&gm_l36), ((uint8_t *)&gm_l68), 32);
                                        void * t307 = (void *)((uint8_t *)&F.l2);
                                        gm_b16_8 t308 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l36));
                                        uint64_t t309 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 16));
                                        uint64_t t310 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 24));
                                        gm_tf5srh4mp4b3i t311;
                                        memset(&t311, 0, sizeof t311);
                                        void * t312 = GM_LD(void *, ((uint8_t *)&gm_l30));
                                        uint64_t t313 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                        uint64_t t314 = GM_LD(uint64_t, (((uint8_t *)t312) + (uintptr_t)((uint64_t)1ULL) * 8));
                                        gm_tml3m6miq9l2k t315;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t315 + 0), t308);
                                        GM_ST(uint64_t, ((uint8_t *)&t315 + 16), t309);
                                        GM_ST(uint64_t, ((uint8_t *)&t315 + 24), t310);
                                        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t315 + 32), t311);
                                        GM_ST(uint64_t, ((uint8_t *)&t315 + 48), t314);
                                        gm_tftb1odc739t5 t316 = gm_f_push_m55rf90(t307, &t315);
                                        gm_l69 = t316;
                                        uint8_t t317 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                                        if (!t317) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 1, gm_srckfibkpt681aa, 259, 64);
                                        goto gm_lb32;
                                    }
gm_lb33:;
                                }
gm_lb32:;
                            }
                            goto gm_lb14;
                        }
gm_lb24:;
                        {
                            uint8_t t318 = GM_LD(uint8_t, (((((uint8_t *)t211) + (uintptr_t)t214 * 184) + 24) + 0));
                            uint8_t t319 = (uint8_t)(t318 == ((uint8_t)2ULL));
                            if (!t319) {
                                goto gm_lb34;
                            }
                            gm_th2lqbo77pvsc t320;
                            memset(&t320, 0, sizeof t320);
                            memmove(gm_out, ((uint8_t *)&t320), 24);
                            goto gm_fdone;
                        }
gm_lb34:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 245, 24);
                    }
gm_lb14:;
                    goto gm_lb1;
                }
gm_lb13:;
            }
            {
                {
                    gm_th2lqbo77pvsc t321;
                    memset(&t321, 0, sizeof t321);
                    memmove(gm_out, ((uint8_t *)&t321), 24);
                    goto gm_fdone;
                }
gm_lb35:;
                gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 221, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_trqfvmjtt4sij t322;
        gm_f_list_epvducg((void *)&t322, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l37 = t322;
        gm_trqfvmjtt4sij t323;
        gm_f_list_cfvjofe((void *)&t323, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l38 = t323;
        gm_l39 = ((uint64_t)0ULL);
        gm_l40 = ((uint64_t)1ULL);
        gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        gm_b16_8 t325 = GM_LD(gm_b16_8, ((uint8_t *)&t324));
        gm_l41 = t325;
        gm_l42 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t326 = gm_l42;
                uint64_t t327 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                uint8_t t328 = (t326 < t327);
                if (!t328) {
                    goto gm_lb36;
                }
                {
                    uint64_t t329 = gm_l42;
                    void * t330 = GM_LD(void *, ((uint8_t *)&gm_l41));
                    uint64_t t331 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                    uint64_t t332 = gm_l42;
                    gm_tml3m6miq9l2k t333 = GM_LD(gm_tml3m6miq9l2k, (((uint8_t *)t330) + (uintptr_t)t332 * 56));
                    gm_ttos9gukqrqer t334;
                    GM_ST(uint64_t, ((uint8_t *)&t334 + 0), t329);
                    GM_ST(gm_tml3m6miq9l2k, ((uint8_t *)&t334 + 8), t333);
                    gm_l70 = t334;
                    {
                        {
                            uint64_t t335 = GM_LD(uint64_t, ((uint8_t *)&gm_l70));
                            gm_l43 = t335;
                            memmove(((uint8_t *)&F.l44), (((uint8_t *)&gm_l70) + 8), 56);
                            uint64_t t336 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 16));
                            uint8_t t337 = ((t336 == ((uint64_t)0ULL)));
                            if (t337) {
                                goto gm_lb38;
                            }
                            uint64_t t338 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 48));
                            uint64_t t339 = gm_l39;
                            uint8_t t340 = (t338 > t339);
                            if (t340) {
                                void * t341 = (void *)((uint8_t *)&F.l37);
                                uint64_t t342 = gm_l43;
                                uint64_t t343 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 48));
                                uint64_t t344 = gm_l39;
                                uint64_t r345; bool o346 = __builtin_sub_overflow(t343, t344, &r345);
                                if (o346) gm_trap("integer overflow in `-`", gm_srckfibkpt681aa, 272, 76);
                                uint64_t t347 = r345;
                                gm_t2lqu8jsj511d t348;
                                GM_ST(uint64_t, ((uint8_t *)&t348 + 0), t342);
                                GM_ST(uint64_t, ((uint8_t *)&t348 + 8), t347);
                                gm_f_format_38m1q8c((void *)&gm_l71, (void *)NULL, cx, NULL, t348);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t349 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                                if (!t349) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 8, gm_srckfibkpt681aa, 272, 42);
                                gm_b16_8 t350 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 8));
                                gm_b16_8 t351 = GM_LD(gm_b16_8, ((uint8_t *)&t350));
                                gm_tftb1odc739t5 t352 = gm_f_push_3ef746b(t341, t351);
                                gm_l72 = t352;
                                uint8_t t353 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                if (!t353) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 1, gm_srckfibkpt681aa, 272, 29);
                            }
                            uint64_t t354 = gm_l40;
                            {
                                {
                                    uint8_t t355 = (uint8_t)(((((uint8_t *)&F.l44) + 32))[8] != 0);
                                    if (!t355) {
                                        goto gm_lb42;
                                    }
                                    uint64_t t356 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 32));
                                    gm_l45 = t356;
                                    uint64_t t357 = gm_l45;
                                    uint64_t t358 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 24));
                                    uint64_t t359 = (t357 > t358 ? t357 : t358);
                                    gm_l73 = t359;
                                    goto gm_lb41;
                                }
gm_lb42:;
                                uint64_t t360 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 24));
                                gm_l73 = t360;
                            }
gm_lb41:;
                            uint64_t t361 = gm_l73;
                            uint64_t t362 = (t354 > t361 ? t354 : t361);
                            gm_l40 = t362;
                            {
                                {
                                    uint8_t t363 = (uint8_t)(((((uint8_t *)&F.l44) + 32))[8] != 0);
                                    if (!t363) {
                                        goto gm_lb44;
                                    }
                                    uint64_t t364 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 32));
                                    gm_l46 = t364;
                                    uint64_t t365 = gm_l46;
                                    gm_t4ce0jd3tdro4 t366;
                                    GM_ST(uint64_t, ((uint8_t *)&t366 + 0), t365);
                                    gm_f_format_irg2n6e((void *)&gm_l75, (void *)NULL, cx, NULL, t366);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t367 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                                    if (!t367) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 8, gm_srckfibkpt681aa, 274, 48);
                                    gm_b16_8 t368 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l75) + 8));
                                    gm_b16_8 t369 = GM_LD(gm_b16_8, ((uint8_t *)&t368));
                                    gm_l74 = t369;
                                    goto gm_lb43;
                                }
gm_lb44:;
                                gm_b16_8 t370;
                                GM_ST(void *, (uint8_t *)&t370, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t370 + 8, (uintptr_t)(0));
                                gm_l74 = t370;
                            }
gm_lb43:;
                            gm_b16_8 t371 = gm_l74;
                            gm_l47 = t371;
                            void * t372 = (void *)((uint8_t *)&F.l37);
                            gm_b16_8 t373 = gm_l47;
                            gm_b16_8 t374 = GM_LD(gm_b16_8, ((uint8_t *)&F.l44));
                            uint64_t t375 = gm_l43;
                            gm_tftksr4op114e t376;
                            GM_ST(gm_b16_8, ((uint8_t *)&t376 + 0), t373);
                            GM_ST(gm_b16_8, ((uint8_t *)&t376 + 16), t374);
                            GM_ST(uint64_t, ((uint8_t *)&t376 + 32), t375);
                            gm_f_format_1dfo4pq((void *)&gm_l76, (void *)NULL, cx, NULL, &t376);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t377 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                            if (!t377) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 8, gm_srckfibkpt681aa, 275, 22);
                            gm_b16_8 t378 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l76) + 8));
                            gm_b16_8 t379 = GM_LD(gm_b16_8, ((uint8_t *)&t378));
                            gm_tftb1odc739t5 t380 = gm_f_push_3ef746b(t372, t379);
                            gm_l77 = t380;
                            uint8_t t381 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                            if (!t381) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 1, gm_srckfibkpt681aa, 275, 9);
                            void * t382 = (void *)((uint8_t *)&F.l38);
                            uint64_t t383 = gm_l43;
                            gm_t4ce0jd3tdro4 t384;
                            GM_ST(uint64_t, ((uint8_t *)&t384 + 0), t383);
                            gm_f_format_tc7tn98((void *)&gm_l78, (void *)NULL, cx, NULL, t384);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t385 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                            if (!t385) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 8, gm_srckfibkpt681aa, 276, 38);
                            gm_b16_8 t386 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l78) + 8));
                            gm_b16_8 t387 = GM_LD(gm_b16_8, ((uint8_t *)&t386));
                            uint64_t t388 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 48));
                            gm_t3877pk7lv748 t389;
                            GM_ST(gm_b16_8, ((uint8_t *)&t389 + 0), t387);
                            GM_ST(uint64_t, ((uint8_t *)&t389 + 16), t388);
                            gm_tftb1odc739t5 t390 = gm_f_push_q9fsb22(t382, &t389);
                            gm_l79 = t390;
                            uint8_t t391 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                            if (!t391) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 1, gm_srckfibkpt681aa, 276, 9);
                            uint64_t t392 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 24));
                            uint64_t t393 = (t392 > ((uint64_t)1ULL) ? t392 : ((uint64_t)1ULL));
                            gm_l48 = t393;
                            uint64_t t394 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 48));
                            uint64_t t395 = GM_LD(uint64_t, (((uint8_t *)&F.l44) + 16));
                            uint64_t t396 = gm_l48;
                            uint64_t r397; bool o398 = __builtin_add_overflow(t395, t396, &r397);
                            if (o398) gm_trap("integer overflow in `+`", gm_srckfibkpt681aa, 279, 27);
                            uint64_t t399 = r397;
                            uint64_t r400; bool o401 = __builtin_sub_overflow(t399, ((uint64_t)1ULL), &r400);
                            if (o401) gm_trap("integer overflow in `-`", gm_srckfibkpt681aa, 279, 27);
                            uint64_t t402 = r400;
                            uint64_t t403 = gm_l48;
                            if (t403 == 0) gm_trap("division by zero", gm_srckfibkpt681aa, 279, 26);
                            uint64_t t404 = (uint64_t)(t402 / t403);
                            uint64_t t405 = gm_l48;
                            uint64_t r406; bool o407 = __builtin_mul_overflow(t404, t405, &r406);
                            if (o407) gm_trap("integer overflow in `*`", gm_srckfibkpt681aa, 279, 26);
                            uint64_t t408 = r406;
                            uint64_t r409; bool o410 = __builtin_add_overflow(t394, t408, &r409);
                            if (o410) gm_trap("integer overflow in `+`", gm_srckfibkpt681aa, 279, 18);
                            uint64_t t411 = r409;
                            gm_l39 = t411;
                            goto gm_lb39;
                        }
gm_lb40:;
                        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 270, 5);
                    }
gm_lb39:;
                }
gm_lb38:;
                uint64_t t412 = gm_l42;
                uint64_t r413; bool o414 = __builtin_add_overflow(t412, ((uint64_t)1ULL), &r413);
                uint64_t t415 = r413;
                gm_l42 = t415;
            }
        }
gm_lb36:;
    }
    {
        void * t416 = gm_l0;
        gm_t88rcucg1b7uk t417 = gm_l1;
        uint64_t t418 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t416, t417);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t419 = gm_l39;
        uint8_t t420 = (t418 > t419);
        if (t420) {
            void * t421 = (void *)((uint8_t *)&F.l37);
            void * t422 = gm_l0;
            gm_t88rcucg1b7uk t423 = gm_l1;
            uint64_t t424 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t422, t423);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t425 = gm_l39;
            uint64_t r426; bool o427 = __builtin_sub_overflow(t424, t425, &r426);
            if (o427) gm_trap("integer overflow in `-`", gm_srckfibkpt681aa, 281, 73);
            uint64_t t428 = r426;
            gm_t4ce0jd3tdro4 t429;
            GM_ST(uint64_t, ((uint8_t *)&t429 + 0), t428);
            gm_f_format_kl8i1sq((void *)&gm_l80, (void *)NULL, cx, NULL, t429);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t430 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
            if (!t430) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 8, gm_srckfibkpt681aa, 281, 43);
            gm_b16_8 t431 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l80) + 8));
            gm_b16_8 t432 = GM_LD(gm_b16_8, ((uint8_t *)&t431));
            gm_tftb1odc739t5 t433 = gm_f_push_3ef746b(t421, t432);
            gm_l81 = t433;
            uint8_t t434 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
            if (!t434) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 1, gm_srckfibkpt681aa, 281, 30);
        }
        void * t435 = gm_l0;
        gm_t88rcucg1b7uk t436 = gm_l1;
        gm_b16_8 t437 = GM_LD(gm_b16_8, ((uint8_t *)&F.l37));
        gm_b16_8 t438 = GM_LD(gm_b16_8, ((uint8_t *)&t437));
        gm_b16_8 t439;
        GM_ST(void *, (uint8_t *)&t439, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t439 + 8, (uintptr_t)(1));
        gm_f_join_sfknljb((void *)&gm_l82, (void *)NULL, cx, NULL, t438, t439);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t440 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
        if (!t440) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l82) + 8, gm_srckfibkpt681aa, 282, 31);
        gm_b16_8 t441 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l82) + 8));
        gm_b16_8 t442 = GM_LD(gm_b16_8, ((uint8_t *)&t441));
        gm_b16_8 t443 = GM_LD(gm_b16_8, ((uint8_t *)&F.l38));
        gm_b16_8 t444 = GM_LD(gm_b16_8, ((uint8_t *)&t443));
        uint64_t t445 = gm_l40;
        gm_b16_8 t446 = gm_f_define_typed_m3mqpm3((void *)NULL, cx, NULL, t435, t436, t442, t444, t445);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t447;
        GM_ST(uint8_t, (uint8_t *)&t447 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t447 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t447 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t447), t446);
        ((uint8_t *)&t447)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t447), 24);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_member_names_17el54k(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_mirror_lcjegsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_mirror_lcjegsf F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_th2lqbo77pvsc gm_l2; /* m */
    gm_th2lqbo77pvsc gm_l3; /* m */
    GM_STACK_CHECK(&F, 150u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_ti5ramhglnvdt gm_l4;
    gm_tftb1odc739t5 gm_l5;
    {
        {
            void * t4 = gm_l0;
            void * t5 = (void *)(((uint8_t *)t4) + 600);
            uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
            gm_f_get_9k2rdij((void *)&gm_l4, t5, t6);
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l4))[24] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l2), ((uint8_t *)&gm_l4), 24);
            memmove(gm_out, ((uint8_t *)&gm_l2), 24);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t8 = gm_l0;
    gm_t88rcucg1b7uk t9 = gm_l1;
    gm_f_make_mirror_rudvl6p((void *)&gm_l3, (void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 600);
    uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    gm_tftb1odc739t5 t13 = gm_f_put_dm7u1sg(t11, t12, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)));
    gm_l5 = t13;
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srckfibkpt681aa, 341, 5);
    memmove(gm_out, ((uint8_t *)&gm_l3), 24);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_mirror_text_cr5muc9(void * a0);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_payload_union_feshns2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_payload_union_feshns2 F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* tys */
    uint64_t gm_l3; /* size */
    uint64_t gm_l4; /* align */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    gm_t88rcucg1b7uk gm_l8; /* t */
    GM_STACK_CHECK(&F, 767u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tlcm4lvu3g1a8 gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_trqfvmjtt4sij t7;
    gm_f_list_epvducg((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t7;
    gm_l3 = ((uint64_t)0ULL);
    gm_l4 = ((uint64_t)1ULL);
    gm_b16_8 t8 = gm_l1;
    gm_l5 = t8;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l6;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                uint64_t t12 = gm_l6;
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t15 = gm_l6;
                gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + (uintptr_t)t15 * 4));
                gm_tlcm4lvu3g1a8 t17;
                GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t17 + 0), t12);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t17 + 8), t16);
                gm_l9 = t17;
                {
                    {
                        uint64_t t18 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                        gm_l7 = t18;
                        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l9) + 8));
                        gm_l8 = t19;
                        void * t20 = gm_l0;
                        gm_t88rcucg1b7uk t21 = gm_l8;
                        uint64_t t22 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t20, t21);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t23 = (t22 > ((uint64_t)0ULL));
                        if (t23) {
                            void * t24 = (void *)((uint8_t *)&F.l2);
                            void * t25 = gm_l0;
                            gm_t88rcucg1b7uk t26 = gm_l8;
                            gm_b16_8 t27 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t25, t26);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t28 = gm_l7;
                            gm_t3877pk7lv748 t29;
                            GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t27);
                            GM_ST(uint64_t, ((uint8_t *)&t29 + 16), t28);
                            gm_f_format_3buamil((void *)&gm_l10, (void *)NULL, cx, NULL, &t29);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                            if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srckfibkpt681aa, 299, 26);
                            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                            gm_tftb1odc739t5 t33 = gm_f_push_3ef746b(t24, t32);
                            gm_l11 = t33;
                            uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                            if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srckfibkpt681aa, 299, 13);
                            uint64_t t35 = gm_l3;
                            void * t36 = gm_l0;
                            gm_t88rcucg1b7uk t37 = gm_l8;
                            uint64_t t38 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t36, t37);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t39 = (t35 > t38 ? t35 : t38);
                            gm_l3 = t39;
                            uint64_t t40 = gm_l4;
                            void * t41 = gm_l0;
                            gm_t88rcucg1b7uk t42 = gm_l8;
                            uint64_t t43 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t41, t42);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t44 = (t40 > t43 ? t40 : t43);
                            gm_l4 = t44;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 297, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t45 = gm_l6;
            uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
            uint64_t t48 = r46;
            gm_l6 = t48;
        }
    }
gm_lb1:;
    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
    uint8_t t50 = ((t49 == ((uint64_t)0ULL)));
    if (t50) {
        gm_t8ldkt09h6492 t51;
        memset(&t51, 0, sizeof t51);
        memmove(gm_out, ((uint8_t *)&t51), 40);
        goto gm_fdone;
    }
    gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(gm_s3p0rbs9hc3lg));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(1));
    gm_f_join_sfknljb((void *)&gm_l12, (void *)NULL, cx, NULL, t53, t54);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srckfibkpt681aa, 305, 59);
    gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
    gm_tnpifv20irnph t57;
    GM_ST(gm_b16_8, ((uint8_t *)&t57 + 0), t56);
    gm_f_format_vgl5p2e((void *)&gm_l13, (void *)NULL, cx, NULL, t57);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srckfibkpt681aa, 305, 30);
    gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
    gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
    uint64_t t61 = gm_l3;
    uint64_t t62 = gm_l4;
    gm_tgbgti82mf879 t63;
    GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t60);
    GM_ST(uint64_t, ((uint8_t *)&t63 + 16), t61);
    GM_ST(uint64_t, ((uint8_t *)&t63 + 24), t62);
    gm_t8ldkt09h6492 t64;
    GM_ST(uint8_t, (uint8_t *)&t64 + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&t64 + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&t64 + 36, 0);
    GM_ST(gm_tgbgti82mf879, ((uint8_t *)&t64), t63);
    ((uint8_t *)&t64)[32] = 1;
    memmove(gm_out, ((uint8_t *)&t64), 40);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_repr_53quqki F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* signed */
    uint16_t gm_l3; /* bits */
    uint16_t gm_l4; /* bits */
    gm_t88rcucg1b7uk gm_l5; /* inner */
    GM_STACK_CHECK(&F, 604u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t5v8n6qrn4rh4 gm_l6;
    gm_t53at80pfdus3 gm_l7;
    gm_t5v8n6qrn4rh4 gm_l8;
    void * t18 = gm_l0;
    gm_t88rcucg1b7uk t19 = gm_l1;
    gm_f_kind_ghvav22((void *)&gm_l7, t18, GM_CONV(gm_csguankko4lmhd, t19));
    {
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)0ULL));
            if (!t21) {
                goto gm_lb2;
            }
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 8));
            gm_l2 = t22;
            uint16_t t23 = GM_LD(uint16_t, ((((uint8_t *)&gm_l7) + 8) + 2));
            gm_l3 = t23;
            uint8_t t24 = gm_l2;
            uint16_t t25 = gm_l3;
            uint32_t t26 = (uint32_t)t25;
            void * t27 = gm_l0;
            gm_t88rcucg1b7uk t28 = gm_l1;
            uint64_t t29 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tuglhjjmj2g58 t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), t24);
            GM_ST(uint32_t, ((uint8_t *)&t30 + 4), t26);
            GM_ST(uint64_t, ((uint8_t *)&t30 + 8), t29);
            gm_t5v8n6qrn4rh4 t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tuglhjjmj2g58, ((uint8_t *)&t31 + 8), t30);
            gm_l6 = t31;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)1ULL));
            if (!t33) {
                goto gm_lb3;
            }
            void * t34 = gm_l0;
            void * t35 = (void *)(((uint8_t *)t34) + 8);
            uint32_t t36 = gm_f_ptr_bits_glps5i7(t35);
            void * t37 = gm_l0;
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 8));
            gm_tuglhjjmj2g58 t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)0));
            GM_ST(uint32_t, ((uint8_t *)&t39 + 4), t36);
            GM_ST(uint64_t, ((uint8_t *)&t39 + 8), t38);
            gm_t5v8n6qrn4rh4 t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tuglhjjmj2g58, ((uint8_t *)&t40 + 8), t39);
            gm_l6 = t40;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)2ULL));
            if (!t42) {
                goto gm_lb4;
            }
            void * t43 = gm_l0;
            void * t44 = (void *)(((uint8_t *)t43) + 8);
            uint32_t t45 = gm_f_ptr_bits_glps5i7(t44);
            void * t46 = gm_l0;
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)t46) + 8));
            gm_tuglhjjmj2g58 t48;
            GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)1));
            GM_ST(uint32_t, ((uint8_t *)&t48 + 4), t45);
            GM_ST(uint64_t, ((uint8_t *)&t48 + 8), t47);
            gm_t5v8n6qrn4rh4 t49;
            GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)0ULL));
            GM_ST(gm_tuglhjjmj2g58, ((uint8_t *)&t49 + 8), t48);
            gm_l6 = t49;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)3ULL));
            if (!t51) {
                goto gm_lb5;
            }
            uint16_t t52 = GM_LD(uint16_t, (((uint8_t *)&gm_l7) + 8));
            gm_l4 = t52;
            uint16_t t53 = gm_l4;
            gm_t5v8n6qrn4rh4 t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t54 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)1ULL));
            GM_ST(uint16_t, ((uint8_t *)&t54 + 8), t53);
            gm_l6 = t54;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)4ULL));
            if (!t56) {
                goto gm_lb6;
            }
            gm_t5v8n6qrn4rh4 t57;
            GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t57 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t57 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)2ULL));
            gm_l6 = t57;
            goto gm_lb1;
        }
gm_lb6:;
        {
            {
                {
                    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint8_t t59 = (uint8_t)(t58 == ((uint8_t)8ULL));
                    if (!t59) {
                        goto gm_lb9;
                    }
                    goto gm_lb8;
                }
gm_lb9:;
                {
                    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint8_t t61 = (uint8_t)(t60 == ((uint8_t)9ULL));
                    if (!t61) {
                        goto gm_lb10;
                    }
                    goto gm_lb8;
                }
gm_lb10:;
                {
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)7ULL));
                    if (!t63) {
                        goto gm_lb11;
                    }
                    goto gm_lb8;
                }
gm_lb11:;
                {
                    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint8_t t65 = (uint8_t)(t64 == ((uint8_t)16ULL));
                    if (!t65) {
                        goto gm_lb12;
                    }
                    goto gm_lb8;
                }
gm_lb12:;
                goto gm_lb7;
            }
gm_lb8:;
            gm_t5v8n6qrn4rh4 t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t66 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t66 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)3ULL));
            gm_l6 = t66;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t68 = (uint8_t)(t67 == ((uint8_t)13ULL));
            if (!t68) {
                goto gm_lb13;
            }
            gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
            gm_l5 = t69;
            void * t70 = gm_l0;
            void * t71 = GM_LD(void *, ((uint8_t *)t70));
            gm_t88rcucg1b7uk t72 = gm_l5;
            uint8_t t73 = gm_f_nullable_pointer_7qfmn6o(t71, GM_CONV(gm_csguankko4lmhd, t72));
            if (t73) {
                gm_t5v8n6qrn4rh4 t74;
                GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t74 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t74 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)3ULL));
                gm_l8 = t74;
            } else {
                gm_t5v8n6qrn4rh4 t75;
                GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t75 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t75 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)6ULL));
                gm_l8 = t75;
            }
            memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l8), 24);
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t77 = (uint8_t)(t76 == ((uint8_t)15ULL));
            if (!t77) {
                goto gm_lb14;
            }
            gm_t5v8n6qrn4rh4 t78;
            GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t78 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t78 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)4ULL));
            gm_l6 = t78;
            goto gm_lb1;
        }
gm_lb14:;
        {
            uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t80 = (uint8_t)(t79 == ((uint8_t)25ULL));
            if (!t80) {
                goto gm_lb15;
            }
            gm_t5v8n6qrn4rh4 t81;
            GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t81 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t81 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)5ULL));
            gm_l6 = t81;
            goto gm_lb1;
        }
gm_lb15:;
        {
            gm_t5v8n6qrn4rh4 t82;
            GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t82 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t82 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)6ULL));
            gm_l6 = t82;
            goto gm_lb1;
        }
gm_lb16:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 143, 55);
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&gm_l6), 24);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_typed_stkcgvr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_typed_stkcgvr F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_th2lqbo77pvsc gm_l2; /* m */
    gm_th2lqbo77pvsc gm_l3; /* m */
    GM_STACK_CHECK(&F, 150u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_ti5ramhglnvdt gm_l4;
    gm_tftb1odc739t5 gm_l5;
    {
        {
            void * t4 = gm_l0;
            void * t5 = (void *)(((uint8_t *)t4) + 408);
            uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
            gm_f_get_9k2rdij((void *)&gm_l4, t5, t6);
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l4))[24] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l2), ((uint8_t *)&gm_l4), 24);
            memmove(gm_out, ((uint8_t *)&gm_l2), 24);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t8 = gm_l0;
    gm_t88rcucg1b7uk t9 = gm_l1;
    gm_f_make_typed_2bh0dpj((void *)&gm_l3, (void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 408);
    uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    gm_tftb1odc739t5 t13 = gm_f_put_dm7u1sg(t11, t12, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l3)));
    gm_l5 = t13;
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srckfibkpt681aa, 200, 5);
    memmove(gm_out, ((uint8_t *)&gm_l3), 24);
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vector_typedef_vpgf6u8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint64_t a3, uint64_t a4) {
    struct gm_fr_vector_typedef_vpgf6u8 F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* lane */
    gm_b16_8 gm_l2; /* tag */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* bytes */
    gm_b16_8 gm_l5; /* name */
    GM_STACK_CHECK(&F, 450u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l6;
    uint8_t gm_l7;
    gm_tqs5p4n2tq19v gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    uint64_t t5 = gm_l3;
    gm_b16_8 t6 = gm_l2;
    gm_tf1bjg81u0glv t7;
    GM_ST(uint64_t, ((uint8_t *)&t7 + 0), t5);
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t6);
    gm_f_format_dtmr1c2((void *)&gm_l6, (void *)NULL, cx, NULL, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srckfibkpt681aa, 328, 13);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
    gm_l5 = t9;
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 752);
    gm_b16_8 t12 = gm_l5;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
    gm_tqs5p4n2tq19v t14 = gm_f_get_p217beu(t11, t13);
    gm_l8 = t14;
    {
        {
            uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l8))[1] != 0);
            if (t15) {
                goto gm_lb2;
            }
            gm_l7 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l7 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 329, 8);
    }
gm_lb1:;
    uint8_t t16 = gm_l7;
    if (t16) {
        void * t17 = gm_l0;
        void * t18 = (void *)(((uint8_t *)t17) + 752);
        gm_b16_8 t19 = gm_l5;
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
        gm_tftb1odc739t5 t21 = gm_f_put_5c6qgui(t18, t20, ((uint8_t)1));
        gm_l9 = t21;
        uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srckfibkpt681aa, 330, 9);
        void * t23 = gm_l0;
        void * t24 = (void *)(((uint8_t *)t23) + 560);
        gm_b16_8 t25 = gm_l1;
        gm_b16_8 t26 = gm_l5;
        uint64_t t27 = gm_l3;
        uint64_t t28 = gm_l4;
        uint64_t r29; bool o30 = __builtin_mul_overflow(t27, t28, &r29);
        if (o30) gm_trap("integer overflow in `*`", gm_srckfibkpt681aa, 331, 94);
        uint64_t t31 = r29;
        gm_tftksr4op114e t32;
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t25);
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 16), t26);
        GM_ST(uint64_t, ((uint8_t *)&t32 + 32), t31);
        gm_tftb1odc739t5 t33 = gm_f_write_1h8ac6i(t24, &t32);
        gm_l10 = t33;
        uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
        if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srckfibkpt681aa, 331, 9);
    }
    gm_b16_8 t35 = gm_l5;
    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
    F.ret = t36;
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
