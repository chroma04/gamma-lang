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
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
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
typedef struct { gm_t88rcucg1b7uk a[3]; } gm_t10fqegjtup5b;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(base: ^Expr, index: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t66j1im9ip128 f0; uint8_t f1; uint8_t pe[7]; } gm_td8hfgklo8dil;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
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
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t10fqegjtup5b) == 12 && _Alignof(gm_t10fqegjtup5b) == 4 && offsetof(gm_t10fqegjtup5b, a) == 0, "layout of `[3]Ty`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; gm_tkn42u7na3t81 v1; } f1; } gm_t1qr0p6kln6tf;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
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
typedef struct { gm_t2i8ne5ibd7v4 f0; uint8_t f1; uint8_t pe[3]; } gm_t8uadrbfpr7kk;
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
typedef struct { gm_ts6oqppjh0ba3 f0; gm_ts6oqppjh0ba3 f1; } gm_t9ldr1q5hn9fq;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_td8hfgklo8dil) == 40 && _Alignof(gm_td8hfgklo8dil) == 8 && offsetof(gm_td8hfgklo8dil, f0) == 0 && offsetof(gm_td8hfgklo8dil, f1) == 32, "layout of `?(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tbngattl1fo51 f0; uint8_t f1; uint8_t pe[3]; } gm_thetuhtouve7f;
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1qr0p6kln6tf) == 12 && _Alignof(gm_t1qr0p6kln6tf) == 4 && offsetof(gm_t1qr0p6kln6tf, f0) == 0 && offsetof(gm_t1qr0p6kln6tf, f1) == 4, "layout of `LabelValue`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t f2; uint8_t p3[3]; gm_t1qr0p6kln6tf f3; uint8_t pe[4]; } gm_t8enl5g7b22jb;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8uadrbfpr7kk) == 20 && _Alignof(gm_t8uadrbfpr7kk) == 4 && offsetof(gm_t8uadrbfpr7kk, f0) == 0 && offsetof(gm_t8uadrbfpr7kk, f1) == 16, "layout of `?(ty: Ty, at: Span)`");
_Static_assert(sizeof(gm_t9ldr1q5hn9fq) == 4 && _Alignof(gm_t9ldr1q5hn9fq) == 1 && offsetof(gm_t9ldr1q5hn9fq, f0) == 0 && offsetof(gm_t9ldr1q5hn9fq, f1) == 2, "layout of `(BinOp, BinOp)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_thetuhtouve7f) == 12 && _Alignof(gm_thetuhtouve7f) == 4 && offsetof(gm_thetuhtouve7f, f0) == 0 && offsetof(gm_thetuhtouve7f, f1) == 8, "layout of `?(inclusive: bool, elem: Ty)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t8enl5g7b22jb) == 48 && _Alignof(gm_t8enl5g7b22jb) == 8 && offsetof(gm_t8enl5g7b22jb, f0) == 0 && offsetof(gm_t8enl5g7b22jb, f1) == 24 && offsetof(gm_t8enl5g7b22jb, f2) == 28 && offsetof(gm_t8enl5g7b22jb, f3) == 32, "layout of `LabelInfo`");
typedef struct { gm_t8enl5g7b22jb f0; uint8_t f1; uint8_t pe[7]; } gm_te5gbhu3us3k4;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_te5gbhu3us3k4) == 56 && _Alignof(gm_te5gbhu3us3k4) == 8 && offsetof(gm_te5gbhu3us3k4, f0) == 0 && offsetof(gm_te5gbhu3us3k4, f1) == 48, "layout of `?LabelInfo`");
typedef struct { gm_t1s75i08qtptd f0; gm_t0d02142k4tu8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tnrgict8sthcv;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t0d02142k4tu8 f0; uint8_t f1; uint8_t pe[7]; } gm_toqf51949ppls;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
_Static_assert(sizeof(gm_tnrgict8sthcv) == 120 && _Alignof(gm_tnrgict8sthcv) == 8 && offsetof(gm_tnrgict8sthcv, f0) == 0 && offsetof(gm_tnrgict8sthcv, f1) == 40 && offsetof(gm_tnrgict8sthcv, f2) == 104, "layout of `PatArg`");
_Static_assert(sizeof(gm_toqf51949ppls) == 72 && _Alignof(gm_toqf51949ppls) == 8 && offsetof(gm_toqf51949ppls, f0) == 0 && offsetof(gm_toqf51949ppls, f1) == 64, "layout of `?Pattern`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
struct gm_fr_block_of_2gjl2i1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_block_q09sn1p {
    gm_frame h;
    void * ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_coercion_steps_ibqvhta {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_counting_loop_s4u2ugd {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* cond */
    gm_trqfvmjtt4sij l10; /* more */
    void * ret;
};
struct gm_fr_deinit_pal9hnf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_q21h982 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_3v2gjd4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fv6i5s8 {
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
struct gm_fr_empty_slice_ilis91k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qm5tp1g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r9g5q6n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
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
struct gm_fr_extend_nkql463 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_for_each_value_5qklbg7 {
    gm_frame h;
    gm_tl874t2atnlh3 l4; /* span */
    gm_trqfvmjtt4sij l18; /* stmts */
    gm_trqfvmjtt4sij l34; /* items */
    gm_trqfvmjtt4sij l41; /* stmts */
    gm_trqfvmjtt4sij l47; /* stmts */
    gm_trqfvmjtt4sij l56; /* stmts */
    void * ret;
};
struct gm_fr_for_loop_b6hjsbq {
    gm_frame h;
    void * ret;
};
struct gm_fr_format_2bpp75h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ftgbk9j {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h1n6lc9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t00o2ec {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vc9g56o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_in_expr_su8ofmp {
    gm_frame h;
    gm_trqfvmjtt4sij l20; /* vals */
    gm_trqfvmjtt4sij l29; /* members */
    gm_trqfvmjtt4sij l30; /* etys */
    gm_trqfvmjtt4sij l41; /* lets */
    gm_trqfvmjtt4sij l58; /* items */
    gm_trqfvmjtt4sij l59; /* jts */
    gm_trqfvmjtt4sij l65; /* stmts */
    void * ret;
};
struct gm_fr_increment_12n73t5 {
    gm_frame h;
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_members_97aedtg {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* all */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_58nr42v_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6l26q4a {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s1kpkpr {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ttksl9g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d1o9b_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_i91hglv {
    gm_frame h;
    void * ret;
};
struct gm_fr_local_l5o3d47 {
    gm_frame h;
    void * ret;
};
struct gm_fr_loop_with_pattern_d8vl0mj {
    gm_frame h;
    gm_t0d02142k4tu8 l8; /* pat_ast */
    gm_trqfvmjtt4sij l9; /* args */
    gm_t0d02142k4tu8 l12; /* p */
    gm_trqfvmjtt4sij l14; /* binds */
    gm_trqfvmjtt4sij l20; /* arms */
    gm_trqfvmjtt4sij l22; /* stmts */
    void * ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_t2b6tm8c0vgo1 l1; /* kind */
    void * ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_8sodrk9 {
    gm_frame h;
    gm_td8hfgklo8dil ret;
};
struct gm_fr_pop_dt2bd2s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_pop_ruvva9t {
    gm_frame h;
    gm_t8enl5g7b22jb l2; /* x */
    gm_te5gbhu3us3k4 ret;
};
struct gm_fr_pop_scope_g5j7o4e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_7koe9lk {
    gm_frame h;
    gm_tpm220j5qhjke l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tcgrjq9toscke l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_eb7kbhp_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l74t9rq {
    gm_frame h;
    gm_tnrgict8sthcv l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pf02nv6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_range_loop_44fv3au {
    gm_frame h;
    gm_tl874t2atnlh3 l6; /* span */
    gm_trqfvmjtt4sij l11; /* stmts */
    gm_trqfvmjtt4sij l14; /* stop */
    gm_trqfvmjtt4sij l15; /* more */
    gm_trqfvmjtt4sij l16; /* parts */
    gm_trqfvmjtt4sij l18; /* cond */
    void * ret;
};
struct gm_fr_range_test_tqcppmu {
    gm_frame h;
    gm_t88rcucg1b7uk l5; /* ty */
    gm_tl874t2atnlh3 l8; /* span */
    uint32_t l10; /* tmp */
    gm_trqfvmjtt4sij l14; /* stmts */
    void * ret;
};
struct gm_fr_regrow_4bta8ne_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5719f1b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_emnjbt3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_t5pgl2t {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
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
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_truncate_7r782uc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_daelje0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_dsb08pk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_tuple_elem_join_om8llaj {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_tuple_l3i6trt {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_x_dmu97o4 {
    gm_frame h;
    void * ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_counting_loop_s4u2ugd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint32_t a3, gm_t88rcucg1b7uk a4, void * a5, void * a6);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_binds_rk1ncsj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_deinit_pal9hnf(void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_q21h982(void * a0);
__attribute__((aligned(4))) void gm_f_early_fallback_error_4lq49mb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) uint8_t gm_f_eq_allowed_d841u9b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_for_each_value_5qklbg7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_for_iterator_cc5qo7n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_for_loop_b6hjsbq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_forget_label_breaks_vl4mn72(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_format_2bpp75h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ftgbk9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_h1n6lc9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_t00o2ec(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_vc9g56o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_spread_bndtpd6(gm_b16_8 a0);
__attribute__((aligned(4))) void * gm_f_in_expr_su8ofmp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_in_patterns_mi6p7hb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_increment_12n73t5(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_is_pattern_operand_p51tban(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_is_positional_array_6sufsk8(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_join_members_97aedtg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_join_type_cck70vp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6l26q4a(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_local_i91hglv(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void * gm_f_local_l5o3d47(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_loop_with_pattern_d8vl0mj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, void * a3, gm_b16_8 a4, void * a5);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_label_3207tcb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2, gm_t1qr0p6kln6tf a3);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_packed_place_5ck3439(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_pattern_5gn6kid(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, void * a4);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_place_mutability_0tlm4g8(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_range_loop_44fv3au(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, gm_t88rcucg1b7uk a4, uint8_t a5);
__attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_range_test_tqcppmu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, void * a3, void * a4, gm_t88rcucg1b7uk a5, uint8_t a6, uint8_t a7, gm_tl874t2atnlh3 a8);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_set_fallback_s66n99d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_soa_deref_uiefo9t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_soa_for_ovoqsoj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3);
__attribute__((aligned(4))) void gm_f_soa_of_jufdcj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_tuple_elem_join_om8llaj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_tuple_l3i6trt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unify_operands_5sp7j1j(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_x_dmu97o4(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_k0nv110r163[40] __attribute__((aligned(8)));
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern const uint8_t gm_s5958kaiuh54g[84];
extern const uint8_t gm_sa4nk3mrdf2d0[36];
extern const uint8_t gm_sg50kn7lo9hgn[86];
extern const uint8_t gm_sg8s3e7n1hsck[34];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_skue5huof65fj[80];
extern const uint8_t gm_sl4ns6hobicht[6];
extern const uint8_t gm_smdscldu1a8hg[42];
extern const uint8_t gm_so4bvja70211d[63];
extern const uint8_t gm_so4k586t2v79t[64];
extern const uint8_t gm_sp1srk1sv0crk[52];
extern const uint8_t gm_sp6ona76fn0di[13];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srclfnv05vuf0b8[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ssk8mqd1u6nn7[52];
extern const uint8_t gm_su22guvkav5b5[21];
extern const gm_tmpl gm_tplaggdk85jgg2m;
extern const gm_tmpl gm_tplb2ihdc8dbskd;
extern const gm_tmpl gm_tplkun0h8smcrq9;
extern const gm_tmpl gm_tpllqboe50ngs7g;
extern const gm_tmpl gm_tplvqpq1chrhvno;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqbqb4gb1d92(const uint8_t *a, const uint8_t *b) {
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
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_block_of_2gjl2i1 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* stmts */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_l1;
    gm_tl874t2atnlh3 t6 = gm_l2;
    void * t7 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t4, t5, ((void *)0), t6);
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
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_block_q09sn1p F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* value */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    uint8_t t4 = gm_l2;
    gm_tkn42u7na3t81 t5;
    memset(&t5, 0, sizeof t5);
    void * t6 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t2, t3, t4, t5);
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
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_coercion_steps_ibqvhta F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_f_steps_for_768gb3b((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t2, t3, t4, ((uint8_t)1));
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
inline __attribute__((aligned(4))) void gm_f_deinit_pal9hnf(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_pal9hnf F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, ((((uint8_t *)t5) + 24) + 8));
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 16));
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 24));
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, ((uint8_t *)t10));
        void * t12 = t11;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)8ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)8ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_fv6i5s8();
    GM_ST(gm_b16_8, ((uint8_t *)t20), t21);
    void * t22 = gm_l0;
    GM_ST(uint64_t, (((uint8_t *)t22) + 16), ((uint64_t)0ULL));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_q21h982(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_q21h982 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, ((((uint8_t *)t5) + 24) + 8));
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 16));
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 24));
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, ((uint8_t *)t10));
        void * t12 = t11;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)4ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)4ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_rcd2jln();
    GM_ST(gm_b16_8, ((uint8_t *)t20), t21);
    void * t22 = gm_l0;
    GM_ST(uint64_t, (((uint8_t *)t22) + 16), ((uint64_t)0ULL));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_emit_dph5v1e F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fv6i5s8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ilis91k F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lmbdkhb_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qm5tp1g F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r9g5q6n F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_nkql463 F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_h08har5_2(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 32);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 32)));
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
inline __attribute__((aligned(4))) void gm_f_format_2bpp75h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_2bpp75h F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so4bvja70211d));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkun0h8smcrq9), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ftgbk9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ftgbk9j F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5958kaiuh54g));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(83));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplb2ihdc8dbskd), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_h1n6lc9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h1n6lc9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp1srk1sv0crk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllqboe50ngs7g), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t00o2ec(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t00o2ec F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sg50kn7lo9hgn));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(85));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplaggdk85jgg2m), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_vc9g56o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vc9g56o F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssk8mqd1u6nn7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvqpq1chrhvno), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_infer_3o82a0b F;
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
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 252, 57);
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
    uint8_t t8 = (uint8_t)(t7 & ((uint8_t)2ULL));
    uint8_t t9 = (!(t8 == ((uint8_t)0ULL)));
    F.ret = t9;
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_3v2gjd4_2();
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
inline __attribute__((aligned(4))) void gm_f_list_6l26q4a(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6l26q4a F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ilis91k();
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
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s1kpkpr F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fv6i5s8();
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
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ttksl9g F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_r9g5q6n();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d1o9b_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lmbdkhb_2();
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
inline __attribute__((aligned(4))) void * gm_f_local_i91hglv(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_local_i91hglv F;
    uint32_t gm_l0; /* id */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t88rcucg1b7uk t2 = gm_l1;
    uint32_t t3 = gm_l0;
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_range_loop_44fv3au *)((&F.h)->up))->l6));
    gm_t2b6tm8c0vgo1 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t5 + 8), t3);
    void * t6 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t2, &t5, t4);
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
inline __attribute__((aligned(4))) void * gm_f_local_l5o3d47(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_local_l5o3d47 F;
    uint32_t gm_l0; /* id */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t88rcucg1b7uk t2 = gm_l1;
    uint32_t t3 = gm_l0;
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_for_each_value_5qklbg7 *)((&F.h)->up))->l4));
    gm_t2b6tm8c0vgo1 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t5 + 8), t3);
    void * t6 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t2, &t5, t4);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l1 = t3;
    uint64_t t4 = gm_l1;
    uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
    if (t5) {
        gm_t9805qtonbsag t6;
        memset(&t6, 0, sizeof t6);
        F.ret = t6;
        goto gm_fret;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t13 = r11;
    if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_srcrgc9bkquf6nr, 133, 10);
    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t8) + (uintptr_t)t13 * 4));
    gm_l2 = t14;
    void * t15 = gm_l0;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
    uint64_t t19 = gm_l1;
    uint64_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint64_t)1ULL), &r20);
    if (o21) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t22 = r20;
    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t24 = (uintptr_t)t22;
    if (t23 > t24 || t24 > (uintptr_t)t18) gm_trap_slice(t23, t24, t18, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t25 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 4)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t15), t26);
    uint32_t t27 = gm_l2;
    gm_t9805qtonbsag t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[4] = 1;
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_8sodrk9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t66j1im9ip128 gm_l2; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l1 = t3;
    uint64_t t4 = gm_l1;
    uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
    if (t5) {
        gm_td8hfgklo8dil t6;
        memset(&t6, 0, sizeof t6);
        memmove(gm_out, ((uint8_t *)&t6), 40);
        goto gm_fdone;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t13 = r11;
    if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_srcrgc9bkquf6nr, 133, 10);
    memmove(((uint8_t *)&gm_l2), (((uint8_t *)t8) + (uintptr_t)t13 * 32), 32);
    void * t14 = gm_l0;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t15) + 8));
    uint64_t t18 = gm_l1;
    uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
    if (o20) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t21 = r19;
    uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t23 = (uintptr_t)t21;
    if (t22 > t23 || t23 > (uintptr_t)t17) gm_trap_slice(t22, t23, t17, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t16 + (uintptr_t)((uint64_t)0ULL) * 32)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    GM_ST(gm_b16_8, ((uint8_t *)t14), t25);
    gm_t66j1im9ip128 t26 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)&gm_l2));
    gm_td8hfgklo8dil t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 36, 0);
    GM_ST(gm_t66j1im9ip128, ((uint8_t *)&t27), t26);
    ((uint8_t *)&t27)[32] = 1;
    memmove(gm_out, ((uint8_t *)&t27), 40);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_dt2bd2s F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l1 = t3;
    uint64_t t4 = gm_l1;
    uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
    if (t5) {
        gm_tqetrgo0q24b5 t6;
        memset(&t6, 0, sizeof t6);
        memmove(gm_out, ((uint8_t *)&t6), 48);
        goto gm_fdone;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t13 = r11;
    if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_srcrgc9bkquf6nr, 133, 10);
    memmove(((uint8_t *)&F.l2), (((uint8_t *)t8) + (uintptr_t)t13 * 40), 40);
    void * t14 = gm_l0;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t15) + 8));
    uint64_t t18 = gm_l1;
    uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
    if (o20) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t21 = r19;
    uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t23 = (uintptr_t)t21;
    if (t22 > t23 || t23 > (uintptr_t)t17) gm_trap_slice(t22, t23, t17, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t16 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    GM_ST(gm_b16_8, ((uint8_t *)t14), t25);
    gm_trqfvmjtt4sij t26 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
    gm_tqetrgo0q24b5 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 41, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 42, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 44, 0);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t27), t26);
    ((uint8_t *)&t27)[40] = 1;
    memmove(gm_out, ((uint8_t *)&t27), 48);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_ruvva9t F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l1 = t3;
    uint64_t t4 = gm_l1;
    uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
    if (t5) {
        gm_te5gbhu3us3k4 t6;
        memset(&t6, 0, sizeof t6);
        memmove(gm_out, ((uint8_t *)&t6), 56);
        goto gm_fdone;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t13 = r11;
    if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_srcrgc9bkquf6nr, 133, 10);
    memmove(((uint8_t *)&F.l2), (((uint8_t *)t8) + (uintptr_t)t13 * 48), 48);
    void * t14 = gm_l0;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t15) + 8));
    uint64_t t18 = gm_l1;
    uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
    if (o20) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t21 = r19;
    uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t23 = (uintptr_t)t21;
    if (t22 > t23 || t23 > (uintptr_t)t17) gm_trap_slice(t22, t23, t17, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t16 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    GM_ST(gm_b16_8, ((uint8_t *)t14), t25);
    gm_t8enl5g7b22jb t26 = GM_LD(gm_t8enl5g7b22jb, ((uint8_t *)&F.l2));
    gm_te5gbhu3us3k4 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 49, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 50, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 52, 0);
    GM_ST(gm_t8enl5g7b22jb, ((uint8_t *)&t27), t26);
    ((uint8_t *)&t27)[48] = 1;
    memmove(gm_out, ((uint8_t *)&t27), 56);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_scope_g5j7o4e F;
    void * gm_l0; /* cx */
    gm_t66j1im9ip128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_td8hfgklo8dil gm_l2;
    void * t4 = gm_l0;
    void * t5 = (void *)(((uint8_t *)t4) + 56);
    gm_tqetrgo0q24b5 t6;
    gm_f_pop_dt2bd2s((void *)&t6, t5);
    void * t7 = gm_l0;
    void * t8 = (void *)(((uint8_t *)t7) + 616);
    gm_t9805qtonbsag t9 = gm_f_pop_570g79n(t8);
    {
        {
            void * t10 = gm_l0;
            void * t11 = (void *)(((uint8_t *)t10) + 656);
            gm_f_pop_8sodrk9((void *)&gm_l2, t11);
            uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l2))[32] != 0);
            if (!t12) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l2), 32);
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, ((uint8_t *)t13));
            gm_f_restore_scope_edqq1v8(t14, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l1)));
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_7koe9lk F;
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
        gm_b16_8 t9 = gm_f_empty_slice_qm5tp1g();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_emnjbt3(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_2 F;
    void * gm_l0; /* l */
    gm_tbbqefuhv2r8h gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_3v2gjd4_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h9tbucc_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_eb7kbhp_2 F;
    void * gm_l0; /* l */
    gm_tlr9dp305htm7 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_lmbdkhb_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4bta8ne_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l74t9rq F;
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
        gm_b16_8 t9 = gm_f_empty_slice_r9g5q6n();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_t5pgl2t(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 120), ((uint8_t *)&F.l1), 120);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pf02nv6 F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_fv6i5s8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5719f1b(t8, ((uint64_t)1ULL), t10);
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
    void * t18 = gm_l1;
    GM_ST(void *, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4bta8ne_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5719f1b F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_emnjbt3 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h9tbucc_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_t5pgl2t F;
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
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scope_edqq1v8 F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* mark */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 1608);
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)a1));
    gm_f_truncate_7r782uc(t3, t4);
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 1648);
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
    gm_f_truncate_daelje0(t6, t7);
    void * t8 = gm_l0;
    void * t9 = (void *)(((uint8_t *)t8) + 1728);
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
    gm_f_truncate_dsb08pk(t9, t10);
    void * t11 = gm_l0;
    void * t12 = (void *)(((uint8_t *)t11) + 1688);
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
    gm_f_truncate_upeumbt(t12, t13);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
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
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_7r782uc F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_daelje0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_dsb08pk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_upeumbt F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 224)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_tuple_l3i6trt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_tuple_l3i6trt F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* elems */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    gm_t6j6smj7qoctu t6;
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    gm_t53at80pfdus3 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t7 + 8), t6);
    gm_t88rcucg1b7uk t8 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t7);
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
inline __attribute__((aligned(4))) void * gm_f_x_dmu97o4(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_x_dmu97o4 F;
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t88rcucg1b7uk t2 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_range_test_tqcppmu *)((&F.h)->up))->l5));
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_range_test_tqcppmu *)((&F.h)->up))->l10));
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_range_test_tqcppmu *)((&F.h)->up))->l8));
    gm_t2b6tm8c0vgo1 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t5 + 8), t3);
    void * t6 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t2, &t5, t4);
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
inline __attribute__((aligned(4))) void * gm_f_counting_loop_s4u2ugd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint32_t a3, gm_t88rcucg1b7uk a4, void * a5, void * a6) {
    struct gm_fr_counting_loop_s4u2ugd F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    uint32_t gm_l3; /* i */
    gm_t88rcucg1b7uk gm_l4; /* ity */
    void * gm_l5; /* bound */
    void * gm_l6; /* item */
    gm_tl874t2atnlh3 gm_l7; /* span */
    void * gm_l8; /* lt */
    GM_STACK_CHECK(&F, 632u);
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
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    void * t2 = gm_l1;
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 104));
    gm_l7 = t3;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = (void *)(((uint8_t *)t5) + 8);
    gm_t88rcucg1b7uk t7 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t8 = gm_l4;
    uint32_t t9 = gm_l3;
    gm_tl874t2atnlh3 t10 = gm_l7;
    gm_ts6oqppjh0ba3 t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)10ULL));
    gm_t2b6tm8c0vgo1 t12;
    memset(&t12, 0, sizeof t12);
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t12 + 8), t9);
    void * t13 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t8, &t12, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t14 = gm_l5;
    gm_trl9d7m1k5lbd t15;
    GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t15 + 0), t11);
    GM_ST(void *, ((uint8_t *)&t15 + 8), t13);
    GM_ST(void *, ((uint8_t *)&t15 + 16), t14);
    gm_tl874t2atnlh3 t16 = gm_l7;
    gm_t2b6tm8c0vgo1 t17;
    memset(&t17, 0, sizeof t17);
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)30ULL));
    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t17 + 8), t15);
    void * t18 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t7, &t17, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t18;
    gm_trqfvmjtt4sij t19;
    gm_f_list_v5d1o9b_2((void *)&t19, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l9 = t19;
    void * t20 = (void *)((uint8_t *)&F.l9);
    void * t21 = gm_l8;
    gm_tlr9dp305htm7 t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t22 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
    GM_ST(void *, ((uint8_t *)&t22 + 8), t21);
    gm_tftb1odc739t5 t23 = gm_f_push_eb7kbhp_2(t20, &t22);
    gm_l11 = t23;
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srclfnv05vuf0b8, 266, 5);
    gm_trqfvmjtt4sij t25;
    gm_f_list_ir0h3tc_2((void *)&t25, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l10 = t25;
    void * t26 = (void *)((uint8_t *)&F.l10);
    uint32_t t27 = gm_l3;
    gm_t88rcucg1b7uk t28 = gm_l4;
    gm_tl874t2atnlh3 t29 = gm_l7;
    gm_trcmmd12cefb1 t30;
    gm_f_increment_12n73t5((void *)&t30, (void *)NULL, cx, NULL, t27, t28, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t31 = gm_f_push_d6c76ud_2(t26, &t30);
    gm_l12 = t31;
    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srclfnv05vuf0b8, 268, 5);
    void * t33 = gm_l0;
    void * t34 = gm_l1;
    gm_t88rcucg1b7uk t35 = gm_l2;
    void * t36 = gm_l6;
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
    void * t38 = gm_l0;
    void * t39 = GM_LD(void *, ((uint8_t *)t38));
    gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
    gm_tl874t2atnlh3 t41 = gm_l7;
    void * t42 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t39, t40, t41);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t43 = gm_f_loop_with_pattern_d8vl0mj((void *)NULL, cx, NULL, t33, t34, t35, t36, t37, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
__attribute__((aligned(4))) void * gm_f_for_each_value_5qklbg7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_for_each_value_5qklbg7 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    void * gm_l2; /* src0 */
    void * gm_l3; /* tys */
    void * gm_l5; /* src */
    gm_tjpqvfd72o62u gm_l6; /* info */
    gm_t88rcucg1b7uk gm_l7; /* st */
    void * gm_l8; /* slice */
    gm_t88rcucg1b7uk gm_l9; /* elem */
    gm_t88rcucg1b7uk gm_l10; /* e */
    gm_t88rcucg1b7uk gm_l11; /* e */
    gm_t88rcucg1b7uk gm_l12; /* e */
    gm_tqs5p4n2tq19v gm_l13; /* m */
    uint8_t gm_l14; /* x */
    uint32_t gm_l15; /* t */
    gm_t88rcucg1b7uk gm_l16; /* sty */
    void * gm_l17; /* s */
    uint8_t gm_l19; /* c */
    gm_t88rcucg1b7uk gm_l20; /* pointee */
    gm_t88rcucg1b7uk gm_l21; /* e */
    gm_t88rcucg1b7uk gm_l22; /* e */
    void * gm_l23; /* arr */
    gm_b16_8 gm_l24; /* elems */
    gm_t88rcucg1b7uk gm_l25; /* e0 */
    gm_tkn42u7na3t81 gm_l26; /* joined */
    gm_t88rcucg1b7uk gm_l27; /* j */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* k */
    gm_t88rcucg1b7uk gm_l31; /* a */
    gm_t88rcucg1b7uk gm_l32; /* b */
    uint32_t gm_l33; /* t */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* k */
    void * gm_l38; /* field */
    void * gm_l39; /* arr */
    void * gm_l40; /* lp */
    gm_tbngattl1fo51 gm_l42; /* r */
    uint32_t gm_l43; /* t */
    void * gm_l44; /* lo */
    void * gm_l45; /* hi */
    void * gm_l46; /* lp */
    gm_t88rcucg1b7uk gm_l48; /* sty */
    gm_t88rcucg1b7uk gm_l49; /* usize_t */
    uint32_t gm_l50; /* s */
    uint32_t gm_l51; /* i */
    gm_closure gm_l52; /* local */
    void * gm_l53; /* len */
    void * gm_l54; /* item */
    void * gm_l55; /* lp */
    GM_STACK_CHECK(&F, 4914u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t8cc1mtbrhlpt gm_l57;
    gm_t53at80pfdus3 gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_t53at80pfdus3 gm_l60;
    uint8_t gm_l61;
    uint8_t gm_l62;
    gm_t9qkh0rsooc8d gm_l63;
    gm_tabb710d9mq2e gm_l64;
    gm_tftb1odc739t5 gm_l65;
    void * gm_l66;
    void * gm_l67;
    gm_tftb1odc739t5 gm_l68;
    gm_tftb1odc739t5 gm_l69;
    gm_thetuhtouve7f gm_l70;
    gm_tbngattl1fo51 gm_l71;
    gm_tabb710d9mq2e gm_l72;
    gm_tftb1odc739t5 gm_l73;
    gm_tftb1odc739t5 gm_l74;
    gm_tftb1odc739t5 gm_l75;
    gm_tftb1odc739t5 gm_l76;
    gm_tftb1odc739t5 gm_l77;
    {
        void * t43 = gm_l0;
        void * t44 = GM_LD(void *, ((uint8_t *)t43));
        void * t45 = (void *)(((uint8_t *)t44) + 8);
        gm_l3 = t45;
        void * t46 = gm_l1;
        gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t46) + 104));
        F.l4 = t47;
        void * t48 = gm_l0;
        void * t49 = gm_l2;
        void * t50 = gm_f_soa_deref_uiefo9t((void *)NULL, cx, NULL, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t50;
        {
            {
                void * t51 = gm_l0;
                void * t52 = gm_l5;
                gm_f_soa_of_jufdcj2((void *)&gm_l57, (void *)NULL, cx, NULL, t51, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t53 = (uint8_t)((((uint8_t *)&gm_l57))[32] != 0);
                if (!t53) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l57), 32);
                void * t54 = gm_l0;
                void * t55 = gm_l1;
                void * t56 = gm_l5;
                void * t57 = gm_f_soa_for_ovoqsoj((void *)NULL, cx, NULL, t54, t55, t56, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l6)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t57;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t58 = gm_l0;
        void * t59 = (void *)(((uint8_t *)t58) + 96);
        void * t60 = gm_l0;
        void * t61 = GM_LD(void *, ((uint8_t *)t60));
        void * t62 = gm_l5;
        gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t62));
        gm_t88rcucg1b7uk t64 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t59, t61, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t64;
        void * t65 = gm_l5;
        gm_l8 = t65;
        gm_t88rcucg1b7uk t66 = gm_l7;
        gm_l9 = t66;
        void * t67 = gm_l3;
        gm_t88rcucg1b7uk t68 = gm_l7;
        gm_f_kind_jcnt4hm((void *)&gm_l58, t67, GM_CONV(gm_csguankko4lmhd, t68));
        {
            {
                {
                    uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t70 = (uint8_t)(t69 == ((uint8_t)10ULL));
                    if (!t70) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l58) + 8) + 4));
                    gm_l10 = t71;
                    gm_t88rcucg1b7uk t72 = gm_l10;
                    gm_l9 = t72;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    {
                        {
                            uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                            uint8_t t74 = (uint8_t)(t73 == ((uint8_t)11ULL));
                            if (!t74) {
                                goto gm_lb7;
                            }
                            gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l58) + 8) + 8));
                            gm_l11 = t75;
                            goto gm_lb6;
                        }
gm_lb7:;
                        {
                            uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                            uint8_t t77 = (uint8_t)(t76 == ((uint8_t)20ULL));
                            if (!t77) {
                                goto gm_lb8;
                            }
                            gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l58) + 8) + 4));
                            gm_l11 = t78;
                            goto gm_lb6;
                        }
gm_lb8:;
                        goto gm_lb5;
                    }
gm_lb6:;
                    gm_t88rcucg1b7uk t79 = gm_l11;
                    gm_l9 = t79;
                    void * t80 = gm_l0;
                    void * t81 = gm_l5;
                    gm_tqs5p4n2tq19v t82 = gm_f_place_mutability_0tlm4g8(t80, t81);
                    gm_l13 = t82;
                    void * t83 = gm_l0;
                    void * t84 = GM_LD(void *, ((uint8_t *)t83));
                    void * t85 = gm_l5;
                    uint8_t t86 = gm_f_packed_place_5ck3439(t84, t85);
                    if (t86) {
                        gm_tqs5p4n2tq19v t87;
                        memset(&t87, 0, sizeof t87);
                        gm_l13 = t87;
                    }
                    {
                        {
                            uint8_t t88 = (uint8_t)((((uint8_t *)&gm_l13))[1] != 0);
                            if (!t88) {
                                goto gm_lb10;
                            }
                            uint8_t t89 = GM_LD(uint8_t, ((uint8_t *)&gm_l13));
                            gm_l14 = t89;
                            void * t90 = gm_l3;
                            uint8_t t91 = gm_l14;
                            uint8_t t92 = (uint8_t)!t91;
                            gm_t88rcucg1b7uk t93 = gm_l11;
                            gm_t88rcucg1b7uk t94 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t90, t92, t93);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t95 = gm_l5;
                            gm_tao63pk3r18v5 t96;
                            GM_ST(uint8_t, (uint8_t *)&t96 + 25, 0);
                            GM_ST(uint16_t, (uint8_t *)&t96 + 26, 0);
                            GM_ST(uint32_t, (uint8_t *)&t96 + 28, 0);
                            GM_ST(void *, ((uint8_t *)&t96 + 0), t95);
                            GM_ST(void *, ((uint8_t *)&t96 + 8), ((void *)0));
                            GM_ST(void *, ((uint8_t *)&t96 + 16), ((void *)0));
                            GM_ST(uint8_t, ((uint8_t *)&t96 + 24), ((uint8_t)0));
                            gm_tl874t2atnlh3 t97 = F.l4;
                            gm_t2b6tm8c0vgo1 t98;
                            GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t98 + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&t98 + 48, 0);
                            GM_ST(uint64_t, (uint8_t *)&t98 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)21ULL));
                            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t98 + 8), t96);
                            void * t99 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t94, &t98, t97);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l8 = t99;
                            goto gm_lb9;
                        }
gm_lb10:;
                        void * t100 = gm_l0;
                        gm_t88rcucg1b7uk t101 = gm_l7;
                        gm_tl874t2atnlh3 t102 = F.l4;
                        uint32_t t103 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t100, t101, t102);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l15 = t103;
                        void * t104 = gm_l3;
                        gm_t88rcucg1b7uk t105 = gm_l11;
                        gm_t88rcucg1b7uk t106 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t104, ((uint8_t)1), t105);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l16 = t106;
                        gm_t88rcucg1b7uk t107 = gm_l16;
                        gm_t88rcucg1b7uk t108 = gm_l7;
                        uint32_t t109 = gm_l15;
                        gm_tl874t2atnlh3 t110 = F.l4;
                        gm_t2b6tm8c0vgo1 t111;
                        memset(&t111, 0, sizeof t111);
                        GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)15ULL));
                        GM_ST(uint32_t, ((uint8_t *)&t111 + 8), t109);
                        void * t112 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t108, &t111, t110);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tao63pk3r18v5 t113;
                        GM_ST(uint8_t, (uint8_t *)&t113 + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&t113 + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&t113 + 28, 0);
                        GM_ST(void *, ((uint8_t *)&t113 + 0), t112);
                        GM_ST(void *, ((uint8_t *)&t113 + 8), ((void *)0));
                        GM_ST(void *, ((uint8_t *)&t113 + 16), ((void *)0));
                        GM_ST(uint8_t, ((uint8_t *)&t113 + 24), ((uint8_t)0));
                        gm_tl874t2atnlh3 t114 = F.l4;
                        gm_t2b6tm8c0vgo1 t115;
                        GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t115 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t115 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&t115 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)21ULL));
                        GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t115 + 8), t113);
                        void * t116 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t107, &t115, t114);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l17 = t116;
                        gm_trqfvmjtt4sij t117;
                        gm_f_list_ir0h3tc_2((void *)&t117, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l18 = t117;
                        void * t118 = (void *)((uint8_t *)&F.l18);
                        uint32_t t119 = gm_l15;
                        void * t120 = gm_l5;
                        gm_ttggq7ng4kp3p t121;
                        GM_ST(uint32_t, (uint8_t *)&t121 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t121 + 0), t119);
                        GM_ST(void *, ((uint8_t *)&t121 + 8), t120);
                        gm_trcmmd12cefb1 t122;
                        GM_ST(uint8_t, (uint8_t *)&t122 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t122 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t122 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t122 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t122 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t122 + 8), t121);
                        gm_tftb1odc739t5 t123 = gm_f_push_d6c76ud_2(t118, &t122);
                        gm_l59 = t123;
                        uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                        if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srclfnv05vuf0b8, 337, 17);
                        gm_t88rcucg1b7uk t125 = gm_l16;
                        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                        void * t127 = gm_l17;
                        gm_tl874t2atnlh3 t128 = F.l4;
                        void * t129 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t125, t126, t127, t128);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l8 = t129;
                    }
gm_lb9:;
                    goto gm_lb3;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t131 = (uint8_t)(t130 == ((uint8_t)8ULL));
                    if (!t131) {
                        goto gm_lb11;
                    }
                    uint8_t t132 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 8));
                    gm_l19 = t132;
                    gm_t88rcucg1b7uk t133 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l58) + 8) + 4));
                    gm_l20 = t133;
                    void * t134 = gm_l3;
                    gm_t88rcucg1b7uk t135 = gm_l20;
                    gm_f_kind_jcnt4hm((void *)&gm_l60, t134, GM_CONV(gm_csguankko4lmhd, t135));
                    {
                        {
                            {
                                {
                                    uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l60) + 0));
                                    uint8_t t137 = (uint8_t)(t136 == ((uint8_t)11ULL));
                                    if (!t137) {
                                        goto gm_lb15;
                                    }
                                    gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l60) + 8) + 8));
                                    gm_l21 = t138;
                                    goto gm_lb14;
                                }
gm_lb15:;
                                {
                                    uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l60) + 0));
                                    uint8_t t140 = (uint8_t)(t139 == ((uint8_t)20ULL));
                                    if (!t140) {
                                        goto gm_lb16;
                                    }
                                    gm_t88rcucg1b7uk t141 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l60) + 8) + 4));
                                    gm_l21 = t141;
                                    goto gm_lb14;
                                }
gm_lb16:;
                                goto gm_lb13;
                            }
gm_lb14:;
                            gm_t88rcucg1b7uk t142 = gm_l21;
                            gm_l9 = t142;
                            gm_t88rcucg1b7uk t143 = gm_l20;
                            void * t144 = gm_l5;
                            gm_tl874t2atnlh3 t145 = F.l4;
                            gm_t2b6tm8c0vgo1 t146;
                            memset(&t146, 0, sizeof t146);
                            GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)18ULL));
                            GM_ST(void *, ((uint8_t *)&t146 + 8), t144);
                            void * t147 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t143, &t146, t145);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l23 = t147;
                            void * t148 = gm_l3;
                            uint8_t t149 = gm_l19;
                            gm_t88rcucg1b7uk t150 = gm_l21;
                            gm_t88rcucg1b7uk t151 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t148, t149, t150);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t152 = gm_l23;
                            gm_tao63pk3r18v5 t153;
                            GM_ST(uint8_t, (uint8_t *)&t153 + 25, 0);
                            GM_ST(uint16_t, (uint8_t *)&t153 + 26, 0);
                            GM_ST(uint32_t, (uint8_t *)&t153 + 28, 0);
                            GM_ST(void *, ((uint8_t *)&t153 + 0), t152);
                            GM_ST(void *, ((uint8_t *)&t153 + 8), ((void *)0));
                            GM_ST(void *, ((uint8_t *)&t153 + 16), ((void *)0));
                            GM_ST(uint8_t, ((uint8_t *)&t153 + 24), ((uint8_t)0));
                            gm_tl874t2atnlh3 t154 = F.l4;
                            gm_t2b6tm8c0vgo1 t155;
                            GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t155 + 40, 0);
                            GM_ST(uint64_t, (uint8_t *)&t155 + 48, 0);
                            GM_ST(uint64_t, (uint8_t *)&t155 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)21ULL));
                            GM_ST(gm_tao63pk3r18v5, ((uint8_t *)&t155 + 8), t153);
                            void * t156 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t151, &t155, t154);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l8 = t156;
                            goto gm_lb12;
                        }
gm_lb13:;
                        {
                            F.ret = ((void *)0);
                            goto gm_fret;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 342, 13);
                    }
gm_lb12:;
                    goto gm_lb3;
                }
gm_lb11:;
                {
                    uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t158 = (uint8_t)(t157 == ((uint8_t)26ULL));
                    if (!t158) {
                        goto gm_lb18;
                    }
                    void * t159 = gm_l0;
                    void * t160 = GM_LD(void *, ((uint8_t *)t159));
                    gm_tl874t2atnlh3 t161 = F.l4;
                    void * t162 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t160, t161);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t162;
                    goto gm_fret;
                }
gm_lb18:;
                {
                    uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t164 = (uint8_t)(t163 == ((uint8_t)12ULL));
                    if (!t164) {
                        goto gm_lb19;
                    }
                    gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                    gm_l24 = t165;
                    uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                    uint8_t t167 = ((t166 == ((uint64_t)0ULL)));
                    if (t167) {
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
                    void * t168 = GM_LD(void *, ((uint8_t *)&gm_l24));
                    uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t169)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t169, gm_srclfnv05vuf0b8, 356, 19);
                    gm_t88rcucg1b7uk t170 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t168) + (uintptr_t)((uint64_t)0ULL) * 4));
                    gm_l25 = t170;
                    void * t171 = gm_l0;
                    gm_b16_8 t172 = gm_l24;
                    gm_tkn42u7na3t81 t173 = gm_f_tuple_elem_join_om8llaj((void *)NULL, cx, NULL, t171, t172);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l26 = t173;
                    {
                        {
                            uint8_t t174 = (uint8_t)((((uint8_t *)&gm_l26))[4] != 0);
                            if (!t174) {
                                goto gm_lb21;
                            }
                            gm_t88rcucg1b7uk t175 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l26));
                            gm_l27 = t175;
                            gm_t88rcucg1b7uk t176 = gm_l27;
                            gm_l25 = t176;
                            goto gm_lb20;
                        }
gm_lb21:;
                    }
gm_lb20:;
                    gm_l29 = ((uint64_t)1ULL);
                    uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                    gm_l28 = t177;
                    {
                        for (;;) {
                            uint64_t t179 = gm_l29;
                            uint64_t t180 = gm_l28;
                            uint8_t t181 = (t179 < t180);
                            if (!t181) {
                                goto gm_lb22;
                            }
                            {
                                {
                                    {
                                        uint64_t t182 = gm_l29;
                                        gm_l30 = t182;
                                        {
                                            {
                                                {
                                                    {
                                                        uint8_t t183 = (uint8_t)((((uint8_t *)&gm_l26))[4] != 0);
                                                        if (t183) {
                                                            goto gm_lb30;
                                                        }
                                                        gm_l61 = ((uint8_t)1);
                                                        goto gm_lb29;
                                                    }
gm_lb30:;
                                                    {
                                                        gm_l61 = ((uint8_t)0);
                                                        goto gm_lb29;
                                                    }
gm_lb31:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 361, 20);
                                                }
gm_lb29:;
                                                uint8_t t184 = gm_l61;
                                                if (!t184) {
                                                    goto gm_lb28;
                                                }
                                                void * t185 = gm_l0;
                                                void * t186 = (void *)(((uint8_t *)t185) + 96);
                                                void * t187 = gm_l3;
                                                gm_t88rcucg1b7uk t188 = gm_l25;
                                                void * t189 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                                uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                uint64_t t191 = gm_l30;
                                                gm_t88rcucg1b7uk t192 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t189) + (uintptr_t)t191 * 4));
                                                gm_t9qkh0rsooc8d t193 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t186, t187, t188, t192);
                                                if (gm_unw.kind) {
                                                    goto gm_ll178;
                                                }
                                                gm_l63 = t193;
                                                {
                                                    {
                                                        uint8_t t194 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                                        if (t194) {
                                                            goto gm_lb33;
                                                        }
                                                        gm_l62 = ((uint8_t)1);
                                                        goto gm_lb32;
                                                    }
gm_lb33:;
                                                    {
                                                        gm_l62 = ((uint8_t)0);
                                                        goto gm_lb32;
                                                    }
gm_lb34:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 361, 39);
                                                }
gm_lb32:;
                                                uint8_t t195 = gm_l62;
                                                if (!t195) {
                                                    goto gm_lb28;
                                                }
                                                void * t196 = gm_l0;
                                                void * t197 = (void *)(((uint8_t *)t196) + 96);
                                                void * t198 = gm_l0;
                                                void * t199 = GM_LD(void *, ((uint8_t *)t198));
                                                gm_t88rcucg1b7uk t200 = gm_l25;
                                                gm_t88rcucg1b7uk t201 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t197, t199, t200);
                                                if (gm_unw.kind) {
                                                    goto gm_ll178;
                                                }
                                                gm_l31 = t201;
                                                void * t202 = gm_l0;
                                                void * t203 = (void *)(((uint8_t *)t202) + 96);
                                                void * t204 = gm_l0;
                                                void * t205 = GM_LD(void *, ((uint8_t *)t204));
                                                void * t206 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                                uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                uint64_t t208 = gm_l30;
                                                gm_t88rcucg1b7uk t209 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t206) + (uintptr_t)t208 * 4));
                                                gm_t88rcucg1b7uk t210 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t203, t205, t209);
                                                if (gm_unw.kind) {
                                                    goto gm_ll178;
                                                }
                                                gm_l32 = t210;
                                                void * t211 = gm_l0;
                                                void * t212 = gm_l1;
                                                void * t213 = GM_LD(void *, (((uint8_t *)t212) + 56));
                                                gm_tl874t2atnlh3 t214 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t213) + 72));
                                                void * t215 = gm_l0;
                                                gm_t88rcucg1b7uk t216 = gm_l31;
                                                gm_b16_8 t217 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t215, t216);
                                                if (gm_unw.kind) {
                                                    goto gm_ll178;
                                                }
                                                void * t218 = gm_l0;
                                                gm_t88rcucg1b7uk t219 = gm_l32;
                                                gm_b16_8 t220 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t218, t219);
                                                if (gm_unw.kind) {
                                                    goto gm_ll178;
                                                }
                                                gm_t6j6smj7qoctu t221;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t221 + 0), t217);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t221 + 16), t220);
                                                gm_f_format_t00o2ec((void *)&gm_l64, (void *)NULL, cx, NULL, &t221);
                                                if (gm_unw.kind) {
                                                    goto gm_ll178;
                                                }
                                                uint8_t t222 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                                if (!t222) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 8, gm_srclfnv05vuf0b8, 364, 43);
                                                gm_b16_8 t223 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 8));
                                                gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&t223));
                                                gm_b0_1 t225 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t211, t214, t224);
                                                if (gm_unw.kind) {
                                                    goto gm_ll178;
                                                }
                                                void * t226 = gm_l0;
                                                void * t227 = GM_LD(void *, ((uint8_t *)t226));
                                                gm_tl874t2atnlh3 t228 = F.l4;
                                                void * t229 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t227, t228);
                                                if (gm_unw.kind) {
                                                    goto gm_ll178;
                                                }
                                                F.ret = t229;
                                                goto gm_fret;
                                            }
gm_lb28:;
                                        }
gm_lb27:;
                                        goto gm_lb25;
                                    }
gm_lb26:;
                                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 360, 13);
                                }
gm_lb25:;
                            }
gm_lb24:;
                            uint64_t t230 = gm_l29;
                            uint64_t r231; bool o232 = __builtin_add_overflow(t230, ((uint64_t)1ULL), &r231);
                            uint64_t t233 = r231;
                            gm_l29 = t233;
                        }
                    }
                    goto gm_ls234;
gm_ll178:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb22; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb24; }
                    }
                    goto gm_lad;
gm_ls234:;
gm_lb22:;
                    void * t235 = gm_l0;
                    gm_t88rcucg1b7uk t236 = gm_l7;
                    gm_tl874t2atnlh3 t237 = F.l4;
                    uint32_t t238 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t235, t236, t237);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l33 = t238;
                    gm_trqfvmjtt4sij t239;
                    gm_f_list_vp1djv1_2((void *)&t239, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l34 = t239;
                    gm_l36 = ((uint64_t)0ULL);
                    uint64_t t240 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                    gm_l35 = t240;
                    {
                        for (;;) {
                            uint64_t t242 = gm_l36;
                            uint64_t t243 = gm_l35;
                            uint8_t t244 = (t242 < t243);
                            if (!t244) {
                                goto gm_lb35;
                            }
                            {
                                {
                                    {
                                        uint64_t t245 = gm_l36;
                                        gm_l37 = t245;
                                        void * t246 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                        uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                        uint64_t t248 = gm_l37;
                                        gm_t88rcucg1b7uk t249 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t246) + (uintptr_t)t248 * 4));
                                        gm_t88rcucg1b7uk t250 = gm_l7;
                                        uint32_t t251 = gm_l33;
                                        gm_tl874t2atnlh3 t252 = F.l4;
                                        gm_t2b6tm8c0vgo1 t253;
                                        memset(&t253, 0, sizeof t253);
                                        GM_ST(uint8_t, ((uint8_t *)&t253 + 0), ((uint8_t)15ULL));
                                        GM_ST(uint32_t, ((uint8_t *)&t253 + 8), t251);
                                        void * t254 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t250, &t253, t252);
                                        if (gm_unw.kind) {
                                            goto gm_ll241;
                                        }
                                        uint64_t t255 = gm_l37;
                                        uint32_t t256 = (uint32_t)t255;
                                        gm_tbl8il92l97ut t257;
                                        GM_ST(uint32_t, (uint8_t *)&t257 + 12, 0);
                                        GM_ST(void *, ((uint8_t *)&t257 + 0), t254);
                                        GM_ST(uint32_t, ((uint8_t *)&t257 + 8), t256);
                                        gm_tl874t2atnlh3 t258 = F.l4;
                                        gm_t2b6tm8c0vgo1 t259;
                                        memset(&t259, 0, sizeof t259);
                                        GM_ST(uint8_t, ((uint8_t *)&t259 + 0), ((uint8_t)19ULL));
                                        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t259 + 8), t257);
                                        void * t260 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t249, &t259, t258);
                                        if (gm_unw.kind) {
                                            goto gm_ll241;
                                        }
                                        gm_l38 = t260;
                                        void * t261 = (void *)((uint8_t *)&F.l34);
                                        void * t262 = gm_l0;
                                        void * t263 = gm_l38;
                                        gm_t88rcucg1b7uk t264 = gm_l25;
                                        void * t265 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t262, t263, t264);
                                        if (gm_unw.kind) {
                                            goto gm_ll241;
                                        }
                                        gm_tftb1odc739t5 t266 = gm_f_push_d943vgp_2(t261, &(*(gm_tcgrjq9toscke *)((uint8_t *)t265)));
                                        gm_l65 = t266;
                                        uint8_t t267 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                                        if (!t267) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 1, gm_srclfnv05vuf0b8, 372, 17);
                                        goto gm_lb38;
                                    }
gm_lb39:;
                                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 370, 13);
                                }
gm_lb38:;
                            }
gm_lb37:;
                            uint64_t t268 = gm_l36;
                            uint64_t r269; bool o270 = __builtin_add_overflow(t268, ((uint64_t)1ULL), &r269);
                            uint64_t t271 = r269;
                            gm_l36 = t271;
                        }
                    }
                    goto gm_ls272;
gm_ll241:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb35; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb37; }
                    }
                    goto gm_lad;
gm_ls272:;
gm_lb35:;
                    void * t273 = gm_l3;
                    uint64_t t274 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                    uint64_t t275 = (uint64_t)t274;
                    gm_t88rcucg1b7uk t276 = gm_l25;
                    gm_t88rcucg1b7uk t277 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t273, t275, t276);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)&F.l34));
                    gm_tl874t2atnlh3 t279 = F.l4;
                    gm_t2b6tm8c0vgo1 t280;
                    memset(&t280, 0, sizeof t280);
                    GM_ST(uint8_t, ((uint8_t *)&t280 + 0), ((uint8_t)36ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t280 + 8), t278);
                    void * t281 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t277, &t280, t279);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l39 = t281;
                    void * t282 = gm_l0;
                    void * t283 = gm_l1;
                    void * t284 = gm_l39;
                    void * t285 = gm_f_for_each_value_5qklbg7((void *)NULL, cx, NULL, t282, t283, t284);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l66 = t285;
                    uint8_t t286 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l66)) != 0);
                    if (t286) {
                        void * t287 = GM_LD(void *, ((uint8_t *)&gm_l66));
                        gm_l67 = t287;
                    } else {
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
                    void * t288 = gm_l67;
                    gm_l40 = t288;
                    gm_trqfvmjtt4sij t289;
                    gm_f_list_ir0h3tc_2((void *)&t289, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l41 = t289;
                    void * t290 = (void *)((uint8_t *)&F.l41);
                    uint32_t t291 = gm_l33;
                    void * t292 = gm_l5;
                    gm_ttggq7ng4kp3p t293;
                    GM_ST(uint32_t, (uint8_t *)&t293 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t293 + 0), t291);
                    GM_ST(void *, ((uint8_t *)&t293 + 8), t292);
                    gm_trcmmd12cefb1 t294;
                    GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t294 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t294 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t294 + 8), t293);
                    gm_tftb1odc739t5 t295 = gm_f_push_d6c76ud_2(t290, &t294);
                    gm_l68 = t295;
                    uint8_t t296 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                    if (!t296) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_srclfnv05vuf0b8, 377, 13);
                    void * t297 = (void *)((uint8_t *)&F.l41);
                    void * t298 = gm_l40;
                    gm_trcmmd12cefb1 t299;
                    GM_ST(uint8_t, (uint8_t *)&t299 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t299 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t299 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t299 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t299 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t299 + 0), ((uint8_t)4ULL));
                    GM_ST(void *, ((uint8_t *)&t299 + 8), t298);
                    gm_tftb1odc739t5 t300 = gm_f_push_d6c76ud_2(t297, &t299);
                    gm_l69 = t300;
                    uint8_t t301 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                    if (!t301) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 1, gm_srclfnv05vuf0b8, 378, 13);
                    void * t302 = gm_l0;
                    void * t303 = GM_LD(void *, ((uint8_t *)t302));
                    gm_b16_8 t304 = GM_LD(gm_b16_8, ((uint8_t *)&F.l41));
                    gm_tl874t2atnlh3 t305 = F.l4;
                    void * t306 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t303, t304, t305);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t306;
                    goto gm_fret;
                }
gm_lb19:;
            }
            {
                {
                    uint8_t t307 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                    uint8_t t308 = (uint8_t)(t307 == ((uint8_t)17ULL));
                    if (!t308) {
                        goto gm_lb40;
                    }
                    void * t309 = gm_l0;
                    void * t310 = GM_LD(void *, ((uint8_t *)t309));
                    gm_t88rcucg1b7uk t311 = gm_l7;
                    gm_thetuhtouve7f t312 = gm_f_range_of_dmmo768((void *)NULL, cx, NULL, t310, t311);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l70 = t312;
                    uint8_t t313 = (uint8_t)((((uint8_t *)&gm_l70))[8] != 0);
                    if (t313) {
                        gm_tbngattl1fo51 t314 = GM_LD(gm_tbngattl1fo51, ((uint8_t *)&gm_l70));
                        gm_l71 = t314;
                    } else {
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
                    gm_tbngattl1fo51 t315 = gm_l71;
                    gm_l42 = t315;
                    void * t316 = gm_l3;
                    gm_t88rcucg1b7uk t317 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
                    uint8_t t318 = gm_f_is_integer_8d4f9ek(t316, GM_CONV(gm_csguankko4lmhd, t317));
                    uint8_t t319 = (uint8_t)!t318;
                    if (t319) {
                        void * t320 = gm_l0;
                        void * t321 = gm_l1;
                        void * t322 = GM_LD(void *, (((uint8_t *)t321) + 56));
                        gm_tl874t2atnlh3 t323 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t322) + 72));
                        void * t324 = gm_l0;
                        gm_t88rcucg1b7uk t325 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
                        gm_b16_8 t326 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t324, t325);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t327;
                        GM_ST(gm_b16_8, ((uint8_t *)&t327 + 0), t326);
                        gm_f_format_h1n6lc9((void *)&gm_l72, (void *)NULL, cx, NULL, t327);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t328 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                        if (!t328) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 8, gm_srclfnv05vuf0b8, 385, 39);
                        gm_b16_8 t329 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l72) + 8));
                        gm_b16_8 t330 = GM_LD(gm_b16_8, ((uint8_t *)&t329));
                        gm_b0_1 t331 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t320, t323, t330);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t332 = gm_l0;
                        void * t333 = GM_LD(void *, ((uint8_t *)t332));
                        gm_tl874t2atnlh3 t334 = F.l4;
                        void * t335 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t333, t334);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t335;
                        goto gm_fret;
                    }
                    void * t336 = gm_l0;
                    gm_t88rcucg1b7uk t337 = gm_l7;
                    gm_tl874t2atnlh3 t338 = F.l4;
                    uint32_t t339 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t336, t337, t338);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l43 = t339;
                    gm_t88rcucg1b7uk t340 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
                    gm_t88rcucg1b7uk t341 = gm_l7;
                    uint32_t t342 = gm_l43;
                    gm_tl874t2atnlh3 t343 = F.l4;
                    gm_t2b6tm8c0vgo1 t344;
                    memset(&t344, 0, sizeof t344);
                    GM_ST(uint8_t, ((uint8_t *)&t344 + 0), ((uint8_t)15ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t344 + 8), t342);
                    void * t345 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t341, &t344, t343);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tbl8il92l97ut t346;
                    GM_ST(uint32_t, (uint8_t *)&t346 + 12, 0);
                    GM_ST(void *, ((uint8_t *)&t346 + 0), t345);
                    GM_ST(uint32_t, ((uint8_t *)&t346 + 8), ((uint32_t)0ULL));
                    gm_tl874t2atnlh3 t347 = F.l4;
                    gm_t2b6tm8c0vgo1 t348;
                    memset(&t348, 0, sizeof t348);
                    GM_ST(uint8_t, ((uint8_t *)&t348 + 0), ((uint8_t)19ULL));
                    GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t348 + 8), t346);
                    void * t349 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t340, &t348, t347);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l44 = t349;
                    gm_t88rcucg1b7uk t350 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
                    gm_t88rcucg1b7uk t351 = gm_l7;
                    uint32_t t352 = gm_l43;
                    gm_tl874t2atnlh3 t353 = F.l4;
                    gm_t2b6tm8c0vgo1 t354;
                    memset(&t354, 0, sizeof t354);
                    GM_ST(uint8_t, ((uint8_t *)&t354 + 0), ((uint8_t)15ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t354 + 8), t352);
                    void * t355 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t351, &t354, t353);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tbl8il92l97ut t356;
                    GM_ST(uint32_t, (uint8_t *)&t356 + 12, 0);
                    GM_ST(void *, ((uint8_t *)&t356 + 0), t355);
                    GM_ST(uint32_t, ((uint8_t *)&t356 + 8), ((uint32_t)1ULL));
                    gm_tl874t2atnlh3 t357 = F.l4;
                    gm_t2b6tm8c0vgo1 t358;
                    memset(&t358, 0, sizeof t358);
                    GM_ST(uint8_t, ((uint8_t *)&t358 + 0), ((uint8_t)19ULL));
                    GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t358 + 8), t356);
                    void * t359 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t350, &t358, t357);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l45 = t359;
                    void * t360 = gm_l0;
                    void * t361 = gm_l1;
                    void * t362 = gm_l44;
                    void * t363 = gm_l45;
                    gm_t88rcucg1b7uk t364 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l42) + 4));
                    uint8_t t365 = GM_LD(uint8_t, ((uint8_t *)&gm_l42));
                    void * t366 = gm_f_range_loop_44fv3au((void *)NULL, cx, NULL, t360, t361, t362, t363, t364, t365);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l46 = t366;
                    gm_trqfvmjtt4sij t367;
                    gm_f_list_ir0h3tc_2((void *)&t367, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l47 = t367;
                    void * t368 = (void *)((uint8_t *)&F.l47);
                    uint32_t t369 = gm_l43;
                    void * t370 = gm_l5;
                    gm_ttggq7ng4kp3p t371;
                    GM_ST(uint32_t, (uint8_t *)&t371 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t371 + 0), t369);
                    GM_ST(void *, ((uint8_t *)&t371 + 8), t370);
                    gm_trcmmd12cefb1 t372;
                    GM_ST(uint8_t, (uint8_t *)&t372 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t372 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t372 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t372 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t372 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t372 + 8), t371);
                    gm_tftb1odc739t5 t373 = gm_f_push_d6c76ud_2(t368, &t372);
                    gm_l73 = t373;
                    uint8_t t374 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
                    if (!t374) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l73) + 1, gm_srclfnv05vuf0b8, 393, 13);
                    void * t375 = (void *)((uint8_t *)&F.l47);
                    void * t376 = gm_l46;
                    gm_trcmmd12cefb1 t377;
                    GM_ST(uint8_t, (uint8_t *)&t377 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t377 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t377 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t377 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t377 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t377 + 0), ((uint8_t)4ULL));
                    GM_ST(void *, ((uint8_t *)&t377 + 8), t376);
                    gm_tftb1odc739t5 t378 = gm_f_push_d6c76ud_2(t375, &t377);
                    gm_l74 = t378;
                    uint8_t t379 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                    if (!t379) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 1, gm_srclfnv05vuf0b8, 394, 13);
                    void * t380 = gm_l0;
                    void * t381 = GM_LD(void *, ((uint8_t *)t380));
                    gm_b16_8 t382 = GM_LD(gm_b16_8, ((uint8_t *)&F.l47));
                    gm_tl874t2atnlh3 t383 = F.l4;
                    void * t384 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t381, t382, t383);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t384;
                    goto gm_fret;
                }
gm_lb40:;
            }
            {
                {
                    F.ret = ((void *)0);
                    goto gm_fret;
                }
gm_lb41:;
                gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 321, 5);
            }
        }
gm_lb3:;
    }
    {
        void * t385 = gm_l8;
        gm_t88rcucg1b7uk t386 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t385));
        gm_l48 = t386;
        void * t387 = gm_l3;
        gm_t88rcucg1b7uk t388 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t387);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l49 = t388;
        void * t389 = gm_l0;
        gm_t88rcucg1b7uk t390 = gm_l48;
        gm_tl874t2atnlh3 t391 = F.l4;
        uint32_t t392 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t389, t390, t391);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l50 = t392;
        void * t393 = gm_l0;
        gm_t88rcucg1b7uk t394 = gm_l49;
        gm_tl874t2atnlh3 t395 = F.l4;
        uint32_t t396 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t393, t394, t395);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l51 = t396;
        gm_closure t397 = ((gm_closure){(void *)gm_f_local_l5o3d47, (void *)&F.h, cx});
        gm_l52 = t397;
        gm_t88rcucg1b7uk t398 = gm_l49;
        gm_closure t399 = gm_l52;
        uint32_t t400 = gm_l50;
        gm_t88rcucg1b7uk t401 = gm_l48;
        gm_closure t402 = t399;
        gm_node *ccx403 = t402.cx;
        gm_node cn404; { gm_node *f = gm_find(cx, 587034433); if (f) { cn404 = *f; cn404.next = ccx403; ccx403 = &cn404; } }
        void * t405 = t402.code;
        void * t406;
        if (GM_PTR_TAGGED(t405)) {
            t406 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t405))(t400, GM_CONV(gm_csguankko4lmhd, t401));
        } else {
            t406 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t405)(t402.env, ccx403, NULL, t400, t401);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t407 = F.l4;
        gm_t2b6tm8c0vgo1 t408;
        memset(&t408, 0, sizeof t408);
        GM_ST(uint8_t, ((uint8_t *)&t408 + 0), ((uint8_t)22ULL));
        GM_ST(void *, ((uint8_t *)&t408 + 8), t406);
        void * t409 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t398, &t408, t407);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l53 = t409;
        gm_t88rcucg1b7uk t410 = gm_l9;
        gm_closure t411 = gm_l52;
        uint32_t t412 = gm_l50;
        gm_t88rcucg1b7uk t413 = gm_l48;
        gm_closure t414 = t411;
        gm_node *ccx415 = t414.cx;
        gm_node cn416; { gm_node *f = gm_find(cx, 587034433); if (f) { cn416 = *f; cn416.next = ccx415; ccx415 = &cn416; } }
        void * t417 = t414.code;
        void * t418;
        if (GM_PTR_TAGGED(t417)) {
            t418 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t417))(t412, GM_CONV(gm_csguankko4lmhd, t413));
        } else {
            t418 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t417)(t414.env, ccx415, NULL, t412, t413);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_closure t419 = gm_l52;
        uint32_t t420 = gm_l51;
        gm_t88rcucg1b7uk t421 = gm_l49;
        gm_closure t422 = t419;
        gm_node *ccx423 = t422.cx;
        gm_node cn424; { gm_node *f = gm_find(cx, 587034433); if (f) { cn424 = *f; cn424.next = ccx423; ccx423 = &cn424; } }
        void * t425 = t422.code;
        void * t426;
        if (GM_PTR_TAGGED(t425)) {
            t426 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t425))(t420, GM_CONV(gm_csguankko4lmhd, t421));
        } else {
            t426 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t425)(t422.env, ccx423, NULL, t420, t421);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tlcl4u558l6p5 t427;
        GM_ST(uint8_t, (uint8_t *)&t427 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t427 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t427 + 20, 0);
        GM_ST(void *, ((uint8_t *)&t427 + 0), t418);
        GM_ST(void *, ((uint8_t *)&t427 + 8), t426);
        GM_ST(uint8_t, ((uint8_t *)&t427 + 16), ((uint8_t)0));
        gm_tl874t2atnlh3 t428 = F.l4;
        gm_t2b6tm8c0vgo1 t429;
        memset(&t429, 0, sizeof t429);
        GM_ST(uint8_t, ((uint8_t *)&t429 + 0), ((uint8_t)20ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t429 + 8), t427);
        void * t430 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t410, &t429, t428);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l54 = t430;
        void * t431 = gm_l0;
        void * t432 = gm_l1;
        gm_t88rcucg1b7uk t433 = gm_l9;
        uint32_t t434 = gm_l51;
        gm_t88rcucg1b7uk t435 = gm_l49;
        void * t436 = gm_l53;
        void * t437 = gm_l54;
        void * t438 = gm_f_counting_loop_s4u2ugd((void *)NULL, cx, NULL, t431, t432, t433, t434, t435, t436, t437);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l55 = t438;
        gm_trqfvmjtt4sij t439;
        gm_f_list_ir0h3tc_2((void *)&t439, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l56 = t439;
        void * t440 = (void *)((uint8_t *)&F.l56);
        uint32_t t441 = gm_l50;
        void * t442 = gm_l8;
        gm_ttggq7ng4kp3p t443;
        GM_ST(uint32_t, (uint8_t *)&t443 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t443 + 0), t441);
        GM_ST(void *, ((uint8_t *)&t443 + 8), t442);
        gm_trcmmd12cefb1 t444;
        GM_ST(uint8_t, (uint8_t *)&t444 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t444 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t444 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t444 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t444 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t444 + 8), t443);
        gm_tftb1odc739t5 t445 = gm_f_push_d6c76ud_2(t440, &t444);
        gm_l75 = t445;
        uint8_t t446 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
        if (!t446) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 1, gm_srclfnv05vuf0b8, 409, 5);
        void * t447 = (void *)((uint8_t *)&F.l56);
        uint32_t t448 = gm_l51;
        gm_t88rcucg1b7uk t449 = gm_l49;
        gm_tbci5ospk5nnn t450;
        gm_f_zero_s25l990((void *)&t450, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t451 = F.l4;
        gm_t2b6tm8c0vgo1 t452;
        GM_ST(uint8_t, (uint8_t *)&t452 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t452 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t452 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t452 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t452 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t452 + 8), t450);
        void * t453 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t449, &t452, t451);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t454;
        GM_ST(uint32_t, (uint8_t *)&t454 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t454 + 0), t448);
        GM_ST(void *, ((uint8_t *)&t454 + 8), t453);
        gm_trcmmd12cefb1 t455;
        GM_ST(uint8_t, (uint8_t *)&t455 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t455 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t455 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t455 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t455 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t455 + 8), t454);
        gm_tftb1odc739t5 t456 = gm_f_push_d6c76ud_2(t447, &t455);
        gm_l76 = t456;
    }
    {
        uint8_t t457 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
        if (!t457) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 1, gm_srclfnv05vuf0b8, 410, 5);
        void * t458 = (void *)((uint8_t *)&F.l56);
        void * t459 = gm_l55;
        gm_trcmmd12cefb1 t460;
        GM_ST(uint8_t, (uint8_t *)&t460 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t460 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t460 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t460 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t460 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t460 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t460 + 8), t459);
        gm_tftb1odc739t5 t461 = gm_f_push_d6c76ud_2(t458, &t460);
        gm_l77 = t461;
        uint8_t t462 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
        if (!t462) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 1, gm_srclfnv05vuf0b8, 411, 5);
        void * t463 = gm_l0;
        void * t464 = GM_LD(void *, ((uint8_t *)t463));
        gm_b16_8 t465 = GM_LD(gm_b16_8, ((uint8_t *)&F.l56));
        gm_tl874t2atnlh3 t466 = F.l4;
        void * t467 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t464, t465, t466);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t467;
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
__attribute__((aligned(4))) void * gm_f_for_loop_b6hjsbq(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_for_loop_b6hjsbq F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint8_t gm_l3; /* is_range */
    gm_ts6oqppjh0ba3 gm_l4; /* op */
    void * gm_l5; /* src */
    void * gm_l6; /* e */
    gm_ts6oqppjh0ba3 gm_l7; /* op */
    void * gm_l8; /* lhs */
    void * gm_l9; /* rhs */
    void * gm_l10; /* lo */
    void * gm_l11; /* hi */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    gm_t10fqegjtup5b gm_l13; /* <tmp> */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t88rcucg1b7uk gm_l16; /* t */
    gm_t2i8ne5ibd7v4 gm_l17; /* c */
    GM_STACK_CHECK(&F, 1354u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l18;
    void * gm_l19;
    gm_t8uadrbfpr7kk gm_l20;
    gm_tftb1odc739t5 gm_l21;
    void * t18 = gm_l1;
    gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t18) + 104));
    gm_l2 = t19;
    gm_l3 = ((uint8_t)0);
    {
        {
            void * t20 = gm_l1;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 56));
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t21) + 8) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)13ULL));
            if (!t23) {
                goto gm_lb2;
            }
            gm_ts6oqppjh0ba3 t24 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t21) + 8) + 8));
            gm_l4 = t24;
            gm_ts6oqppjh0ba3 t25 = gm_l4;
            gm_ts6oqppjh0ba3 t26;
            GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)10ULL));
            gm_ts6oqppjh0ba3 t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)11ULL));
            gm_t9ldr1q5hn9fq t28;
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t28 + 0), t26);
            GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t28 + 2), t27);
            uint8_t t29 = 0;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t28) + 0, ((uint8_t *)&t25))) t29 = 1;
            if (gm_eqbqb4gb1d92(((uint8_t *)&t28) + 2, ((uint8_t *)&t25))) t29 = 1;
            uint8_t t30 = (uint8_t)(t29);
            gm_l3 = t30;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    uint8_t t31 = gm_l3;
    uint8_t t32 = (uint8_t)!t31;
    if (t32) {
        void * t33 = gm_l1;
        void * t34 = GM_LD(void *, (((uint8_t *)t33) + 56));
        {
            {
                uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t34) + 8) + 0));
                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)19ULL));
                if (!t36) {
                    goto gm_lb4;
                }
                gm_l18 = ((uint8_t)1);
                goto gm_lb3;
            }
gm_lb4:;
            {
                gm_l18 = ((uint8_t)0);
                goto gm_lb3;
            }
gm_lb5:;
            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 194, 12);
        }
gm_lb3:;
        uint8_t t37 = gm_l18;
        if (t37) {
            void * t38 = gm_l0;
            void * t39 = gm_l1;
            void * t40 = gm_f_for_iterator_cc5qo7n((void *)NULL, cx, NULL, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t40;
            goto gm_fret;
        }
        void * t41 = gm_l0;
        void * t42 = gm_l1;
        void * t43 = GM_LD(void *, (((uint8_t *)t42) + 56));
        gm_tkn42u7na3t81 t44;
        memset(&t44, 0, sizeof t44);
        void * t45 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t41, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t45;
        {
            {
                void * t46 = gm_l0;
                void * t47 = gm_l1;
                void * t48 = gm_l5;
                void * t49 = gm_f_for_each_value_5qklbg7((void *)NULL, cx, NULL, t46, t47, t48);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t49;
                uint8_t t50 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l19)) != 0);
                if (!t50) {
                    goto gm_lb7;
                }
                void * t51 = GM_LD(void *, ((uint8_t *)&gm_l19));
                gm_l6 = t51;
                void * t52 = gm_l6;
                F.ret = t52;
                goto gm_fret;
            }
gm_lb7:;
        }
gm_lb6:;
        void * t53 = gm_l0;
        void * t54 = gm_l1;
        void * t55 = GM_LD(void *, (((uint8_t *)t54) + 56));
        gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 72));
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_skue5huof65fj));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(79));
        gm_b0_1 t58 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t53, t56, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t59 = gm_l0;
        void * t60 = GM_LD(void *, ((uint8_t *)t59));
        gm_tl874t2atnlh3 t61 = gm_l2;
        void * t62 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t60, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t62;
        goto gm_fret;
    }
    {
        {
            void * t63 = gm_l1;
            void * t64 = GM_LD(void *, (((uint8_t *)t63) + 56));
            uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t64) + 8) + 0));
            uint8_t t66 = (uint8_t)(t65 == ((uint8_t)13ULL));
            if (!t66) {
                goto gm_lb9;
            }
            gm_ts6oqppjh0ba3 t67 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t64) + 8) + 8));
            gm_l7 = t67;
            void * t68 = GM_LD(void *, (((((uint8_t *)t64) + 8) + 8) + 8));
            gm_l8 = t68;
            void * t69 = GM_LD(void *, (((((uint8_t *)t64) + 8) + 8) + 16));
            gm_l9 = t69;
            void * t70 = gm_l0;
            void * t71 = gm_l8;
            gm_tkn42u7na3t81 t72;
            memset(&t72, 0, sizeof t72);
            void * t73 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t70, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t73;
            void * t74 = gm_l0;
            void * t75 = gm_l9;
            gm_tkn42u7na3t81 t76;
            memset(&t76, 0, sizeof t76);
            void * t77 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t74, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t77;
            void * t78 = gm_l0;
            void * t79 = gm_l10;
            gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t79));
            void * t81 = gm_l11;
            gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t81));
            void * t83 = gm_l1;
            void * t84 = GM_LD(void *, (((uint8_t *)t83) + 56));
            gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t84) + 72));
            gm_b16_8 t86;
            GM_ST(void *, (uint8_t *)&t86, (void *)(gm_sp6ona76fn0di));
            GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(12));
            gm_t88rcucg1b7uk t87 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t78, t80, t82, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t87;
            gm_t88rcucg1b7uk t88 = gm_l12;
            void * t89 = gm_l10;
            gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t89));
            void * t91 = gm_l11;
            gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t91));
            gm_t10fqegjtup5b t93;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t93 + 0), t88);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t93 + 4), t90);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t93 + 8), t92);
            gm_l13 = t93;
            void * t94 = (void *)((uint8_t *)&gm_l13);
            uint64_t t95 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t96 = (uintptr_t)((uint64_t)3ULL);
            if (t95 > t96 || t96 > (uintptr_t)((uint64_t)3ULL)) gm_trap_slice(t95, t96, ((uint64_t)3ULL), gm_srclfnv05vuf0b8, 204, 5);
            uint64_t t97 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t98;
            GM_ST(void *, (uint8_t *)&t98, (void *)(((uint8_t *)t94 + (uintptr_t)((uint64_t)0ULL) * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(t97));
            gm_l14 = t98;
            gm_l15 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t99 = gm_l15;
                    uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint8_t t101 = (t99 < t100);
                    if (!t101) {
                        goto gm_lb10;
                    }
                    {
                        void * t102 = GM_LD(void *, ((uint8_t *)&gm_l14));
                        uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                        uint64_t t104 = gm_l15;
                        {
                            {
                                gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t102) + (uintptr_t)t104 * 4));
                                gm_l16 = t105;
                                {
                                    {
                                        void * t106 = gm_l0;
                                        void * t107 = (void *)(((uint8_t *)t106) + 96);
                                        void * t108 = gm_l0;
                                        void * t109 = GM_LD(void *, ((uint8_t *)t108));
                                        void * t110 = (void *)(((uint8_t *)t109) + 8);
                                        gm_t88rcucg1b7uk t111 = gm_l16;
                                        gm_t9qkh0rsooc8d t112;
                                        GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)1ULL));
                                        gm_f_set_fallback_s66n99d((void *)&gm_l20, (void *)NULL, cx, NULL, t107, t110, t111, t112);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t113 = (uint8_t)((((uint8_t *)&gm_l20))[16] != 0);
                                        if (!t113) {
                                            goto gm_lb16;
                                        }
                                        gm_t2i8ne5ibd7v4 t114 = GM_LD(gm_t2i8ne5ibd7v4, ((uint8_t *)&gm_l20));
                                        gm_l17 = t114;
                                        void * t115 = gm_l0;
                                        void * t116 = GM_LD(void *, ((uint8_t *)t115));
                                        void * t117 = gm_l0;
                                        void * t118 = GM_LD(void *, ((uint8_t *)t117));
                                        void * t119 = gm_l1;
                                        void * t120 = GM_LD(void *, (((uint8_t *)t119) + 56));
                                        gm_tl874t2atnlh3 t121 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t120) + 72));
                                        gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l17));
                                        gm_tl874t2atnlh3 t123 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l17) + 4));
                                        gm_t4gut2c9fj1kt t124;
                                        gm_f_early_fallback_error_4lq49mb((void *)&t124, (void *)NULL, cx, NULL, t118, t121, t122, t123);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b0_1 t125 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t116, &t124);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb10;
                                    }
gm_lb16:;
                                }
gm_lb15:;
                                goto gm_lb13;
                            }
gm_lb14:;
                            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 204, 5);
                        }
gm_lb13:;
                    }
gm_lb12:;
                    uint64_t t126 = gm_l15;
                    uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                    uint64_t t129 = r127;
                    gm_l15 = t129;
                }
            }
gm_lb10:;
            void * t130 = gm_l0;
            void * t131 = (void *)(((uint8_t *)t130) + 320);
            gm_t88rcucg1b7uk t132 = gm_l12;
            void * t133 = gm_l1;
            void * t134 = GM_LD(void *, (((uint8_t *)t133) + 56));
            gm_tl874t2atnlh3 t135 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t134) + 72));
            gm_b16_8 t136;
            GM_ST(void *, (uint8_t *)&t136, (void *)(gm_sl4ns6hobicht));
            GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(5));
            gm_t91vm1g0k2rpm t137;
            GM_ST(uint16_t, (uint8_t *)&t137 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t137 + 36, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t137 + 0), t132);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t137 + 4), t135);
            GM_ST(gm_b16_8, ((uint8_t *)&t137 + 16), t136);
            GM_ST(uint8_t, ((uint8_t *)&t137 + 32), ((uint8_t)1));
            GM_ST(uint8_t, ((uint8_t *)&t137 + 33), ((uint8_t)0));
            gm_tpm220j5qhjke t138;
            GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)3ULL));
            GM_ST(gm_t91vm1g0k2rpm, ((uint8_t *)&t138 + 8), t137);
            gm_tftb1odc739t5 t139 = gm_f_push_7koe9lk(t131, &t138);
            gm_l21 = t139;
            uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
            if (!t140) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srclfnv05vuf0b8, 210, 5);
            void * t141 = gm_l0;
            void * t142 = gm_l1;
            void * t143 = gm_l10;
            void * t144 = gm_l11;
            gm_t88rcucg1b7uk t145 = gm_l12;
            gm_ts6oqppjh0ba3 t146 = gm_l7;
            uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&t146) + 0));
            uint8_t t148 = (uint8_t)((t147 == ((uint8_t)11ULL)));
            gm_ts6oqppjh0ba3 t149;
            GM_ST(uint8_t, (uint8_t *)&t149 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)11ULL));
            void * t150 = gm_f_range_loop_44fv3au((void *)NULL, cx, NULL, t141, t142, t143, t144, t145, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t150;
            goto gm_fret;
        }
gm_lb9:;
        void * t151 = gm_l0;
        void * t152 = GM_LD(void *, ((uint8_t *)t151));
        gm_tl874t2atnlh3 t153 = gm_l2;
        void * t154 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t152, t153);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t154;
        goto gm_fret;
    }
gm_lb8:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_in_expr_su8ofmp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_in_expr_su8ofmp F;
    void * gm_l0; /* cx */
    void * gm_l1; /* checked */
    void * gm_l2; /* container */
    uint8_t gm_l3; /* negated */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    void * gm_l6; /* elem */
    gm_ts6oqppjh0ba3 gm_l7; /* op */
    void * gm_l8; /* lhs */
    void * gm_l9; /* rhs */
    void * gm_l10; /* lo */
    void * gm_l11; /* hi */
    gm_t88rcucg1b7uk gm_l12; /* ty */
    gm_b16_8 gm_l13; /* elems */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint8_t gm_l17; /* inline */
    gm_b16_8 gm_l18; /* elems */
    gm_b16_8 gm_l19; /* elems */
    gm_t88rcucg1b7uk gm_l21; /* et */
    uint8_t gm_l22; /* wraps */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    void * gm_l26; /* v */
    gm_t88rcucg1b7uk gm_l27; /* vt */
    gm_t88rcucg1b7uk gm_l28; /* joined */
    gm_t88rcucg1b7uk gm_l31; /* jr */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    void * gm_l34; /* v */
    void * gm_l35; /* m */
    gm_t88rcucg1b7uk gm_l36; /* mt */
    void * gm_l37; /* c */
    gm_t88rcucg1b7uk gm_l38; /* ct */
    gm_tbngattl1fo51 gm_l39; /* r */
    uint32_t gm_l40; /* t */
    void * gm_l42; /* lo */
    void * gm_l43; /* hi */
    gm_t88rcucg1b7uk gm_l44; /* item */
    gm_t88rcucg1b7uk gm_l45; /* e */
    gm_t88rcucg1b7uk gm_l46; /* e */
    gm_t88rcucg1b7uk gm_l47; /* e */
    gm_b16_8 gm_l48; /* es */
    gm_b16_8 gm_l49; /* es */
    gm_tkn42u7na3t81 gm_l50; /* joined */
    uint64_t gm_l51; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    uint64_t gm_l53; /* k */
    gm_t88rcucg1b7uk gm_l54; /* a */
    gm_t88rcucg1b7uk gm_l55; /* b */
    gm_t88rcucg1b7uk gm_l56; /* j */
    uint32_t gm_l57; /* t */
    uint64_t gm_l60; /* <tmp> */
    uint64_t gm_l61; /* <tmp> */
    uint64_t gm_l62; /* k */
    void * gm_l63; /* field */
    gm_t88rcucg1b7uk gm_l64; /* jt */
    void * gm_l66; /* agg */
    gm_t88rcucg1b7uk gm_l67; /* rt */
    gm_t88rcucg1b7uk gm_l68; /* ri */
    gm_t88rcucg1b7uk gm_l69; /* et */
    GM_STACK_CHECK(&F, 5702u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tftb1odc739t5 gm_l70;
    uint8_t gm_l71;
    gm_toqf51949ppls gm_l72;
    void * gm_l73;
    void * gm_l74;
    uint8_t gm_l75;
    uint8_t gm_l76;
    gm_t53at80pfdus3 gm_l77;
    uint8_t gm_l78;
    uint8_t gm_l79;
    gm_t53at80pfdus3 gm_l80;
    uint8_t gm_l81;
    gm_th2lqbo77pvsc gm_l82;
    gm_tftb1odc739t5 gm_l83;
    uint8_t gm_l84;
    gm_th2lqbo77pvsc gm_l85;
    gm_tftb1odc739t5 gm_l86;
    gm_tftb1odc739t5 gm_l87;
    gm_thetuhtouve7f gm_l88;
    gm_tftb1odc739t5 gm_l89;
    gm_t53at80pfdus3 gm_l90;
    gm_tabb710d9mq2e gm_l91;
    gm_t53at80pfdus3 gm_l92;
    uint8_t gm_l93;
    uint8_t gm_l94;
    gm_t9qkh0rsooc8d gm_l95;
    gm_tabb710d9mq2e gm_l96;
    gm_tftb1odc739t5 gm_l97;
    gm_tftb1odc739t5 gm_l98;
    gm_tftb1odc739t5 gm_l99;
    uint8_t gm_l100;
    gm_th2lqbo77pvsc gm_l101;
    gm_tabb710d9mq2e gm_l102;
    {
        void * t84 = gm_l0;
        void * t85 = GM_LD(void *, ((uint8_t *)t84));
        void * t86 = (void *)(((uint8_t *)t85) + 8);
        gm_l5 = t86;
        void * t87 = gm_l1;
        gm_l6 = t87;
        {
            {
                void * t88 = gm_l2;
                uint8_t t89 = GM_LD(uint8_t, ((((uint8_t *)t88) + 8) + 0));
                uint8_t t90 = (uint8_t)(t89 == ((uint8_t)13ULL));
                if (!t90) {
                    goto gm_lb2;
                }
                gm_ts6oqppjh0ba3 t91 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t88) + 8) + 8));
                gm_l7 = t91;
                void * t92 = GM_LD(void *, (((((uint8_t *)t88) + 8) + 8) + 8));
                gm_l8 = t92;
                void * t93 = GM_LD(void *, (((((uint8_t *)t88) + 8) + 8) + 16));
                gm_l9 = t93;
                gm_ts6oqppjh0ba3 t94 = gm_l7;
                gm_ts6oqppjh0ba3 t95;
                GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)10ULL));
                gm_ts6oqppjh0ba3 t96;
                GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)11ULL));
                gm_t9ldr1q5hn9fq t97;
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t97 + 0), t95);
                GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t97 + 2), t96);
                uint8_t t98 = 0;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t97) + 0, ((uint8_t *)&t94))) t98 = 1;
                if (gm_eqbqb4gb1d92(((uint8_t *)&t97) + 2, ((uint8_t *)&t94))) t98 = 1;
                uint8_t t99 = (uint8_t)(t98);
                if (!t99) {
                    goto gm_lb2;
                }
                void * t100 = gm_l0;
                void * t101 = gm_l8;
                void * t102 = gm_l6;
                gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t102));
                gm_tkn42u7na3t81 t104;
                GM_ST(uint8_t, (uint8_t *)&t104 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t104 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t104), t103);
                ((uint8_t *)&t104)[4] = 1;
                void * t105 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t100, t101, t104);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t105;
                void * t106 = gm_l0;
                void * t107 = gm_l9;
                void * t108 = gm_l6;
                gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t108));
                gm_tkn42u7na3t81 t110;
                GM_ST(uint8_t, (uint8_t *)&t110 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t110 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t110), t109);
                ((uint8_t *)&t110)[4] = 1;
                void * t111 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t106, t107, t110);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t111;
                void * t112 = gm_l0;
                void * t113 = gm_l6;
                gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t113));
                void * t115 = gm_l10;
                gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t115));
                gm_tl874t2atnlh3 t117 = gm_l4;
                gm_b16_8 t118;
                GM_ST(void *, (uint8_t *)&t118, (void *)(gm_su22guvkav5b5));
                GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(20));
                gm_t88rcucg1b7uk t119 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t112, t114, t116, t117, t118);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t119;
                void * t120 = gm_l0;
                gm_t88rcucg1b7uk t121 = gm_l12;
                void * t122 = gm_l11;
                gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t122));
                gm_tl874t2atnlh3 t124 = gm_l4;
                gm_b16_8 t125;
                GM_ST(void *, (uint8_t *)&t125, (void *)(gm_su22guvkav5b5));
                GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(20));
                gm_t88rcucg1b7uk t126 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t120, t121, t123, t124, t125);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t127 = gm_l0;
                void * t128 = (void *)(((uint8_t *)t127) + 320);
                gm_t88rcucg1b7uk t129 = gm_l12;
                gm_tl874t2atnlh3 t130 = gm_l4;
                gm_t2i8ne5ibd7v4 t131;
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t131 + 0), t129);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t131 + 4), t130);
                gm_tpm220j5qhjke t132;
                GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t132 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t132 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t132 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t132 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t2i8ne5ibd7v4, ((uint8_t *)&t132 + 8), t131);
                gm_tftb1odc739t5 t133 = gm_f_push_7koe9lk(t128, &t132);
                gm_l70 = t133;
                uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 1, gm_srclfnv05vuf0b8, 14, 9);
                void * t135 = gm_l0;
                void * t136 = gm_l6;
                gm_b16_8 t137 = gm_f_empty_slice_vj3ckdj_2();
                gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
                void * t139 = gm_l10;
                void * t140 = gm_l11;
                gm_t88rcucg1b7uk t141 = gm_l12;
                gm_ts6oqppjh0ba3 t142 = gm_l7;
                uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&t142) + 0));
                uint8_t t144 = (uint8_t)((t143 == ((uint8_t)11ULL)));
                uint8_t t145 = gm_l3;
                gm_tl874t2atnlh3 t146 = gm_l4;
                gm_ts6oqppjh0ba3 t147;
                GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)11ULL));
                void * t148 = gm_f_range_test_tqcppmu((void *)NULL, cx, NULL, t135, t136, t138, t139, t140, t141, t144, t145, t146);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t148;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        {
            {
                void * t149 = gm_l2;
                uint8_t t150 = GM_LD(uint8_t, ((((uint8_t *)t149) + 8) + 0));
                uint8_t t151 = (uint8_t)(t150 == ((uint8_t)10ULL));
                if (!t151) {
                    goto gm_lb4;
                }
                gm_b16_8 t152 = GM_LD(gm_b16_8, ((((uint8_t *)t149) + 8) + 8));
                gm_l13 = t152;
                gm_b16_8 t153 = gm_l13;
                gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
                uint8_t t155 = gm_f_has_spread_bndtpd6(t154);
                uint8_t t156 = (uint8_t)!t155;
                if (!t156) {
                    goto gm_lb4;
                }
                gm_l15 = ((uint64_t)0ULL);
                uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                gm_l14 = t157;
                {
                    for (;;) {
                        uint64_t t158 = gm_l15;
                        uint64_t t159 = gm_l14;
                        uint8_t t160 = (t158 < t159);
                        if (!t160) {
                            goto gm_lb5;
                        }
                        {
                            {
                                {
                                    uint64_t t161 = gm_l15;
                                    gm_l16 = t161;
                                    void * t162 = gm_l0;
                                    void * t163 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                    uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                    uint64_t t165 = gm_l16;
                                    void * t166 = (void *)((((uint8_t *)t163) + (uintptr_t)t165 * 136) + 40);
                                    void * t167 = gm_l6;
                                    gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t167));
                                    gm_f_is_pattern_operand_p51tban((void *)&gm_l72, (void *)NULL, cx, NULL, t162, t166, t168);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    {
                                        {
                                            uint8_t t169 = (uint8_t)((((uint8_t *)&gm_l72))[64] != 0);
                                            if (t169) {
                                                goto gm_lb11;
                                            }
                                            gm_l71 = ((uint8_t)0);
                                            goto gm_lb10;
                                        }
gm_lb11:;
                                        {
                                            gm_l71 = ((uint8_t)1);
                                            goto gm_lb10;
                                        }
gm_lb12:;
                                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 21, 16);
                                    }
gm_lb10:;
                                    uint8_t t170 = gm_l71;
                                    if (t170) {
                                        void * t171 = gm_l0;
                                        void * t172 = gm_l6;
                                        gm_b16_8 t173 = gm_l13;
                                        gm_b16_8 t174 = GM_LD(gm_b16_8, ((uint8_t *)&t173));
                                        uint8_t t175 = gm_l3;
                                        gm_tl874t2atnlh3 t176 = gm_l4;
                                        void * t177 = gm_f_in_patterns_mi6p7hb((void *)NULL, cx, NULL, t171, t172, t174, t175, t176);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t177;
                                        goto gm_fret;
                                    }
                                    goto gm_lb8;
                                }
gm_lb9:;
                                gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 20, 9);
                            }
gm_lb8:;
                        }
gm_lb7:;
                        uint64_t t178 = gm_l15;
                        uint64_t r179; bool o180 = __builtin_add_overflow(t178, ((uint64_t)1ULL), &r179);
                        uint64_t t181 = r179;
                        gm_l15 = t181;
                    }
                }
gm_lb5:;
                goto gm_lb3;
            }
gm_lb4:;
        }
gm_lb3:;
        gm_l17 = ((uint8_t)0);
        {
            {
                void * t182 = gm_l2;
                uint8_t t183 = GM_LD(uint8_t, ((((uint8_t *)t182) + 8) + 0));
                uint8_t t184 = (uint8_t)(t183 == ((uint8_t)10ULL));
                if (!t184) {
                    goto gm_lb14;
                }
                gm_b16_8 t185 = GM_LD(gm_b16_8, ((((uint8_t *)t182) + 8) + 8));
                gm_l18 = t185;
                gm_b16_8 t186 = gm_l18;
                gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&t186));
                uint8_t t188 = gm_f_has_spread_bndtpd6(t187);
                uint8_t t189 = (uint8_t)!t188;
                if (!t189) {
                    goto gm_lb14;
                }
                gm_l17 = ((uint8_t)1);
                goto gm_lb13;
            }
gm_lb14:;
        }
gm_lb13:;
    }
    {
        {
            {
                {
                    {
                        uint8_t t192 = gm_l17;
                        if (!t192) {
                            goto gm_lb16;
                        }
                        void * t193 = gm_l2;
                        uint8_t t194 = GM_LD(uint8_t, ((((uint8_t *)t193) + 8) + 0));
                        uint8_t t195 = (uint8_t)(t194 == ((uint8_t)10ULL));
                        if (!t195) {
                            goto gm_lb16;
                        }
                        gm_b16_8 t196 = GM_LD(gm_b16_8, ((((uint8_t *)t193) + 8) + 8));
                        gm_l19 = t196;
                        {
                            {
                                gm_trqfvmjtt4sij t198;
                                gm_f_list_s1kpkpr((void *)&t198, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll191;
                                }
                                F.l20 = t198;
                                void * t199 = gm_l0;
                                void * t200 = (void *)(((uint8_t *)t199) + 96);
                                void * t201 = gm_l0;
                                void * t202 = GM_LD(void *, ((uint8_t *)t201));
                                void * t203 = gm_l6;
                                gm_t88rcucg1b7uk t204 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t203));
                                gm_t88rcucg1b7uk t205 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t200, t202, t204);
                                if (gm_unw.kind) {
                                    goto gm_ld197_1;
                                }
                                gm_l21 = t205;
                                void * t206 = gm_l5;
                                gm_t88rcucg1b7uk t207 = gm_l21;
                                gm_f_kind_jcnt4hm((void *)&gm_l77, t206, GM_CONV(gm_csguankko4lmhd, t207));
                                {
                                    {
                                        uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&gm_l77) + 0));
                                        uint8_t t209 = (uint8_t)(t208 == ((uint8_t)13ULL));
                                        if (!t209) {
                                            goto gm_lb19;
                                        }
                                        gm_l76 = ((uint8_t)1);
                                        goto gm_lb18;
                                    }
gm_lb19:;
                                    {
                                        uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l77) + 0));
                                        uint8_t t211 = (uint8_t)(t210 == ((uint8_t)14ULL));
                                        if (!t211) {
                                            goto gm_lb20;
                                        }
                                        gm_l76 = ((uint8_t)1);
                                        goto gm_lb18;
                                    }
gm_lb20:;
                                    {
                                        gm_l76 = ((uint8_t)0);
                                        goto gm_lb18;
                                    }
gm_lb21:;
                                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 34, 18);
                                }
gm_lb18:;
                                uint8_t t212 = gm_l76;
                                if (t212) {
                                    void * t213 = gm_l5;
                                    gm_t88rcucg1b7uk t214 = gm_l21;
                                    uint8_t t215 = gm_f_has_infer_3o82a0b(t213, GM_CONV(gm_csguankko4lmhd, t214));
                                    uint8_t t216 = (uint8_t)!t215;
                                    gm_l75 = t216;
                                } else {
                                    gm_l75 = ((uint8_t)0);
                                }
                                uint8_t t217 = gm_l75;
                                gm_l22 = t217;
                                gm_l24 = ((uint64_t)0ULL);
                                uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                gm_l23 = t218;
                                {
                                    for (;;) {
                                        uint64_t t219 = gm_l24;
                                        uint64_t t220 = gm_l23;
                                        uint8_t t221 = (t219 < t220);
                                        if (!t221) {
                                            goto gm_lb22;
                                        }
                                        {
                                            {
                                                {
                                                    uint64_t t222 = gm_l24;
                                                    gm_l25 = t222;
                                                    void * t223 = gm_l0;
                                                    void * t224 = GM_LD(void *, ((uint8_t *)&gm_l19));
                                                    uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                                    uint64_t t226 = gm_l25;
                                                    void * t227 = (void *)((((uint8_t *)t224) + (uintptr_t)t226 * 136) + 40);
                                                    void * t228 = gm_l6;
                                                    gm_t88rcucg1b7uk t229 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t228));
                                                    gm_tkn42u7na3t81 t230;
                                                    GM_ST(uint8_t, (uint8_t *)&t230 + 5, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t230 + 6, 0);
                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t230), t229);
                                                    ((uint8_t *)&t230)[4] = 1;
                                                    void * t231 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t223, t227, t230);
                                                    if (gm_unw.kind) {
                                                        goto gm_ld197_1;
                                                    }
                                                    gm_l26 = t231;
                                                    void * t232 = gm_l0;
                                                    void * t233 = (void *)(((uint8_t *)t232) + 96);
                                                    void * t234 = gm_l0;
                                                    void * t235 = GM_LD(void *, ((uint8_t *)t234));
                                                    void * t236 = gm_l26;
                                                    gm_t88rcucg1b7uk t237 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t236));
                                                    gm_t88rcucg1b7uk t238 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t233, t235, t237);
                                                    if (gm_unw.kind) {
                                                        goto gm_ld197_1;
                                                    }
                                                    gm_l27 = t238;
                                                    {
                                                        {
                                                            uint8_t t239 = gm_l22;
                                                            if (!t239) {
                                                                goto gm_lb28;
                                                            }
                                                            gm_t88rcucg1b7uk t240 = gm_l27;
                                                            gm_t88rcucg1b7uk t241 = gm_l21;
                                                            uint8_t t242 = (!gm_eq9m6mvtmgde(((uint8_t *)&t240), ((uint8_t *)&t241)));
                                                            if (!t242) {
                                                                goto gm_lb28;
                                                            }
                                                            void * t243 = gm_l5;
                                                            gm_t88rcucg1b7uk t244 = gm_l27;
                                                            gm_f_kind_jcnt4hm((void *)&gm_l80, t243, GM_CONV(gm_csguankko4lmhd, t244));
                                                            {
                                                                {
                                                                    uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&gm_l80) + 0));
                                                                    uint8_t t246 = (uint8_t)(t245 == ((uint8_t)23ULL));
                                                                    if (!t246) {
                                                                        goto gm_lb30;
                                                                    }
                                                                    gm_l79 = ((uint8_t)1);
                                                                    goto gm_lb29;
                                                                }
gm_lb30:;
                                                                {
                                                                    gm_l79 = ((uint8_t)0);
                                                                    goto gm_lb29;
                                                                }
gm_lb31:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 38, 44);
                                                            }
gm_lb29:;
                                                            uint8_t t247 = gm_l79;
                                                            if (t247) {
                                                                gm_l78 = ((uint8_t)1);
                                                            } else {
                                                                void * t248 = gm_l0;
                                                                gm_t88rcucg1b7uk t249 = gm_l27;
                                                                gm_t88rcucg1b7uk t250 = gm_l21;
                                                                gm_f_coercion_steps_ibqvhta((void *)&gm_l82, (void *)NULL, cx, NULL, t248, t249, t250);
                                                                if (gm_unw.kind) {
                                                                    goto gm_ld197_1;
                                                                }
                                                                {
                                                                    {
                                                                        uint8_t t251 = (uint8_t)((((uint8_t *)&gm_l82))[16] != 0);
                                                                        if (t251) {
                                                                            goto gm_lb33;
                                                                        }
                                                                        gm_l81 = ((uint8_t)0);
                                                                        goto gm_lb32;
                                                                    }
gm_lb33:;
                                                                    {
                                                                        gm_l81 = ((uint8_t)1);
                                                                        goto gm_lb32;
                                                                    }
gm_lb34:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 38, 73);
                                                                }
gm_lb32:;
                                                                uint8_t t252 = gm_l81;
                                                                gm_l78 = t252;
                                                            }
                                                            uint8_t t253 = gm_l78;
                                                            if (!t253) {
                                                                goto gm_lb28;
                                                            }
                                                            void * t254 = gm_l0;
                                                            void * t255 = gm_l26;
                                                            gm_t88rcucg1b7uk t256 = gm_l21;
                                                            void * t257 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t254, t255, t256);
                                                            if (gm_unw.kind) {
                                                                goto gm_ld197_1;
                                                            }
                                                            gm_l26 = t257;
                                                            goto gm_lb27;
                                                        }
gm_lb28:;
                                                    }
gm_lb27:;
                                                    void * t258 = (void *)((uint8_t *)&F.l20);
                                                    void * t259 = gm_l26;
                                                    gm_tftb1odc739t5 t260 = gm_f_push_pf02nv6(t258, t259);
                                                    gm_l83 = t260;
                                                    uint8_t t261 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
                                                    if (!t261) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l83) + 1, gm_srclfnv05vuf0b8, 41, 13);
                                                    goto gm_lb25;
                                                }
gm_lb26:;
                                                gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 35, 9);
                                            }
gm_lb25:;
                                        }
gm_lb24:;
                                        uint64_t t262 = gm_l24;
                                        uint64_t r263; bool o264 = __builtin_add_overflow(t262, ((uint64_t)1ULL), &r263);
                                        uint64_t t265 = r263;
                                        gm_l24 = t265;
                                    }
                                }
gm_lb22:;
                            }
                            {
                                void * t266 = gm_l0;
                                void * t267 = gm_l6;
                                gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&t268));
                                gm_t88rcucg1b7uk t270 = gm_f_join_members_97aedtg((void *)NULL, cx, NULL, t266, t267, t269);
                                if (gm_unw.kind) {
                                    goto gm_ld197_1;
                                }
                                gm_l28 = t270;
                                gm_t88rcucg1b7uk t271 = gm_l28;
                                void * t272 = gm_l6;
                                gm_t88rcucg1b7uk t273 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t272));
                                uint8_t t274 = (!gm_eq9m6mvtmgde(((uint8_t *)&t271), ((uint8_t *)&t273)));
                                if (t274) {
                                    void * t275 = gm_l0;
                                    void * t276 = gm_l6;
                                    gm_t88rcucg1b7uk t277 = gm_l28;
                                    void * t278 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t275, t276, t277);
                                    if (gm_unw.kind) {
                                        goto gm_ld197_1;
                                    }
                                    gm_l6 = t278;
                                }
                                gm_trqfvmjtt4sij t279;
                                gm_f_list_vp1djv1_2((void *)&t279, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ld197_1;
                                }
                                F.l29 = t279;
                                gm_trqfvmjtt4sij t280;
                                gm_f_list_nof7vfj((void *)&t280, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ld197_1;
                                }
                                F.l30 = t280;
                                void * t281 = gm_l0;
                                void * t282 = (void *)(((uint8_t *)t281) + 96);
                                void * t283 = gm_l0;
                                void * t284 = GM_LD(void *, ((uint8_t *)t283));
                                gm_t88rcucg1b7uk t285 = gm_l28;
                                gm_t88rcucg1b7uk t286 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t282, t284, t285);
                                if (gm_unw.kind) {
                                    goto gm_ld197_1;
                                }
                                gm_l31 = t286;
                                gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                gm_l32 = t287;
                                gm_l33 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t288 = gm_l33;
                                        uint64_t t289 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                        uint8_t t290 = (t288 < t289);
                                        if (!t290) {
                                            goto gm_lb35;
                                        }
                                        {
                                            void * t291 = GM_LD(void *, ((uint8_t *)&gm_l32));
                                            uint64_t t292 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                            uint64_t t293 = gm_l33;
                                            {
                                                {
                                                    void * t294 = GM_LD(void *, (((uint8_t *)t291) + (uintptr_t)t293 * 8));
                                                    gm_l34 = t294;
                                                    void * t295 = gm_l34;
                                                    gm_l35 = t295;
                                                    void * t296 = gm_l0;
                                                    void * t297 = (void *)(((uint8_t *)t296) + 96);
                                                    void * t298 = gm_l0;
                                                    void * t299 = GM_LD(void *, ((uint8_t *)t298));
                                                    void * t300 = gm_l35;
                                                    gm_t88rcucg1b7uk t301 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t300));
                                                    gm_t88rcucg1b7uk t302 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t297, t299, t301);
                                                    if (gm_unw.kind) {
                                                        goto gm_ld197_1;
                                                    }
                                                    gm_l36 = t302;
                                                    {
                                                        {
                                                            gm_t88rcucg1b7uk t303 = gm_l36;
                                                            gm_t88rcucg1b7uk t304 = gm_l31;
                                                            uint8_t t305 = (!gm_eq9m6mvtmgde(((uint8_t *)&t303), ((uint8_t *)&t304)));
                                                            if (!t305) {
                                                                goto gm_lb41;
                                                            }
                                                            void * t306 = gm_l5;
                                                            gm_t88rcucg1b7uk t307 = gm_l36;
                                                            uint8_t t308 = gm_f_has_infer_3o82a0b(t306, GM_CONV(gm_csguankko4lmhd, t307));
                                                            uint8_t t309 = (uint8_t)!t308;
                                                            if (!t309) {
                                                                goto gm_lb41;
                                                            }
                                                            void * t310 = gm_l0;
                                                            gm_t88rcucg1b7uk t311 = gm_l36;
                                                            gm_t88rcucg1b7uk t312 = gm_l31;
                                                            gm_f_coercion_steps_ibqvhta((void *)&gm_l85, (void *)NULL, cx, NULL, t310, t311, t312);
                                                            if (gm_unw.kind) {
                                                                goto gm_ld197_1;
                                                            }
                                                            {
                                                                {
                                                                    uint8_t t313 = (uint8_t)((((uint8_t *)&gm_l85))[16] != 0);
                                                                    if (t313) {
                                                                        goto gm_lb43;
                                                                    }
                                                                    gm_l84 = ((uint8_t)0);
                                                                    goto gm_lb42;
                                                                }
gm_lb43:;
                                                                {
                                                                    gm_l84 = ((uint8_t)1);
                                                                    goto gm_lb42;
                                                                }
gm_lb44:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 51, 59);
                                                            }
gm_lb42:;
                                                            uint8_t t314 = gm_l84;
                                                            if (!t314) {
                                                                goto gm_lb41;
                                                            }
                                                            void * t315 = gm_l0;
                                                            void * t316 = gm_l35;
                                                            gm_t88rcucg1b7uk t317 = gm_l31;
                                                            void * t318 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t315, t316, t317);
                                                            if (gm_unw.kind) {
                                                                goto gm_ld197_1;
                                                            }
                                                            gm_l35 = t318;
                                                            goto gm_lb40;
                                                        }
gm_lb41:;
                                                    }
gm_lb40:;
                                                    void * t319 = gm_l0;
                                                    void * t320 = gm_l6;
                                                    gm_t88rcucg1b7uk t321 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t320));
                                                    void * t322 = gm_l35;
                                                    gm_t88rcucg1b7uk t323 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t322));
                                                    void * t324 = gm_l35;
                                                    gm_tl874t2atnlh3 t325 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t324) + 72));
                                                    gm_b16_8 t326;
                                                    GM_ST(void *, (uint8_t *)&t326, (void *)(gm_smdscldu1a8hg));
                                                    GM_ST(uintptr_t, (uint8_t *)&t326 + 8, (uintptr_t)(41));
                                                    gm_t88rcucg1b7uk t327 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t319, t321, t323, t325, t326);
                                                    if (gm_unw.kind) {
                                                        goto gm_ld197_1;
                                                    }
                                                    void * t328 = (void *)((uint8_t *)&F.l29);
                                                    void * t329 = gm_l35;
                                                    gm_tftb1odc739t5 t330 = gm_f_push_d943vgp_2(t328, &(*(gm_tcgrjq9toscke *)((uint8_t *)t329)));
                                                    gm_l86 = t330;
                                                    uint8_t t331 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
                                                    if (!t331) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l86) + 1, gm_srclfnv05vuf0b8, 53, 13);
                                                    void * t332 = (void *)((uint8_t *)&F.l30);
                                                    void * t333 = gm_l35;
                                                    gm_t88rcucg1b7uk t334 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t333));
                                                    gm_tftb1odc739t5 t335 = gm_f_push_k3l9nic(t332, GM_CONV(gm_csguankko4lmhd, t334));
                                                    gm_l87 = t335;
                                                    uint8_t t336 = (uint8_t)!((((uint8_t *)&gm_l87))[0] != 0);
                                                    if (!t336) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l87) + 1, gm_srclfnv05vuf0b8, 54, 13);
                                                    goto gm_lb38;
                                                }
gm_lb39:;
                                                gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 48, 9);
                                            }
gm_lb38:;
                                        }
gm_lb37:;
                                        uint64_t t337 = gm_l33;
                                        uint64_t r338; bool o339 = __builtin_add_overflow(t337, ((uint64_t)1ULL), &r338);
                                        uint64_t t340 = r338;
                                        gm_l33 = t340;
                                    }
                                }
gm_lb35:;
                            }
                            {
                                void * t341 = gm_l5;
                                gm_b16_8 t342 = GM_LD(gm_b16_8, ((uint8_t *)&F.l30));
                                gm_b16_8 t343 = GM_LD(gm_b16_8, ((uint8_t *)&t342));
                                gm_t88rcucg1b7uk t344 = gm_f_tuple_l3i6trt((void *)NULL, cx, NULL, t341, t343);
                                if (gm_unw.kind) {
                                    goto gm_ld197_1;
                                }
                                gm_b16_8 t345 = GM_LD(gm_b16_8, ((uint8_t *)&F.l29));
                                void * t346 = gm_l2;
                                gm_tl874t2atnlh3 t347 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t346) + 72));
                                gm_t2b6tm8c0vgo1 t348;
                                memset(&t348, 0, sizeof t348);
                                GM_ST(uint8_t, ((uint8_t *)&t348 + 0), ((uint8_t)35ULL));
                                GM_ST(gm_b16_8, ((uint8_t *)&t348 + 8), t345);
                                void * t349 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t344, &t348, t347);
                                if (gm_unw.kind) {
                                    goto gm_ld197_1;
                                }
                                gm_l74 = t349;
                            }
                        }
                        {
                            void * t350 = (void *)((uint8_t *)&F.l20);
                            gm_f_deinit_pal9hnf(t350);
                        }
                        goto gm_ls351;
gm_ld197_1:;
                        {
                            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                            void * t352 = (void *)((uint8_t *)&F.l20);
                            gm_f_deinit_pal9hnf(t352);
                            gm_unw = gm_saved;
                        }
                        goto gm_ll191;
gm_ls351:;
gm_lb17:;
                    }
                    {
                        void * t353 = gm_l74;
                        gm_l73 = t353;
                        goto gm_lb15;
                    }
                }
                goto gm_ls354;
gm_ll191:;
                if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741839) { gm_unw.kind = GM_NONE; goto gm_lb16; }
                goto gm_ll190;
gm_ls354:;
gm_lb16:;
            }
            {
                void * t355 = gm_l0;
                void * t356 = gm_l2;
                gm_tkn42u7na3t81 t357;
                memset(&t357, 0, sizeof t357);
                void * t358 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t355, t356, t357);
                if (gm_unw.kind) {
                    goto gm_ll190;
                }
                gm_l73 = t358;
            }
        }
        goto gm_ls359;
gm_ll190:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741838) { gm_unw.kind = GM_NONE; goto gm_lb15; }
        goto gm_lad;
gm_ls359:;
gm_lb15:;
    }
    {
        void * t360 = gm_l73;
        gm_l37 = t360;
        void * t361 = gm_l0;
        void * t362 = (void *)(((uint8_t *)t361) + 96);
        void * t363 = gm_l0;
        void * t364 = GM_LD(void *, ((uint8_t *)t363));
        void * t365 = gm_l37;
        gm_t88rcucg1b7uk t366 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t365));
        gm_t88rcucg1b7uk t367 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t362, t364, t366);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l38 = t367;
        {
            {
                void * t368 = gm_l0;
                void * t369 = GM_LD(void *, ((uint8_t *)t368));
                gm_t88rcucg1b7uk t370 = gm_l38;
                gm_thetuhtouve7f t371 = gm_f_range_of_dmmo768((void *)NULL, cx, NULL, t369, t370);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l88 = t371;
                uint8_t t372 = (uint8_t)((((uint8_t *)&gm_l88))[8] != 0);
                if (!t372) {
                    goto gm_lb46;
                }
                gm_tbngattl1fo51 t373 = GM_LD(gm_tbngattl1fo51, ((uint8_t *)&gm_l88));
                gm_l39 = t373;
                void * t374 = gm_l0;
                void * t375 = gm_l6;
                gm_t88rcucg1b7uk t376 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t375));
                gm_t88rcucg1b7uk t377 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l39) + 4));
                gm_tl874t2atnlh3 t378 = gm_l4;
                gm_b16_8 t379;
                GM_ST(void *, (uint8_t *)&t379, (void *)(gm_sg8s3e7n1hsck));
                GM_ST(uintptr_t, (uint8_t *)&t379 + 8, (uintptr_t)(33));
                gm_t88rcucg1b7uk t380 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t374, t376, t377, t378, t379);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t381 = gm_l0;
                gm_t88rcucg1b7uk t382 = gm_l38;
                gm_tl874t2atnlh3 t383 = gm_l4;
                uint32_t t384 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t381, t382, t383);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l40 = t384;
                gm_trqfvmjtt4sij t385;
                gm_f_list_ir0h3tc_2((void *)&t385, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l41 = t385;
                void * t386 = (void *)((uint8_t *)&F.l41);
                uint32_t t387 = gm_l40;
                void * t388 = gm_l37;
                gm_ttggq7ng4kp3p t389;
                GM_ST(uint32_t, (uint8_t *)&t389 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t389 + 0), t387);
                GM_ST(void *, ((uint8_t *)&t389 + 8), t388);
                gm_trcmmd12cefb1 t390;
                GM_ST(uint8_t, (uint8_t *)&t390 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t390 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t390 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t390 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t390 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t390 + 8), t389);
                gm_tftb1odc739t5 t391 = gm_f_push_d6c76ud_2(t386, &t390);
                gm_l89 = t391;
                uint8_t t392 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                if (!t392) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l89) + 1, gm_srclfnv05vuf0b8, 66, 9);
                gm_t88rcucg1b7uk t393 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l39) + 4));
                gm_t88rcucg1b7uk t394 = gm_l38;
                uint32_t t395 = gm_l40;
                gm_tl874t2atnlh3 t396 = gm_l4;
                gm_t2b6tm8c0vgo1 t397;
                memset(&t397, 0, sizeof t397);
                GM_ST(uint8_t, ((uint8_t *)&t397 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t397 + 8), t395);
                void * t398 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t394, &t397, t396);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbl8il92l97ut t399;
                GM_ST(uint32_t, (uint8_t *)&t399 + 12, 0);
                GM_ST(void *, ((uint8_t *)&t399 + 0), t398);
                GM_ST(uint32_t, ((uint8_t *)&t399 + 8), ((uint32_t)0ULL));
                gm_tl874t2atnlh3 t400 = gm_l4;
                gm_t2b6tm8c0vgo1 t401;
                memset(&t401, 0, sizeof t401);
                GM_ST(uint8_t, ((uint8_t *)&t401 + 0), ((uint8_t)19ULL));
                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t401 + 8), t399);
                void * t402 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t393, &t401, t400);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l42 = t402;
                gm_t88rcucg1b7uk t403 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l39) + 4));
                gm_t88rcucg1b7uk t404 = gm_l38;
                uint32_t t405 = gm_l40;
                gm_tl874t2atnlh3 t406 = gm_l4;
                gm_t2b6tm8c0vgo1 t407;
                memset(&t407, 0, sizeof t407);
                GM_ST(uint8_t, ((uint8_t *)&t407 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t407 + 8), t405);
                void * t408 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t404, &t407, t406);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbl8il92l97ut t409;
                GM_ST(uint32_t, (uint8_t *)&t409 + 12, 0);
                GM_ST(void *, ((uint8_t *)&t409 + 0), t408);
                GM_ST(uint32_t, ((uint8_t *)&t409 + 8), ((uint32_t)1ULL));
                gm_tl874t2atnlh3 t410 = gm_l4;
                gm_t2b6tm8c0vgo1 t411;
                memset(&t411, 0, sizeof t411);
                GM_ST(uint8_t, ((uint8_t *)&t411 + 0), ((uint8_t)19ULL));
                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t411 + 8), t409);
                void * t412 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t403, &t411, t410);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l43 = t412;
                void * t413 = gm_l0;
                void * t414 = gm_l6;
                gm_b16_8 t415 = GM_LD(gm_b16_8, ((uint8_t *)&F.l41));
                gm_b16_8 t416 = GM_LD(gm_b16_8, ((uint8_t *)&t415));
                void * t417 = gm_l42;
                void * t418 = gm_l43;
                gm_t88rcucg1b7uk t419 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l39) + 4));
                uint8_t t420 = GM_LD(uint8_t, ((uint8_t *)&gm_l39));
                uint8_t t421 = gm_l3;
                gm_tl874t2atnlh3 t422 = gm_l4;
                void * t423 = gm_f_range_test_tqcppmu((void *)NULL, cx, NULL, t413, t414, t416, t417, t418, t419, t420, t421, t422);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t423;
                goto gm_fret;
            }
gm_lb46:;
        }
gm_lb45:;
    }
    {
        void * t424 = gm_l6;
        gm_t88rcucg1b7uk t425 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t424));
        gm_l44 = t425;
        void * t426 = gm_l5;
        gm_t88rcucg1b7uk t427 = gm_l38;
        gm_f_kind_jcnt4hm((void *)&gm_l90, t426, GM_CONV(gm_csguankko4lmhd, t427));
        {
            {
                uint8_t t428 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                uint8_t t429 = (uint8_t)(t428 == ((uint8_t)11ULL));
                if (!t429) {
                    goto gm_lb48;
                }
                gm_t88rcucg1b7uk t430 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l90) + 8) + 8));
                gm_l45 = t430;
                gm_t88rcucg1b7uk t431 = gm_l45;
                gm_l44 = t431;
                goto gm_lb47;
            }
gm_lb48:;
            {
                uint8_t t432 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                uint8_t t433 = (uint8_t)(t432 == ((uint8_t)20ULL));
                if (!t433) {
                    goto gm_lb49;
                }
                gm_t88rcucg1b7uk t434 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l90) + 8) + 4));
                gm_l46 = t434;
                gm_t88rcucg1b7uk t435 = gm_l46;
                gm_l44 = t435;
                goto gm_lb47;
            }
gm_lb49:;
            {
                uint8_t t436 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                uint8_t t437 = (uint8_t)(t436 == ((uint8_t)10ULL));
                if (!t437) {
                    goto gm_lb50;
                }
                gm_t88rcucg1b7uk t438 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l90) + 8) + 4));
                gm_l47 = t438;
                gm_t88rcucg1b7uk t439 = gm_l47;
                gm_l44 = t439;
                goto gm_lb47;
            }
gm_lb50:;
            {
                uint8_t t440 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                uint8_t t441 = (uint8_t)(t440 == ((uint8_t)12ULL));
                if (!t441) {
                    goto gm_lb51;
                }
                gm_b16_8 t442 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l90) + 8));
                gm_l48 = t442;
                uint64_t t443 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                uint8_t t444 = (t443 > ((uint64_t)0ULL));
                if (t444) {
                    void * t445 = GM_LD(void *, ((uint8_t *)&gm_l48));
                    uint64_t t446 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                    gm_t88rcucg1b7uk t447 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t445) + (uintptr_t)((uint64_t)0ULL) * 4));
                    gm_l44 = t447;
                }
                goto gm_lb47;
            }
gm_lb51:;
            {
                uint8_t t448 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                uint8_t t449 = (uint8_t)(t448 == ((uint8_t)26ULL));
                if (!t449) {
                    goto gm_lb52;
                }
                void * t450 = gm_l0;
                void * t451 = GM_LD(void *, ((uint8_t *)t450));
                gm_tl874t2atnlh3 t452 = gm_l4;
                void * t453 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t451, t452);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t453;
                goto gm_fret;
            }
gm_lb52:;
            {
                void * t454 = gm_l0;
                void * t455 = gm_l2;
                gm_tl874t2atnlh3 t456 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t455) + 72));
                void * t457 = gm_l0;
                gm_t88rcucg1b7uk t458 = gm_l38;
                gm_b16_8 t459 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t457, t458);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t460;
                GM_ST(gm_b16_8, ((uint8_t *)&t460 + 0), t459);
                gm_f_format_vc9g56o((void *)&gm_l91, (void *)NULL, cx, NULL, t460);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t461 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                if (!t461) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 8, gm_srclfnv05vuf0b8, 79, 38);
                gm_b16_8 t462 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l91) + 8));
                gm_b16_8 t463 = GM_LD(gm_b16_8, ((uint8_t *)&t462));
                gm_b0_1 t464 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t454, t456, t463);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t465 = gm_l0;
                void * t466 = GM_LD(void *, ((uint8_t *)t465));
                gm_tl874t2atnlh3 t467 = gm_l4;
                void * t468 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t466, t467);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t468;
                goto gm_fret;
            }
gm_lb53:;
            gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 72, 5);
        }
gm_lb47:;
    }
    {
        {
            {
                void * t469 = gm_l5;
                gm_t88rcucg1b7uk t470 = gm_l38;
                gm_f_kind_jcnt4hm((void *)&gm_l92, t469, GM_CONV(gm_csguankko4lmhd, t470));
                uint8_t t471 = GM_LD(uint8_t, (((uint8_t *)&gm_l92) + 0));
                uint8_t t472 = (uint8_t)(t471 == ((uint8_t)12ULL));
                if (!t472) {
                    goto gm_lb55;
                }
                gm_b16_8 t473 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l92) + 8));
                gm_l49 = t473;
                uint64_t t474 = GM_LD(uint64_t, (((uint8_t *)&gm_l49) + 8));
                uint8_t t475 = (t474 > ((uint64_t)0ULL));
                if (!t475) {
                    goto gm_lb55;
                }
                uint8_t t476 = gm_l17;
                uint8_t t477 = (uint8_t)!t476;
                if (!t477) {
                    goto gm_lb55;
                }
                void * t478 = gm_l0;
                gm_b16_8 t479 = gm_l49;
                gm_tkn42u7na3t81 t480 = gm_f_tuple_elem_join_om8llaj((void *)NULL, cx, NULL, t478, t479);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l50 = t480;
                gm_l52 = ((uint64_t)1ULL);
                uint64_t t481 = GM_LD(uint64_t, (((uint8_t *)&gm_l49) + 8));
                gm_l51 = t481;
                {
                    for (;;) {
                        uint64_t t482 = gm_l52;
                        uint64_t t483 = gm_l51;
                        uint8_t t484 = (t482 < t483);
                        if (!t484) {
                            goto gm_lb56;
                        }
                        {
                            {
                                {
                                    uint64_t t485 = gm_l52;
                                    gm_l53 = t485;
                                    {
                                        {
                                            {
                                                {
                                                    uint8_t t486 = (uint8_t)((((uint8_t *)&gm_l50))[4] != 0);
                                                    if (t486) {
                                                        goto gm_lb64;
                                                    }
                                                    gm_l93 = ((uint8_t)1);
                                                    goto gm_lb63;
                                                }
gm_lb64:;
                                                {
                                                    gm_l93 = ((uint8_t)0);
                                                    goto gm_lb63;
                                                }
gm_lb65:;
                                                gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 89, 16);
                                            }
gm_lb63:;
                                            uint8_t t487 = gm_l93;
                                            if (!t487) {
                                                goto gm_lb62;
                                            }
                                            void * t488 = gm_l0;
                                            void * t489 = (void *)(((uint8_t *)t488) + 96);
                                            void * t490 = gm_l5;
                                            gm_t88rcucg1b7uk t491 = gm_l44;
                                            void * t492 = GM_LD(void *, ((uint8_t *)&gm_l49));
                                            uint64_t t493 = GM_LD(uint64_t, (((uint8_t *)&gm_l49) + 8));
                                            uint64_t t494 = gm_l53;
                                            gm_t88rcucg1b7uk t495 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t492) + (uintptr_t)t494 * 4));
                                            gm_t9qkh0rsooc8d t496 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t489, t490, t491, t495);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l95 = t496;
                                            {
                                                {
                                                    uint8_t t497 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
                                                    if (t497) {
                                                        goto gm_lb67;
                                                    }
                                                    gm_l94 = ((uint8_t)1);
                                                    goto gm_lb66;
                                                }
gm_lb67:;
                                                {
                                                    gm_l94 = ((uint8_t)0);
                                                    goto gm_lb66;
                                                }
gm_lb68:;
                                                gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 89, 35);
                                            }
gm_lb66:;
                                            uint8_t t498 = gm_l94;
                                            if (!t498) {
                                                goto gm_lb62;
                                            }
                                            void * t499 = gm_l0;
                                            void * t500 = (void *)(((uint8_t *)t499) + 96);
                                            void * t501 = gm_l0;
                                            void * t502 = GM_LD(void *, ((uint8_t *)t501));
                                            gm_t88rcucg1b7uk t503 = gm_l44;
                                            gm_t88rcucg1b7uk t504 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t500, t502, t503);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l54 = t504;
                                            void * t505 = gm_l0;
                                            void * t506 = (void *)(((uint8_t *)t505) + 96);
                                            void * t507 = gm_l0;
                                            void * t508 = GM_LD(void *, ((uint8_t *)t507));
                                            void * t509 = GM_LD(void *, ((uint8_t *)&gm_l49));
                                            uint64_t t510 = GM_LD(uint64_t, (((uint8_t *)&gm_l49) + 8));
                                            uint64_t t511 = gm_l53;
                                            gm_t88rcucg1b7uk t512 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t509) + (uintptr_t)t511 * 4));
                                            gm_t88rcucg1b7uk t513 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t506, t508, t512);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l55 = t513;
                                            void * t514 = gm_l0;
                                            void * t515 = gm_l2;
                                            gm_tl874t2atnlh3 t516 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t515) + 72));
                                            void * t517 = gm_l0;
                                            gm_t88rcucg1b7uk t518 = gm_l54;
                                            gm_b16_8 t519 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t517, t518);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            void * t520 = gm_l0;
                                            gm_t88rcucg1b7uk t521 = gm_l55;
                                            gm_b16_8 t522 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t520, t521);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_t6j6smj7qoctu t523;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t523 + 0), t519);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t523 + 16), t522);
                                            gm_f_format_ftgbk9j((void *)&gm_l96, (void *)NULL, cx, NULL, &t523);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t524 = (uint8_t)!((((uint8_t *)&gm_l96))[0] != 0);
                                            if (!t524) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l96) + 8, gm_srclfnv05vuf0b8, 92, 42);
                                            gm_b16_8 t525 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l96) + 8));
                                            gm_b16_8 t526 = GM_LD(gm_b16_8, ((uint8_t *)&t525));
                                            gm_b0_1 t527 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t514, t516, t526);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            void * t528 = gm_l0;
                                            void * t529 = GM_LD(void *, ((uint8_t *)t528));
                                            gm_tl874t2atnlh3 t530 = gm_l4;
                                            void * t531 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t529, t530);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.ret = t531;
                                            goto gm_fret;
                                        }
gm_lb62:;
                                    }
gm_lb61:;
                                    goto gm_lb59;
                                }
gm_lb60:;
                                gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 88, 9);
                            }
gm_lb59:;
                        }
gm_lb58:;
                        uint64_t t532 = gm_l52;
                        uint64_t r533; bool o534 = __builtin_add_overflow(t532, ((uint64_t)1ULL), &r533);
                        uint64_t t535 = r533;
                        gm_l52 = t535;
                    }
                }
gm_lb56:;
                {
                    {
                        uint8_t t536 = (uint8_t)((((uint8_t *)&gm_l50))[4] != 0);
                        if (!t536) {
                            goto gm_lb70;
                        }
                        gm_t88rcucg1b7uk t537 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l50));
                        gm_l56 = t537;
                        gm_t88rcucg1b7uk t538 = gm_l56;
                        gm_l44 = t538;
                        void * t539 = gm_l0;
                        gm_t88rcucg1b7uk t540 = gm_l38;
                        gm_tl874t2atnlh3 t541 = gm_l4;
                        uint32_t t542 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t539, t540, t541);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l57 = t542;
                        gm_trqfvmjtt4sij t543;
                        gm_f_list_vp1djv1_2((void *)&t543, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l58 = t543;
                        gm_trqfvmjtt4sij t544;
                        gm_f_list_nof7vfj((void *)&t544, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l59 = t544;
                        gm_l61 = ((uint64_t)0ULL);
                        uint64_t t545 = GM_LD(uint64_t, (((uint8_t *)&gm_l49) + 8));
                        gm_l60 = t545;
                        {
                            for (;;) {
                                uint64_t t546 = gm_l61;
                                uint64_t t547 = gm_l60;
                                uint8_t t548 = (t546 < t547);
                                if (!t548) {
                                    goto gm_lb71;
                                }
                                {
                                    {
                                        {
                                            uint64_t t549 = gm_l61;
                                            gm_l62 = t549;
                                            void * t550 = GM_LD(void *, ((uint8_t *)&gm_l49));
                                            uint64_t t551 = GM_LD(uint64_t, (((uint8_t *)&gm_l49) + 8));
                                            uint64_t t552 = gm_l62;
                                            gm_t88rcucg1b7uk t553 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t550) + (uintptr_t)t552 * 4));
                                            gm_t88rcucg1b7uk t554 = gm_l38;
                                            uint32_t t555 = gm_l57;
                                            gm_tl874t2atnlh3 t556 = gm_l4;
                                            gm_t2b6tm8c0vgo1 t557;
                                            memset(&t557, 0, sizeof t557);
                                            GM_ST(uint8_t, ((uint8_t *)&t557 + 0), ((uint8_t)15ULL));
                                            GM_ST(uint32_t, ((uint8_t *)&t557 + 8), t555);
                                            void * t558 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t554, &t557, t556);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint64_t t559 = gm_l62;
                                            uint32_t t560 = (uint32_t)t559;
                                            gm_tbl8il92l97ut t561;
                                            GM_ST(uint32_t, (uint8_t *)&t561 + 12, 0);
                                            GM_ST(void *, ((uint8_t *)&t561 + 0), t558);
                                            GM_ST(uint32_t, ((uint8_t *)&t561 + 8), t560);
                                            gm_tl874t2atnlh3 t562 = gm_l4;
                                            gm_t2b6tm8c0vgo1 t563;
                                            memset(&t563, 0, sizeof t563);
                                            GM_ST(uint8_t, ((uint8_t *)&t563 + 0), ((uint8_t)19ULL));
                                            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t563 + 8), t561);
                                            void * t564 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t553, &t563, t562);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l63 = t564;
                                            void * t565 = (void *)((uint8_t *)&F.l58);
                                            void * t566 = gm_l0;
                                            void * t567 = gm_l63;
                                            gm_t88rcucg1b7uk t568 = gm_l56;
                                            void * t569 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t566, t567, t568);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_tftb1odc739t5 t570 = gm_f_push_d943vgp_2(t565, &(*(gm_tcgrjq9toscke *)((uint8_t *)t569)));
                                            gm_l97 = t570;
                                            uint8_t t571 = (uint8_t)!((((uint8_t *)&gm_l97))[0] != 0);
                                            if (!t571) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l97) + 1, gm_srclfnv05vuf0b8, 103, 17);
                                            void * t572 = (void *)((uint8_t *)&F.l59);
                                            gm_t88rcucg1b7uk t573 = gm_l56;
                                            gm_tftb1odc739t5 t574 = gm_f_push_k3l9nic(t572, GM_CONV(gm_csguankko4lmhd, t573));
                                            gm_l98 = t574;
                                            uint8_t t575 = (uint8_t)!((((uint8_t *)&gm_l98))[0] != 0);
                                            if (!t575) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l98) + 1, gm_srclfnv05vuf0b8, 104, 17);
                                            goto gm_lb74;
                                        }
gm_lb75:;
                                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 101, 13);
                                    }
gm_lb74:;
                                }
gm_lb73:;
                                uint64_t t576 = gm_l61;
                                uint64_t r577; bool o578 = __builtin_add_overflow(t576, ((uint64_t)1ULL), &r577);
                                uint64_t t579 = r577;
                                gm_l61 = t579;
                            }
                        }
gm_lb71:;
                        void * t580 = gm_l5;
                        gm_b16_8 t581 = GM_LD(gm_b16_8, ((uint8_t *)&F.l59));
                        gm_b16_8 t582 = GM_LD(gm_b16_8, ((uint8_t *)&t581));
                        gm_t88rcucg1b7uk t583 = gm_f_tuple_l3i6trt((void *)NULL, cx, NULL, t580, t582);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l64 = t583;
                        gm_trqfvmjtt4sij t584;
                        gm_f_list_ir0h3tc_2((void *)&t584, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l65 = t584;
                        void * t585 = (void *)((uint8_t *)&F.l65);
                        uint32_t t586 = gm_l57;
                        void * t587 = gm_l37;
                        gm_ttggq7ng4kp3p t588;
                        GM_ST(uint32_t, (uint8_t *)&t588 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t588 + 0), t586);
                        GM_ST(void *, ((uint8_t *)&t588 + 8), t587);
                        gm_trcmmd12cefb1 t589;
                        GM_ST(uint8_t, (uint8_t *)&t589 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t589 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t589 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t589 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t589 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t589 + 8), t588);
                        gm_tftb1odc739t5 t590 = gm_f_push_d6c76ud_2(t585, &t589);
                        gm_l99 = t590;
                        uint8_t t591 = (uint8_t)!((((uint8_t *)&gm_l99))[0] != 0);
                        if (!t591) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l99) + 1, gm_srclfnv05vuf0b8, 108, 13);
                        gm_t88rcucg1b7uk t592 = gm_l64;
                        gm_b16_8 t593 = GM_LD(gm_b16_8, ((uint8_t *)&F.l58));
                        gm_tl874t2atnlh3 t594 = gm_l4;
                        gm_t2b6tm8c0vgo1 t595;
                        memset(&t595, 0, sizeof t595);
                        GM_ST(uint8_t, ((uint8_t *)&t595 + 0), ((uint8_t)35ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t595 + 8), t593);
                        void * t596 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t592, &t595, t594);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l66 = t596;
                        gm_t88rcucg1b7uk t597 = gm_l64;
                        gm_b16_8 t598 = GM_LD(gm_b16_8, ((uint8_t *)&F.l65));
                        void * t599 = gm_l66;
                        gm_tl874t2atnlh3 t600 = gm_l4;
                        void * t601 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t597, t598, t599, t600);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l37 = t601;
                        goto gm_lb69;
                    }
gm_lb70:;
                }
gm_lb69:;
                goto gm_lb54;
            }
gm_lb55:;
        }
gm_lb54:;
    }
    {
        void * t602 = gm_l0;
        void * t603 = (void *)(((uint8_t *)t602) + 96);
        void * t604 = gm_l0;
        void * t605 = GM_LD(void *, ((uint8_t *)t604));
        void * t606 = gm_l6;
        gm_t88rcucg1b7uk t607 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t606));
        gm_t88rcucg1b7uk t608 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t603, t605, t607);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l67 = t608;
        void * t609 = gm_l0;
        void * t610 = (void *)(((uint8_t *)t609) + 96);
        void * t611 = gm_l0;
        void * t612 = GM_LD(void *, ((uint8_t *)t611));
        gm_t88rcucg1b7uk t613 = gm_l44;
        gm_t88rcucg1b7uk t614 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t610, t612, t613);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l68 = t614;
        {
            {
                gm_t88rcucg1b7uk t615 = gm_l67;
                gm_t88rcucg1b7uk t616 = gm_l68;
                uint8_t t617 = (!gm_eq9m6mvtmgde(((uint8_t *)&t615), ((uint8_t *)&t616)));
                if (!t617) {
                    goto gm_lb77;
                }
                void * t618 = gm_l5;
                gm_t88rcucg1b7uk t619 = gm_l67;
                uint8_t t620 = gm_f_has_infer_3o82a0b(t618, GM_CONV(gm_csguankko4lmhd, t619));
                uint8_t t621 = (uint8_t)!t620;
                if (!t621) {
                    goto gm_lb77;
                }
                void * t622 = gm_l5;
                gm_t88rcucg1b7uk t623 = gm_l68;
                uint8_t t624 = gm_f_has_infer_3o82a0b(t622, GM_CONV(gm_csguankko4lmhd, t623));
                uint8_t t625 = (uint8_t)!t624;
                if (!t625) {
                    goto gm_lb77;
                }
                void * t626 = gm_l0;
                gm_t88rcucg1b7uk t627 = gm_l67;
                gm_t88rcucg1b7uk t628 = gm_l68;
                gm_f_coercion_steps_ibqvhta((void *)&gm_l101, (void *)NULL, cx, NULL, t626, t627, t628);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                {
                    {
                        uint8_t t629 = (uint8_t)((((uint8_t *)&gm_l101))[16] != 0);
                        if (t629) {
                            goto gm_lb79;
                        }
                        gm_l100 = ((uint8_t)0);
                        goto gm_lb78;
                    }
gm_lb79:;
                    {
                        gm_l100 = ((uint8_t)1);
                        goto gm_lb78;
                    }
gm_lb80:;
                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 117, 77);
                }
gm_lb78:;
                uint8_t t630 = gm_l100;
                if (!t630) {
                    goto gm_lb77;
                }
                void * t631 = gm_l0;
                void * t632 = gm_l6;
                gm_t88rcucg1b7uk t633 = gm_l68;
                void * t634 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t631, t632, t633);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t634;
                goto gm_lb76;
            }
gm_lb77:;
        }
gm_lb76:;
        void * t635 = gm_l0;
        void * t636 = gm_l6;
        gm_t88rcucg1b7uk t637 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t636));
        gm_t88rcucg1b7uk t638 = gm_l44;
        gm_tl874t2atnlh3 t639 = gm_l4;
        gm_b16_8 t640;
        GM_ST(void *, (uint8_t *)&t640, (void *)(gm_sa4nk3mrdf2d0));
        GM_ST(uintptr_t, (uint8_t *)&t640 + 8, (uintptr_t)(35));
        gm_t88rcucg1b7uk t641 = gm_f_unify_operands_5sp7j1j((void *)NULL, cx, NULL, t635, t637, t638, t639, t640);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t642 = gm_l0;
        void * t643 = (void *)(((uint8_t *)t642) + 96);
        void * t644 = gm_l0;
        void * t645 = GM_LD(void *, ((uint8_t *)t644));
        gm_t88rcucg1b7uk t646 = gm_l44;
        gm_t88rcucg1b7uk t647 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t643, t645, t646);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l69 = t647;
    }
    {
        {
            {
                void * t648 = gm_l5;
                gm_t88rcucg1b7uk t649 = gm_l69;
                uint8_t t650 = gm_f_has_infer_3o82a0b(t648, GM_CONV(gm_csguankko4lmhd, t649));
                uint8_t t651 = (uint8_t)!t650;
                if (!t651) {
                    goto gm_lb82;
                }
                void * t652 = gm_l5;
                gm_t88rcucg1b7uk t653 = gm_l69;
                uint8_t t654 = gm_f_is_error_8k27gd1(t652, GM_CONV(gm_csguankko4lmhd, t653));
                uint8_t t655 = (uint8_t)!t654;
                if (!t655) {
                    goto gm_lb82;
                }
                void * t656 = gm_l0;
                gm_t88rcucg1b7uk t657 = gm_l69;
                uint8_t t658 = gm_f_eq_allowed_d841u9b((void *)NULL, cx, NULL, t656, t657);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t659 = (uint8_t)!t658;
                if (!t659) {
                    goto gm_lb82;
                }
                void * t660 = gm_l0;
                gm_tl874t2atnlh3 t661 = gm_l4;
                void * t662 = gm_l0;
                gm_t88rcucg1b7uk t663 = gm_l69;
                gm_b16_8 t664 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t662, t663);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t665;
                GM_ST(gm_b16_8, ((uint8_t *)&t665 + 0), t664);
                gm_f_format_2bpp75h((void *)&gm_l102, (void *)NULL, cx, NULL, t665);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t666 = (uint8_t)!((((uint8_t *)&gm_l102))[0] != 0);
                if (!t666) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l102) + 8, gm_srclfnv05vuf0b8, 124, 24);
                gm_b16_8 t667 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l102) + 8));
                gm_b16_8 t668 = GM_LD(gm_b16_8, ((uint8_t *)&t667));
                gm_b0_1 t669 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t660, t661, t668);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb81;
            }
gm_lb82:;
        }
gm_lb81:;
        void * t670 = gm_l5;
        gm_t88rcucg1b7uk t671 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t670);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t672 = gm_l6;
        void * t673 = gm_l37;
        uint8_t t674 = gm_l3;
        gm_tlcl4u558l6p5 t675;
        GM_ST(uint8_t, (uint8_t *)&t675 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t675 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t675 + 20, 0);
        GM_ST(void *, ((uint8_t *)&t675 + 0), t672);
        GM_ST(void *, ((uint8_t *)&t675 + 8), t673);
        GM_ST(uint8_t, ((uint8_t *)&t675 + 16), t674);
        gm_tl874t2atnlh3 t676 = gm_l4;
        gm_t2b6tm8c0vgo1 t677;
        memset(&t677, 0, sizeof t677);
        GM_ST(uint8_t, ((uint8_t *)&t677 + 0), ((uint8_t)45ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t677 + 8), t675);
        void * t678 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t671, &t677, t676);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t678;
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
inline __attribute__((aligned(4))) void gm_f_increment_12n73t5(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_increment_12n73t5 F;
    uint32_t gm_l0; /* i */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* cur */
    void * gm_l4; /* next */
    GM_STACK_CHECK(&F, 535u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t88rcucg1b7uk t2 = gm_l1;
    uint32_t t3 = gm_l0;
    gm_tl874t2atnlh3 t4 = gm_l2;
    gm_t2b6tm8c0vgo1 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t5 + 8), t3);
    void * t6 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t2, &t5, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t6;
    gm_t88rcucg1b7uk t7 = gm_l1;
    gm_t9qkh0rsooc8d t8;
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)1ULL));
    gm_t88rcucg1b7uk t9 = gm_l1;
    uint32_t t10 = gm_l0;
    gm_tl874t2atnlh3 t11 = gm_l2;
    gm_ts6oqppjh0ba3 t12;
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 1), t8);
    gm_t2b6tm8c0vgo1 t13;
    memset(&t13, 0, sizeof t13);
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t13 + 8), t10);
    void * t14 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t9, &t13, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t15 = gm_l1;
    gm_tbci5ospk5nnn t16;
    gm_f_one_08qg1op((void *)&t16, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tl874t2atnlh3 t17 = gm_l2;
    gm_t2b6tm8c0vgo1 t18;
    GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t18 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t18 + 8), t16);
    void * t19 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t15, &t18, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trl9d7m1k5lbd t20;
    GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t20 + 0), t12);
    GM_ST(void *, ((uint8_t *)&t20 + 8), t14);
    GM_ST(void *, ((uint8_t *)&t20 + 16), t19);
    gm_tl874t2atnlh3 t21 = gm_l2;
    gm_t2b6tm8c0vgo1 t22;
    memset(&t22, 0, sizeof t22);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)30ULL));
    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t22 + 8), t20);
    void * t23 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t7, &t22, t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t23;
    void * t24 = gm_l3;
    void * t25 = gm_l4;
    gm_t3mf58gv9kf3n t26;
    GM_ST(void *, ((uint8_t *)&t26 + 0), t24);
    GM_ST(void *, ((uint8_t *)&t26 + 8), t25);
    gm_trcmmd12cefb1 t27;
    GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t27 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t27 + 8), t26);
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_join_members_97aedtg(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_join_members_97aedtg F;
    void * gm_l0; /* cx */
    void * gm_l1; /* elem */
    gm_b16_8 gm_l2; /* members */
    void * gm_l3; /* tys */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    void * gm_l7; /* m */
    gm_t88rcucg1b7uk gm_l8; /* t */
    gm_t88rcucg1b7uk gm_l9; /* joined */
    gm_t88rcucg1b7uk gm_l10; /* jr */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t88rcucg1b7uk gm_l13; /* t */
    gm_t88rcucg1b7uk gm_l14; /* r */
    GM_STACK_CHECK(&F, 753u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l15;
    uint8_t gm_l16;
    gm_tftb1odc739t5 gm_l17;
    uint8_t gm_l18;
    gm_th2lqbo77pvsc gm_l19;
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((uint8_t *)t19));
        void * t21 = (void *)(((uint8_t *)t20) + 8);
        gm_l3 = t21;
        gm_trqfvmjtt4sij t22;
        gm_f_list_nof7vfj((void *)&t22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t22;
        void * t23 = (void *)((uint8_t *)&F.l4);
        void * t24 = gm_l1;
        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t24));
        gm_tftb1odc739t5 t26 = gm_f_push_k3l9nic(t23, GM_CONV(gm_csguankko4lmhd, t25));
        gm_l15 = t26;
        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srclfnv05vuf0b8, 136, 5);
        gm_b16_8 t28 = gm_l2;
        gm_l5 = t28;
        gm_l6 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t29 = gm_l6;
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint8_t t31 = (t29 < t30);
                if (!t31) {
                    goto gm_lb2;
                }
                {
                    void * t32 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint64_t t34 = gm_l6;
                    {
                        {
                            void * t35 = GM_LD(void *, (((uint8_t *)t32) + (uintptr_t)t34 * 8));
                            gm_l7 = t35;
                            void * t36 = gm_l0;
                            void * t37 = (void *)(((uint8_t *)t36) + 96);
                            void * t38 = gm_l0;
                            void * t39 = GM_LD(void *, ((uint8_t *)t38));
                            void * t40 = gm_l7;
                            gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t40));
                            gm_t88rcucg1b7uk t42 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t37, t39, t41);
                            if (gm_unw.kind) {
                                goto gm_ld18_1;
                            }
                            gm_l8 = t42;
                            void * t43 = gm_l3;
                            gm_t88rcucg1b7uk t44 = gm_l8;
                            uint8_t t45 = gm_f_has_infer_3o82a0b(t43, GM_CONV(gm_csguankko4lmhd, t44));
                            if (t45) {
                                gm_l16 = ((uint8_t)1);
                            } else {
                                void * t46 = gm_l3;
                                gm_t88rcucg1b7uk t47 = gm_l8;
                                uint8_t t48 = gm_f_is_error_8k27gd1(t46, GM_CONV(gm_csguankko4lmhd, t47));
                                gm_l16 = t48;
                            }
                            uint8_t t49 = gm_l16;
                            if (t49) {
                                void * t50 = gm_l1;
                                gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t50));
                                F.ret = t51;
                                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                                goto gm_ld18_1;
                            }
                            void * t52 = (void *)((uint8_t *)&F.l4);
                            gm_t88rcucg1b7uk t53 = gm_l8;
                            gm_tftb1odc739t5 t54 = gm_f_push_k3l9nic(t52, GM_CONV(gm_csguankko4lmhd, t53));
                            gm_l17 = t54;
                            uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                            if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srclfnv05vuf0b8, 140, 9);
                            goto gm_lb5;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 137, 5);
                    }
gm_lb5:;
                }
gm_lb4:;
                uint64_t t56 = gm_l6;
                uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                uint64_t t59 = r57;
                gm_l6 = t59;
            }
        }
gm_lb2:;
        void * t60 = gm_l3;
        void * t61 = gm_l0;
        void * t62 = (void *)(((uint8_t *)t61) + 96);
        void * t63 = gm_l0;
        void * t64 = GM_LD(void *, ((uint8_t *)t63));
        void * t65 = gm_l1;
        gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t65));
        gm_t88rcucg1b7uk t67 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t62, t64, t66);
        if (gm_unw.kind) {
            goto gm_ld18_1;
        }
        uint8_t t68 = gm_f_has_infer_3o82a0b(t60, GM_CONV(gm_csguankko4lmhd, t67));
        if (t68) {
            void * t69 = gm_l1;
            gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t69));
            F.ret = t70;
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld18_1;
        }
        void * t71 = gm_l0;
        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
        gm_t88rcucg1b7uk t74 = gm_f_join_type_cck70vp((void *)NULL, cx, NULL, t71, t73);
        if (gm_unw.kind) {
            goto gm_ld18_1;
        }
        gm_l9 = t74;
        void * t75 = gm_l0;
        void * t76 = (void *)(((uint8_t *)t75) + 96);
        void * t77 = gm_l0;
        void * t78 = GM_LD(void *, ((uint8_t *)t77));
        gm_t88rcucg1b7uk t79 = gm_l9;
        gm_t88rcucg1b7uk t80 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t76, t78, t79);
        if (gm_unw.kind) {
            goto gm_ld18_1;
        }
        gm_l10 = t80;
        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_l11 = t81;
        gm_l12 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t82 = gm_l12;
                uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t84 = (t82 < t83);
                if (!t84) {
                    goto gm_lb7;
                }
                {
                    void * t85 = GM_LD(void *, ((uint8_t *)&gm_l11));
                    uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint64_t t87 = gm_l12;
                    {
                        {
                            gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t85) + (uintptr_t)t87 * 4));
                            gm_l13 = t88;
                            void * t89 = gm_l0;
                            void * t90 = (void *)(((uint8_t *)t89) + 96);
                            void * t91 = gm_l0;
                            void * t92 = GM_LD(void *, ((uint8_t *)t91));
                            gm_t88rcucg1b7uk t93 = gm_l13;
                            gm_t88rcucg1b7uk t94 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t90, t92, t93);
                            if (gm_unw.kind) {
                                goto gm_ld18_1;
                            }
                            gm_l14 = t94;
                            {
                                {
                                    gm_t88rcucg1b7uk t95 = gm_l14;
                                    gm_t88rcucg1b7uk t96 = gm_l10;
                                    uint8_t t97 = (!gm_eq9m6mvtmgde(((uint8_t *)&t95), ((uint8_t *)&t96)));
                                    if (!t97) {
                                        goto gm_lb13;
                                    }
                                    void * t98 = gm_l0;
                                    gm_t88rcucg1b7uk t99 = gm_l14;
                                    gm_t88rcucg1b7uk t100 = gm_l10;
                                    gm_f_coercion_steps_ibqvhta((void *)&gm_l19, (void *)NULL, cx, NULL, t98, t99, t100);
                                    if (gm_unw.kind) {
                                        goto gm_ld18_1;
                                    }
                                    {
                                        {
                                            uint8_t t101 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
                                            if (t101) {
                                                goto gm_lb15;
                                            }
                                            gm_l18 = ((uint8_t)1);
                                            goto gm_lb14;
                                        }
gm_lb15:;
                                        {
                                            gm_l18 = ((uint8_t)0);
                                            goto gm_lb14;
                                        }
gm_lb16:;
                                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 148, 28);
                                    }
gm_lb14:;
                                    uint8_t t102 = gm_l18;
                                    if (!t102) {
                                        goto gm_lb13;
                                    }
                                    void * t103 = gm_l1;
                                    gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t103));
                                    F.ret = t104;
                                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                                    goto gm_ld18_1;
                                }
gm_lb13:;
                            }
gm_lb12:;
                            goto gm_lb10;
                        }
gm_lb11:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 146, 5);
                    }
gm_lb10:;
                }
gm_lb9:;
                uint64_t t105 = gm_l12;
                uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                uint64_t t108 = r106;
                gm_l12 = t108;
            }
        }
gm_lb7:;
        gm_t88rcucg1b7uk t109 = gm_l9;
        F.ret = t109;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld18_1;
    }
    {
        void * t110 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_q21h982(t110);
    }
    goto gm_ls111;
gm_ld18_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t112 = (void *)((uint8_t *)&F.l4);
        gm_f_deinit_q21h982(t112);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls111:;
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
extern __attribute__((aligned(4))) void * gm_f_local_i91hglv(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) void * gm_f_local_l5o3d47(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_loop_with_pattern_d8vl0mj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, void * a3, gm_b16_8 a4, void * a5) {
    struct gm_fr_loop_with_pattern_d8vl0mj F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    void * gm_l3; /* item */
    gm_b16_8 gm_l4; /* cond */
    void * gm_l5; /* step */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_t08olq97rscn3 gm_l7; /* l */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_tl874t2atnlh3 gm_l13; /* sp */
    void * gm_l15; /* pat */
    gm_th2lqbo77pvsc gm_l16; /* label_name */
    gm_t08olq97rscn3 gm_l17; /* l */
    uint32_t gm_l18; /* id */
    void * gm_l19; /* body */
    void * gm_l21; /* m */
    void * gm_l23; /* lb */
    GM_STACK_CHECK(&F, 2302u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tq7s98ihonkju gm_l27;
    void * t13 = gm_l1;
    gm_tl874t2atnlh3 t14 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t13) + 104));
    gm_l6 = t14;
    void * t15 = gm_l0;
    gm_b0_1 t16 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            void * t17 = gm_l1;
            uint8_t t18 = (uint8_t)((((uint8_t *)t17))[32] != 0);
            if (!t18) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l7), ((uint8_t *)t17), 32);
            void * t19 = gm_l0;
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
            gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 16));
            gm_t930q5l38hdap t22;
            memset(&t22, 0, sizeof t22);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)2ULL));
            gm_b0_1 t23 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t19, t20, &t22, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t24 = gm_l1;
    void * t25 = GM_LD(void *, (((uint8_t *)t24) + 40));
    uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t24) + 40) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t26, gm_srclfnv05vuf0b8, 278, 16);
    memmove(((uint8_t *)&F.l8), (((uint8_t *)t25) + (uintptr_t)((uint64_t)0ULL) * 64), 64);
    void * t27 = gm_l1;
    uint64_t t28 = GM_LD(uint64_t, ((((uint8_t *)t27) + 40) + 8));
    uint8_t t29 = (!(t28 == ((uint64_t)1ULL)));
    if (t29) {
        gm_trqfvmjtt4sij t30;
        gm_f_list_ttksl9g((void *)&t30, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l9 = t30;
        void * t31 = gm_l1;
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)t31) + 40));
        gm_l10 = t32;
        gm_l11 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t33 = gm_l11;
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint8_t t35 = (t33 < t34);
                if (!t35) {
                    goto gm_lb3;
                }
                {
                    void * t36 = GM_LD(void *, ((uint8_t *)&gm_l10));
                    uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint64_t t38 = gm_l11;
                    {
                        {
                            memmove(((uint8_t *)&F.l12), (((uint8_t *)t36) + (uintptr_t)t38 * 64), 64);
                            void * t39 = (void *)((uint8_t *)&F.l9);
                            gm_t1s75i08qtptd t40;
                            memset(&t40, 0, sizeof t40);
                            gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 48));
                            gm_t0d02142k4tu8 t42 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)&F.l12));
                            gm_tnrgict8sthcv t43;
                            GM_ST(uint32_t, (uint8_t *)&t43 + 116, 0);
                            GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&t43 + 0), t40);
                            GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t43 + 40), t42);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t43 + 104), t41);
                            gm_tftb1odc739t5 t44 = gm_f_push_l74t9rq(t39, &t43);
                            gm_l24 = t44;
                            uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                            if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srclfnv05vuf0b8, 281, 27);
                            goto gm_lb6;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 281, 9);
                    }
gm_lb6:;
                }
gm_lb5:;
                uint64_t t46 = gm_l11;
                uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                uint64_t t49 = r47;
                gm_l11 = t49;
            }
        }
gm_lb3:;
        void * t50 = gm_l1;
        void * t51 = GM_LD(void *, (((uint8_t *)t50) + 40));
        uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)t50) + 40) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t52, gm_srclfnv05vuf0b8, 282, 25);
        gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t51) + (uintptr_t)((uint64_t)0ULL) * 64) + 48));
        void * t54 = gm_l1;
        void * t55 = GM_LD(void *, (((uint8_t *)t54) + 40));
        uint64_t t56 = GM_LD(uint64_t, ((((uint8_t *)t54) + 40) + 8));
        void * t57 = gm_l1;
        uint64_t t58 = GM_LD(uint64_t, ((((uint8_t *)t57) + 40) + 8));
        uint64_t r59; bool o60 = __builtin_sub_overflow(t58, ((uint64_t)1ULL), &r59);
        if (o60) gm_trap("integer overflow in `-`", gm_srclfnv05vuf0b8, 282, 48);
        uint64_t t61 = r59;
        if ((uintptr_t)t61 >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)t61, t56, gm_srclfnv05vuf0b8, 282, 41);
        gm_tl874t2atnlh3 t62 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t55) + (uintptr_t)t61 * 64) + 48));
        gm_tl874t2atnlh3 t63 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t53), GM_CONV(gm_cseahvg8v7p0vs, t62)));
        gm_l13 = t63;
        void * t64 = gm_l1;
        void * t65 = GM_LD(void *, (((uint8_t *)t64) + 40));
        uint64_t t66 = GM_LD(uint64_t, ((((uint8_t *)t64) + 40) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t66)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t66, gm_srclfnv05vuf0b8, 283, 35);
        uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)t65) + (uintptr_t)((uint64_t)0ULL) * 64));
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
        gm_tl874t2atnlh3 t69 = gm_l13;
        gm_to361lgknsu69 t70;
        GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t70 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t70 + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)8ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 8), t68);
        gm_t0d02142k4tu8 t71;
        GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t71 + 60, 0);
        GM_ST(uint32_t, ((uint8_t *)&t71 + 0), t67);
        GM_ST(gm_to361lgknsu69, ((uint8_t *)&t71 + 8), t70);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t71 + 48), t69);
        F.l8 = t71;
    }
    gm_trqfvmjtt4sij t72;
    gm_f_list_6l26q4a((void *)&t72, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l14 = t72;
    void * t73 = gm_l0;
    void * t74 = (void *)((uint8_t *)&F.l8);
    gm_t88rcucg1b7uk t75 = gm_l2;
    void * t76 = (void *)((uint8_t *)&F.l14);
    gm_t9qkh0rsooc8d t77;
    GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)1ULL));
    void * t78 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t73, t74, t75, t77, t76);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l15 = t78;
    {
        {
            void * t79 = gm_l0;
            void * t80 = GM_LD(void *, ((uint8_t *)t79));
            void * t81 = (void *)(((uint8_t *)t80) + 8);
            gm_t88rcucg1b7uk t82 = gm_l2;
            uint8_t t83 = gm_f_is_error_8k27gd1(t81, GM_CONV(gm_csguankko4lmhd, t82));
            uint8_t t84 = (uint8_t)!t83;
            if (!t84) {
                goto gm_lb9;
            }
            void * t85 = gm_l0;
            void * t86 = GM_LD(void *, ((uint8_t *)t85));
            void * t87 = gm_l15;
            uint8_t t88 = gm_f_irrefutable_lpkmeii((void *)NULL, cx, NULL, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t89 = (uint8_t)!t88;
            if (!t89) {
                goto gm_lb9;
            }
            void * t90 = gm_l0;
            gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 48));
            gm_b16_8 t92;
            GM_ST(void *, (uint8_t *)&t92, (void *)(gm_so4k586t2v79t));
            GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(63));
            gm_b0_1 t93 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t90, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb8;
        }
gm_lb9:;
    }
gm_lb8:;
    void * t94 = gm_l0;
    void * t95 = (void *)((uint8_t *)&F.l14);
    gm_b0_1 t96 = gm_f_declare_binds_rk1ncsj((void *)NULL, cx, NULL, t94, t95);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_th2lqbo77pvsc t97;
    memset(&t97, 0, sizeof t97);
    gm_l16 = t97;
    {
        {
            void * t98 = gm_l1;
            uint8_t t99 = (uint8_t)((((uint8_t *)t98))[32] != 0);
            if (!t99) {
                goto gm_lb11;
            }
            memmove(((uint8_t *)&gm_l17), ((uint8_t *)t98), 32);
            gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
            gm_th2lqbo77pvsc t101;
            GM_ST(uint8_t, (uint8_t *)&t101 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t101 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t101 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t101), t100);
            ((uint8_t *)&t101)[16] = 1;
            gm_l16 = t101;
            goto gm_lb10;
        }
gm_lb11:;
    }
gm_lb10:;
    void * t102 = gm_l0;
    void * t103 = gm_l0;
    void * t104 = GM_LD(void *, ((uint8_t *)t103));
    void * t105 = (void *)(((uint8_t *)t104) + 8);
    gm_th2lqbo77pvsc t106 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l16));
    gm_t88rcucg1b7uk t107 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t105);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t1qr0p6kln6tf t108;
    GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t108 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t108 + 4), t107);
    uint32_t t109 = gm_f_new_label_3207tcb((void *)NULL, cx, NULL, t102, &t106, ((uint8_t)1), t108);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l18 = t109;
    void * t110 = gm_l0;
    void * t111 = gm_l1;
    void * t112 = (void *)(((uint8_t *)t111) + 64);
    void * t113 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t110, t112, ((uint8_t)0));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l19 = t113;
    void * t114 = gm_l0;
    void * t115 = (void *)(((uint8_t *)t114) + 232);
    gm_te5gbhu3us3k4 t116;
    gm_f_pop_ruvva9t((void *)&t116, t115);
    void * t117 = gm_l0;
    uint32_t t118 = gm_l18;
    uint8_t t119 = gm_f_forget_label_breaks_vl4mn72(t117, t118);
    void * t120 = gm_l0;
    gm_f_pop_scope_g5j7o4e(t120);
    gm_trqfvmjtt4sij t121;
    gm_f_list_58nr42v_2((void *)&t121, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l20 = t121;
    void * t122 = (void *)((uint8_t *)&F.l20);
    void * t123 = gm_l15;
    void * t124 = gm_l19;
    gm_tbbqefuhv2r8h t125;
    GM_ST(void *, ((uint8_t *)&t125 + 0), t123);
    GM_ST(void *, ((uint8_t *)&t125 + 8), ((void *)0));
    GM_ST(void *, ((uint8_t *)&t125 + 16), t124);
    gm_tftb1odc739t5 t126 = gm_f_push_bpovr2k_2(t122, &t125);
    gm_l25 = t126;
    uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
    if (!t127) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srclfnv05vuf0b8, 299, 5);
    void * t128 = gm_l0;
    void * t129 = GM_LD(void *, ((uint8_t *)t128));
    void * t130 = (void *)(((uint8_t *)t129) + 8);
    gm_t88rcucg1b7uk t131 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t130);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t132 = gm_l3;
    gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
    gm_tkaapcii86udk t134;
    GM_ST(void *, ((uint8_t *)&t134 + 0), t132);
    GM_ST(gm_b16_8, ((uint8_t *)&t134 + 8), t133);
    gm_tl874t2atnlh3 t135 = gm_l6;
    gm_t2b6tm8c0vgo1 t136;
    memset(&t136, 0, sizeof t136);
    GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)44ULL));
    GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t136 + 8), t134);
    void * t137 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t131, &t136, t135);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l21 = t137;
    gm_trqfvmjtt4sij t138;
    gm_f_list_ir0h3tc_2((void *)&t138, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l22 = t138;
    void * t139 = (void *)((uint8_t *)&F.l22);
    void * t140 = gm_l21;
    gm_trcmmd12cefb1 t141;
    GM_ST(uint8_t, (uint8_t *)&t141 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t141 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t141 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t141 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t141 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)4ULL));
    GM_ST(void *, ((uint8_t *)&t141 + 8), t140);
    gm_tftb1odc739t5 t142 = gm_f_push_d6c76ud_2(t139, &t141);
    gm_l26 = t142;
    uint8_t t143 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
    if (!t143) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srclfnv05vuf0b8, 302, 5);
    gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
    gm_tl874t2atnlh3 t145 = gm_l6;
    gm_tgabaikr08mjg t146;
    GM_ST(uint32_t, (uint8_t *)&t146 + 36, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t146 + 0), t144);
    GM_ST(void *, ((uint8_t *)&t146 + 16), ((void *)0));
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t146 + 24), t145);
    gm_tq7s98ihonkju t147 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t146, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0nv110r163)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l27 = t147;
    uint8_t t148 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
    if (!t148) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_srclfnv05vuf0b8, 303, 11);
    void * t149 = GM_LD(void *, (((uint8_t *)&gm_l27) + 8));
    gm_l23 = t149;
    void * t150 = gm_l0;
    void * t151 = GM_LD(void *, ((uint8_t *)t150));
    void * t152 = (void *)(((uint8_t *)t151) + 8);
    gm_t88rcucg1b7uk t153 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t152);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t154 = gm_l18;
    gm_b16_8 t155 = gm_l4;
    void * t156 = gm_l23;
    void * t157 = gm_l5;
    gm_t710lsrq1m8gk t158;
    GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t158 + 0), t154);
    GM_ST(gm_b16_8, ((uint8_t *)&t158 + 8), t155);
    GM_ST(void *, ((uint8_t *)&t158 + 24), t156);
    GM_ST(void *, ((uint8_t *)&t158 + 32), t157);
    gm_tl874t2atnlh3 t159 = gm_l6;
    gm_t2b6tm8c0vgo1 t160;
    GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t160 + 48, 0);
    GM_ST(uint64_t, (uint8_t *)&t160 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)48ULL));
    GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t160 + 8), t158);
    void * t161 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t153, &t160, t159);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t161;
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
__attribute__((aligned(4))) void * gm_f_range_loop_44fv3au(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, gm_t88rcucg1b7uk a4, uint8_t a5) {
    struct gm_fr_range_loop_44fv3au F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    void * gm_l2; /* lo */
    void * gm_l3; /* hi */
    gm_t88rcucg1b7uk gm_l4; /* elem */
    uint8_t gm_l5; /* inclusive */
    gm_t88rcucg1b7uk gm_l7; /* b */
    uint32_t gm_l8; /* hi_t */
    uint32_t gm_l9; /* i */
    gm_closure gm_l10; /* local */
    uint32_t gm_l12; /* going */
    void * gm_l13; /* at_end */
    void * gm_l17; /* step */
    void * gm_l19; /* lp */
    GM_STACK_CHECK(&F, 2410u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    void * t2 = gm_l1;
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 104));
    F.l6 = t3;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = (void *)(((uint8_t *)t5) + 8);
    gm_t88rcucg1b7uk t7 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t7;
    void * t8 = gm_l0;
    gm_t88rcucg1b7uk t9 = gm_l4;
    gm_tl874t2atnlh3 t10 = F.l6;
    uint32_t t11 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t8, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t11;
    void * t12 = gm_l0;
    gm_t88rcucg1b7uk t13 = gm_l4;
    gm_tl874t2atnlh3 t14 = F.l6;
    uint32_t t15 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t12, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t15;
    gm_closure t16 = ((gm_closure){(void *)gm_f_local_i91hglv, (void *)&F.h, cx});
    gm_l10 = t16;
    gm_trqfvmjtt4sij t17;
    gm_f_list_ir0h3tc_2((void *)&t17, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l11 = t17;
    void * t18 = (void *)((uint8_t *)&F.l11);
    uint32_t t19 = gm_l9;
    void * t20 = gm_l2;
    gm_ttggq7ng4kp3p t21;
    GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t21 + 0), t19);
    GM_ST(void *, ((uint8_t *)&t21 + 8), t20);
    gm_trcmmd12cefb1 t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t22 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t22 + 8), t21);
    gm_tftb1odc739t5 t23 = gm_f_push_d6c76ud_2(t18, &t22);
    gm_l20 = t23;
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
    if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srclfnv05vuf0b8, 230, 5);
    void * t25 = (void *)((uint8_t *)&F.l11);
    uint32_t t26 = gm_l8;
    void * t27 = gm_l3;
    gm_ttggq7ng4kp3p t28;
    GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t28 + 0), t26);
    GM_ST(void *, ((uint8_t *)&t28 + 8), t27);
    gm_trcmmd12cefb1 t29;
    GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)0ULL));
    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t29 + 8), t28);
    gm_tftb1odc739t5 t30 = gm_f_push_d6c76ud_2(t25, &t29);
    gm_l21 = t30;
    uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
    if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srclfnv05vuf0b8, 231, 5);
    uint8_t t32 = gm_l5;
    uint8_t t33 = (uint8_t)!t32;
    if (t33) {
        void * t34 = (void *)((uint8_t *)&F.l11);
        void * t35 = gm_l0;
        void * t36 = gm_l1;
        gm_t88rcucg1b7uk t37 = gm_l4;
        uint32_t t38 = gm_l9;
        gm_t88rcucg1b7uk t39 = gm_l4;
        gm_closure t40 = gm_l10;
        uint32_t t41 = gm_l8;
        gm_t88rcucg1b7uk t42 = gm_l4;
        gm_closure t43 = t40;
        gm_node *ccx44 = t43.cx;
        gm_node cn45; { gm_node *f = gm_find(cx, 587034433); if (f) { cn45 = *f; cn45.next = ccx44; ccx44 = &cn45; } }
        void * t46 = t43.code;
        void * t47;
        if (GM_PTR_TAGGED(t46)) {
            t47 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t46))(t41, GM_CONV(gm_csguankko4lmhd, t42));
        } else {
            t47 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t46)(t43.env, ccx44, NULL, t41, t42);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_closure t48 = gm_l10;
        uint32_t t49 = gm_l9;
        gm_t88rcucg1b7uk t50 = gm_l4;
        gm_closure t51 = t48;
        gm_node *ccx52 = t51.cx;
        gm_node cn53; { gm_node *f = gm_find(cx, 587034433); if (f) { cn53 = *f; cn53.next = ccx52; ccx52 = &cn53; } }
        void * t54 = t51.code;
        void * t55;
        if (GM_PTR_TAGGED(t54)) {
            t55 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t54))(t49, GM_CONV(gm_csguankko4lmhd, t50));
        } else {
            t55 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t54)(t51.env, ccx52, NULL, t49, t50);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t56 = gm_f_counting_loop_s4u2ugd((void *)NULL, cx, NULL, t35, t36, t37, t38, t39, t47, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trcmmd12cefb1 t57;
        GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t57 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t57 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t57 + 8), t56);
        gm_tftb1odc739t5 t58 = gm_f_push_d6c76ud_2(t34, &t57);
        gm_l22 = t58;
        uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
        if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srclfnv05vuf0b8, 233, 9);
        void * t60 = gm_l0;
        void * t61 = GM_LD(void *, ((uint8_t *)t60));
        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        gm_tl874t2atnlh3 t63 = F.l6;
        void * t64 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t61, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t64;
        goto gm_fret;
    }
    void * t65 = gm_l0;
    gm_t88rcucg1b7uk t66 = gm_l7;
    gm_tl874t2atnlh3 t67 = F.l6;
    uint32_t t68 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t65, t66, t67);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t68;
    void * t69 = (void *)((uint8_t *)&F.l11);
    uint32_t t70 = gm_l12;
    gm_t88rcucg1b7uk t71 = gm_l7;
    gm_closure t72 = gm_l10;
    uint32_t t73 = gm_l9;
    gm_t88rcucg1b7uk t74 = gm_l4;
    gm_ts6oqppjh0ba3 t75;
    GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)11ULL));
    gm_closure t76 = t72;
    gm_node *ccx77 = t76.cx;
    gm_node cn78; { gm_node *f = gm_find(cx, 587034433); if (f) { cn78 = *f; cn78.next = ccx77; ccx77 = &cn78; } }
    void * t79 = t76.code;
    void * t80;
    if (GM_PTR_TAGGED(t79)) {
        t80 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t79))(t73, GM_CONV(gm_csguankko4lmhd, t74));
    } else {
        t80 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t79)(t76.env, ccx77, NULL, t73, t74);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_closure t81 = gm_l10;
    uint32_t t82 = gm_l8;
    gm_t88rcucg1b7uk t83 = gm_l4;
    gm_closure t84 = t81;
    gm_node *ccx85 = t84.cx;
    gm_node cn86; { gm_node *f = gm_find(cx, 587034433); if (f) { cn86 = *f; cn86.next = ccx85; ccx85 = &cn86; } }
    void * t87 = t84.code;
    void * t88;
    if (GM_PTR_TAGGED(t87)) {
        t88 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t87))(t82, GM_CONV(gm_csguankko4lmhd, t83));
    } else {
        t88 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t87)(t84.env, ccx85, NULL, t82, t83);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trl9d7m1k5lbd t89;
    GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t89 + 0), t75);
    GM_ST(void *, ((uint8_t *)&t89 + 8), t80);
    GM_ST(void *, ((uint8_t *)&t89 + 16), t88);
    gm_tl874t2atnlh3 t90 = F.l6;
    gm_t2b6tm8c0vgo1 t91;
    memset(&t91, 0, sizeof t91);
    GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)30ULL));
    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t91 + 8), t89);
    void * t92 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t71, &t91, t90);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_ttggq7ng4kp3p t93;
    GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t93 + 0), t70);
    GM_ST(void *, ((uint8_t *)&t93 + 8), t92);
    gm_trcmmd12cefb1 t94;
    GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t94 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)0ULL));
    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t94 + 8), t93);
    gm_tftb1odc739t5 t95 = gm_f_push_d6c76ud_2(t69, &t94);
    gm_l23 = t95;
    uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srclfnv05vuf0b8, 237, 5);
    gm_t88rcucg1b7uk t97 = gm_l7;
    gm_closure t98 = gm_l10;
    uint32_t t99 = gm_l9;
    gm_t88rcucg1b7uk t100 = gm_l4;
    gm_closure t101 = t98;
    gm_node *ccx102 = t101.cx;
    gm_node cn103; { gm_node *f = gm_find(cx, 587034433); if (f) { cn103 = *f; cn103.next = ccx102; ccx102 = &cn103; } }
    void * t104 = t101.code;
    void * t105;
    if (GM_PTR_TAGGED(t104)) {
        t105 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t104))(t99, GM_CONV(gm_csguankko4lmhd, t100));
    } else {
        t105 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t104)(t101.env, ccx102, NULL, t99, t100);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_closure t106 = gm_l10;
    uint32_t t107 = gm_l8;
    gm_t88rcucg1b7uk t108 = gm_l4;
    gm_closure t109 = t106;
    gm_node *ccx110 = t109.cx;
    gm_node cn111; { gm_node *f = gm_find(cx, 587034433); if (f) { cn111 = *f; cn111.next = ccx110; ccx110 = &cn111; } }
    void * t112 = t109.code;
    void * t113;
    if (GM_PTR_TAGGED(t112)) {
        t113 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t112))(t107, GM_CONV(gm_csguankko4lmhd, t108));
    } else {
        t113 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t112)(t109.env, ccx110, NULL, t107, t108);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tlcl4u558l6p5 t114;
    GM_ST(uint8_t, (uint8_t *)&t114 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t114 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t114 + 20, 0);
    GM_ST(void *, ((uint8_t *)&t114 + 0), t105);
    GM_ST(void *, ((uint8_t *)&t114 + 8), t113);
    GM_ST(uint8_t, ((uint8_t *)&t114 + 16), ((uint8_t)0));
    gm_tl874t2atnlh3 t115 = F.l6;
    gm_t2b6tm8c0vgo1 t116;
    memset(&t116, 0, sizeof t116);
    GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)43ULL));
    GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t116 + 8), t114);
    void * t117 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t97, &t116, t115);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l13 = t117;
    gm_trqfvmjtt4sij t118;
    gm_f_list_ir0h3tc_2((void *)&t118, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l14 = t118;
    void * t119 = (void *)((uint8_t *)&F.l14);
    gm_closure t120 = gm_l10;
    uint32_t t121 = gm_l12;
    gm_t88rcucg1b7uk t122 = gm_l7;
    gm_closure t123 = t120;
    gm_node *ccx124 = t123.cx;
    gm_node cn125; { gm_node *f = gm_find(cx, 587034433); if (f) { cn125 = *f; cn125.next = ccx124; ccx124 = &cn125; } }
    void * t126 = t123.code;
    void * t127;
    if (GM_PTR_TAGGED(t126)) {
        t127 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t126))(t121, GM_CONV(gm_csguankko4lmhd, t122));
    } else {
        t127 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t126)(t123.env, ccx124, NULL, t121, t122);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t128 = gm_l7;
    gm_tl874t2atnlh3 t129 = F.l6;
    gm_t2b6tm8c0vgo1 t130;
    memset(&t130, 0, sizeof t130);
    GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)4ULL));
    GM_ST(uint8_t, ((uint8_t *)&t130 + 8), ((uint8_t)0));
    void * t131 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t128, &t130, t129);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t3mf58gv9kf3n t132;
    GM_ST(void *, ((uint8_t *)&t132 + 0), t127);
    GM_ST(void *, ((uint8_t *)&t132 + 8), t131);
    gm_trcmmd12cefb1 t133;
    GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t133 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t133 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t133 + 8), t132);
    gm_tftb1odc739t5 t134 = gm_f_push_d6c76ud_2(t119, &t133);
    gm_l24 = t134;
    uint8_t t135 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
    if (!t135) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srclfnv05vuf0b8, 240, 5);
    gm_trqfvmjtt4sij t136;
    gm_f_list_ir0h3tc_2((void *)&t136, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l15 = t136;
    void * t137 = (void *)((uint8_t *)&F.l15);
    uint32_t t138 = gm_l9;
    gm_t88rcucg1b7uk t139 = gm_l4;
    gm_tl874t2atnlh3 t140 = F.l6;
    gm_trcmmd12cefb1 t141;
    gm_f_increment_12n73t5((void *)&t141, (void *)NULL, cx, NULL, t138, t139, t140);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t142 = gm_f_push_d6c76ud_2(t137, &t141);
    gm_l25 = t142;
    uint8_t t143 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
    if (!t143) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srclfnv05vuf0b8, 242, 5);
    gm_trqfvmjtt4sij t144;
    gm_f_list_v5d1o9b_2((void *)&t144, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l16 = t144;
    void * t145 = (void *)((uint8_t *)&F.l16);
    void * t146 = gm_l13;
    gm_tlr9dp305htm7 t147;
    GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t147 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)0ULL));
    GM_ST(void *, ((uint8_t *)&t147 + 8), t146);
    gm_tftb1odc739t5 t148 = gm_f_push_eb7kbhp_2(t145, &t147);
    gm_l26 = t148;
    uint8_t t149 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
    if (!t149) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srclfnv05vuf0b8, 244, 5);
    void * t150 = gm_l0;
    void * t151 = GM_LD(void *, ((uint8_t *)t150));
    void * t152 = (void *)(((uint8_t *)t151) + 8);
    gm_t88rcucg1b7uk t153 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t152);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
    void * t155 = gm_l0;
    void * t156 = GM_LD(void *, ((uint8_t *)t155));
    gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
    gm_tl874t2atnlh3 t158 = F.l6;
    void * t159 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t156, t157, t158);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t160 = gm_l0;
    void * t161 = GM_LD(void *, ((uint8_t *)t160));
    gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
    gm_tl874t2atnlh3 t163 = F.l6;
    void * t164 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t161, t162, t163);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trl2khmuphncd t165;
    GM_ST(gm_b16_8, ((uint8_t *)&t165 + 0), t154);
    GM_ST(void *, ((uint8_t *)&t165 + 16), t159);
    GM_ST(void *, ((uint8_t *)&t165 + 24), t164);
    gm_tl874t2atnlh3 t166 = F.l6;
    gm_t2b6tm8c0vgo1 t167;
    GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t167 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t167 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t167 + 48, 0);
    GM_ST(uint64_t, (uint8_t *)&t167 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)47ULL));
    GM_ST(gm_trl2khmuphncd, ((uint8_t *)&t167 + 8), t165);
    void * t168 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t153, &t167, t166);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l17 = t168;
    gm_trqfvmjtt4sij t169;
    gm_f_list_v5d1o9b_2((void *)&t169, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l18 = t169;
    void * t170 = (void *)((uint8_t *)&F.l18);
    gm_closure t171 = gm_l10;
    uint32_t t172 = gm_l12;
    gm_t88rcucg1b7uk t173 = gm_l7;
    gm_closure t174 = t171;
    gm_node *ccx175 = t174.cx;
    gm_node cn176; { gm_node *f = gm_find(cx, 587034433); if (f) { cn176 = *f; cn176.next = ccx175; ccx175 = &cn176; } }
    void * t177 = t174.code;
    void * t178;
    if (GM_PTR_TAGGED(t177)) {
        t178 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t177))(t172, GM_CONV(gm_csguankko4lmhd, t173));
    } else {
        t178 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t177)(t174.env, ccx175, NULL, t172, t173);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tlr9dp305htm7 t179;
    GM_ST(uint8_t, (uint8_t *)&t179 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t179 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t179 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t179 + 16, 0);
    GM_ST(uint8_t, ((uint8_t *)&t179 + 0), ((uint8_t)0ULL));
    GM_ST(void *, ((uint8_t *)&t179 + 8), t178);
    gm_tftb1odc739t5 t180 = gm_f_push_eb7kbhp_2(t170, &t179);
    gm_l27 = t180;
    uint8_t t181 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
    if (!t181) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srclfnv05vuf0b8, 247, 5);
    void * t182 = gm_l0;
    void * t183 = gm_l1;
    gm_t88rcucg1b7uk t184 = gm_l4;
    gm_closure t185 = gm_l10;
    uint32_t t186 = gm_l9;
    gm_t88rcucg1b7uk t187 = gm_l4;
    gm_closure t188 = t185;
    gm_node *ccx189 = t188.cx;
    gm_node cn190; { gm_node *f = gm_find(cx, 587034433); if (f) { cn190 = *f; cn190.next = ccx189; ccx189 = &cn190; } }
    void * t191 = t188.code;
    void * t192;
    if (GM_PTR_TAGGED(t191)) {
        t192 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t191))(t186, GM_CONV(gm_csguankko4lmhd, t187));
    } else {
        t192 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t191)(t188.env, ccx189, NULL, t186, t187);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
    void * t194 = gm_l17;
    void * t195 = gm_f_loop_with_pattern_d8vl0mj((void *)NULL, cx, NULL, t182, t183, t184, t192, t193, t194);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l19 = t195;
    void * t196 = (void *)((uint8_t *)&F.l11);
    void * t197 = gm_l19;
    gm_trcmmd12cefb1 t198;
    GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t198 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t198 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)4ULL));
    GM_ST(void *, ((uint8_t *)&t198 + 8), t197);
    gm_tftb1odc739t5 t199 = gm_f_push_d6c76ud_2(t196, &t198);
    gm_l28 = t199;
    uint8_t t200 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
    if (!t200) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srclfnv05vuf0b8, 249, 5);
    void * t201 = gm_l0;
    void * t202 = GM_LD(void *, ((uint8_t *)t201));
    gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
    gm_tl874t2atnlh3 t204 = F.l6;
    void * t205 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t202, t203, t204);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t205;
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
__attribute__((aligned(4))) void * gm_f_range_test_tqcppmu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, void * a3, void * a4, gm_t88rcucg1b7uk a5, uint8_t a6, uint8_t a7, gm_tl874t2atnlh3 a8) {
    struct gm_fr_range_test_tqcppmu F;
    void * gm_l0; /* cx */
    void * gm_l1; /* elem */
    gm_b16_8 gm_l2; /* lets */
    void * gm_l3; /* lo */
    void * gm_l4; /* hi */
    uint8_t gm_l6; /* inclusive */
    uint8_t gm_l7; /* negated */
    gm_t88rcucg1b7uk gm_l9; /* b */
    gm_closure gm_l11; /* x */
    gm_ts6oqppjh0ba3 gm_l12; /* upper */
    void * gm_l13; /* test */
    GM_STACK_CHECK(&F, 981u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    F.l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    F.l8 = a8;
    gm_ts6oqppjh0ba3 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t5;
    void * t6 = gm_l0;
    gm_t88rcucg1b7uk t7 = F.l5;
    gm_tl874t2atnlh3 t8 = F.l8;
    uint32_t t9 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t6, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l10 = t9;
    gm_closure t10 = ((gm_closure){(void *)gm_f_x_dmu97o4, (void *)&F.h, cx});
    gm_l11 = t10;
    uint8_t t11 = gm_l6;
    if (t11) {
        gm_ts6oqppjh0ba3 t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)11ULL));
        gm_l15 = t12;
    } else {
        gm_ts6oqppjh0ba3 t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)10ULL));
        gm_l15 = t13;
    }
    gm_ts6oqppjh0ba3 t14 = gm_l15;
    gm_l12 = t14;
    gm_t88rcucg1b7uk t15 = gm_l9;
    gm_t88rcucg1b7uk t16 = gm_l9;
    void * t17 = gm_l3;
    gm_closure t18 = gm_l11;
    gm_ts6oqppjh0ba3 t19;
    GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)11ULL));
    gm_closure t20 = t18;
    gm_node *ccx21 = t20.cx;
    gm_node cn22; { gm_node *f = gm_find(cx, 587034433); if (f) { cn22 = *f; cn22.next = ccx21; ccx21 = &cn22; } }
    void * t23 = t20.code;
    void * t24;
    if (GM_PTR_TAGGED(t23)) {
        t24 = ((void * (*)(void))GM_PTR_UNTAG(t23))();
    } else {
        t24 = ((void * (*)(void *, gm_node *, gm_node *))t23)(t20.env, ccx21, NULL);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trl9d7m1k5lbd t25;
    GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t25 + 0), t19);
    GM_ST(void *, ((uint8_t *)&t25 + 8), t17);
    GM_ST(void *, ((uint8_t *)&t25 + 16), t24);
    gm_tl874t2atnlh3 t26 = F.l8;
    gm_t2b6tm8c0vgo1 t27;
    memset(&t27, 0, sizeof t27);
    GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)30ULL));
    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t27 + 8), t25);
    void * t28 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t16, &t27, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t29 = gm_l9;
    gm_ts6oqppjh0ba3 t30 = gm_l12;
    gm_closure t31 = gm_l11;
    gm_closure t32 = t31;
    gm_node *ccx33 = t32.cx;
    gm_node cn34; { gm_node *f = gm_find(cx, 587034433); if (f) { cn34 = *f; cn34.next = ccx33; ccx33 = &cn34; } }
    void * t35 = t32.code;
    void * t36;
    if (GM_PTR_TAGGED(t35)) {
        t36 = ((void * (*)(void))GM_PTR_UNTAG(t35))();
    } else {
        t36 = ((void * (*)(void *, gm_node *, gm_node *))t35)(t32.env, ccx33, NULL);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t37 = gm_l4;
    gm_trl9d7m1k5lbd t38;
    GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t38 + 0), t30);
    GM_ST(void *, ((uint8_t *)&t38 + 8), t36);
    GM_ST(void *, ((uint8_t *)&t38 + 16), t37);
    gm_tl874t2atnlh3 t39 = F.l8;
    gm_t2b6tm8c0vgo1 t40;
    memset(&t40, 0, sizeof t40);
    GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)30ULL));
    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t40 + 8), t38);
    void * t41 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t29, &t40, t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_ta612fil812pm t42;
    GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)1));
    GM_ST(void *, ((uint8_t *)&t42 + 8), t28);
    GM_ST(void *, ((uint8_t *)&t42 + 16), t41);
    gm_tl874t2atnlh3 t43 = F.l8;
    gm_t2b6tm8c0vgo1 t44;
    memset(&t44, 0, sizeof t44);
    GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)31ULL));
    GM_ST(gm_ta612fil812pm, ((uint8_t *)&t44 + 8), t42);
    void * t45 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t15, &t44, t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l13 = t45;
    uint8_t t46 = gm_l7;
    if (t46) {
        gm_t88rcucg1b7uk t47 = gm_l9;
        void * t48 = gm_l13;
        gm_t9qkh0rsooc8d t49;
        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)2ULL));
        gm_t8ghgl2jj0qha t50;
        GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t50 + 0), t49);
        GM_ST(void *, ((uint8_t *)&t50 + 8), t48);
        gm_tl874t2atnlh3 t51 = F.l8;
        gm_t2b6tm8c0vgo1 t52;
        memset(&t52, 0, sizeof t52);
        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)29ULL));
        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t52 + 8), t50);
        void * t53 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t47, &t52, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t53;
    }
    gm_trqfvmjtt4sij t54;
    gm_f_list_ir0h3tc_2((void *)&t54, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l14 = t54;
    void * t55 = (void *)((uint8_t *)&F.l14);
    uint32_t t56 = F.l10;
    void * t57 = gm_l1;
    gm_ttggq7ng4kp3p t58;
    GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t58 + 0), t56);
    GM_ST(void *, ((uint8_t *)&t58 + 8), t57);
    gm_trcmmd12cefb1 t59;
    GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t59 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t59 + 8), t58);
    gm_tftb1odc739t5 t60 = gm_f_push_d6c76ud_2(t55, &t59);
    gm_l16 = t60;
    uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srclfnv05vuf0b8, 183, 5);
    void * t62 = (void *)((uint8_t *)&F.l14);
    gm_b16_8 t63 = gm_l2;
    gm_tftb1odc739t5 t64 = gm_f_extend_nkql463(t62, t63);
    gm_l17 = t64;
    uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srclfnv05vuf0b8, 184, 5);
    gm_t88rcucg1b7uk t66 = gm_l9;
    gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
    void * t68 = gm_l13;
    gm_tl874t2atnlh3 t69 = F.l8;
    void * t70 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t66, t67, t68, t69);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t70;
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_tuple_elem_join_om8llaj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_tuple_elem_join_om8llaj F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* elems */
    void * gm_l2; /* tys */
    gm_t88rcucg1b7uk gm_l3; /* first */
    uint8_t gm_l4; /* differ */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* e */
    gm_t88rcucg1b7uk gm_l8; /* r */
    gm_t88rcucg1b7uk gm_l9; /* joined */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t88rcucg1b7uk gm_l12; /* e */
    gm_t88rcucg1b7uk gm_l13; /* r */
    GM_STACK_CHECK(&F, 714u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l14;
    uint8_t gm_l15;
    gm_th2lqbo77pvsc gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_t53at80pfdus3 gm_l19;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    void * t22 = (void *)(((uint8_t *)t21) + 8);
    gm_l2 = t22;
    void * t23 = gm_l0;
    void * t24 = (void *)(((uint8_t *)t23) + 96);
    void * t25 = gm_l0;
    void * t26 = GM_LD(void *, ((uint8_t *)t25));
    void * t27 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t28)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t28, gm_srclfnv05vuf0b8, 158, 37);
    gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + (uintptr_t)((uint64_t)0ULL) * 4));
    gm_t88rcucg1b7uk t30 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t24, t26, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t30;
    gm_l4 = ((uint8_t)0);
    gm_b16_8 t31 = gm_l1;
    gm_l5 = t31;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t32 = gm_l6;
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t34 = (t32 < t33);
            if (!t34) {
                goto gm_lb1;
            }
            {
                void * t35 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t37 = gm_l6;
                {
                    {
                        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t35) + (uintptr_t)t37 * 4));
                        gm_l7 = t38;
                        void * t39 = gm_l0;
                        void * t40 = (void *)(((uint8_t *)t39) + 96);
                        void * t41 = gm_l0;
                        void * t42 = GM_LD(void *, ((uint8_t *)t41));
                        gm_t88rcucg1b7uk t43 = gm_l7;
                        gm_t88rcucg1b7uk t44 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t40, t42, t43);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l8 = t44;
                        void * t45 = gm_l2;
                        gm_t88rcucg1b7uk t46 = gm_l8;
                        uint8_t t47 = gm_f_has_infer_3o82a0b(t45, GM_CONV(gm_csguankko4lmhd, t46));
                        if (t47) {
                            gm_l14 = ((uint8_t)1);
                        } else {
                            void * t48 = gm_l2;
                            gm_t88rcucg1b7uk t49 = gm_l8;
                            uint8_t t50 = gm_f_is_error_8k27gd1(t48, GM_CONV(gm_csguankko4lmhd, t49));
                            gm_l14 = t50;
                        }
                        uint8_t t51 = gm_l14;
                        if (t51) {
                            gm_tkn42u7na3t81 t52;
                            memset(&t52, 0, sizeof t52);
                            F.ret = t52;
                            goto gm_fret;
                        }
                        gm_t88rcucg1b7uk t53 = gm_l8;
                        gm_t88rcucg1b7uk t54 = gm_l3;
                        uint8_t t55 = (!gm_eq9m6mvtmgde(((uint8_t *)&t53), ((uint8_t *)&t54)));
                        if (t55) {
                            gm_l4 = ((uint8_t)1);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 160, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t56 = gm_l6;
            uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
            uint64_t t59 = r57;
            gm_l6 = t59;
        }
    }
gm_lb1:;
    uint8_t t60 = gm_l4;
    uint8_t t61 = (uint8_t)!t60;
    if (t61) {
        gm_tkn42u7na3t81 t62;
        memset(&t62, 0, sizeof t62);
        F.ret = t62;
        goto gm_fret;
    }
    void * t63 = gm_l0;
    void * t64 = (void *)(((uint8_t *)t63) + 96);
    void * t65 = gm_l0;
    void * t66 = GM_LD(void *, ((uint8_t *)t65));
    void * t67 = gm_l0;
    gm_b16_8 t68 = gm_l1;
    gm_t88rcucg1b7uk t69 = gm_f_join_type_cck70vp((void *)NULL, cx, NULL, t67, t68);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t70 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t64, t66, t69);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t70;
    gm_b16_8 t71 = gm_l1;
    gm_l10 = t71;
    gm_l11 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t72 = gm_l11;
            uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            uint8_t t74 = (t72 < t73);
            if (!t74) {
                goto gm_lb6;
            }
            {
                void * t75 = GM_LD(void *, ((uint8_t *)&gm_l10));
                uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint64_t t77 = gm_l11;
                {
                    {
                        gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t75) + (uintptr_t)t77 * 4));
                        gm_l12 = t78;
                        void * t79 = gm_l0;
                        void * t80 = (void *)(((uint8_t *)t79) + 96);
                        void * t81 = gm_l0;
                        void * t82 = GM_LD(void *, ((uint8_t *)t81));
                        gm_t88rcucg1b7uk t83 = gm_l12;
                        gm_t88rcucg1b7uk t84 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t80, t82, t83);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l13 = t84;
                        {
                            {
                                gm_t88rcucg1b7uk t85 = gm_l13;
                                gm_t88rcucg1b7uk t86 = gm_l9;
                                uint8_t t87 = (!gm_eq9m6mvtmgde(((uint8_t *)&t85), ((uint8_t *)&t86)));
                                if (!t87) {
                                    goto gm_lb12;
                                }
                                void * t88 = gm_l0;
                                gm_t88rcucg1b7uk t89 = gm_l13;
                                gm_t88rcucg1b7uk t90 = gm_l9;
                                gm_f_coercion_steps_ibqvhta((void *)&gm_l16, (void *)NULL, cx, NULL, t88, t89, t90);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                {
                                    {
                                        uint8_t t91 = (uint8_t)((((uint8_t *)&gm_l16))[16] != 0);
                                        if (t91) {
                                            goto gm_lb14;
                                        }
                                        gm_l15 = ((uint8_t)1);
                                        goto gm_lb13;
                                    }
gm_lb14:;
                                    {
                                        gm_l15 = ((uint8_t)0);
                                        goto gm_lb13;
                                    }
gm_lb15:;
                                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 169, 32);
                                }
gm_lb13:;
                                uint8_t t92 = gm_l15;
                                if (!t92) {
                                    goto gm_lb12;
                                }
                                void * t93 = gm_l2;
                                gm_t88rcucg1b7uk t94 = gm_l13;
                                uint8_t t95 = gm_f_is_positional_array_6sufsk8(t93, GM_CONV(gm_csguankko4lmhd, t94));
                                if (t95) {
                                    void * t96 = gm_l2;
                                    gm_t88rcucg1b7uk t97 = gm_l9;
                                    gm_f_kind_jcnt4hm((void *)&gm_l19, t96, GM_CONV(gm_csguankko4lmhd, t97));
                                    {
                                        {
                                            uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                                            uint8_t t99 = (uint8_t)(t98 == ((uint8_t)10ULL));
                                            if (!t99) {
                                                goto gm_lb17;
                                            }
                                            gm_l18 = ((uint8_t)1);
                                            goto gm_lb16;
                                        }
gm_lb17:;
                                        {
                                            gm_l18 = ((uint8_t)0);
                                            goto gm_lb16;
                                        }
gm_lb18:;
                                        gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 169, 109);
                                    }
gm_lb16:;
                                    uint8_t t100 = gm_l18;
                                    gm_l17 = t100;
                                } else {
                                    gm_l17 = ((uint8_t)0);
                                }
                                uint8_t t101 = gm_l17;
                                uint8_t t102 = (uint8_t)!t101;
                                if (!t102) {
                                    goto gm_lb12;
                                }
                                gm_tkn42u7na3t81 t103;
                                memset(&t103, 0, sizeof t103);
                                F.ret = t103;
                                goto gm_fret;
                            }
gm_lb12:;
                        }
gm_lb11:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srclfnv05vuf0b8, 167, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t104 = gm_l11;
            uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
            uint64_t t107 = r105;
            gm_l11 = t107;
        }
    }
gm_lb6:;
    gm_t88rcucg1b7uk t108 = gm_l9;
    gm_tkn42u7na3t81 t109;
    GM_ST(uint8_t, (uint8_t *)&t109 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t109 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t109), t108);
    ((uint8_t *)&t109)[4] = 1;
    F.ret = t109;
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
extern __attribute__((aligned(4))) void * gm_f_x_dmu97o4(void *env, gm_node *cx, gm_node *inst);
