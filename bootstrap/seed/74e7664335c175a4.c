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
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint16_t f0; } gm_tu0pangbbaeec;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
typedef struct gm_csa3m9feqhg846 { uint64_t f0; gm_csguankko4lmhd f1; uint64_t f2; } gm_csa3m9feqhg846;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t7neromnu8t62;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; gm_b16_8 f4; } gm_t89vrq05jgle5;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; uint64_t f2; } gm_tismvprs8r13f;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint64_t f5; } gm_tjftu5su6fh8e;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; } gm_tmr5oep1578tb;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tuglhjjmj2g58 f0; uint8_t f1; uint8_t pe[7]; } gm_tqcc6dmlne11b;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; uint64_t f5; } gm_tte9sujb6sb62;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; gm_b16_8 f4; gm_b16_8 f5; } gm_ttq7m9ctqad93;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; } gm_tttl9nsdo1iu6;
_Static_assert(sizeof(gm_tu0pangbbaeec) == 2 && _Alignof(gm_tu0pangbbaeec) == 2 && offsetof(gm_tu0pangbbaeec, f0) == 0, "layout of `(u16,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_csa3m9feqhg846) == 24 && _Alignof(gm_csa3m9feqhg846) == 8 && offsetof(gm_csa3m9feqhg846, f0) == 0 && offsetof(gm_csa3m9feqhg846, f1) == 8 && offsetof(gm_csa3m9feqhg846, f2) == 16, "layout of `VecParts`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t7neromnu8t62) == 40 && _Alignof(gm_t7neromnu8t62) == 8 && offsetof(gm_t7neromnu8t62, f0) == 0 && offsetof(gm_t7neromnu8t62, f1) == 8 && offsetof(gm_t7neromnu8t62, f2) == 24, "layout of `(u64, string, string)`");
_Static_assert(sizeof(gm_t89vrq05jgle5) == 72 && _Alignof(gm_t89vrq05jgle5) == 8 && offsetof(gm_t89vrq05jgle5, f0) == 0 && offsetof(gm_t89vrq05jgle5, f1) == 16 && offsetof(gm_t89vrq05jgle5, f2) == 32 && offsetof(gm_t89vrq05jgle5, f3) == 48 && offsetof(gm_t89vrq05jgle5, f4) == 56, "layout of `(string, string, string, u64, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tismvprs8r13f) == 24 && _Alignof(gm_tismvprs8r13f) == 8 && offsetof(gm_tismvprs8r13f, f0) == 0 && offsetof(gm_tismvprs8r13f, f1) == 8 && offsetof(gm_tismvprs8r13f, f2) == 16, "layout of `VecParts`");
_Static_assert(sizeof(gm_tjftu5su6fh8e) == 88 && _Alignof(gm_tjftu5su6fh8e) == 8 && offsetof(gm_tjftu5su6fh8e, f0) == 0 && offsetof(gm_tjftu5su6fh8e, f1) == 16 && offsetof(gm_tjftu5su6fh8e, f2) == 32 && offsetof(gm_tjftu5su6fh8e, f3) == 48 && offsetof(gm_tjftu5su6fh8e, f4) == 64 && offsetof(gm_tjftu5su6fh8e, f5) == 80, "layout of `(string, string, string, []u8, string, usize)`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tmr5oep1578tb) == 56 && _Alignof(gm_tmr5oep1578tb) == 8 && offsetof(gm_tmr5oep1578tb, f0) == 0 && offsetof(gm_tmr5oep1578tb, f1) == 16 && offsetof(gm_tmr5oep1578tb, f2) == 32 && offsetof(gm_tmr5oep1578tb, f3) == 40, "layout of `(string, []u8, usize, []u8)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tqcc6dmlne11b) == 24 && _Alignof(gm_tqcc6dmlne11b) == 8 && offsetof(gm_tqcc6dmlne11b, f0) == 0 && offsetof(gm_tqcc6dmlne11b, f1) == 16, "layout of `?IntInfo`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tte9sujb6sb62) == 72 && _Alignof(gm_tte9sujb6sb62) == 8 && offsetof(gm_tte9sujb6sb62, f0) == 0 && offsetof(gm_tte9sujb6sb62, f1) == 16 && offsetof(gm_tte9sujb6sb62, f2) == 24 && offsetof(gm_tte9sujb6sb62, f3) == 40 && offsetof(gm_tte9sujb6sb62, f4) == 56 && offsetof(gm_tte9sujb6sb62, f5) == 64, "layout of `(string, u64, string, string, u64, u64)`");
_Static_assert(sizeof(gm_ttq7m9ctqad93) == 80 && _Alignof(gm_ttq7m9ctqad93) == 8 && offsetof(gm_ttq7m9ctqad93, f0) == 0 && offsetof(gm_ttq7m9ctqad93, f1) == 16 && offsetof(gm_ttq7m9ctqad93, f2) == 24 && offsetof(gm_ttq7m9ctqad93, f3) == 40 && offsetof(gm_ttq7m9ctqad93, f4) == 48 && offsetof(gm_ttq7m9ctqad93, f5) == 64, "layout of `(string, u64, string, u64, string, string)`");
_Static_assert(sizeof(gm_tttl9nsdo1iu6) == 56 && _Alignof(gm_tttl9nsdo1iu6) == 8 && offsetof(gm_tttl9nsdo1iu6, f0) == 0 && offsetof(gm_tttl9nsdo1iu6, f1) == 16 && offsetof(gm_tttl9nsdo1iu6, f2) == 32 && offsetof(gm_tttl9nsdo1iu6, f3) == 48, "layout of `(string, string, string, u64)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
struct gm_fr_bool_ty_kotvo8t {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
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
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_29f0d3k {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_fn_c4j9irb {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_fn_hfolh19 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_fn_j0gskii {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_format_0gjpujh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1amnh72 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1mojd22 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_21ahlbb {
    gm_frame h;
    gm_tif3elqk01bdl l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tif3elqk01bdl l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_27c89bp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_368205k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3tnvb6a {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_48m8fhm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4f5imdp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_61d2klb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7lru3c2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_adcs7to {
    gm_frame h;
    gm_tif3elqk01bdl l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tif3elqk01bdl l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_apgk46s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c0h6o29 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cjnp8ds {
    gm_frame h;
    gm_tjftu5su6fh8e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tjftu5su6fh8e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dlemvkb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eqjcikj {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f89fec3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h9otq43 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hjgrmth {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_iqjnutf {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jo1gjr5 {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l0pq86n {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l9df9v3 {
    gm_frame h;
    gm_th2d9c7aju8hp l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_th2d9c7aju8hp l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lqnh3lt {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mdlbfc1 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mnm0p9v {
    gm_frame h;
    gm_tttl9nsdo1iu6 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tttl9nsdo1iu6 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nhrpa25 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_njau13r {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nvhh8d2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s4jotpe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s8juhic {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sojmgl9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t4gbkjg {
    gm_frame h;
    gm_tftksr4op114e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vmsom9v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_is_float_gthejtb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_eo6jkc5 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_vector_builtin_njc3mga {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_line_l6u87p9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_4bjepb3 {
    gm_frame h;
    gm_tif3elqk01bdl l1; /* args */
    gm_tif3elqk01bdl l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_4ht2lgi {
    gm_frame h;
    gm_tftksr4op114e l1; /* args */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_5476lg2 {
    gm_frame h;
    gm_tud7oovfvnf43 l1; /* args */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_6cjbh5j {
    gm_frame h;
    gm_tftksr4op114e l1; /* args */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_6mfrmej {
    gm_frame h;
    gm_tmr5oep1578tb l1; /* args */
    gm_tmr5oep1578tb l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_7cnqmuh {
    gm_frame h;
    gm_tttl9nsdo1iu6 l1; /* args */
    gm_tttl9nsdo1iu6 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_7e7j1ug {
    gm_frame h;
    gm_ttq7m9ctqad93 l1; /* args */
    gm_ttq7m9ctqad93 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_ar0alse {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_c0cfb71 {
    gm_frame h;
    gm_tif3elqk01bdl l1; /* args */
    gm_tif3elqk01bdl l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_di76emp {
    gm_frame h;
    gm_tud7oovfvnf43 l1; /* args */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_gjfdarm {
    gm_frame h;
    gm_tmr5oep1578tb l1; /* args */
    gm_tmr5oep1578tb l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_nac2kse {
    gm_frame h;
    gm_t7neromnu8t62 l1; /* args */
    gm_t7neromnu8t62 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_ou0dv81 {
    gm_frame h;
    gm_t89vrq05jgle5 l1; /* args */
    gm_t89vrq05jgle5 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_pai11tb {
    gm_frame h;
    gm_tte9sujb6sb62 l1; /* args */
    gm_tte9sujb6sb62 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_qno93os {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_sk93pav {
    gm_frame h;
    gm_th2d9c7aju8hp l1; /* args */
    gm_th2d9c7aju8hp l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_ssnaj62 {
    gm_frame h;
    gm_tud7oovfvnf43 l1; /* args */
    gm_tud7oovfvnf43 l3; /* <tmp> */
    gm_b0_1 ret;
};
struct gm_fr_linef_uu8i6ue {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_ptr_bytes_eocneng {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_push_3ef746b {
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
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_slice_len_a3383br {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_slice_ptr_hojfgr5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_8ekedsl {
    gm_frame h;
    gm_b16_8 ret;
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
struct gm_fr_vec_blend_2oerv54 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_compare_mask_1e833o5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_compare_vrcged3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_convert_94b6r5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_lane_at_i7st991 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_lane_bytes_c5pmsp6 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_vec_lane_loop_a3gvooh {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vec_load_8m1nglt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_load_store_ng9gfpc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_mask_test_9mgimk6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_minmax_abs_4le8bpj {
    gm_frame h;
    void * l0; /* fg */
    gm_tvdg94bk7aa22 l3; /* op */
    gm_t88rcucg1b7uk l7; /* lane */
    uint64_t l8; /* bytes */
    gm_b16_8 l12; /* pa */
    gm_th2lqbo77pvsc l13; /* pb */
    gm_b16_8 l15; /* loc */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vec_parts_srl0rr9 {
    gm_frame h;
    gm_tismvprs8r13f ret;
};
struct gm_fr_vec_reduce_m5gdqn2 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vec_select_3f37tvf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_shuffle_j6jmffl {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* idx */
    gm_b16_8 ret;
};
struct gm_fr_vec_store_6d0dm94 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vec_ty_adod8ur {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_vector_binary_value_u702bne {
    gm_frame h;
    void * l0; /* fg */
    gm_tl874t2atnlh3 l1; /* span */
    gm_ts6oqppjh0ba3 l3; /* op */
    gm_b16_8 l5; /* b */
    gm_t88rcucg1b7uk l8; /* lane */
    uint64_t l9; /* bytes */
    gm_b16_8 l15; /* pa */
    gm_b16_8 l16; /* pa */
    gm_b16_8 l17; /* pb */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vector_builtin_values_cm12ee6 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_vector_unary_value_o07noaa {
    gm_frame h;
    void * l0; /* fg */
    gm_tl874t2atnlh3 l1; /* span */
    gm_t88rcucg1b7uk l7; /* lane */
    uint64_t l8; /* bytes */
    gm_b16_8 l9; /* pv */
    gm_t5o0tpv5jka2l ret;
};
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_float_from_r6b8fu0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_fn_29f0d3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_fn_c4j9irb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_fn_hfolh19(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_fn_j0gskii(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_format_0gjpujh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_1mojd22(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_1o6fs38(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_21ahlbb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_27c89bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_368205k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_3tnvb6a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_48m8fhm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_4f5imdp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_61d2klb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_65ee4ra(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_6hrn7j8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_6ti8an5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tu0pangbbaeec a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_74pd3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_7lru3c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_adcs7to(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_apgk46s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_c0h6o29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_cjnp8ds(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_dlemvkb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_eqjcikj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_f89fec3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fjqj2pj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_flhcmdb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_h9otq43(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tu0pangbbaeec a0);
inline __attribute__((aligned(4))) void gm_f_format_hjgrmth(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_iqjnutf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_jo1gjr5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l0pq86n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_l9df9v3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lqnh3lt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mdlbfc1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mnm0p9v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_nhrpa25(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_njau13r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nv3e0h2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_nvhh8d2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_p453i5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_q4qso5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s4jotpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_s8juhic(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_sojmgl9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_t4gbkjg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_te7kh3c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_vmsom9v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) void gm_f_ii_min_lku7pdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_int_binary_kp3u7i8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_ts6oqppjh0ba3 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_gthejtb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_eo6jkc5(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_vector_builtin_njc3mga(gm_tvdg94bk7aa22 a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_1jnit4p(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_4bjepb3(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_4ht2lgi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_5476lg2(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_6cjbh5j(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_6mfrmej(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_7cnqmuh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_7e7j1ug(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_8r881aa(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ah3nlha(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ao39327(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ar0alse(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_c0cfb71(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_di76emp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_gjfdarm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_mrrdu75(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_msr3bde(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_nac2kse(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_os35pa0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ou0dv81(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_p9t398q(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_pai11tb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_pgpsu01(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qd0v194(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qk177qj(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qno93os(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_rho83iv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_sbd4ln9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_sk93pav(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_slpvrc7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ssnaj62(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_t97l5gi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_u25b6gb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_simd_eafgrv9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_blend_2oerv54(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_compare_mask_1e833o5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_compare_vrcged3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tvdg94bk7aa22 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_convert_94b6r5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_lane_at_i7st991(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) uint64_t gm_f_vec_lane_bytes_c5pmsp6(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_vec_lane_loop_a3gvooh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_closure a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_load_8m1nglt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_load_store_ng9gfpc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tvdg94bk7aa22 a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_mask_test_9mgimk6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_vec_minmax_abs_4le8bpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tvdg94bk7aa22 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_vec_parts_srl0rr9(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_csa3m9feqhg846 gm_f_vec_parts_srl0rr9_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_vec_reduce_m5gdqn2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_select_3f37tvf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_shuffle_j6jmffl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_store_6d0dm94(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_ty_adod8ur(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, gm_tqs5p4n2tq19v a3);
__attribute__((aligned(4))) void gm_f_vector_binary_value_u702bne(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_ts6oqppjh0ba3 a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_vector_builtin_values_cm12ee6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tvdg94bk7aa22 a3, gm_b16_8 a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_vector_typedef_vpgf6u8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint64_t a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_vector_unary_value_o07noaa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s0m9d22aen6f8[2];
extern const uint8_t gm_s0p6vt35dv9nv[6];
extern const uint8_t gm_s0ugop4l95o1j[6];
extern const uint8_t gm_s12ng38ve3205[8];
extern const uint8_t gm_s1ecdmpgj8faq[24];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1tdsk62jj75n[8];
extern const uint8_t gm_s20mq4dk47bdd[4];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s35qfb045sqkh[59];
extern const uint8_t gm_s3lqm9addfamn[56];
extern const uint8_t gm_s4cess8gum6i7[6];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s4p53khe03les[18];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s4qqlf0cno16q[45];
extern const uint8_t gm_s5cbc2v9e254r[37];
extern const uint8_t gm_s5ie9tn1qnf6v[9];
extern const uint8_t gm_s5uhm93lpt8jn[11];
extern const uint8_t gm_s63qpv8hba1m1[20];
extern const uint8_t gm_s6mq991cbd8md[53];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s7fl8t4aje2e7[24];
extern const uint8_t gm_s7md88e2dla9a[9];
extern const uint8_t gm_s866ofni9sb8u[7];
extern const uint8_t gm_s87c4jti7ujk5[31];
extern const uint8_t gm_s8e4i8e286ns3[25];
extern const uint8_t gm_s9d990kf69gae[7];
extern const uint8_t gm_s9ehkuquejmat[45];
extern const uint8_t gm_s9grtrvtmi962[8];
extern const uint8_t gm_s9hi985r6f61m[3];
extern const uint8_t gm_sadte2ukivobf[28];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_sal17k00cm1q9[23];
extern const uint8_t gm_salbbur153hif[21];
extern const uint8_t gm_savs0ep448c40[33];
extern const uint8_t gm_sb22ajpcscr7v[41];
extern const uint8_t gm_sc103i8mmkjaq[41];
extern const uint8_t gm_scqusp7lgvp02[4];
extern const uint8_t gm_sd4qrpv3lpilp[4];
extern const uint8_t gm_sd70ob31745fn[4];
extern const uint8_t gm_sd7spmqdlcohl[25];
extern const uint8_t gm_sdd9j9qgq2abj[29];
extern const uint8_t gm_se20eu14i6umf[9];
extern const uint8_t gm_se43ddanqan26[38];
extern const uint8_t gm_sfbr39r30psvt[4];
extern const uint8_t gm_sft2smqlktfvo[19];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgjc0ebf56hf4[8];
extern const uint8_t gm_si62ejtp0vbct[3];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sitlbs33jqqal[2];
extern const uint8_t gm_sj718thiqudru[10];
extern const uint8_t gm_sjc00l2ljprs2[4];
extern const uint8_t gm_sjmccqdb90fnl[3];
extern const uint8_t gm_sk8it0s509dbd[38];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sl34pggfk1a23[38];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_sl6fvbluu7spr[26];
extern const uint8_t gm_slof5tl7m8grj[4];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_smdj5floh97je[10];
extern const uint8_t gm_smerqm8g793l6[2];
extern const uint8_t gm_smjg62rnkmnon[21];
extern const uint8_t gm_smk33hl0qg9hs[4];
extern const uint8_t gm_smk40q3nf1t3o[32];
extern const uint8_t gm_sn9gq20tga4vu[35];
extern const uint8_t gm_snd30h4tliigb[12];
extern const uint8_t gm_so31m6feg820p[4];
extern const uint8_t gm_sphr6fu31dtrl[3];
extern const uint8_t gm_spmgsv94458ho[59];
extern const uint8_t gm_sqdv9jbtf4ujj[46];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0gtuh94b1fce[];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src7hmnlrtmirgt[];
extern const char gm_srckfibkpt681aa[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_ssld67umkdenq[20];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_stnlf4pvbut1c[3];
extern const uint8_t gm_stol2i9gi9a35[18];
extern const uint8_t gm_suvpgtuh49tp7[20];
extern const uint8_t gm_sv6dkmq38siqt[27];
extern const uint8_t gm_sv8qamggba7e1[18];
extern const uint8_t gm_svaet5ugb1fj3[22];
extern const gm_tmpl gm_tpl0epjr4745p10;
extern const gm_tmpl gm_tpl0pva6pvhuq6i;
extern const gm_tmpl gm_tpl1rdf0mm7rjgv;
extern const gm_tmpl gm_tpl20t94jlvdndv;
extern const gm_tmpl gm_tpl31lgghh3mal3;
extern const gm_tmpl gm_tpl344kemfor61g;
extern const gm_tmpl gm_tpl3gep2a57gf23;
extern const gm_tmpl gm_tpl3kfnv3qa5itp;
extern const gm_tmpl gm_tpl4bj6lgqq92ev;
extern const gm_tmpl gm_tpl4upfck3qu2er;
extern const gm_tmpl gm_tpl51u3ki3ilqou;
extern const gm_tmpl gm_tpl5cbugllfe764;
extern const gm_tmpl gm_tpl5ikuibflsuu2;
extern const gm_tmpl gm_tpl6gc9e2atg845;
extern const gm_tmpl gm_tpl737bm31hf71o;
extern const gm_tmpl gm_tpl7kmb2inm0gau;
extern const gm_tmpl gm_tpl7sqjangpsngf;
extern const gm_tmpl gm_tpl8g9lchslmbjf;
extern const gm_tmpl gm_tpl9e18q8to9md2;
extern const gm_tmpl gm_tpl9qg0hlqqke7e;
extern const gm_tmpl gm_tplbmcpcu1c6osb;
extern const gm_tmpl gm_tplcb1495nujsb7;
extern const gm_tmpl gm_tpldnca4vvdfaim;
extern const gm_tmpl gm_tpldofkl5m8hqug;
extern const gm_tmpl gm_tple6jpca094i2j;
extern const gm_tmpl gm_tple7ipevcaonst;
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplfjua3t6jsoe6;
extern const gm_tmpl gm_tplgja2nber62dc;
extern const gm_tmpl gm_tplhojm4dgu514i;
extern const gm_tmpl gm_tpli0tmonaamd45;
extern const gm_tmpl gm_tpli8aunvo88ks7;
extern const gm_tmpl gm_tpljv32on6ui3vn;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplkr4062p8a8bi;
extern const gm_tmpl gm_tpll66vdkdoc48m;
extern const gm_tmpl gm_tpllcjujjfoc50t;
extern const gm_tmpl gm_tpllmofqkiu75lh;
extern const gm_tmpl gm_tplooqfb0umhrq0;
extern const gm_tmpl gm_tplpvfl1p0n8is8;
extern const gm_tmpl gm_tplq0stkp3a31gd;
extern const gm_tmpl gm_tplq7vstpm0lgda;
extern const gm_tmpl gm_tplqbglsd979t5c;
extern const gm_tmpl gm_tplqj931pe0994k;
extern const gm_tmpl gm_tplr35nagqnpmad;
extern const gm_tmpl gm_tplrgtnfea81qcj;
extern const gm_tmpl gm_tplspkvoi1o9of6;
extern const gm_tmpl gm_tplsrmv18emggok;
extern const gm_tmpl gm_tplt56hco5v1mvf;
extern const gm_tmpl gm_tplt68r4kihke54;
extern const gm_tmpl gm_tplv1i7e9gejv6a;
extern const gm_tmpl gm_tplvj9n0kinsqlj;
extern const gm_type gm_tyo86ks9qkvt;
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
inline __attribute__((aligned(4))) void gm_f_format_0gjpujh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0gjpujh F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl6fvbluu7spr));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(25));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4upfck3qu2er), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_1amnh72 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_1mojd22(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_1mojd22 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfbr39r30psvt));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpli8aunvo88ks7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_21ahlbb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_21ahlbb F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sal17k00cm1q9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(22));
    gm_tif3elqk01bdl t3 = GM_LD(gm_tif3elqk01bdl, ((uint8_t *)a0));
    gm_f_format_6hrn7j8((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplrgtnfea81qcj), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_27c89bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_27c89bp F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scqusp7lgvp02));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplr35nagqnpmad), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_368205k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_368205k F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se20eu14i6umf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5ikuibflsuu2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3tnvb6a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_3tnvb6a F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slof5tl7m8grj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7kmb2inm0gau), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_48m8fhm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_48m8fhm F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1tdsk62jj75n));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfjua3t6jsoe6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_4f5imdp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_4f5imdp F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd4qrpv3lpilp));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpli0tmonaamd45), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_61d2klb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_61d2klb F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smk33hl0qg9hs));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplt56hco5v1mvf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7lru3c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7lru3c2 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stol2i9gi9a35));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvj9n0kinsqlj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_adcs7to(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_adcs7to F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s87c4jti7ujk5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(30));
    gm_tif3elqk01bdl t3 = GM_LD(gm_tif3elqk01bdl, ((uint8_t *)a0));
    gm_f_format_1o6fs38((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0epjr4745p10), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_apgk46s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_apgk46s F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s20mq4dk47bdd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl20t94jlvdndv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_c0h6o29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_c0h6o29 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scqusp7lgvp02));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplr35nagqnpmad), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cjnp8ds(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cjnp8ds F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_savs0ep448c40));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_tjftu5su6fh8e t3 = GM_LD(gm_tjftu5su6fh8e, ((uint8_t *)a0));
    gm_f_format_p453i5r((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl6gc9e2atg845), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_dlemvkb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dlemvkb F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd70ob31745fn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9qg0hlqqke7e), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_eqjcikj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_eqjcikj F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smjg62rnkmnon));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(20));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_65ee4ra((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhojm4dgu514i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_f89fec3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_f89fec3 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sk8it0s509dbd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(37));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_74pd3in((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4bj6lgqq92ev), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_h9otq43(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tu0pangbbaeec a0) {
    struct gm_fr_format_h9otq43 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so31m6feg820p));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_6ti8an5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldofkl5m8hqug), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_hjgrmth(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_hjgrmth F;
    gm_tvaas22dl64e0 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tvaas22dl64e0 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smdj5floh97je));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_tvaas22dl64e0 t3 = GM_LD(gm_tvaas22dl64e0, ((uint8_t *)a0));
    gm_f_format_te7kh3c((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplpvfl1p0n8is8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_iqjnutf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_iqjnutf F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_suvpgtuh49tp7));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(19));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1rdf0mm7rjgv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jo1gjr5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jo1gjr5 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8e4i8e286ns3));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(24));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpll66vdkdoc48m), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l0pq86n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l0pq86n F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sv8qamggba7e1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(17));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplv1i7e9gejv6a), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_l9df9v3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l9df9v3 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5cbc2v9e254r));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(36));
    gm_th2d9c7aju8hp t3 = GM_LD(gm_th2d9c7aju8hp, ((uint8_t *)a0));
    gm_f_format_q4qso5a((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplcb1495nujsb7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lqnh3lt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lqnh3lt F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sv6dkmq38siqt));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllmofqkiu75lh), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mdlbfc1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mdlbfc1 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5uhm93lpt8jn));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5cbugllfe764), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mnm0p9v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mnm0p9v F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s1ecdmpgj8faq));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(23));
    gm_tttl9nsdo1iu6 t3 = GM_LD(gm_tttl9nsdo1iu6, ((uint8_t *)a0));
    gm_f_format_nv3e0h2((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl31lgghh3mal3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_nhrpa25(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nhrpa25 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjc00l2ljprs2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl344kemfor61g), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_njau13r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_njau13r F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7md88e2dla9a));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplgja2nber62dc), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_nvhh8d2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nvhh8d2 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ssld67umkdenq));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(19));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplspkvoi1o9of6), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_s4jotpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_s4jotpe F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sj718thiqudru));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl8g9lchslmbjf), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_s8juhic(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_s8juhic F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smk40q3nf1t3o));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(31));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqbglsd979t5c), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_sojmgl9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_sojmgl9 F;
    gm_tvaas22dl64e0 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tvaas22dl64e0 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl5h9isb3bijl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(4));
    gm_tvaas22dl64e0 t3 = GM_LD(gm_tvaas22dl64e0, ((uint8_t *)a0));
    gm_f_format_te7kh3c((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplei8v65ors6jc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_t4gbkjg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t4gbkjg F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sd7spmqdlcohl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(24));
    gm_tftksr4op114e t3 = GM_LD(gm_tftksr4op114e, ((uint8_t *)a0));
    gm_f_format_flhcmdb((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0pva6pvhuq6i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_vmsom9v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vmsom9v F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sft2smqlktfvo));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllcjujjfoc50t), t3);
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fresh_5kokjmg F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 152));
    uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 465, 5);
    uint32_t t7 = r5;
    GM_ST(uint32_t, (((uint8_t *)t2) + 152), t7);
    void * t8 = gm_l0;
    void * t9 = gm_l0;
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 280));
    uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 466, 5);
    uint32_t t13 = r11;
    GM_ST(uint32_t, (((uint8_t *)t8) + 280), t13);
    void * t14 = gm_l0;
    uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 152));
    F.ret = t15;
    goto gm_fret;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_line_l6u87p9 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK(&F, 90u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    gm_b0_1 t3 = gm_f_indent_line_b3mckqa((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t4 = gm_l0;
    void * t5 = (void *)(((uint8_t *)t4) + 104);
    gm_b16_8 t6 = gm_l1;
    gm_tftb1odc739t5 t7 = gm_f_extend_0cs85gs(t5, t6);
    gm_l2 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcmok80ho8hh01, 449, 5);
    void * t9 = gm_l0;
    void * t10 = (void *)(((uint8_t *)t9) + 104);
    gm_tftb1odc739t5 t11 = gm_f_push_t83rg2m(t10, ((uint8_t)10ULL));
    gm_l3 = t11;
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcmok80ho8hh01, 450, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_4bjepb3(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_4bjepb3 F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6mq991cbd8md));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_b0_1 t4 = gm_f_linef_ah3nlha((void *)NULL, cx, NULL, t2, &(*(gm_tif3elqk01bdl *)((uint8_t *)a1)), ((void *)&gm_tpl7sqjangpsngf), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_4ht2lgi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_4ht2lgi F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s63qpv8hba1m1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_b0_1 t4 = gm_f_linef_rho83iv((void *)NULL, cx, NULL, t2, &(*(gm_tftksr4op114e *)((uint8_t *)a1)), ((void *)&gm_tpljv32on6ui3vn), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_5476lg2(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_5476lg2 F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn9gq20tga4vu));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_b0_1 t4 = gm_f_linef_u25b6gb((void *)NULL, cx, NULL, t2, &(*(gm_tud7oovfvnf43 *)((uint8_t *)a1)), ((void *)&gm_tpldnca4vvdfaim), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_6cjbh5j(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_6cjbh5j F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_salbbur153hif));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_b0_1 t4 = gm_f_linef_rho83iv((void *)NULL, cx, NULL, t2, &(*(gm_tftksr4op114e *)((uint8_t *)a1)), ((void *)&gm_tpl737bm31hf71o), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_6mfrmej(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_6mfrmej F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl34pggfk1a23));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
    gm_b0_1 t4 = gm_f_linef_msr3bde((void *)NULL, cx, NULL, t2, &(*(gm_tmr5oep1578tb *)((uint8_t *)a1)), ((void *)&gm_tplsrmv18emggok), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_7cnqmuh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_7cnqmuh F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4p53khe03les));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_b0_1 t4 = gm_f_linef_8r881aa((void *)NULL, cx, NULL, t2, &(*(gm_tttl9nsdo1iu6 *)((uint8_t *)a1)), ((void *)&gm_tplqj931pe0994k), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_7e7j1ug(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_7e7j1ug F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s35qfb045sqkh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(58));
    gm_b0_1 t4 = gm_f_linef_os35pa0((void *)NULL, cx, NULL, t2, &(*(gm_ttq7m9ctqad93 *)((uint8_t *)a1)), ((void *)&gm_tpl3gep2a57gf23), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ar0alse(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_ar0alse F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdd9j9qgq2abj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_b0_1 t4 = gm_f_linef_qk177qj((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplq7vstpm0lgda), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_c0cfb71(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_c0cfb71 F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7fl8t4aje2e7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(23));
    gm_b0_1 t4 = gm_f_linef_pgpsu01((void *)NULL, cx, NULL, t2, &(*(gm_tif3elqk01bdl *)((uint8_t *)a1)), ((void *)&gm_tplbmcpcu1c6osb), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_di76emp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_di76emp F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqdv9jbtf4ujj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(45));
    gm_b0_1 t4 = gm_f_linef_sbd4ln9((void *)NULL, cx, NULL, t2, &(*(gm_tud7oovfvnf43 *)((uint8_t *)a1)), ((void *)&gm_tpl9e18q8to9md2), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_gjfdarm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_gjfdarm F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se43ddanqan26));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
    gm_b0_1 t4 = gm_f_linef_msr3bde((void *)NULL, cx, NULL, t2, &(*(gm_tmr5oep1578tb *)((uint8_t *)a1)), ((void *)&gm_tplooqfb0umhrq0), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_nac2kse(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_nac2kse F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9ehkuquejmat));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_b0_1 t4 = gm_f_linef_mrrdu75((void *)NULL, cx, NULL, t2, &(*(gm_t7neromnu8t62 *)((uint8_t *)a1)), ((void *)&gm_tplt68r4kihke54), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ou0dv81(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_ou0dv81 F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc103i8mmkjaq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
    gm_b0_1 t4 = gm_f_linef_1jnit4p((void *)NULL, cx, NULL, t2, &(*(gm_t89vrq05jgle5 *)((uint8_t *)a1)), ((void *)&gm_tplq0stkp3a31gd), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_pai11tb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_pai11tb F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spmgsv94458ho));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(58));
    gm_b0_1 t4 = gm_f_linef_p9t398q((void *)NULL, cx, NULL, t2, &(*(gm_tte9sujb6sb62 *)((uint8_t *)a1)), ((void *)&gm_tple7ipevcaonst), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qno93os(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_qno93os F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3lqm9addfamn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_b0_1 t4 = gm_f_linef_ao39327((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplkr4062p8a8bi), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_sk93pav(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_sk93pav F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sadte2ukivobf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
    gm_b0_1 t4 = gm_f_linef_qd0v194((void *)NULL, cx, NULL, t2, &(*(gm_th2d9c7aju8hp *)((uint8_t *)a1)), ((void *)&gm_tpl3kfnv3qa5itp), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ssnaj62(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_ssnaj62 F;
    void * gm_l0; /* fg */
    void * gm_l2; /* out */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4qqlf0cno16q));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_b0_1 t4 = gm_f_linef_t97l5gi((void *)NULL, cx, NULL, t2, &(*(gm_tud7oovfvnf43 *)((uint8_t *)a1)), ((void *)&gm_tpl51u3ki3ilqou), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_uu8i6ue F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    void * gm_l2; /* out */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5ie9tn1qnf6v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_b0_1 t4 = gm_f_linef_slpvrc7((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tple6jpca094i2j), t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bytes_eocneng F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 8) + 8));
    F.ret = t4;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_slice_len_a3383br F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* addr */
    GM_STACK_CHECK(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    gm_b16_8 t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = gm_f_ptr_bytes_eocneng(t3);
    gm_t3877pk7lv748 t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 16), t4);
    gm_f_format_0gjpujh((void *)&gm_l2, (void *)NULL, cx, NULL, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_src0gtuh94b1fce, 291, 72);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_slice_ptr_hojfgr5 F;
    gm_b16_8 gm_l0; /* addr */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l1;
    gm_b16_8 t2 = gm_l0;
    gm_tnpifv20irnph t3;
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
    gm_f_format_7lru3c2((void *)&gm_l1, (void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t4) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_src0gtuh94b1fce, 289, 60);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_8ekedsl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    void * t5 = (void *)(((uint8_t *)t4) + 8);
    gm_t88rcucg1b7uk t6 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t7 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t2, t6);
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
inline __attribute__((aligned(4))) void gm_f_fn_29f0d3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_29f0d3k F;
    gm_b16_8 gm_l0; /* i */
    gm_b16_8 gm_l1; /* x */
    gm_b16_8 gm_l2; /* pbv */
    gm_b16_8 gm_l3; /* y */
    gm_b16_8 gm_l4; /* f */
    gm_tuglhjjmj2g58 gm_l5; /* ii */
    gm_b16_8 gm_l6; /* min */
    GM_STACK_CHECK(&F, 1018u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tqcc6dmlne11b gm_l9;
    gm_tabb710d9mq2e gm_l10;
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
    gm_t88rcucg1b7uk t5 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l7));
    uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l12));
    gm_b16_8 t8 = gm_l0;
    gm_b16_8 t9 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t4, t5, t6, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t9;
    {
        {
            uint8_t t10 = (uint8_t)((((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l13))[16] != 0);
            if (!t10) {
                goto gm_lb2;
            }
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l13));
            gm_l2 = t11;
            void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
            gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l7));
            uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l8));
            gm_b16_8 t15 = gm_l2;
            gm_b16_8 t16 = gm_l0;
            gm_b16_8 t17 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t12, t13, t14, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t17;
            gm_tvdg94bk7aa22 t18 = GM_LD(gm_tvdg94bk7aa22, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l3));
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&t18) + 0));
            uint8_t t20 = (uint8_t)((t19 == ((uint8_t)53ULL)));
            gm_tvdg94bk7aa22 t21;
            GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t21 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)53ULL));
            if (t20) {
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s12ng38ve3205));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(7));
                gm_l7 = t22;
            } else {
                gm_b16_8 t23;
                GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sgjc0ebf56hf4));
                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(7));
                gm_l7 = t23;
            }
            gm_b16_8 t24 = gm_l7;
            gm_l4 = t24;
            void * t25 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
            gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l7));
            gm_b16_8 t27 = gm_l4;
            gm_b16_8 t28 = gm_l1;
            gm_b16_8 t29 = gm_l3;
            gm_tfeab279c16rr t30;
            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t27);
            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 16), t28);
            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 32), t29);
            gm_f_format_lqnh3lt((void *)&gm_l8, (void *)NULL, cx, NULL, &t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_src7hmnlrtmirgt, 338, 44);
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            gm_b16_8 t34 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t25, t26, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 32, 0);
            ((uint8_t *)&t35)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t35 + 8), t34);
            memmove(gm_out, ((uint8_t *)&t35), 40);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t36 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
    gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l7));
    gm_f_int_info_2tcdf1n((void *)&gm_l9, (void *)NULL, cx, NULL, t36, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t38 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
    if (!t38) gm_trap("unwrap of null", gm_src7hmnlrtmirgt, 340, 19);
    gm_tuglhjjmj2g58 t39 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l9));
    gm_l5 = t39;
    void * t40 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
    gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l7));
    gm_tuglhjjmj2g58 t42 = gm_l5;
    gm_tbci5ospk5nnn t43;
    gm_f_ii_min_lku7pdi((void *)&t43, (void *)NULL, cx, NULL, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t44 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t40, t41, &t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t44;
    void * t45 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l0));
    gm_b16_8 t46 = gm_l1;
    gm_b16_8 t47 = gm_l6;
    gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vec_minmax_abs_4le8bpj *)((&F.h)->up))->l15));
    gm_tfeab279c16rr t49;
    GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t46);
    GM_ST(gm_b16_8, ((uint8_t *)&t49 + 16), t47);
    GM_ST(gm_b16_8, ((uint8_t *)&t49 + 32), t48);
    gm_b0_1 t50 = gm_f_linef_qno93os((void *)NULL, cx, NULL, t45, &t49);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t51 = gm_l1;
    gm_b16_8 t52 = gm_l1;
    gm_b16_8 t53 = gm_l1;
    gm_tfeab279c16rr t54;
    GM_ST(gm_b16_8, ((uint8_t *)&t54 + 0), t51);
    GM_ST(gm_b16_8, ((uint8_t *)&t54 + 16), t52);
    GM_ST(gm_b16_8, ((uint8_t *)&t54 + 32), t53);
    gm_f_format_iqjnutf((void *)&gm_l10, (void *)NULL, cx, NULL, &t54);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src7hmnlrtmirgt, 343, 20);
    gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
    gm_t5o0tpv5jka2l t58;
    GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t58 + 24, 0);
    GM_ST(uint64_t, (uint8_t *)&t58 + 32, 0);
    ((uint8_t *)&t58)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t57);
    memmove(gm_out, ((uint8_t *)&t58), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_fn_c4j9irb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_c4j9irb F;
    gm_b16_8 gm_l0; /* i */
    gm_b16_8 gm_l1; /* x */
    gm_b16_8 gm_l2; /* y */
    GM_STACK_CHECK(&F, 415u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t5o0tpv5jka2l gm_l3;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l0));
    gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l8));
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l9));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l16));
    gm_b16_8 t6 = gm_l0;
    gm_b16_8 t7 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t2, t3, t4, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t7;
    void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l0));
    gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l8));
    uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l9));
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l17));
    gm_b16_8 t12 = gm_l0;
    gm_b16_8 t13 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t8, t9, t10, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t13;
    void * t14 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l0));
    gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l1));
    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l8));
    gm_ts6oqppjh0ba3 t17 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l3));
    gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l8));
    gm_b16_8 t19 = gm_l1;
    gm_b16_8 t20 = gm_l2;
    gm_f_int_binary_kp3u7i8((void *)&gm_l3, (void *)NULL, cx, NULL, t14, t15, t16, t17, t18, t19, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t21) {
    } else {
        gm_t08olq97rscn3 t22 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l3) + 8));
        gm_t5o0tpv5jka2l t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        ((uint8_t *)&t23)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t23 + 8), t22);
        memmove(gm_out, ((uint8_t *)&t23), 40);
        goto gm_fdone;
    }
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_t5o0tpv5jka2l t25;
    GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t25 + 24, 0);
    GM_ST(uint64_t, (uint8_t *)&t25 + 32, 0);
    ((uint8_t *)&t25)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 8), t24);
    memmove(gm_out, ((uint8_t *)&t25), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_fn_hfolh19(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_hfolh19 F;
    gm_b16_8 gm_l0; /* i */
    gm_b16_8 gm_l1; /* x */
    GM_STACK_CHECK(&F, 331u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t5o0tpv5jka2l gm_l2;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l0));
    gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l8));
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l9));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l15));
    gm_b16_8 t6 = gm_l0;
    gm_b16_8 t7 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t2, t3, t4, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t7;
    void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l0));
    gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l1));
    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l8));
    gm_ts6oqppjh0ba3 t11 = GM_LD(gm_ts6oqppjh0ba3, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l3));
    gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l8));
    gm_b16_8 t13 = gm_l1;
    gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vector_binary_value_u702bne *)((&F.h)->up))->l5));
    gm_f_int_binary_kp3u7i8((void *)&gm_l2, (void *)NULL, cx, NULL, t8, t9, t10, t11, t12, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t15) {
    } else {
        gm_t08olq97rscn3 t16 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l2) + 8));
        gm_t5o0tpv5jka2l t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
        ((uint8_t *)&t17)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t17 + 8), t16);
        memmove(gm_out, ((uint8_t *)&t17), 40);
        goto gm_fdone;
    }
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
    gm_t5o0tpv5jka2l t19;
    GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t19 + 24, 0);
    GM_ST(uint64_t, (uint8_t *)&t19 + 32, 0);
    ((uint8_t *)&t19)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t19 + 8), t18);
    memmove(gm_out, ((uint8_t *)&t19), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_fn_j0gskii(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_j0gskii F;
    gm_b16_8 gm_l0; /* i */
    gm_b16_8 gm_l1; /* y */
    GM_STACK_CHECK(&F, 332u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t5o0tpv5jka2l gm_l2;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_unary_value_o07noaa *)((&F.h)->up))->l0));
    gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_unary_value_o07noaa *)((&F.h)->up))->l7));
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_vector_unary_value_o07noaa *)((&F.h)->up))->l8));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_vector_unary_value_o07noaa *)((&F.h)->up))->l9));
    gm_b16_8 t6 = gm_l0;
    gm_b16_8 t7 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t2, t3, t4, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t7;
    void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_vector_unary_value_o07noaa *)((&F.h)->up))->l0));
    gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_vector_unary_value_o07noaa *)((&F.h)->up))->l1));
    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_unary_value_o07noaa *)((&F.h)->up))->l7));
    gm_t9qkh0rsooc8d t11;
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
    gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_vector_unary_value_o07noaa *)((&F.h)->up))->l7));
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sicgmpca63jln));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(1));
    gm_b16_8 t14 = gm_l1;
    gm_ts6oqppjh0ba3 t15;
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t11);
    gm_f_int_binary_kp3u7i8((void *)&gm_l2, (void *)NULL, cx, NULL, t8, t9, t10, t15, t12, t13, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t16) {
    } else {
        gm_t08olq97rscn3 t17 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l2) + 8));
        gm_t5o0tpv5jka2l t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        ((uint8_t *)&t18)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t18 + 8), t17);
        memmove(gm_out, ((uint8_t *)&t18), 40);
        goto gm_fdone;
    }
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
    gm_t5o0tpv5jka2l t20;
    GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t20 + 24, 0);
    GM_ST(uint64_t, (uint8_t *)&t20 + 32, 0);
    ((uint8_t *)&t20)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t20 + 8), t19);
    memmove(gm_out, ((uint8_t *)&t20), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) uint8_t gm_f_is_vector_builtin_njc3mga(gm_tvdg94bk7aa22 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_vector_builtin_njc3mga F;
    gm_tvdg94bk7aa22 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    {
        {
            {
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)40ULL));
                    if (!t31) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t33 = (uint8_t)(t32 == ((uint8_t)41ULL));
                    if (!t33) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t35 = (uint8_t)(t34 == ((uint8_t)42ULL));
                    if (!t35) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)43ULL));
                    if (!t37) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t39 = (uint8_t)(t38 == ((uint8_t)44ULL));
                    if (!t39) {
                        goto gm_lb8;
                    }
                    goto gm_lb3;
                }
gm_lb8:;
                {
                    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)45ULL));
                    if (!t41) {
                        goto gm_lb9;
                    }
                    goto gm_lb3;
                }
gm_lb9:;
                {
                    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)46ULL));
                    if (!t43) {
                        goto gm_lb10;
                    }
                    goto gm_lb3;
                }
gm_lb10:;
                {
                    uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t45 = (uint8_t)(t44 == ((uint8_t)47ULL));
                    if (!t45) {
                        goto gm_lb11;
                    }
                    goto gm_lb3;
                }
gm_lb11:;
                {
                    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)48ULL));
                    if (!t47) {
                        goto gm_lb12;
                    }
                    goto gm_lb3;
                }
gm_lb12:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)49ULL));
                    if (!t49) {
                        goto gm_lb15;
                    }
                    goto gm_lb14;
                }
gm_lb15:;
                {
                    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)50ULL));
                    if (!t51) {
                        goto gm_lb16;
                    }
                    goto gm_lb14;
                }
gm_lb16:;
                {
                    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t53 = (uint8_t)(t52 == ((uint8_t)51ULL));
                    if (!t53) {
                        goto gm_lb17;
                    }
                    goto gm_lb14;
                }
gm_lb17:;
                {
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t55 = (uint8_t)(t54 == ((uint8_t)52ULL));
                    if (!t55) {
                        goto gm_lb18;
                    }
                    goto gm_lb14;
                }
gm_lb18:;
                {
                    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t57 = (uint8_t)(t56 == ((uint8_t)53ULL));
                    if (!t57) {
                        goto gm_lb19;
                    }
                    goto gm_lb14;
                }
gm_lb19:;
                {
                    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t59 = (uint8_t)(t58 == ((uint8_t)54ULL));
                    if (!t59) {
                        goto gm_lb20;
                    }
                    goto gm_lb14;
                }
gm_lb20:;
                {
                    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t61 = (uint8_t)(t60 == ((uint8_t)55ULL));
                    if (!t61) {
                        goto gm_lb21;
                    }
                    goto gm_lb14;
                }
gm_lb21:;
                {
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)56ULL));
                    if (!t63) {
                        goto gm_lb22;
                    }
                    goto gm_lb14;
                }
gm_lb22:;
                goto gm_lb13;
            }
gm_lb14:;
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb13:;
        {
            {
                {
                    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t65 = (uint8_t)(t64 == ((uint8_t)57ULL));
                    if (!t65) {
                        goto gm_lb25;
                    }
                    goto gm_lb24;
                }
gm_lb25:;
                {
                    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t67 = (uint8_t)(t66 == ((uint8_t)58ULL));
                    if (!t67) {
                        goto gm_lb26;
                    }
                    goto gm_lb24;
                }
gm_lb26:;
                {
                    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t69 = (uint8_t)(t68 == ((uint8_t)59ULL));
                    if (!t69) {
                        goto gm_lb27;
                    }
                    goto gm_lb24;
                }
gm_lb27:;
                goto gm_lb23;
            }
gm_lb24:;
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb23:;
        {
            gm_l1 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb28:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 230, 53);
    }
gm_lb1:;
    uint8_t t70 = gm_l1;
    F.ret = t70;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_blend_2oerv54(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_vec_blend_2oerv54 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* m */
    gm_b16_8 gm_l3; /* a */
    gm_b16_8 gm_l4; /* b */
    gm_tismvprs8r13f gm_l5; /* vp */
    gm_b16_8 gm_l6; /* w */
    gm_b16_8 gm_l7; /* u */
    gm_b16_8 gm_l8; /* s */
    gm_b16_8 gm_l9; /* mm */
    GM_STACK_CHECK(&F, 812u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l10;
    gm_tabb710d9mq2e gm_l11;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_vec_parts_srl0rr9((void *)&gm_l5, t2, GM_CONV(gm_csguankko4lmhd, t3));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
    gm_tqs5p4n2tq19v t7;
    GM_ST(uint8_t, ((uint8_t *)&t7), ((uint8_t)1));
    ((uint8_t *)&t7)[1] = 1;
    gm_b16_8 t8 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t4, t5, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t8;
    void * t9 = gm_l0;
    uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
    gm_tqs5p4n2tq19v t12;
    GM_ST(uint8_t, ((uint8_t *)&t12), ((uint8_t)0));
    ((uint8_t *)&t12)[1] = 1;
    gm_b16_8 t13 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t9, t10, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t13;
    void * t14 = gm_l0;
    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
    gm_tqs5p4n2tq19v t17;
    memset(&t17, 0, sizeof t17);
    gm_b16_8 t18 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t14, t15, t16, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t18;
    void * t19 = gm_l0;
    uint32_t t20 = gm_f_fresh_5kokjmg(t19);
    gm_t88rcucg1b7uk t21;
    GM_ST(uint32_t, ((uint8_t *)&t21 + 0), t20);
    gm_f_format_apgk46s((void *)&gm_l10, (void *)NULL, cx, NULL, t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src7hmnlrtmirgt, 224, 11);
    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_l9 = t23;
    void * t24 = gm_l0;
    gm_b16_8 t25 = gm_l7;
    gm_b16_8 t26 = gm_l9;
    gm_b16_8 t27 = gm_l7;
    gm_b16_8 t28 = gm_l2;
    gm_b16_8 t29 = gm_l6;
    gm_tud7oovfvnf43 t30;
    GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t25);
    GM_ST(gm_b16_8, ((uint8_t *)&t30 + 16), t26);
    GM_ST(gm_b16_8, ((uint8_t *)&t30 + 32), t27);
    GM_ST(gm_b16_8, ((uint8_t *)&t30 + 48), t28);
    GM_ST(gm_b16_8, ((uint8_t *)&t30 + 64), t29);
    gm_b0_1 t31 = gm_f_linef_di76emp((void *)NULL, cx, NULL, t24, &t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t32 = gm_l8;
    gm_b16_8 t33 = gm_l7;
    gm_b16_8 t34 = gm_l3;
    gm_b16_8 t35 = gm_l9;
    gm_b16_8 t36 = gm_l7;
    gm_b16_8 t37 = gm_l4;
    gm_b16_8 t38 = gm_l9;
    gm_th2d9c7aju8hp t39;
    GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t32);
    GM_ST(gm_b16_8, ((uint8_t *)&t39 + 16), t33);
    GM_ST(gm_b16_8, ((uint8_t *)&t39 + 32), t34);
    GM_ST(gm_b16_8, ((uint8_t *)&t39 + 48), t35);
    GM_ST(gm_b16_8, ((uint8_t *)&t39 + 64), t36);
    GM_ST(gm_b16_8, ((uint8_t *)&t39 + 80), t37);
    GM_ST(gm_b16_8, ((uint8_t *)&t39 + 96), t38);
    gm_f_format_l9df9v3((void *)&gm_l11, (void *)NULL, cx, NULL, &t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_src7hmnlrtmirgt, 226, 12);
    gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
    gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
    F.ret = t42;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_compare_mask_1e833o5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_vec_compare_mask_1e833o5 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* sym */
    gm_b16_8 gm_l3; /* a */
    gm_b16_8 gm_l4; /* b */
    gm_tismvprs8r13f gm_l5; /* vp */
    void * gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* bool_ty */
    gm_b16_8 gm_l8; /* wide */
    gm_b16_8 gm_l9; /* m8 */
    gm_b16_8 gm_l10; /* c */
    GM_STACK_CHECK(&F, 750u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l11;
    gm_tabb710d9mq2e gm_l12;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_vec_parts_srl0rr9((void *)&gm_l5, t2, GM_CONV(gm_csguankko4lmhd, t3));
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = GM_LD(void *, (((uint8_t *)t5) + 8));
    void * t7 = (void *)(((uint8_t *)t6) + 8);
    gm_l6 = t7;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    void * t10 = (void *)(((uint8_t *)t9) + 8);
    gm_t88rcucg1b7uk t11 = gm_f_bool_ty_kotvo8t((void *)NULL, cx, NULL, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t11;
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
    gm_tqs5p4n2tq19v t15;
    GM_ST(uint8_t, ((uint8_t *)&t15), ((uint8_t)1));
    ((uint8_t *)&t15)[1] = 1;
    gm_b16_8 t16 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t12, t13, t14, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t16;
    void * t17 = gm_l0;
    uint64_t t18 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    gm_t88rcucg1b7uk t19 = gm_l7;
    gm_tqs5p4n2tq19v t20;
    memset(&t20, 0, sizeof t20);
    gm_b16_8 t21 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t17, t18, t19, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t21;
    void * t22 = gm_l0;
    uint32_t t23 = gm_f_fresh_5kokjmg(t22);
    gm_t88rcucg1b7uk t24;
    GM_ST(uint32_t, ((uint8_t *)&t24 + 0), t23);
    gm_f_format_3tnvb6a((void *)&gm_l11, (void *)NULL, cx, NULL, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_src7hmnlrtmirgt, 212, 10);
    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
    gm_l10 = t26;
    void * t27 = gm_l0;
    gm_b16_8 t28 = gm_l8;
    gm_b16_8 t29 = gm_l10;
    gm_b16_8 t30 = gm_l8;
    gm_b16_8 t31 = gm_l3;
    gm_b16_8 t32 = gm_l2;
    gm_b16_8 t33 = gm_l4;
    gm_tif3elqk01bdl t34;
    GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t28);
    GM_ST(gm_b16_8, ((uint8_t *)&t34 + 16), t29);
    GM_ST(gm_b16_8, ((uint8_t *)&t34 + 32), t30);
    GM_ST(gm_b16_8, ((uint8_t *)&t34 + 48), t31);
    GM_ST(gm_b16_8, ((uint8_t *)&t34 + 64), t32);
    GM_ST(gm_b16_8, ((uint8_t *)&t34 + 80), t33);
    gm_b0_1 t35 = gm_f_linef_c0cfb71((void *)NULL, cx, NULL, t27, &t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t36 = gm_l0;
    void * t37 = gm_l6;
    uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    gm_t88rcucg1b7uk t39 = gm_l7;
    gm_t88rcucg1b7uk t40 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t37, t38, t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t41 = gm_l10;
    gm_b16_8 t42 = gm_l9;
    gm_t6j6smj7qoctu t43;
    GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t41);
    GM_ST(gm_b16_8, ((uint8_t *)&t43 + 16), t42);
    gm_f_format_f89fec3((void *)&gm_l12, (void *)NULL, cx, NULL, &t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src7hmnlrtmirgt, 214, 50);
    gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
    gm_b16_8 t47 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t36, t40, t46);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t47;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_compare_vrcged3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tvdg94bk7aa22 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_vec_compare_vrcged3 F;
    void * gm_l0; /* fg */
    gm_tvdg94bk7aa22 gm_l1; /* op */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* a */
    gm_b16_8 gm_l4; /* b */
    gm_b16_8 gm_l5; /* va */
    gm_b16_8 gm_l6; /* vb */
    gm_b16_8 gm_l7; /* sym */
    GM_STACK_CHECK(&F, 438u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_b16_8 gm_l8;
    void * t9 = gm_l0;
    gm_t88rcucg1b7uk t10 = gm_l2;
    gm_b16_8 t11 = gm_l3;
    gm_b16_8 t12 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t9, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t12;
    void * t13 = gm_l0;
    gm_t88rcucg1b7uk t14 = gm_l2;
    gm_b16_8 t15 = gm_l4;
    gm_b16_8 t16 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t13, t14, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t16;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)40ULL));
            if (!t18) {
                goto gm_lb2;
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sphr6fu31dtrl));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(2));
            gm_l8 = t19;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)41ULL));
            if (!t21) {
                goto gm_lb3;
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_stnlf4pvbut1c));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(2));
            gm_l8 = t22;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)42ULL));
            if (!t24) {
                goto gm_lb4;
            }
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s1f440st4mnkp));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(1));
            gm_l8 = t25;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)43ULL));
            if (!t27) {
                goto gm_lb5;
            }
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_s4g3cajt15dmi));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(2));
            gm_l8 = t28;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)44ULL));
            if (!t30) {
                goto gm_lb6;
            }
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sm5be4659ulci));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(1));
            gm_l8 = t31;
            goto gm_lb1;
        }
gm_lb6:;
        {
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s02ru83e3fffc));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(2));
            gm_l8 = t32;
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 255, 19);
    }
gm_lb1:;
    gm_b16_8 t33 = gm_l8;
    gm_l7 = t33;
    void * t34 = gm_l0;
    gm_t88rcucg1b7uk t35 = gm_l2;
    gm_b16_8 t36 = gm_l7;
    gm_b16_8 t37 = gm_l5;
    gm_b16_8 t38 = gm_l6;
    gm_b16_8 t39 = gm_f_vec_compare_mask_1e833o5((void *)NULL, cx, NULL, t34, t35, t36, t37, t38);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_convert_94b6r5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_vec_convert_94b6r5u F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* xt */
    gm_b16_8 gm_l3; /* v */
    gm_tismvprs8r13f gm_l4; /* vp */
    gm_b16_8 gm_l5; /* va */
    gm_b16_8 gm_l6; /* tv */
    GM_STACK_CHECK(&F, 363u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l7;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_vec_parts_srl0rr9((void *)&gm_l4, t2, GM_CONV(gm_csguankko4lmhd, t3));
    void * t4 = gm_l0;
    gm_t88rcucg1b7uk t5 = gm_l2;
    gm_b16_8 t6 = gm_l3;
    gm_b16_8 t7 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t4, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t7;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
    gm_tqs5p4n2tq19v t11;
    memset(&t11, 0, sizeof t11);
    gm_b16_8 t12 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t8, t9, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t12;
    void * t13 = gm_l0;
    gm_t88rcucg1b7uk t14 = gm_l1;
    gm_b16_8 t15 = gm_l5;
    gm_b16_8 t16 = gm_l6;
    gm_t6j6smj7qoctu t17;
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t15);
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t16);
    gm_f_format_s8juhic((void *)&gm_l7, (void *)NULL, cx, NULL, &t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_src7hmnlrtmirgt, 372, 28);
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
    gm_b16_8 t21 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t13, t14, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t21;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_lane_at_i7st991(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_vec_lane_at_i7st991 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* lane */
    uint64_t gm_l2; /* bytes */
    gm_b16_8 gm_l3; /* p */
    gm_b16_8 gm_l4; /* i */
    gm_b16_8 gm_l5; /* ce */
    GM_STACK_CHECK(&F, 341u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l6;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t6;
    void * t7 = gm_l0;
    gm_t88rcucg1b7uk t8 = gm_l1;
    gm_b16_8 t9 = gm_l5;
    gm_b16_8 t10 = gm_l3;
    gm_b16_8 t11 = gm_l4;
    uint64_t t12 = gm_l2;
    gm_tttl9nsdo1iu6 t13;
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t9);
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 16), t10);
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 32), t11);
    GM_ST(uint64_t, ((uint8_t *)&t13 + 48), t12);
    gm_f_format_mnm0p9v((void *)&gm_l6, (void *)NULL, cx, NULL, &t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_src7hmnlrtmirgt, 102, 25);
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
    gm_b16_8 t17 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t7, t8, t16);
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
inline __attribute__((aligned(4))) uint64_t gm_f_vec_lane_bytes_c5pmsp6(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vec_lane_bytes_c5pmsp6 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* lane */
    uint16_t gm_l2; /* bits */
    uint16_t gm_l3; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint64_t gm_l4;
    gm_t53at80pfdus3 gm_l5;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = (void *)(((uint8_t *)t7) + 8);
    gm_t88rcucg1b7uk t9 = gm_l1;
    gm_f_kind_ghvav22((void *)&gm_l5, t8, GM_CONV(gm_csguankko4lmhd, t9));
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint16_t t12 = GM_LD(uint16_t, ((((uint8_t *)&gm_l5) + 8) + 2));
            gm_l2 = t12;
            uint16_t t13 = gm_l2;
            uint64_t t14 = (uint64_t)t13;
            if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_src7hmnlrtmirgt, 23, 22);
            uint64_t t15 = (uint64_t)(t14 / ((uint64_t)8ULL));
            gm_l4 = t15;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)3ULL));
            if (!t17) {
                goto gm_lb3;
            }
            uint16_t t18 = GM_LD(uint16_t, (((uint8_t *)&gm_l5) + 8));
            gm_l3 = t18;
            uint16_t t19 = gm_l3;
            uint64_t t20 = (uint64_t)t19;
            if (((uint64_t)8ULL) == 0) gm_trap("division by zero", gm_src7hmnlrtmirgt, 24, 21);
            uint64_t t21 = (uint64_t)(t20 / ((uint64_t)8ULL));
            gm_l4 = t21;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_l4 = ((uint64_t)1ULL);
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 22, 54);
    }
gm_lb1:;
    uint64_t t22 = gm_l4;
    F.ret = t22;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_vec_lane_loop_a3gvooh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, gm_closure a3) {
    struct gm_fr_vec_lane_loop_a3gvooh F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* res_t */
    uint64_t gm_l2; /* n */
    gm_closure gm_l3; /* f */
    gm_tismvprs8r13f gm_l4; /* p */
    gm_b16_8 gm_l5; /* ce */
    gm_b16_8 gm_l6; /* res */
    gm_b16_8 gm_l7; /* i */
    gm_b16_8 gm_l8; /* v */
    GM_STACK_CHECK(&F, 946u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l9;
    gm_t5o0tpv5jka2l gm_l10;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_vec_parts_srl0rr9((void *)&gm_l4, t2, GM_CONV(gm_csguankko4lmhd, t3));
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    void * t6 = (void *)(((uint8_t *)t5) + 8);
    gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
    gm_b16_8 t8 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t8;
    void * t9 = gm_l0;
    gm_t88rcucg1b7uk t10 = gm_l1;
    gm_b16_8 t11 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t11;
    void * t12 = gm_l0;
    uint32_t t13 = gm_f_fresh_5kokjmg(t12);
    gm_t88rcucg1b7uk t14;
    GM_ST(uint32_t, ((uint8_t *)&t14 + 0), t13);
    gm_f_format_c0h6o29((void *)&gm_l9, (void *)NULL, cx, NULL, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src7hmnlrtmirgt, 89, 10);
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
    gm_l7 = t16;
    void * t17 = gm_l0;
    gm_b16_8 t18 = gm_l7;
    gm_b16_8 t19 = gm_l7;
    uint64_t t20 = gm_l2;
    gm_b16_8 t21 = gm_l7;
    gm_tmr5oep1578tb t22;
    GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t18);
    GM_ST(gm_b16_8, ((uint8_t *)&t22 + 16), t19);
    GM_ST(uint64_t, ((uint8_t *)&t22 + 32), t20);
    GM_ST(gm_b16_8, ((uint8_t *)&t22 + 40), t21);
    gm_b0_1 t23 = gm_f_linef_gjfdarm((void *)NULL, cx, NULL, t17, &t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t24 = gm_l0;
    void * t25 = gm_l0;
    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 144));
    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
    if (o28) gm_trap("integer overflow in `+`", gm_src7hmnlrtmirgt, 91, 5);
    uint64_t t29 = r27;
    GM_ST(uint64_t, (((uint8_t *)t24) + 144), t29);
    gm_closure t30 = gm_l3;
    gm_b16_8 t31 = gm_l7;
    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
    gm_closure t33 = t30;
    gm_node *ccx34 = t33.cx;
    gm_node cn35; { gm_node *f = gm_find(cx, 587034433); if (f) { cn35 = *f; cn35.next = ccx34; ccx34 = &cn35; } }
    gm_node cn36; { gm_node *f = gm_find(cx, 2126215853); if (f) { cn36 = *f; cn36.next = ccx34; ccx34 = &cn36; } }
    void * t37 = t33.code;
    gm_t5o0tpv5jka2l t38;
    if (GM_PTR_TAGGED(t37)) {
        t38 = ((gm_t5o0tpv5jka2l (*)(gm_b16_8))GM_PTR_UNTAG(t37))(t32);
    } else {
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))t37)((void *)&t38, t33.env, ccx34, NULL, t32);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t38;
    uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (t39) {
    } else {
        gm_t08olq97rscn3 t40 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l10) + 8));
        gm_t5o0tpv5jka2l t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
        ((uint8_t *)&t41)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t41 + 8), t40);
        memmove(gm_out, ((uint8_t *)&t41), 40);
        goto gm_fdone;
    }
    gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_l8 = t42;
    void * t43 = gm_l0;
    gm_b16_8 t44 = gm_l5;
    gm_b16_8 t45 = gm_l6;
    gm_b16_8 t46 = gm_l7;
    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 16));
    gm_b16_8 t48 = gm_l8;
    gm_t89vrq05jgle5 t49;
    GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t44);
    GM_ST(gm_b16_8, ((uint8_t *)&t49 + 16), t45);
    GM_ST(gm_b16_8, ((uint8_t *)&t49 + 32), t46);
    GM_ST(uint64_t, ((uint8_t *)&t49 + 48), t47);
    GM_ST(gm_b16_8, ((uint8_t *)&t49 + 56), t48);
    gm_b0_1 t50 = gm_f_linef_ou0dv81((void *)NULL, cx, NULL, t43, &t49);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t51 = gm_l0;
    void * t52 = gm_l0;
    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)t52) + 144));
    uint64_t r54; bool o55 = __builtin_sub_overflow(t53, ((uint64_t)1ULL), &r54);
    if (o55) gm_trap("integer overflow in `-`", gm_src7hmnlrtmirgt, 94, 5);
    uint64_t t56 = r54;
    GM_ST(uint64_t, (((uint8_t *)t51) + 144), t56);
    void * t57 = gm_l0;
    gm_b16_8 t58;
    GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s2cuflhqkkuaq));
    GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(1));
    gm_b0_1 t59 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t57, t58);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t60 = gm_l6;
    gm_t5o0tpv5jka2l t61;
    GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t61 + 24, 0);
    GM_ST(uint64_t, (uint8_t *)&t61 + 32, 0);
    ((uint8_t *)&t61)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t61 + 8), t60);
    memmove(gm_out, ((uint8_t *)&t61), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_load_8m1nglt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_vec_load_8m1nglt F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    gm_tismvprs8r13f gm_l3; /* p */
    gm_b16_8 gm_l4; /* vt */
    gm_b16_8 gm_l5; /* pa */
    gm_b16_8 gm_l6; /* name */
    GM_STACK_CHECK(&F, 483u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l7;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_vec_parts_srl0rr9((void *)&gm_l3, t2, GM_CONV(gm_csguankko4lmhd, t3));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
    gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 8));
    gm_tqs5p4n2tq19v t7;
    memset(&t7, 0, sizeof t7);
    gm_b16_8 t8 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t4, t5, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t8;
    void * t9 = gm_l0;
    gm_t88rcucg1b7uk t10 = gm_l1;
    gm_b16_8 t11 = gm_l2;
    gm_b16_8 t12 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t9, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t12;
    void * t13 = gm_l0;
    uint32_t t14 = gm_f_fresh_5kokjmg(t13);
    gm_t88rcucg1b7uk t15;
    GM_ST(uint32_t, ((uint8_t *)&t15 + 0), t14);
    gm_f_format_1mojd22((void *)&gm_l7, (void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_src7hmnlrtmirgt, 69, 13);
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
    gm_l6 = t17;
    void * t18 = gm_l0;
    gm_b16_8 t19 = gm_l4;
    gm_b16_8 t20 = gm_l6;
    gm_b16_8 t21 = gm_l6;
    gm_b16_8 t22 = gm_l5;
    gm_b16_8 t23 = gm_l6;
    gm_tud7oovfvnf43 t24;
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t19);
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 16), t20);
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 32), t21);
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 48), t22);
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 64), t23);
    gm_b0_1 t25 = gm_f_linef_5476lg2((void *)NULL, cx, NULL, t18, &t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t26 = gm_l6;
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
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
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_load_store_ng9gfpc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tvdg94bk7aa22 a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_vec_load_store_ng9gfpc F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tvdg94bk7aa22 gm_l3; /* op */
    gm_b16_8 gm_l4; /* tys */
    gm_b16_8 gm_l5; /* vals */
    uint8_t gm_l6; /* is_load */
    gm_t88rcucg1b7uk gm_l7; /* vt */
    uint64_t gm_l8; /* k */
    gm_tismvprs8r13f gm_l9; /* vp */
    uint64_t gm_l10; /* n */
    gm_b16_8 gm_l11; /* ps */
    gm_b16_8 gm_l12; /* p */
    gm_b16_8 gm_l13; /* len */
    gm_b16_8 gm_l14; /* loc */
    gm_b16_8 gm_l15; /* what */
    uint64_t gm_l16; /* size */
    gm_b16_8 gm_l17; /* pv */
    gm_b16_8 gm_l18; /* res */
    GM_STACK_CHECK(&F, 1149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t88rcucg1b7uk gm_l19;
    uint64_t gm_l20;
    gm_b16_8 gm_l21;
    gm_tvdg94bk7aa22 t2 = gm_l3;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&t2) + 0));
    uint8_t t4 = (uint8_t)((t3 == ((uint8_t)58ULL)));
    gm_tvdg94bk7aa22 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)58ULL));
    gm_l6 = t4;
    uint8_t t6 = gm_l6;
    if (t6) {
        gm_t88rcucg1b7uk t7 = gm_l2;
        gm_l19 = t7;
    } else {
        void * t8 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t9, gm_src7hmnlrtmirgt, 399, 35);
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t8) + (uintptr_t)((uint64_t)0ULL) * 4));
        gm_l19 = t10;
    }
    gm_t88rcucg1b7uk t11 = gm_l19;
    gm_l7 = t11;
    uint8_t t12 = gm_l6;
    if (t12) {
        gm_l20 = ((uint64_t)0ULL);
    } else {
        gm_l20 = ((uint64_t)1ULL);
    }
    uint64_t t13 = gm_l20;
    gm_l8 = t13;
    void * t14 = gm_l0;
    gm_t88rcucg1b7uk t15 = gm_l7;
    gm_f_vec_parts_srl0rr9((void *)&gm_l9, t14, GM_CONV(gm_csguankko4lmhd, t15));
    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
    gm_l10 = t16;
    void * t17 = gm_l0;
    void * t18 = GM_LD(void *, ((uint8_t *)&gm_l4));
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint64_t t20 = gm_l8;
    if ((uintptr_t)t20 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t20, t19, gm_src7hmnlrtmirgt, 403, 22);
    gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t18) + (uintptr_t)t20 * 4));
    void * t22 = GM_LD(void *, ((uint8_t *)&gm_l5));
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t t24 = gm_l8;
    if ((uintptr_t)t24 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t24, t23, gm_src7hmnlrtmirgt, 403, 30);
    gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t22) + (uintptr_t)t24 * 16));
    gm_b16_8 t26 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t17, t21, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t26;
    gm_b16_8 t27 = gm_l11;
    gm_b16_8 t28 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t28;
    void * t29 = gm_l0;
    gm_b16_8 t30 = gm_l11;
    gm_b16_8 t31 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t29, t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l13 = t31;
    void * t32 = gm_l0;
    void * t33 = GM_LD(void *, ((uint8_t *)t32));
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)t33) + 1136));
    if (t34) {
        void * t35 = gm_l0;
        void * t36 = GM_LD(void *, ((uint8_t *)t35));
        gm_tl874t2atnlh3 t37 = gm_l1;
        gm_b16_8 t38 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t38;
        uint8_t t39 = gm_l6;
        if (t39) {
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sqfrv3g5v9m2n));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(4));
            gm_l21 = t40;
        } else {
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s0ugop4l95o1j));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(5));
            gm_l21 = t41;
        }
        gm_b16_8 t42 = gm_l21;
        gm_l15 = t42;
        void * t43 = gm_l0;
        gm_b16_8 t44 = gm_l13;
        uint64_t t45 = gm_l10;
        gm_b16_8 t46 = gm_l15;
        uint64_t t47 = gm_l10;
        gm_b16_8 t48 = gm_l13;
        gm_b16_8 t49 = gm_l14;
        gm_ttq7m9ctqad93 t50;
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t44);
        GM_ST(uint64_t, ((uint8_t *)&t50 + 16), t45);
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 24), t46);
        GM_ST(uint64_t, ((uint8_t *)&t50 + 40), t47);
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 48), t48);
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 64), t49);
        gm_b0_1 t51 = gm_f_linef_7e7j1ug((void *)NULL, cx, NULL, t43, &t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint64_t t52 = gm_l10;
    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 16));
    uint64_t r54; bool o55 = __builtin_mul_overflow(t52, t53, &r54);
    if (o55) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 411, 13);
    uint64_t t56 = r54;
    gm_l16 = t56;
    uint8_t t57 = gm_l6;
    uint8_t t58 = (uint8_t)!t57;
    if (t58) {
        void * t59 = gm_l0;
        gm_t88rcucg1b7uk t60 = gm_l7;
        void * t61 = GM_LD(void *, ((uint8_t *)&gm_l5));
        uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t62, gm_src7hmnlrtmirgt, 413, 30);
        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)t61) + (uintptr_t)((uint64_t)0ULL) * 16));
        gm_b16_8 t64 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t59, t60, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l17 = t64;
        void * t65 = gm_l0;
        gm_b16_8 t66 = gm_l12;
        gm_b16_8 t67 = gm_l17;
        uint64_t t68 = gm_l16;
        gm_tftksr4op114e t69;
        GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t66);
        GM_ST(gm_b16_8, ((uint8_t *)&t69 + 16), t67);
        GM_ST(uint64_t, ((uint8_t *)&t69 + 32), t68);
        gm_b0_1 t70 = gm_f_linef_4ht2lgi((void *)NULL, cx, NULL, t65, &t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t71 = gm_l0;
        gm_b16_8 t72 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t72;
        goto gm_fret;
    }
    void * t73 = gm_l0;
    gm_t88rcucg1b7uk t74 = gm_l7;
    gm_b16_8 t75 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t73, t74);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l18 = t75;
    void * t76 = gm_l0;
    gm_b16_8 t77 = gm_l18;
    gm_b16_8 t78 = gm_l12;
    uint64_t t79 = gm_l16;
    gm_tftksr4op114e t80;
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t77);
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 16), t78);
    GM_ST(uint64_t, ((uint8_t *)&t80 + 32), t79);
    gm_b0_1 t81 = gm_f_linef_6cjbh5j((void *)NULL, cx, NULL, t76, &t80);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t82 = gm_l18;
    F.ret = t82;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_mask_test_9mgimk6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_vec_mask_test_9mgimk6 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* xt */
    gm_b16_8 gm_l4; /* v */
    uint64_t gm_l5; /* n */
    gm_b16_8 gm_l6; /* p */
    gm_b16_8 gm_l7; /* count */
    gm_b16_8 gm_l8; /* r */
    GM_STACK_CHECK(&F, 666u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tismvprs8r13f gm_l9;
    gm_b16_8 gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_tabb710d9mq2e gm_l12;
    void * t6 = gm_l0;
    gm_t88rcucg1b7uk t7 = gm_l3;
    gm_f_vec_parts_srl0rr9((void *)&gm_l9, t6, GM_CONV(gm_csguankko4lmhd, t7));
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
    gm_l5 = t8;
    void * t9 = gm_l0;
    gm_t88rcucg1b7uk t10 = gm_l3;
    gm_b16_8 t11 = gm_l4;
    gm_b16_8 t12 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t9, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t12;
    void * t13 = gm_l0;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    void * t16 = (void *)(((uint8_t *)t15) + 8);
    gm_t88rcucg1b7uk t17 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t18;
    GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sicgmpca63jln));
    GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
    gm_b16_8 t19 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t13, t17, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t19;
    void * t20 = gm_l0;
    uint64_t t21 = gm_l5;
    gm_b16_8 t22 = gm_l7;
    gm_b16_8 t23 = gm_l6;
    gm_t7neromnu8t62 t24;
    GM_ST(uint64_t, ((uint8_t *)&t24 + 0), t21);
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 8), t22);
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 24), t23);
    gm_b0_1 t25 = gm_f_linef_nac2kse((void *)NULL, cx, NULL, t20, &t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)49ULL));
            if (!t27) {
                goto gm_lb2;
            }
            gm_b16_8 t28 = gm_l7;
            uint64_t t29 = gm_l5;
            gm_t3877pk7lv748 t30;
            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t28);
            GM_ST(uint64_t, ((uint8_t *)&t30 + 16), t29);
            gm_f_format_nvhh8d2((void *)&gm_l11, (void *)NULL, cx, NULL, &t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_src7hmnlrtmirgt, 302, 21);
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            gm_l10 = t33;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)50ULL));
            if (!t35) {
                goto gm_lb3;
            }
            gm_b16_8 t36 = gm_l7;
            gm_tnpifv20irnph t37;
            GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
            gm_f_format_vmsom9v((void *)&gm_l12, (void *)NULL, cx, NULL, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src7hmnlrtmirgt, 303, 21);
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            gm_l10 = t40;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_b16_8 t41 = gm_l7;
            gm_l10 = t41;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 301, 17);
    }
gm_lb1:;
    gm_b16_8 t42 = gm_l10;
    gm_l8 = t42;
    void * t43 = gm_l0;
    gm_t88rcucg1b7uk t44 = gm_l1;
    gm_b16_8 t45 = gm_l8;
    gm_b16_8 t46 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t43, t44, t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t46;
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
__attribute__((aligned(4))) void gm_f_vec_minmax_abs_4le8bpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tvdg94bk7aa22 a3, gm_b16_8 a4) {
    struct gm_fr_vec_minmax_abs_4le8bpj F;
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l4; /* vals */
    gm_tismvprs8r13f gm_l5; /* vp */
    uint64_t gm_l6; /* n */
    gm_b16_8 gm_l9; /* a */
    gm_th2lqbo77pvsc gm_l10; /* b */
    uint8_t gm_l11; /* is_abs */
    gm_b16_8 gm_l14; /* bv */
    gm_b16_8 gm_l16; /* va */
    gm_b16_8 gm_l17; /* u */
    gm_b16_8 gm_l18; /* s */
    gm_b16_8 gm_l19; /* r */
    gm_b16_8 gm_l20; /* mask */
    gm_b16_8 gm_l21; /* sh */
    gm_b16_8 gm_l22; /* vb */
    gm_b16_8 gm_l23; /* sym */
    gm_b16_8 gm_l24; /* w */
    gm_b16_8 gm_l25; /* c */
    GM_STACK_CHECK(&F, 2650u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    F.l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l26;
    uint8_t gm_l27;
    uint8_t gm_l28;
    uint8_t gm_l29;
    gm_t5o0tpv5jka2l gm_l30;
    gm_b16_8 gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_b16_8 gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_tabb710d9mq2e gm_l37;
    void * t6 = F.l0;
    gm_t88rcucg1b7uk t7 = gm_l2;
    gm_f_vec_parts_srl0rr9((void *)&gm_l5, t6, GM_CONV(gm_csguankko4lmhd, t7));
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    gm_l6 = t8;
    gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
    F.l7 = t9;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
    F.l8 = t10;
    void * t11 = GM_LD(void *, ((uint8_t *)&gm_l4));
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t12)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t12, gm_src7hmnlrtmirgt, 322, 10);
    gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t11) + (uintptr_t)((uint64_t)0ULL) * 16));
    gm_l9 = t13;
    gm_th2lqbo77pvsc t14;
    memset(&t14, 0, sizeof t14);
    gm_l10 = t14;
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint8_t t16 = (t15 > ((uint64_t)1ULL));
    if (t16) {
        void * t17 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t17) + (uintptr_t)((uint64_t)1ULL) * 16));
        gm_th2lqbo77pvsc t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t20), t19);
        ((uint8_t *)&t20)[16] = 1;
        gm_l10 = t20;
    }
    gm_tvdg94bk7aa22 t21 = F.l3;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
    uint8_t t23 = (uint8_t)((t22 == ((uint8_t)55ULL)));
    gm_tvdg94bk7aa22 t24;
    GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t24 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)55ULL));
    gm_l11 = t23;
    uint8_t t25 = gm_l11;
    uint8_t t26 = (uint8_t)!t25;
    if (t26) {
        void * t27 = F.l0;
        void * t28 = GM_LD(void *, ((uint8_t *)t27));
        void * t29 = (void *)(((uint8_t *)t28) + 8);
        gm_t88rcucg1b7uk t30 = F.l7;
        uint8_t t31 = gm_f_is_float_gthejtb(t29, GM_CONV(gm_csguankko4lmhd, t30));
        gm_l27 = t31;
    } else {
        gm_l27 = ((uint8_t)0);
    }
    uint8_t t32 = gm_l27;
    if (t32) {
        gm_l26 = ((uint8_t)1);
    } else {
        uint8_t t33 = gm_l11;
        if (t33) {
            void * t34 = F.l0;
            void * t35 = GM_LD(void *, ((uint8_t *)t34));
            void * t36 = (void *)(((uint8_t *)t35) + 8);
            gm_t88rcucg1b7uk t37 = F.l7;
            uint8_t t38 = gm_f_is_integer_eo6jkc5(t36, GM_CONV(gm_csguankko4lmhd, t37));
            gm_l29 = t38;
        } else {
            gm_l29 = ((uint8_t)0);
        }
        uint8_t t39 = gm_l29;
        if (t39) {
            void * t40 = F.l0;
            void * t41 = GM_LD(void *, ((uint8_t *)t40));
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)t41) + 1136));
            gm_l28 = t42;
        } else {
            gm_l28 = ((uint8_t)0);
        }
        uint8_t t43 = gm_l28;
        gm_l26 = t43;
    }
    uint8_t t44 = gm_l26;
    if (t44) {
        void * t45 = F.l0;
        gm_t88rcucg1b7uk t46 = gm_l2;
        gm_b16_8 t47 = gm_l9;
        gm_b16_8 t48 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t45, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l12 = t48;
        gm_th2lqbo77pvsc t49;
        memset(&t49, 0, sizeof t49);
        F.l13 = t49;
        {
            {
                uint8_t t50 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
                if (!t50) {
                    goto gm_lb2;
                }
                gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
                gm_l14 = t51;
                void * t52 = F.l0;
                gm_t88rcucg1b7uk t53 = gm_l2;
                gm_b16_8 t54 = gm_l14;
                gm_b16_8 t55 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t52, t53, t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t56;
                GM_ST(uint8_t, (uint8_t *)&t56 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t56 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t56 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t56), t55);
                ((uint8_t *)&t56)[16] = 1;
                F.l13 = t56;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t57 = F.l0;
        void * t58 = GM_LD(void *, ((uint8_t *)t57));
        gm_tl874t2atnlh3 t59 = gm_l1;
        gm_b16_8 t60 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t58, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l15 = t60;
        void * t61 = F.l0;
        gm_t88rcucg1b7uk t62 = gm_l2;
        uint64_t t63 = gm_l6;
        gm_closure t64 = ((gm_closure){(void *)gm_f_fn_29f0d3k, (void *)&F.h, cx});
        gm_f_vec_lane_loop_a3gvooh((void *)&gm_l30, (void *)NULL, cx, NULL, t61, t62, t63, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
        if (t65) {
        } else {
            gm_t08olq97rscn3 t66 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l30) + 8));
            gm_t5o0tpv5jka2l t67;
            GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
            ((uint8_t *)&t67)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t67 + 8), t66);
            memmove(gm_out, ((uint8_t *)&t67), 40);
            goto gm_fdone;
        }
        gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
        gm_t5o0tpv5jka2l t69;
        GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t69 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t69 + 32, 0);
        ((uint8_t *)&t69)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t69 + 8), t68);
        memmove(gm_out, ((uint8_t *)&t69), 40);
        goto gm_fdone;
    }
    void * t70 = F.l0;
    gm_t88rcucg1b7uk t71 = gm_l2;
    gm_b16_8 t72 = gm_l9;
    gm_b16_8 t73 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t70, t71, t72);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l16 = t73;
    void * t74 = F.l0;
    uint64_t t75 = gm_l6;
    gm_t88rcucg1b7uk t76 = F.l7;
    gm_tqs5p4n2tq19v t77;
    GM_ST(uint8_t, ((uint8_t *)&t77), ((uint8_t)0));
    ((uint8_t *)&t77)[1] = 1;
    gm_b16_8 t78 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t74, t75, t76, t77);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l17 = t78;
    void * t79 = F.l0;
    uint64_t t80 = gm_l6;
    gm_t88rcucg1b7uk t81 = F.l7;
    gm_tqs5p4n2tq19v t82;
    memset(&t82, 0, sizeof t82);
    gm_b16_8 t83 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t79, t80, t81, t82);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l18 = t83;
    gm_b16_8 t84;
    GM_ST(void *, (uint8_t *)&t84, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(0));
    gm_l19 = t84;
    {
        {
            uint8_t t85 = gm_l11;
            if (!t85) {
                goto gm_lb4;
            }
            void * t86 = F.l0;
            void * t87 = GM_LD(void *, ((uint8_t *)t86));
            void * t88 = (void *)(((uint8_t *)t87) + 8);
            gm_t88rcucg1b7uk t89 = F.l7;
            uint8_t t90 = gm_f_is_float_gthejtb(t88, GM_CONV(gm_csguankko4lmhd, t89));
            if (!t90) {
                goto gm_lb4;
            }
            uint64_t t91 = F.l8;
            uint8_t t92 = ((t91 == ((uint64_t)4ULL)));
            if (t92) {
                gm_b16_8 t93;
                GM_ST(void *, (uint8_t *)&t93, (void *)(gm_snd30h4tliigb));
                GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(11));
                gm_l31 = t93;
            } else {
                gm_b16_8 t94;
                GM_ST(void *, (uint8_t *)&t94, (void *)(gm_svaet5ugb1fj3));
                GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(21));
                gm_l31 = t94;
            }
            gm_b16_8 t95 = gm_l31;
            gm_l20 = t95;
            gm_b16_8 t96 = gm_l18;
            gm_b16_8 t97 = gm_l17;
            gm_b16_8 t98 = gm_l16;
            gm_b16_8 t99 = gm_l20;
            gm_t0uuhaljsf410 t100;
            GM_ST(gm_b16_8, ((uint8_t *)&t100 + 0), t96);
            GM_ST(gm_b16_8, ((uint8_t *)&t100 + 16), t97);
            GM_ST(gm_b16_8, ((uint8_t *)&t100 + 32), t98);
            GM_ST(gm_b16_8, ((uint8_t *)&t100 + 48), t99);
            gm_f_format_l0pq86n((void *)&gm_l32, (void *)NULL, cx, NULL, &t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
            if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_src7hmnlrtmirgt, 352, 13);
            gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
            gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
            gm_l19 = t103;
            goto gm_lb3;
        }
gm_lb4:;
        uint8_t t104 = gm_l11;
        if (t104) {
            void * t105 = F.l0;
            uint32_t t106 = gm_f_fresh_5kokjmg(t105);
            gm_t88rcucg1b7uk t107;
            GM_ST(uint32_t, ((uint8_t *)&t107 + 0), t106);
            gm_f_format_61d2klb((void *)&gm_l33, (void *)NULL, cx, NULL, t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
            if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_src7hmnlrtmirgt, 354, 15);
            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
            gm_l21 = t109;
            void * t110 = F.l0;
            gm_b16_8 t111 = gm_l18;
            gm_b16_8 t112 = gm_l21;
            gm_b16_8 t113 = gm_l16;
            uint64_t t114 = F.l8;
            uint64_t r115; bool o116 = __builtin_mul_overflow(t114, ((uint64_t)8ULL), &r115);
            if (o116) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 355, 50);
            uint64_t t117 = r115;
            uint64_t r118; bool o119 = __builtin_sub_overflow(t117, ((uint64_t)1ULL), &r118);
            if (o119) gm_trap("integer overflow in `-`", gm_src7hmnlrtmirgt, 355, 50);
            uint64_t t120 = r118;
            gm_tttl9nsdo1iu6 t121;
            GM_ST(gm_b16_8, ((uint8_t *)&t121 + 0), t111);
            GM_ST(gm_b16_8, ((uint8_t *)&t121 + 16), t112);
            GM_ST(gm_b16_8, ((uint8_t *)&t121 + 32), t113);
            GM_ST(uint64_t, ((uint8_t *)&t121 + 48), t120);
            gm_b0_1 t122 = gm_f_linef_7cnqmuh((void *)NULL, cx, NULL, t110, &t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t123 = gm_l18;
            gm_b16_8 t124 = gm_l17;
            gm_b16_8 t125 = gm_l16;
            gm_b16_8 t126 = gm_l21;
            gm_b16_8 t127 = gm_l17;
            gm_b16_8 t128 = gm_l21;
            gm_tif3elqk01bdl t129;
            GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t123);
            GM_ST(gm_b16_8, ((uint8_t *)&t129 + 16), t124);
            GM_ST(gm_b16_8, ((uint8_t *)&t129 + 32), t125);
            GM_ST(gm_b16_8, ((uint8_t *)&t129 + 48), t126);
            GM_ST(gm_b16_8, ((uint8_t *)&t129 + 64), t127);
            GM_ST(gm_b16_8, ((uint8_t *)&t129 + 80), t128);
            gm_f_format_adcs7to((void *)&gm_l34, (void *)NULL, cx, NULL, &t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
            if (!t130) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_src7hmnlrtmirgt, 356, 13);
            gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
            gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
            gm_l19 = t132;
        } else {
            void * t133 = F.l0;
            gm_t88rcucg1b7uk t134 = gm_l2;
            uint8_t t135 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
            if (!t135) gm_trap("unwrap of null", gm_src7hmnlrtmirgt, 358, 30);
            gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
            gm_b16_8 t137 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t133, t134, t136);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t137;
            gm_tvdg94bk7aa22 t138 = F.l3;
            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&t138) + 0));
            uint8_t t140 = (uint8_t)((t139 == ((uint8_t)53ULL)));
            gm_tvdg94bk7aa22 t141;
            GM_ST(uint8_t, (uint8_t *)&t141 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t141 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t141 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t141 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)53ULL));
            if (t140) {
                gm_b16_8 t142;
                GM_ST(void *, (uint8_t *)&t142, (void *)(gm_s1f440st4mnkp));
                GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(1));
                gm_l35 = t142;
            } else {
                gm_b16_8 t143;
                GM_ST(void *, (uint8_t *)&t143, (void *)(gm_sm5be4659ulci));
                GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(1));
                gm_l35 = t143;
            }
            gm_b16_8 t144 = gm_l35;
            gm_l23 = t144;
            void * t145 = F.l0;
            uint64_t t146 = gm_l6;
            gm_t88rcucg1b7uk t147 = F.l7;
            gm_tqs5p4n2tq19v t148;
            GM_ST(uint8_t, ((uint8_t *)&t148), ((uint8_t)1));
            ((uint8_t *)&t148)[1] = 1;
            gm_b16_8 t149 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t145, t146, t147, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t149;
            void * t150 = F.l0;
            uint32_t t151 = gm_f_fresh_5kokjmg(t150);
            gm_t88rcucg1b7uk t152;
            GM_ST(uint32_t, ((uint8_t *)&t152 + 0), t151);
            gm_f_format_3tnvb6a((void *)&gm_l36, (void *)NULL, cx, NULL, t152);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
            if (!t153) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_src7hmnlrtmirgt, 361, 14);
            gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
            gm_l25 = t154;
            void * t155 = F.l0;
            gm_b16_8 t156 = gm_l17;
            gm_b16_8 t157 = gm_l25;
            gm_b16_8 t158 = gm_l17;
            gm_b16_8 t159 = gm_l24;
            gm_b16_8 t160 = gm_l16;
            gm_b16_8 t161 = gm_l23;
            gm_b16_8 t162 = gm_l22;
            gm_th2d9c7aju8hp t163;
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 0), t156);
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 16), t157);
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 32), t158);
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 48), t159);
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 64), t160);
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 80), t161);
            GM_ST(gm_b16_8, ((uint8_t *)&t163 + 96), t162);
            gm_b0_1 t164 = gm_f_linef_sk93pav((void *)NULL, cx, NULL, t155, &t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t165 = gm_l18;
            gm_b16_8 t166 = gm_l17;
            gm_b16_8 t167 = gm_l16;
            gm_b16_8 t168 = gm_l25;
            gm_b16_8 t169 = gm_l17;
            gm_b16_8 t170 = gm_l22;
            gm_b16_8 t171 = gm_l25;
            gm_th2d9c7aju8hp t172;
            GM_ST(gm_b16_8, ((uint8_t *)&t172 + 0), t165);
            GM_ST(gm_b16_8, ((uint8_t *)&t172 + 16), t166);
            GM_ST(gm_b16_8, ((uint8_t *)&t172 + 32), t167);
            GM_ST(gm_b16_8, ((uint8_t *)&t172 + 48), t168);
            GM_ST(gm_b16_8, ((uint8_t *)&t172 + 64), t169);
            GM_ST(gm_b16_8, ((uint8_t *)&t172 + 80), t170);
            GM_ST(gm_b16_8, ((uint8_t *)&t172 + 96), t171);
            gm_f_format_l9df9v3((void *)&gm_l37, (void *)NULL, cx, NULL, &t172);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t173 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
            if (!t173) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_src7hmnlrtmirgt, 363, 13);
            gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
            gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
            gm_l19 = t175;
        }
    }
gm_lb3:;
    void * t176 = F.l0;
    gm_t88rcucg1b7uk t177 = gm_l2;
    gm_b16_8 t178 = gm_l19;
    gm_b16_8 t179 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t176, t177, t178);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t5o0tpv5jka2l t180;
    GM_ST(uint8_t, (uint8_t *)&t180 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t180 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t180 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t180 + 24, 0);
    GM_ST(uint64_t, (uint8_t *)&t180 + 32, 0);
    ((uint8_t *)&t180)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t180 + 8), t179);
    memmove(gm_out, ((uint8_t *)&t180), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_vec_parts_srl0rr9(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_vec_parts_srl0rr9 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tlcm4lvu3g1a8 gm_l2; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_tb7u19loo3j87 gm_l3;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    void * t5 = (void *)(((uint8_t *)t4) + 8);
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_f_vector_parts_crrfopm((void *)&gm_l3, t5, GM_CONV(gm_csguankko4lmhd, t6));
    uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
    if (!t7) gm_trap("unwrap of null", gm_src7hmnlrtmirgt, 29, 10);
    gm_tlcm4lvu3g1a8 t8 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l3));
    gm_l2 = t8;
    uint64_t t9 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
    gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l2) + 8));
    void * t11 = gm_l0;
    gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l2) + 8));
    uint64_t t13 = gm_f_vec_lane_bytes_c5pmsp6(t11, GM_CONV(gm_csguankko4lmhd, t12));
    gm_tismvprs8r13f t14;
    GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t14 + 0), t9);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t14 + 8), t10);
    GM_ST(uint64_t, ((uint8_t *)&t14 + 16), t13);
    memmove(gm_out, ((uint8_t *)&t14), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_csa3m9feqhg846 gm_f_vec_parts_srl0rr9_c(void * a0, gm_csguankko4lmhd a1) { gm_tismvprs8r13f gm_r; gm_f_vec_parts_srl0rr9(&gm_r, a0, a1); return GM_CONV(gm_csa3m9feqhg846, gm_r); }
__attribute__((aligned(4))) void gm_f_vec_reduce_m5gdqn2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tvdg94bk7aa22 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_vec_reduce_m5gdqn2 F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* v */
    gm_tismvprs8r13f gm_l5; /* vp */
    gm_t88rcucg1b7uk gm_l6; /* lane */
    gm_b16_8 gm_l7; /* p */
    gm_b16_8 gm_l8; /* acc */
    gm_b16_8 gm_l9; /* i */
    gm_b16_8 gm_l10; /* y */
    gm_b16_8 gm_l11; /* r */
    gm_b16_8 gm_l12; /* f */
    gm_b16_8 gm_l13; /* fr */
    gm_b16_8 gm_l14; /* gt */
    GM_STACK_CHECK(&F, 1904u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_t5o0tpv5jka2l gm_l17;
    gm_b16_8 gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_b16_8 gm_l20;
    gm_tabb710d9mq2e gm_l21;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l3;
    gm_f_vec_parts_srl0rr9((void *)&gm_l5, t2, GM_CONV(gm_csguankko4lmhd, t3));
    gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
    gm_l6 = t4;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l3;
    gm_b16_8 t7 = gm_l4;
    gm_b16_8 t8 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t5, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t8;
    void * t9 = gm_l0;
    gm_t88rcucg1b7uk t10 = gm_l6;
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
    gm_b16_8 t12 = gm_l7;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sicgmpca63jln));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(1));
    gm_b16_8 t14 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t9, t10, t11, t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t14;
    void * t15 = gm_l0;
    uint32_t t16 = gm_f_fresh_5kokjmg(t15);
    gm_t88rcucg1b7uk t17;
    GM_ST(uint32_t, ((uint8_t *)&t17 + 0), t16);
    gm_f_format_c0h6o29((void *)&gm_l15, (void *)NULL, cx, NULL, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src7hmnlrtmirgt, 271, 10);
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
    gm_l9 = t19;
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_l9;
    gm_b16_8 t22 = gm_l9;
    uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    gm_b16_8 t24 = gm_l9;
    gm_tmr5oep1578tb t25;
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t21);
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 16), t22);
    GM_ST(uint64_t, ((uint8_t *)&t25 + 32), t23);
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 40), t24);
    gm_b0_1 t26 = gm_f_linef_6mfrmej((void *)NULL, cx, NULL, t20, &t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t27 = gm_l0;
    void * t28 = gm_l0;
    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 144));
    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
    if (o31) gm_trap("integer overflow in `+`", gm_src7hmnlrtmirgt, 273, 5);
    uint64_t t32 = r30;
    GM_ST(uint64_t, (((uint8_t *)t27) + 144), t32);
    void * t33 = gm_l0;
    gm_t88rcucg1b7uk t34 = gm_l6;
    uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
    gm_b16_8 t36 = gm_l7;
    gm_b16_8 t37 = gm_l9;
    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
    gm_b16_8 t39 = gm_f_vec_lane_at_i7st991((void *)NULL, cx, NULL, t33, t34, t35, t36, t38);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t39;
    gm_b16_8 t40;
    GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(0));
    gm_l11 = t40;
    gm_tvdg94bk7aa22 t41 = gm_l2;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
    uint8_t t43 = (uint8_t)((t42 == ((uint8_t)46ULL)));
    gm_tvdg94bk7aa22 t44;
    GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t44 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)46ULL));
    if (t43) {
        void * t45 = gm_l0;
        void * t46 = GM_LD(void *, ((uint8_t *)t45));
        void * t47 = (void *)(((uint8_t *)t46) + 8);
        gm_t88rcucg1b7uk t48 = gm_l6;
        uint8_t t49 = gm_f_is_float_gthejtb(t47, GM_CONV(gm_csguankko4lmhd, t48));
        if (t49) {
            void * t50 = gm_l0;
            gm_t88rcucg1b7uk t51 = gm_l6;
            gm_b16_8 t52 = gm_l8;
            gm_b16_8 t53 = gm_l10;
            gm_t6j6smj7qoctu t54;
            GM_ST(gm_b16_8, ((uint8_t *)&t54 + 0), t52);
            GM_ST(gm_b16_8, ((uint8_t *)&t54 + 16), t53);
            gm_f_format_s4jotpe((void *)&gm_l16, (void *)NULL, cx, NULL, &t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
            if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src7hmnlrtmirgt, 278, 30);
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
            gm_b16_8 t58 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t50, t51, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t58;
        } else {
            void * t59 = gm_l0;
            gm_tl874t2atnlh3 t60 = gm_l1;
            gm_t88rcucg1b7uk t61 = gm_l6;
            gm_t9qkh0rsooc8d t62;
            GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)0ULL));
            gm_t88rcucg1b7uk t63 = gm_l6;
            gm_b16_8 t64 = gm_l8;
            gm_b16_8 t65 = gm_l10;
            gm_ts6oqppjh0ba3 t66;
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t66 + 1), t62);
            gm_f_int_binary_kp3u7i8((void *)&gm_l17, (void *)NULL, cx, NULL, t59, t60, t61, t66, t63, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (t67) {
            } else {
                gm_t08olq97rscn3 t68 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l17) + 8));
                gm_t5o0tpv5jka2l t69;
                GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                ((uint8_t *)&t69)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t69 + 8), t68);
                memmove(gm_out, ((uint8_t *)&t69), 40);
                goto gm_fdone;
            }
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
            gm_l11 = t70;
        }
    } else {
        void * t71 = gm_l0;
        void * t72 = GM_LD(void *, ((uint8_t *)t71));
        void * t73 = (void *)(((uint8_t *)t72) + 8);
        gm_t88rcucg1b7uk t74 = gm_l6;
        uint8_t t75 = gm_f_is_float_gthejtb(t73, GM_CONV(gm_csguankko4lmhd, t74));
        if (t75) {
            gm_tvdg94bk7aa22 t76 = gm_l2;
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&t76) + 0));
            uint8_t t78 = (uint8_t)((t77 == ((uint8_t)47ULL)));
            gm_tvdg94bk7aa22 t79;
            GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t79 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)47ULL));
            if (t78) {
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(gm_s12ng38ve3205));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(7));
                gm_l18 = t80;
            } else {
                gm_b16_8 t81;
                GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sgjc0ebf56hf4));
                GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(7));
                gm_l18 = t81;
            }
            gm_b16_8 t82 = gm_l18;
            gm_l12 = t82;
            void * t83 = gm_l0;
            gm_t88rcucg1b7uk t84 = gm_l6;
            gm_b16_8 t85 = gm_l12;
            gm_b16_8 t86 = gm_l8;
            gm_b16_8 t87 = gm_l10;
            gm_tfeab279c16rr t88;
            GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t85);
            GM_ST(gm_b16_8, ((uint8_t *)&t88 + 16), t86);
            GM_ST(gm_b16_8, ((uint8_t *)&t88 + 32), t87);
            gm_f_format_lqnh3lt((void *)&gm_l19, (void *)NULL, cx, NULL, &t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_src7hmnlrtmirgt, 284, 35);
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
            gm_b16_8 t92 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t83, t84, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t92;
            void * t93 = gm_l0;
            gm_t88rcucg1b7uk t94 = gm_l6;
            gm_b16_8 t95 = gm_l13;
            gm_b16_8 t96 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t93, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t96;
        } else {
            gm_tvdg94bk7aa22 t97 = gm_l2;
            uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&t97) + 0));
            uint8_t t99 = (uint8_t)((t98 == ((uint8_t)47ULL)));
            gm_tvdg94bk7aa22 t100;
            GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t100 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)47ULL));
            if (t99) {
                gm_b16_8 t101;
                GM_ST(void *, (uint8_t *)&t101, (void *)(gm_s1f440st4mnkp));
                GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(1));
                gm_l20 = t101;
            } else {
                gm_b16_8 t102;
                GM_ST(void *, (uint8_t *)&t102, (void *)(gm_sm5be4659ulci));
                GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(1));
                gm_l20 = t102;
            }
            gm_b16_8 t103 = gm_l20;
            gm_l14 = t103;
            void * t104 = gm_l0;
            gm_t88rcucg1b7uk t105 = gm_l6;
            gm_b16_8 t106 = gm_l8;
            gm_b16_8 t107 = gm_l14;
            gm_b16_8 t108 = gm_l10;
            gm_b16_8 t109 = gm_l8;
            gm_b16_8 t110 = gm_l10;
            gm_tud7oovfvnf43 t111;
            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 0), t106);
            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 16), t107);
            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 32), t108);
            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 48), t109);
            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 64), t110);
            gm_f_format_eqjcikj((void *)&gm_l21, (void *)NULL, cx, NULL, &t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
            if (!t112) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src7hmnlrtmirgt, 288, 26);
            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
            gm_b16_8 t115 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t104, t105, t114);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t115;
        }
    }
    void * t116 = gm_l0;
    gm_b16_8 t117 = gm_l8;
    gm_b16_8 t118 = gm_l11;
    gm_t6j6smj7qoctu t119;
    GM_ST(gm_b16_8, ((uint8_t *)&t119 + 0), t117);
    GM_ST(gm_b16_8, ((uint8_t *)&t119 + 16), t118);
    gm_b0_1 t120 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t116, &t119);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t121 = gm_l0;
    void * t122 = gm_l0;
    uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)t122) + 144));
    uint64_t r124; bool o125 = __builtin_sub_overflow(t123, ((uint64_t)1ULL), &r124);
    if (o125) gm_trap("integer overflow in `-`", gm_src7hmnlrtmirgt, 291, 5);
    uint64_t t126 = r124;
    GM_ST(uint64_t, (((uint8_t *)t121) + 144), t126);
    void * t127 = gm_l0;
    gm_b16_8 t128;
    GM_ST(void *, (uint8_t *)&t128, (void *)(gm_s2cuflhqkkuaq));
    GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(1));
    gm_b0_1 t129 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t127, t128);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t130 = gm_l8;
    gm_t5o0tpv5jka2l t131;
    GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t131 + 24, 0);
    GM_ST(uint64_t, (uint8_t *)&t131 + 32, 0);
    ((uint8_t *)&t131)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t131 + 8), t130);
    memmove(gm_out, ((uint8_t *)&t131), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_select_3f37tvf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_vec_select_3f37tvf F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* mt */
    gm_b16_8 gm_l3; /* m */
    gm_b16_8 gm_l4; /* a */
    gm_b16_8 gm_l5; /* b */
    gm_b16_8 gm_l6; /* vm */
    gm_b16_8 gm_l7; /* va */
    gm_b16_8 gm_l8; /* vb */
    gm_b16_8 gm_l9; /* r */
    GM_STACK_CHECK(&F, 412u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_b16_8 t4 = gm_l3;
    gm_b16_8 t5 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t5;
    void * t6 = gm_l0;
    gm_t88rcucg1b7uk t7 = gm_l1;
    gm_b16_8 t8 = gm_l4;
    gm_b16_8 t9 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t6, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t9;
    void * t10 = gm_l0;
    gm_t88rcucg1b7uk t11 = gm_l1;
    gm_b16_8 t12 = gm_l5;
    gm_b16_8 t13 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t10, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t13;
    void * t14 = gm_l0;
    gm_t88rcucg1b7uk t15 = gm_l1;
    gm_b16_8 t16 = gm_l6;
    gm_b16_8 t17 = gm_l7;
    gm_b16_8 t18 = gm_l8;
    gm_b16_8 t19 = gm_f_vec_blend_2oerv54((void *)NULL, cx, NULL, t14, t15, t16, t17, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t19;
    void * t20 = gm_l0;
    gm_t88rcucg1b7uk t21 = gm_l1;
    gm_b16_8 t22 = gm_l9;
    gm_b16_8 t23 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t20, t21, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t23;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_shuffle_j6jmffl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_vec_shuffle_j6jmffl F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* xt */
    gm_b16_8 gm_l3; /* v */
    gm_b16_8 gm_l4; /* lanes */
    gm_tismvprs8r13f gm_l5; /* vp */
    gm_t88rcucg1b7uk gm_l6; /* lane */
    uint64_t gm_l7; /* bytes */
    uint64_t gm_l8; /* m */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* k */
    gm_b16_8 gm_l13; /* va */
    gm_b16_8 gm_l14; /* tv */
    gm_b16_8 gm_l15; /* res */
    gm_b16_8 gm_l16; /* r */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* j */
    gm_b16_8 gm_l20; /* k */
    GM_STACK_CHECK(&F, 1504u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tismvprs8r13f gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tabb710d9mq2e gm_l24;
    gm_tabb710d9mq2e gm_l25;
    gm_tf1bjg81u0glv gm_l26;
    void * t12 = gm_l0;
    gm_t88rcucg1b7uk t13 = gm_l2;
    gm_f_vec_parts_srl0rr9((void *)&gm_l5, t12, GM_CONV(gm_csguankko4lmhd, t13));
    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
    gm_l6 = t14;
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
    gm_l7 = t15;
    void * t16 = gm_l0;
    gm_t88rcucg1b7uk t17 = gm_l1;
    gm_f_vec_parts_srl0rr9((void *)&gm_l21, t16, GM_CONV(gm_csguankko4lmhd, t17));
    uint64_t t18 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
    gm_l8 = t18;
    gm_trqfvmjtt4sij t19;
    gm_f_list_epvducg((void *)&t19, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l9 = t19;
    gm_b16_8 t20 = gm_l4;
    gm_l10 = t20;
    gm_l11 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t21 = gm_l11;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            uint8_t t23 = (t21 < t22);
            if (!t23) {
                goto gm_lb1;
            }
            {
                void * t24 = GM_LD(void *, ((uint8_t *)&gm_l10));
                uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint64_t t26 = gm_l11;
                {
                    {
                        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t24) + (uintptr_t)t26 * 8));
                        gm_l12 = t27;
                        void * t28 = (void *)((uint8_t *)&F.l9);
                        uint64_t t29 = gm_l12;
                        gm_t4ce0jd3tdro4 t30;
                        GM_ST(uint64_t, ((uint8_t *)&t30 + 0), t29);
                        gm_f_format_1amnh72((void *)&gm_l22, (void *)NULL, cx, NULL, t30);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                        if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src7hmnlrtmirgt, 381, 31);
                        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                        gm_tftb1odc739t5 t34 = gm_f_push_3ef746b(t28, t33);
                        gm_l23 = t34;
                        uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                        if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_src7hmnlrtmirgt, 381, 22);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 381, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t36 = gm_l11;
            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
            uint64_t t39 = r37;
            gm_l11 = t39;
        }
    }
gm_lb1:;
    void * t40 = gm_l0;
    gm_t88rcucg1b7uk t41 = gm_l2;
    gm_b16_8 t42 = gm_l3;
    gm_b16_8 t43 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t40, t41, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l13 = t43;
    void * t44 = gm_l0;
    uint64_t t45 = gm_l8;
    gm_t88rcucg1b7uk t46 = gm_l6;
    gm_tqs5p4n2tq19v t47;
    memset(&t47, 0, sizeof t47);
    gm_b16_8 t48 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t44, t45, t46, t47);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l14 = t48;
    void * t49 = gm_l0;
    gm_t88rcucg1b7uk t50 = gm_l1;
    gm_b16_8 t51 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t49, t50);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l15 = t51;
    void * t52 = gm_l0;
    uint32_t t53 = gm_f_fresh_5kokjmg(t52);
    gm_t88rcucg1b7uk t54;
    GM_ST(uint32_t, ((uint8_t *)&t54 + 0), t53);
    gm_f_format_1mojd22((void *)&gm_l24, (void *)NULL, cx, NULL, t54);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
    if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_src7hmnlrtmirgt, 385, 10);
    gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
    gm_l16 = t56;
    void * t57 = gm_l0;
    gm_b16_8 t58;
    GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sb22ajpcscr7v));
    GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(40));
    gm_b0_1 t59 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t57, t58);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t60 = gm_l0;
    gm_b16_8 t61 = gm_l14;
    gm_b16_8 t62 = gm_l16;
    gm_b16_8 t63 = gm_l13;
    gm_b16_8 t64 = gm_l13;
    gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
    gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
    gm_b16_8 t67;
    GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sg5gdnrith2i6));
    GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(2));
    gm_f_join_sfknljb((void *)&gm_l25, (void *)NULL, cx, NULL, t66, t67);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
    if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 8, gm_src7hmnlrtmirgt, 387, 77);
    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
    gm_tud7oovfvnf43 t70;
    GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t61);
    GM_ST(gm_b16_8, ((uint8_t *)&t70 + 16), t62);
    GM_ST(gm_b16_8, ((uint8_t *)&t70 + 32), t63);
    GM_ST(gm_b16_8, ((uint8_t *)&t70 + 48), t64);
    GM_ST(gm_b16_8, ((uint8_t *)&t70 + 64), t69);
    gm_b0_1 t71 = gm_f_linef_ssnaj62((void *)NULL, cx, NULL, t60, &t70);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t72 = gm_l0;
    gm_b16_8 t73 = gm_l15;
    gm_b16_8 t74 = gm_l16;
    gm_b16_8 t75 = gm_l15;
    gm_tfeab279c16rr t76;
    GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t73);
    GM_ST(gm_b16_8, ((uint8_t *)&t76 + 16), t74);
    GM_ST(gm_b16_8, ((uint8_t *)&t76 + 32), t75);
    gm_b0_1 t77 = gm_f_linef_ar0alse((void *)NULL, cx, NULL, t72, &t76);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t78 = gm_l0;
    gm_b16_8 t79;
    GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s0p6vt35dv9nv));
    GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(5));
    gm_b0_1 t80 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t78, t79);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
    gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
    gm_l17 = t82;
    gm_l18 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t83 = gm_l18;
            uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
            uint8_t t85 = (t83 < t84);
            if (!t85) {
                goto gm_lb6;
            }
            {
                uint64_t t86 = gm_l18;
                void * t87 = GM_LD(void *, ((uint8_t *)&gm_l17));
                uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint64_t t89 = gm_l18;
                gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)t87) + (uintptr_t)t89 * 16));
                gm_tf1bjg81u0glv t91;
                GM_ST(uint64_t, ((uint8_t *)&t91 + 0), t86);
                GM_ST(gm_b16_8, ((uint8_t *)&t91 + 8), t90);
                gm_l26 = t91;
                {
                    {
                        uint64_t t92 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
                        gm_l19 = t92;
                        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                        gm_l20 = t93;
                        void * t94 = gm_l0;
                        gm_b16_8 t95 = gm_l15;
                        uint64_t t96 = gm_l19;
                        uint64_t t97 = (uint64_t)t96;
                        uint64_t t98 = gm_l7;
                        uint64_t r99; bool o100 = __builtin_mul_overflow(t97, t98, &r99);
                        if (o100) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 391, 85);
                        uint64_t t101 = r99;
                        gm_b16_8 t102 = gm_l13;
                        gm_b16_8 t103 = gm_l20;
                        uint64_t t104 = gm_l7;
                        uint64_t t105 = gm_l7;
                        gm_tte9sujb6sb62 t106;
                        GM_ST(gm_b16_8, ((uint8_t *)&t106 + 0), t95);
                        GM_ST(uint64_t, ((uint8_t *)&t106 + 16), t101);
                        GM_ST(gm_b16_8, ((uint8_t *)&t106 + 24), t102);
                        GM_ST(gm_b16_8, ((uint8_t *)&t106 + 40), t103);
                        GM_ST(uint64_t, ((uint8_t *)&t106 + 56), t104);
                        GM_ST(uint64_t, ((uint8_t *)&t106 + 64), t105);
                        gm_b0_1 t107 = gm_f_linef_pai11tb((void *)NULL, cx, NULL, t94, &t106);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 390, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t108 = gm_l18;
            uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
            uint64_t t111 = r109;
            gm_l18 = t111;
        }
    }
gm_lb6:;
    void * t112 = gm_l0;
    gm_b16_8 t113;
    GM_ST(void *, (uint8_t *)&t113, (void *)(gm_s9d990kf69gae));
    GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(6));
    gm_b0_1 t114 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t112, t113);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t115 = gm_l15;
    F.ret = t115;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_vec_store_6d0dm94(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_vec_store_6d0dm94 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* x */
    gm_b16_8 gm_l3; /* res */
    gm_b16_8 gm_l4; /* n */
    GM_STACK_CHECK(&F, 393u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l5;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t4;
    void * t5 = gm_l0;
    uint32_t t6 = gm_f_fresh_5kokjmg(t5);
    gm_t88rcucg1b7uk t7;
    GM_ST(uint32_t, ((uint8_t *)&t7 + 0), t6);
    gm_f_format_1mojd22((void *)&gm_l5, (void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_src7hmnlrtmirgt, 77, 10);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_l4 = t9;
    void * t10 = gm_l0;
    gm_b16_8 t11 = gm_l2;
    gm_b16_8 t12 = gm_l4;
    gm_b16_8 t13 = gm_l2;
    gm_b16_8 t14 = gm_l3;
    gm_b16_8 t15 = gm_l4;
    gm_b16_8 t16 = gm_l3;
    gm_tif3elqk01bdl t17;
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t11);
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t12);
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 32), t13);
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 48), t14);
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 64), t15);
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 80), t16);
    gm_b0_1 t18 = gm_f_linef_4bjepb3((void *)NULL, cx, NULL, t10, &t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t19 = gm_l3;
    F.ret = t19;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_vec_ty_adod8ur(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, gm_tqs5p4n2tq19v a3) {
    struct gm_fr_vec_ty_adod8ur F;
    void * gm_l0; /* fg */
    uint64_t gm_l1; /* n */
    gm_t88rcucg1b7uk gm_l2; /* lane */
    gm_tqs5p4n2tq19v gm_l3; /* twin */
    uint64_t gm_l4; /* bytes */
    gm_b16_8 gm_l5; /* c */
    gm_b16_8 gm_l6; /* tag */
    uint8_t gm_l7; /* signed */
    uint64_t gm_l8; /* bits */
    uint16_t gm_l9; /* bits */
    uint8_t gm_l10; /* signed */
    uint16_t gm_l11; /* bits */
    GM_STACK_CHECK(&F, 1076u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_t53at80pfdus3 gm_l16;
    gm_b16_8 gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_b16_8 gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_b16_8 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    void * t8 = gm_l0;
    gm_t88rcucg1b7uk t9 = gm_l2;
    uint64_t t10 = gm_f_vec_lane_bytes_c5pmsp6(t8, GM_CONV(gm_csguankko4lmhd, t9));
    gm_l4 = t10;
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s9grtrvtmi962));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(7));
    gm_l5 = t11;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sjmccqdb90fnl));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(2));
    gm_l6 = t12;
    {
        {
            uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l3))[1] != 0);
            if (!t13) {
                goto gm_lb2;
            }
            uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
            gm_l7 = t14;
            uint64_t t15 = gm_l4;
            uint64_t r16; bool o17 = __builtin_mul_overflow(t15, ((uint64_t)8ULL), &r16);
            if (o17) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 40, 17);
            uint64_t t18 = r16;
            gm_l8 = t18;
            uint8_t t19 = gm_l7;
            if (t19) {
                uint64_t t20 = gm_l8;
                gm_t4ce0jd3tdro4 t21;
                GM_ST(uint64_t, ((uint8_t *)&t21 + 0), t20);
                gm_f_format_48m8fhm((void *)&gm_l12, (void *)NULL, cx, NULL, t21);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src7hmnlrtmirgt, 42, 17);
                gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                gm_l5 = t24;
                uint64_t t25 = gm_l8;
                gm_t4ce0jd3tdro4 t26;
                GM_ST(uint64_t, ((uint8_t *)&t26 + 0), t25);
                gm_f_format_27c89bp((void *)&gm_l13, (void *)NULL, cx, NULL, t26);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_src7hmnlrtmirgt, 43, 19);
                gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                gm_l6 = t29;
            } else {
                uint64_t t30 = gm_l8;
                gm_t4ce0jd3tdro4 t31;
                GM_ST(uint64_t, ((uint8_t *)&t31 + 0), t30);
                gm_f_format_368205k((void *)&gm_l14, (void *)NULL, cx, NULL, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src7hmnlrtmirgt, 45, 17);
                gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                gm_l5 = t34;
                uint64_t t35 = gm_l8;
                gm_t4ce0jd3tdro4 t36;
                GM_ST(uint64_t, ((uint8_t *)&t36 + 0), t35);
                gm_f_format_4f5imdp((void *)&gm_l15, (void *)NULL, cx, NULL, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src7hmnlrtmirgt, 46, 19);
                gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                gm_l6 = t39;
            }
            goto gm_lb1;
        }
gm_lb2:;
        void * t40 = gm_l0;
        void * t41 = GM_LD(void *, ((uint8_t *)t40));
        void * t42 = (void *)(((uint8_t *)t41) + 8);
        gm_t88rcucg1b7uk t43 = gm_l2;
        gm_f_kind_ghvav22((void *)&gm_l16, t42, GM_CONV(gm_csguankko4lmhd, t43));
        {
            {
                uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                uint8_t t45 = (uint8_t)(t44 == ((uint8_t)3ULL));
                if (!t45) {
                    goto gm_lb4;
                }
                uint16_t t46 = GM_LD(uint16_t, (((uint8_t *)&gm_l16) + 8));
                gm_l9 = t46;
                uint16_t t47 = gm_l9;
                uint8_t t48 = ((t47 == ((uint16_t)32ULL)));
                if (t48) {
                    gm_b16_8 t49;
                    GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s4cess8gum6i7));
                    GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(5));
                    gm_l17 = t49;
                } else {
                    gm_b16_8 t50;
                    GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s866ofni9sb8u));
                    GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(6));
                    gm_l17 = t50;
                }
                gm_b16_8 t51 = gm_l17;
                gm_l5 = t51;
                uint16_t t52 = gm_l9;
                gm_tu0pangbbaeec t53;
                GM_ST(uint16_t, ((uint8_t *)&t53 + 0), t52);
                gm_f_format_h9otq43((void *)&gm_l18, (void *)NULL, cx, NULL, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src7hmnlrtmirgt, 52, 23);
                gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                gm_l6 = t56;
                goto gm_lb3;
            }
gm_lb4:;
            {
                uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
                uint8_t t58 = (uint8_t)(t57 == ((uint8_t)0ULL));
                if (!t58) {
                    goto gm_lb5;
                }
                uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 8));
                gm_l10 = t59;
                uint16_t t60 = GM_LD(uint16_t, ((((uint8_t *)&gm_l16) + 8) + 2));
                gm_l11 = t60;
                uint8_t t61 = gm_l10;
                if (t61) {
                    gm_b16_8 t62;
                    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sr4qbrk6tlgaa));
                    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(0));
                    gm_l19 = t62;
                } else {
                    gm_b16_8 t63;
                    GM_ST(void *, (uint8_t *)&t63, (void *)(gm_s0m9d22aen6f8));
                    GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(1));
                    gm_l19 = t63;
                }
                gm_b16_8 t64 = gm_l19;
                uint16_t t65 = gm_l11;
                gm_tvaas22dl64e0 t66;
                GM_ST(uint16_t, (uint8_t *)&t66 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t66 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t64);
                GM_ST(uint16_t, ((uint8_t *)&t66 + 16), t65);
                gm_f_format_hjgrmth((void *)&gm_l20, (void *)NULL, cx, NULL, &t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_src7hmnlrtmirgt, 55, 21);
                gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                gm_l5 = t69;
                uint8_t t70 = gm_l10;
                if (t70) {
                    gm_b16_8 t71;
                    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sitlbs33jqqal));
                    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(1));
                    gm_l21 = t71;
                } else {
                    gm_b16_8 t72;
                    GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s0m9d22aen6f8));
                    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(1));
                    gm_l21 = t72;
                }
                gm_b16_8 t73 = gm_l21;
                uint16_t t74 = gm_l11;
                gm_tvaas22dl64e0 t75;
                GM_ST(uint16_t, (uint8_t *)&t75 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t75 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t75 + 0), t73);
                GM_ST(uint16_t, ((uint8_t *)&t75 + 16), t74);
                gm_f_format_sojmgl9((void *)&gm_l22, (void *)NULL, cx, NULL, &t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src7hmnlrtmirgt, 56, 23);
                gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                gm_l6 = t78;
                goto gm_lb3;
            }
gm_lb5:;
            {
                goto gm_lb3;
            }
gm_lb6:;
            gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 49, 9);
        }
gm_lb3:;
    }
gm_lb1:;
    void * t79 = gm_l0;
    void * t80 = GM_LD(void *, ((uint8_t *)t79));
    void * t81 = (void *)(((uint8_t *)t80) + 8);
    gm_b16_8 t82 = gm_l5;
    gm_b16_8 t83 = gm_l6;
    uint64_t t84 = gm_l1;
    uint64_t t85 = gm_l4;
    gm_b16_8 t86 = gm_f_vector_typedef_vpgf6u8((void *)NULL, cx, NULL, t81, t82, t83, t84, t85);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t86;
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
__attribute__((aligned(4))) void gm_f_vector_binary_value_u702bne(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_ts6oqppjh0ba3 a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_vector_binary_value_u702bne F;
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l4; /* a */
    gm_tismvprs8r13f gm_l6; /* vp */
    uint64_t gm_l7; /* n */
    uint8_t gm_l10; /* checks */
    gm_t9qkh0rsooc8d gm_l11; /* f */
    gm_t9qkh0rsooc8d gm_l12; /* f */
    gm_t9qkh0rsooc8d gm_l13; /* f */
    uint8_t gm_l14; /* checked */
    gm_b16_8 gm_l18; /* va */
    gm_b16_8 gm_l19; /* sym */
    uint64_t gm_l20; /* mask */
    gm_b16_8 gm_l21; /* x */
    gm_b16_8 gm_l22; /* u */
    gm_b16_8 gm_l23; /* s */
    gm_b16_8 gm_l24; /* vb */
    gm_b16_8 gm_l25; /* sym */
    gm_b16_8 gm_l26; /* x */
    gm_b16_8 gm_l27; /* u */
    gm_b16_8 gm_l28; /* s */
    GM_STACK_CHECK(&F, 2410u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    F.l3 = a3;
    gm_l4 = a4;
    F.l5 = a5;
    uint8_t gm_l29;
    uint8_t gm_l30;
    uint8_t gm_l31;
    uint8_t gm_l32;
    uint8_t gm_l33;
    gm_t5o0tpv5jka2l gm_l34;
    gm_t5o0tpv5jka2l gm_l35;
    uint8_t gm_l36;
    gm_b16_8 gm_l37;
    gm_tabb710d9mq2e gm_l38;
    gm_tabb710d9mq2e gm_l39;
    gm_b16_8 gm_l40;
    uint8_t gm_l41;
    gm_tabb710d9mq2e gm_l42;
    gm_tabb710d9mq2e gm_l43;
    {
        void * t36 = F.l0;
        gm_t88rcucg1b7uk t37 = gm_l2;
        gm_f_vec_parts_srl0rr9((void *)&gm_l6, t36, GM_CONV(gm_csguankko4lmhd, t37));
        uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
        gm_l7 = t38;
        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
        F.l8 = t39;
        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
        F.l9 = t40;
        void * t41 = F.l0;
        void * t42 = GM_LD(void *, ((uint8_t *)t41));
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)t42) + 1136));
        gm_l10 = t43;
        {
            {
                {
                    {
                        uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                        uint8_t t45 = (uint8_t)(t44 == ((uint8_t)0ULL));
                        if (!t45) {
                            goto gm_lb4;
                        }
                        gm_t9qkh0rsooc8d t46 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l3) + 1));
                        gm_l11 = t46;
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                        uint8_t t48 = (uint8_t)(t47 == ((uint8_t)1ULL));
                        if (!t48) {
                            goto gm_lb5;
                        }
                        gm_t9qkh0rsooc8d t49 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l3) + 1));
                        gm_l11 = t49;
                        goto gm_lb3;
                    }
gm_lb5:;
                    {
                        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                        uint8_t t51 = (uint8_t)(t50 == ((uint8_t)2ULL));
                        if (!t51) {
                            goto gm_lb6;
                        }
                        gm_t9qkh0rsooc8d t52 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l3) + 1));
                        gm_l11 = t52;
                        goto gm_lb3;
                    }
gm_lb6:;
                    goto gm_lb2;
                }
gm_lb3:;
                gm_t9qkh0rsooc8d t53 = gm_l11;
                uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
                uint8_t t55 = (uint8_t)((t54 == ((uint8_t)2ULL)));
                gm_t9qkh0rsooc8d t56;
                GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)2ULL));
                if (t55) {
                    gm_l30 = ((uint8_t)1);
                } else {
                    gm_t9qkh0rsooc8d t57 = gm_l11;
                    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t57) + 0));
                    uint8_t t59 = (uint8_t)((t58 == ((uint8_t)0ULL)));
                    gm_t9qkh0rsooc8d t60;
                    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)0ULL));
                    if (t59) {
                        void * t61 = F.l0;
                        void * t62 = GM_LD(void *, ((uint8_t *)t61));
                        void * t63 = (void *)(((uint8_t *)t62) + 8);
                        gm_t88rcucg1b7uk t64 = F.l8;
                        uint8_t t65 = gm_f_is_integer_eo6jkc5(t63, GM_CONV(gm_csguankko4lmhd, t64));
                        gm_l32 = t65;
                    } else {
                        gm_l32 = ((uint8_t)0);
                    }
                    uint8_t t66 = gm_l32;
                    if (t66) {
                        uint8_t t67 = gm_l10;
                        gm_l31 = t67;
                    } else {
                        gm_l31 = ((uint8_t)0);
                    }
                    uint8_t t68 = gm_l31;
                    gm_l30 = t68;
                }
                uint8_t t69 = gm_l30;
                gm_l29 = t69;
                goto gm_lb1;
            }
gm_lb2:;
            {
                {
                    {
                        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                        uint8_t t71 = (uint8_t)(t70 == ((uint8_t)3ULL));
                        if (!t71) {
                            goto gm_lb9;
                        }
                        goto gm_lb8;
                    }
gm_lb9:;
                    {
                        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                        uint8_t t73 = (uint8_t)(t72 == ((uint8_t)6ULL));
                        if (!t73) {
                            goto gm_lb10;
                        }
                        goto gm_lb8;
                    }
gm_lb10:;
                    goto gm_lb7;
                }
gm_lb8:;
                uint8_t t74 = gm_l10;
                gm_l29 = t74;
                goto gm_lb1;
            }
gm_lb7:;
            {
                gm_l29 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb11:;
            gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 145, 21);
        }
gm_lb1:;
    }
    {
        uint8_t t75 = gm_l29;
        gm_l14 = t75;
        uint8_t t76 = gm_l14;
        if (t76) {
            {
                {
                    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t78 = (uint8_t)(t77 == ((uint8_t)3ULL));
                    if (!t78) {
                        goto gm_lb13;
                    }
                    gm_l33 = ((uint8_t)1);
                    goto gm_lb12;
                }
gm_lb13:;
                {
                    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t80 = (uint8_t)(t79 == ((uint8_t)6ULL));
                    if (!t80) {
                        goto gm_lb14;
                    }
                    gm_l33 = ((uint8_t)1);
                    goto gm_lb12;
                }
gm_lb14:;
                {
                    gm_l33 = ((uint8_t)0);
                    goto gm_lb12;
                }
gm_lb15:;
                gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 151, 12);
            }
gm_lb12:;
            uint8_t t81 = gm_l33;
            if (t81) {
                void * t82 = F.l0;
                gm_t88rcucg1b7uk t83 = gm_l2;
                gm_b16_8 t84 = gm_l4;
                gm_b16_8 t85 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t82, t83, t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l15 = t85;
                void * t86 = F.l0;
                gm_t88rcucg1b7uk t87 = gm_l2;
                uint64_t t88 = gm_l7;
                gm_closure t89 = ((gm_closure){(void *)gm_f_fn_hfolh19, (void *)&F.h, cx});
                gm_f_vec_lane_loop_a3gvooh((void *)&gm_l34, (void *)NULL, cx, NULL, t86, t87, t88, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                if (t90) {
                } else {
                    gm_t08olq97rscn3 t91 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l34) + 8));
                    gm_t5o0tpv5jka2l t92;
                    GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
                    ((uint8_t *)&t92)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t92 + 8), t91);
                    memmove(gm_out, ((uint8_t *)&t92), 40);
                    goto gm_fdone;
                }
                gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                gm_t5o0tpv5jka2l t94;
                GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t94 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t94 + 32, 0);
                ((uint8_t *)&t94)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t94 + 8), t93);
                memmove(gm_out, ((uint8_t *)&t94), 40);
                goto gm_fdone;
            }
            void * t95 = F.l0;
            gm_t88rcucg1b7uk t96 = gm_l2;
            gm_b16_8 t97 = gm_l4;
            gm_b16_8 t98 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t95, t96, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l16 = t98;
            void * t99 = F.l0;
            gm_t88rcucg1b7uk t100 = gm_l2;
            gm_b16_8 t101 = F.l5;
            gm_b16_8 t102 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t99, t100, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l17 = t102;
            void * t103 = F.l0;
            gm_t88rcucg1b7uk t104 = gm_l2;
            uint64_t t105 = gm_l7;
            gm_closure t106 = ((gm_closure){(void *)gm_f_fn_c4j9irb, (void *)&F.h, cx});
            gm_f_vec_lane_loop_a3gvooh((void *)&gm_l35, (void *)NULL, cx, NULL, t103, t104, t105, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
            if (t107) {
            } else {
                gm_t08olq97rscn3 t108 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l35) + 8));
                gm_t5o0tpv5jka2l t109;
                GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
                ((uint8_t *)&t109)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t109 + 8), t108);
                memmove(gm_out, ((uint8_t *)&t109), 40);
                goto gm_fdone;
            }
            gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
            gm_t5o0tpv5jka2l t111;
            GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t111 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t111 + 32, 0);
            ((uint8_t *)&t111)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 8), t110);
            memmove(gm_out, ((uint8_t *)&t111), 40);
            goto gm_fdone;
        }
    }
    {
        void * t112 = F.l0;
        gm_t88rcucg1b7uk t113 = gm_l2;
        gm_b16_8 t114 = gm_l4;
        gm_b16_8 t115 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t112, t113, t114);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t115;
        {
            {
                uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t117 = (uint8_t)(t116 == ((uint8_t)3ULL));
                if (!t117) {
                    goto gm_lb17;
                }
                gm_l36 = ((uint8_t)1);
                goto gm_lb16;
            }
gm_lb17:;
            {
                uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t119 = (uint8_t)(t118 == ((uint8_t)6ULL));
                if (!t119) {
                    goto gm_lb18;
                }
                gm_l36 = ((uint8_t)1);
                goto gm_lb16;
            }
gm_lb18:;
            {
                gm_l36 = ((uint8_t)0);
                goto gm_lb16;
            }
gm_lb19:;
            gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 168, 8);
        }
gm_lb16:;
        uint8_t t120 = gm_l36;
        if (t120) {
            gm_ts6oqppjh0ba3 t121 = F.l3;
            uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&t121) + 0));
            uint8_t t123 = (uint8_t)((t122 == ((uint8_t)6ULL)));
            gm_ts6oqppjh0ba3 t124;
            GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t124 + 0), ((uint8_t)6ULL));
            if (t123) {
                gm_b16_8 t125;
                GM_ST(void *, (uint8_t *)&t125, (void *)(gm_s9hi985r6f61m));
                GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(2));
                gm_l37 = t125;
            } else {
                gm_b16_8 t126;
                GM_ST(void *, (uint8_t *)&t126, (void *)(gm_si62ejtp0vbct));
                GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(2));
                gm_l37 = t126;
            }
            gm_b16_8 t127 = gm_l37;
            gm_l19 = t127;
            uint64_t t128 = F.l9;
            uint64_t r129; bool o130 = __builtin_mul_overflow(t128, ((uint64_t)8ULL), &r129);
            if (o130) gm_trap("integer overflow in `*`", gm_src7hmnlrtmirgt, 172, 17);
            uint64_t t131 = r129;
            uint64_t r132; bool o133 = __builtin_sub_overflow(t131, ((uint64_t)1ULL), &r132);
            if (o133) gm_trap("integer overflow in `-`", gm_src7hmnlrtmirgt, 172, 17);
            uint64_t t134 = r132;
            gm_l20 = t134;
            gm_b16_8 t135;
            GM_ST(void *, (uint8_t *)&t135, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(0));
            gm_l21 = t135;
            gm_ts6oqppjh0ba3 t136 = F.l3;
            uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&t136) + 0));
            uint8_t t138 = (uint8_t)((t137 == ((uint8_t)6ULL)));
            gm_ts6oqppjh0ba3 t139;
            GM_ST(uint8_t, (uint8_t *)&t139 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)6ULL));
            if (t138) {
                gm_b16_8 t140 = gm_l18;
                gm_b16_8 t141 = F.l5;
                uint64_t t142 = gm_l20;
                gm_tftksr4op114e t143;
                GM_ST(gm_b16_8, ((uint8_t *)&t143 + 0), t140);
                GM_ST(gm_b16_8, ((uint8_t *)&t143 + 16), t141);
                GM_ST(uint64_t, ((uint8_t *)&t143 + 32), t142);
                gm_f_format_t4gbkjg((void *)&gm_l38, (void *)NULL, cx, NULL, &t143);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t144 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                if (!t144) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_src7hmnlrtmirgt, 175, 17);
                gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&t145));
                gm_l21 = t146;
            } else {
                void * t147 = F.l0;
                uint64_t t148 = gm_l7;
                gm_t88rcucg1b7uk t149 = F.l8;
                gm_tqs5p4n2tq19v t150;
                GM_ST(uint8_t, ((uint8_t *)&t150), ((uint8_t)0));
                ((uint8_t *)&t150)[1] = 1;
                gm_b16_8 t151 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t147, t148, t149, t150);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t151;
                void * t152 = F.l0;
                uint64_t t153 = gm_l7;
                gm_t88rcucg1b7uk t154 = F.l8;
                gm_tqs5p4n2tq19v t155;
                memset(&t155, 0, sizeof t155);
                gm_b16_8 t156 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t152, t153, t154, t155);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l23 = t156;
                gm_b16_8 t157 = gm_l23;
                gm_b16_8 t158 = gm_l22;
                gm_b16_8 t159 = gm_l18;
                gm_b16_8 t160 = gm_l19;
                gm_b16_8 t161 = F.l5;
                uint64_t t162 = gm_l20;
                gm_tjftu5su6fh8e t163;
                GM_ST(gm_b16_8, ((uint8_t *)&t163 + 0), t157);
                GM_ST(gm_b16_8, ((uint8_t *)&t163 + 16), t158);
                GM_ST(gm_b16_8, ((uint8_t *)&t163 + 32), t159);
                GM_ST(gm_b16_8, ((uint8_t *)&t163 + 48), t160);
                GM_ST(gm_b16_8, ((uint8_t *)&t163 + 64), t161);
                GM_ST(uint64_t, ((uint8_t *)&t163 + 80), t162);
                gm_f_format_cjnp8ds((void *)&gm_l39, (void *)NULL, cx, NULL, &t163);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t164 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                if (!t164) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_src7hmnlrtmirgt, 179, 17);
                gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
                gm_l21 = t166;
            }
            void * t167 = F.l0;
            gm_t88rcucg1b7uk t168 = gm_l2;
            gm_b16_8 t169 = gm_l21;
            gm_b16_8 t170 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t167, t168, t169);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t171;
            GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t171 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t171 + 32, 0);
            ((uint8_t *)&t171)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t171 + 8), t170);
            memmove(gm_out, ((uint8_t *)&t171), 40);
            goto gm_fdone;
        }
    }
    {
        void * t172 = F.l0;
        gm_t88rcucg1b7uk t173 = gm_l2;
        gm_b16_8 t174 = F.l5;
        gm_b16_8 t175 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t172, t173, t174);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l24 = t175;
        {
            {
                uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t177 = (uint8_t)(t176 == ((uint8_t)0ULL));
                if (!t177) {
                    goto gm_lb21;
                }
                gm_b16_8 t178;
                GM_ST(void *, (uint8_t *)&t178, (void *)(gm_sajhum9b4nr96));
                GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(1));
                gm_l40 = t178;
                goto gm_lb20;
            }
gm_lb21:;
            {
                uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t180 = (uint8_t)(t179 == ((uint8_t)1ULL));
                if (!t180) {
                    goto gm_lb22;
                }
                gm_b16_8 t181;
                GM_ST(void *, (uint8_t *)&t181, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(1));
                gm_l40 = t181;
                goto gm_lb20;
            }
gm_lb22:;
            {
                uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t183 = (uint8_t)(t182 == ((uint8_t)2ULL));
                if (!t183) {
                    goto gm_lb23;
                }
                gm_b16_8 t184;
                GM_ST(void *, (uint8_t *)&t184, (void *)(gm_s6s96daqmhcs9));
                GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(1));
                gm_l40 = t184;
                goto gm_lb20;
            }
gm_lb23:;
            {
                uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t186 = (uint8_t)(t185 == ((uint8_t)4ULL));
                if (!t186) {
                    goto gm_lb24;
                }
                gm_b16_8 t187;
                GM_ST(void *, (uint8_t *)&t187, (void *)(gm_st3ja0nqi37bv));
                GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(1));
                gm_l40 = t187;
                goto gm_lb20;
            }
gm_lb24:;
            {
                uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t189 = (uint8_t)(t188 == ((uint8_t)7ULL));
                if (!t189) {
                    goto gm_lb25;
                }
                gm_b16_8 t190;
                GM_ST(void *, (uint8_t *)&t190, (void *)(gm_smerqm8g793l6));
                GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(1));
                gm_l40 = t190;
                goto gm_lb20;
            }
gm_lb25:;
            {
                uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t192 = (uint8_t)(t191 == ((uint8_t)8ULL));
                if (!t192) {
                    goto gm_lb26;
                }
                gm_b16_8 t193;
                GM_ST(void *, (uint8_t *)&t193, (void *)(gm_s2u5posfkbgnq));
                GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(1));
                gm_l40 = t193;
                goto gm_lb20;
            }
gm_lb26:;
            {
                gm_b16_8 t194;
                GM_ST(void *, (uint8_t *)&t194, (void *)(gm_srs1vrn2uaams));
                GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(1));
                gm_l40 = t194;
                goto gm_lb20;
            }
gm_lb27:;
            gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 184, 19);
        }
gm_lb20:;
        gm_b16_8 t195 = gm_l40;
        gm_l25 = t195;
        gm_b16_8 t196;
        GM_ST(void *, (uint8_t *)&t196, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(0));
        gm_l26 = t196;
        {
            {
                void * t197 = F.l0;
                void * t198 = GM_LD(void *, ((uint8_t *)t197));
                void * t199 = (void *)(((uint8_t *)t198) + 8);
                gm_t88rcucg1b7uk t200 = F.l8;
                uint8_t t201 = gm_f_is_integer_eo6jkc5(t199, GM_CONV(gm_csguankko4lmhd, t200));
                if (!t201) {
                    goto gm_lb29;
                }
                {
                    {
                        uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                        uint8_t t203 = (uint8_t)(t202 == ((uint8_t)0ULL));
                        if (!t203) {
                            goto gm_lb31;
                        }
                        gm_l41 = ((uint8_t)1);
                        goto gm_lb30;
                    }
gm_lb31:;
                    {
                        uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                        uint8_t t205 = (uint8_t)(t204 == ((uint8_t)1ULL));
                        if (!t205) {
                            goto gm_lb32;
                        }
                        gm_l41 = ((uint8_t)1);
                        goto gm_lb30;
                    }
gm_lb32:;
                    {
                        uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                        uint8_t t207 = (uint8_t)(t206 == ((uint8_t)2ULL));
                        if (!t207) {
                            goto gm_lb33;
                        }
                        gm_l41 = ((uint8_t)1);
                        goto gm_lb30;
                    }
gm_lb33:;
                    {
                        gm_l41 = ((uint8_t)0);
                        goto gm_lb30;
                    }
gm_lb34:;
                    gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 194, 37);
                }
gm_lb30:;
                uint8_t t208 = gm_l41;
                if (!t208) {
                    goto gm_lb29;
                }
                void * t209 = F.l0;
                uint64_t t210 = gm_l7;
                gm_t88rcucg1b7uk t211 = F.l8;
                gm_tqs5p4n2tq19v t212;
                GM_ST(uint8_t, ((uint8_t *)&t212), ((uint8_t)0));
                ((uint8_t *)&t212)[1] = 1;
                gm_b16_8 t213 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t209, t210, t211, t212);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l27 = t213;
                void * t214 = F.l0;
                uint64_t t215 = gm_l7;
                gm_t88rcucg1b7uk t216 = F.l8;
                gm_tqs5p4n2tq19v t217;
                memset(&t217, 0, sizeof t217);
                gm_b16_8 t218 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t214, t215, t216, t217);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l28 = t218;
                gm_b16_8 t219 = gm_l28;
                gm_b16_8 t220 = gm_l27;
                gm_b16_8 t221 = gm_l18;
                gm_b16_8 t222 = gm_l25;
                gm_b16_8 t223 = gm_l27;
                gm_b16_8 t224 = gm_l24;
                gm_tif3elqk01bdl t225;
                GM_ST(gm_b16_8, ((uint8_t *)&t225 + 0), t219);
                GM_ST(gm_b16_8, ((uint8_t *)&t225 + 16), t220);
                GM_ST(gm_b16_8, ((uint8_t *)&t225 + 32), t221);
                GM_ST(gm_b16_8, ((uint8_t *)&t225 + 48), t222);
                GM_ST(gm_b16_8, ((uint8_t *)&t225 + 64), t223);
                GM_ST(gm_b16_8, ((uint8_t *)&t225 + 80), t224);
                gm_f_format_21ahlbb((void *)&gm_l42, (void *)NULL, cx, NULL, &t225);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t226 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                if (!t226) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 8, gm_src7hmnlrtmirgt, 198, 13);
                gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l42) + 8));
                gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&t227));
                gm_l26 = t228;
                goto gm_lb28;
            }
gm_lb29:;
            gm_b16_8 t229 = gm_l18;
            gm_b16_8 t230 = gm_l25;
            gm_b16_8 t231 = gm_l24;
            gm_tfeab279c16rr t232;
            GM_ST(gm_b16_8, ((uint8_t *)&t232 + 0), t229);
            GM_ST(gm_b16_8, ((uint8_t *)&t232 + 16), t230);
            GM_ST(gm_b16_8, ((uint8_t *)&t232 + 32), t231);
            gm_f_format_mdlbfc1((void *)&gm_l43, (void *)NULL, cx, NULL, &t232);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t233 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
            if (!t233) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 8, gm_src7hmnlrtmirgt, 200, 13);
            gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
            gm_b16_8 t235 = GM_LD(gm_b16_8, ((uint8_t *)&t234));
            gm_l26 = t235;
        }
gm_lb28:;
    }
    {
        void * t236 = F.l0;
        gm_t88rcucg1b7uk t237 = gm_l2;
        gm_b16_8 t238 = gm_l26;
        gm_b16_8 t239 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t236, t237, t238);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t240;
        GM_ST(uint8_t, (uint8_t *)&t240 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t240 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t240 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t240 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t240 + 32, 0);
        ((uint8_t *)&t240)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t240 + 8), t239);
        memmove(gm_out, ((uint8_t *)&t240), 40);
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
__attribute__((aligned(4))) void gm_f_vector_builtin_values_cm12ee6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tvdg94bk7aa22 a3, gm_b16_8 a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_vector_builtin_values_cm12ee6 F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tvdg94bk7aa22 gm_l3; /* op */
    gm_b16_8 gm_l4; /* tys */
    gm_b16_8 gm_l5; /* vals */
    gm_b16_8 gm_l6; /* lanes */
    GM_STACK_CHECK(&F, 1450u);
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
    gm_t5o0tpv5jka2l gm_l7;
    gm_t5o0tpv5jka2l gm_l8;
    {
        {
            {
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)40ULL));
                    if (!t31) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t33 = (uint8_t)(t32 == ((uint8_t)41ULL));
                    if (!t33) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t35 = (uint8_t)(t34 == ((uint8_t)42ULL));
                    if (!t35) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)43ULL));
                    if (!t37) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t39 = (uint8_t)(t38 == ((uint8_t)44ULL));
                    if (!t39) {
                        goto gm_lb8;
                    }
                    goto gm_lb3;
                }
gm_lb8:;
                {
                    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)45ULL));
                    if (!t41) {
                        goto gm_lb9;
                    }
                    goto gm_lb3;
                }
gm_lb9:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t42 = gm_l0;
            gm_tvdg94bk7aa22 t43 = gm_l3;
            void * t44 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t45)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t45, gm_src7hmnlrtmirgt, 241, 91);
            gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t44) + (uintptr_t)((uint64_t)0ULL) * 4));
            void * t47 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t48)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t48, gm_src7hmnlrtmirgt, 241, 99);
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t47) + (uintptr_t)((uint64_t)0ULL) * 16));
            void * t50 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t51)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t51, gm_src7hmnlrtmirgt, 241, 108);
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)t50) + (uintptr_t)((uint64_t)1ULL) * 16));
            gm_b16_8 t53 = gm_f_vec_compare_vrcged3((void *)NULL, cx, NULL, t42, t43, t46, t49, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t54 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t54 + 32, 0);
            ((uint8_t *)&t54)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t54 + 8), t53);
            memmove(gm_out, ((uint8_t *)&t54), 40);
            goto gm_fdone;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t56 = (uint8_t)(t55 == ((uint8_t)46ULL));
                    if (!t56) {
                        goto gm_lb12;
                    }
                    goto gm_lb11;
                }
gm_lb12:;
                {
                    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t58 = (uint8_t)(t57 == ((uint8_t)47ULL));
                    if (!t58) {
                        goto gm_lb13;
                    }
                    goto gm_lb11;
                }
gm_lb13:;
                {
                    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t60 = (uint8_t)(t59 == ((uint8_t)48ULL));
                    if (!t60) {
                        goto gm_lb14;
                    }
                    goto gm_lb11;
                }
gm_lb14:;
                goto gm_lb10;
            }
gm_lb11:;
            void * t61 = gm_l0;
            gm_tl874t2atnlh3 t62 = gm_l1;
            gm_tvdg94bk7aa22 t63 = gm_l3;
            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t65, gm_src7hmnlrtmirgt, 242, 82);
            gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t64) + (uintptr_t)((uint64_t)0ULL) * 4));
            void * t67 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t68)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t68, gm_src7hmnlrtmirgt, 242, 90);
            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)t67) + (uintptr_t)((uint64_t)0ULL) * 16));
            gm_f_vec_reduce_m5gdqn2((void *)&gm_l7, (void *)NULL, cx, NULL, t61, t62, t63, t66, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (t70) {
            } else {
                gm_t08olq97rscn3 t71 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l7) + 8));
                gm_t5o0tpv5jka2l t72;
                GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
                ((uint8_t *)&t72)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t72 + 8), t71);
                memmove(gm_out, ((uint8_t *)&t72), 40);
                goto gm_fdone;
            }
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
            gm_t5o0tpv5jka2l t74;
            GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t74 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t74 + 32, 0);
            ((uint8_t *)&t74)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t74 + 8), t73);
            memmove(gm_out, ((uint8_t *)&t74), 40);
            goto gm_fdone;
        }
gm_lb10:;
        {
            {
                {
                    uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t76 = (uint8_t)(t75 == ((uint8_t)49ULL));
                    if (!t76) {
                        goto gm_lb17;
                    }
                    goto gm_lb16;
                }
gm_lb17:;
                {
                    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t78 = (uint8_t)(t77 == ((uint8_t)50ULL));
                    if (!t78) {
                        goto gm_lb18;
                    }
                    goto gm_lb16;
                }
gm_lb18:;
                {
                    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t80 = (uint8_t)(t79 == ((uint8_t)51ULL));
                    if (!t80) {
                        goto gm_lb19;
                    }
                    goto gm_lb16;
                }
gm_lb19:;
                goto gm_lb15;
            }
gm_lb16:;
            void * t81 = gm_l0;
            gm_t88rcucg1b7uk t82 = gm_l2;
            gm_tvdg94bk7aa22 t83 = gm_l3;
            void * t84 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t85)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t85, gm_src7hmnlrtmirgt, 243, 74);
            gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t84) + (uintptr_t)((uint64_t)0ULL) * 4));
            void * t87 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t88)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t88, gm_src7hmnlrtmirgt, 243, 82);
            gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)t87) + (uintptr_t)((uint64_t)0ULL) * 16));
            gm_b16_8 t90 = gm_f_vec_mask_test_9mgimk6((void *)NULL, cx, NULL, t81, t82, t83, t86, t89);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t91;
            GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t91 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t91 + 32, 0);
            ((uint8_t *)&t91)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t91 + 8), t90);
            memmove(gm_out, ((uint8_t *)&t91), 40);
            goto gm_fdone;
        }
gm_lb15:;
        {
            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t93 = (uint8_t)(t92 == ((uint8_t)52ULL));
            if (!t93) {
                goto gm_lb20;
            }
            void * t94 = gm_l0;
            gm_t88rcucg1b7uk t95 = gm_l2;
            void * t96 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t97)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t97, gm_src7hmnlrtmirgt, 244, 44);
            gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t96) + (uintptr_t)((uint64_t)0ULL) * 4));
            void * t99 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t100)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t100, gm_src7hmnlrtmirgt, 244, 52);
            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)t99) + (uintptr_t)((uint64_t)0ULL) * 16));
            void * t102 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t103)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t103, gm_src7hmnlrtmirgt, 244, 61);
            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)t102) + (uintptr_t)((uint64_t)1ULL) * 16));
            void * t105 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t106)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t106, gm_src7hmnlrtmirgt, 244, 70);
            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)t105) + (uintptr_t)((uint64_t)2ULL) * 16));
            gm_b16_8 t108 = gm_f_vec_select_3f37tvf((void *)NULL, cx, NULL, t94, t95, t98, t101, t104, t107);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t109;
            GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t109 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t109 + 32, 0);
            ((uint8_t *)&t109)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t109 + 8), t108);
            memmove(gm_out, ((uint8_t *)&t109), 40);
            goto gm_fdone;
        }
gm_lb20:;
        {
            {
                {
                    uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t111 = (uint8_t)(t110 == ((uint8_t)53ULL));
                    if (!t111) {
                        goto gm_lb23;
                    }
                    goto gm_lb22;
                }
gm_lb23:;
                {
                    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t113 = (uint8_t)(t112 == ((uint8_t)54ULL));
                    if (!t113) {
                        goto gm_lb24;
                    }
                    goto gm_lb22;
                }
gm_lb24:;
                {
                    uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t115 = (uint8_t)(t114 == ((uint8_t)55ULL));
                    if (!t115) {
                        goto gm_lb25;
                    }
                    goto gm_lb22;
                }
gm_lb25:;
                goto gm_lb21;
            }
gm_lb22:;
            void * t116 = gm_l0;
            gm_tl874t2atnlh3 t117 = gm_l1;
            gm_t88rcucg1b7uk t118 = gm_l2;
            gm_tvdg94bk7aa22 t119 = gm_l3;
            gm_b16_8 t120 = gm_l5;
            gm_f_vec_minmax_abs_4le8bpj((void *)&gm_l8, (void *)NULL, cx, NULL, t116, t117, t118, t119, t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t121 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (t121) {
            } else {
                gm_t08olq97rscn3 t122 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l8) + 8));
                gm_t5o0tpv5jka2l t123;
                GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t123 + 4, 0);
                ((uint8_t *)&t123)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t123 + 8), t122);
                memmove(gm_out, ((uint8_t *)&t123), 40);
                goto gm_fdone;
            }
            gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_t5o0tpv5jka2l t125;
            GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t125 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t125 + 32, 0);
            ((uint8_t *)&t125)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t125 + 8), t124);
            memmove(gm_out, ((uint8_t *)&t125), 40);
            goto gm_fdone;
        }
gm_lb21:;
        {
            uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t127 = (uint8_t)(t126 == ((uint8_t)56ULL));
            if (!t127) {
                goto gm_lb26;
            }
            void * t128 = gm_l0;
            gm_t88rcucg1b7uk t129 = gm_l2;
            void * t130 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t131)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t131, gm_src7hmnlrtmirgt, 246, 50);
            gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t130) + (uintptr_t)((uint64_t)0ULL) * 4));
            void * t133 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t134)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t134, gm_src7hmnlrtmirgt, 246, 58);
            gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)t133) + (uintptr_t)((uint64_t)0ULL) * 16));
            gm_b16_8 t136 = gm_f_vec_convert_94b6r5u((void *)NULL, cx, NULL, t128, t129, t132, t135);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t137;
            GM_ST(uint8_t, (uint8_t *)&t137 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t137 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t137 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t137 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t137 + 32, 0);
            ((uint8_t *)&t137)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t137 + 8), t136);
            memmove(gm_out, ((uint8_t *)&t137), 40);
            goto gm_fdone;
        }
gm_lb26:;
        {
            uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t139 = (uint8_t)(t138 == ((uint8_t)57ULL));
            if (!t139) {
                goto gm_lb27;
            }
            void * t140 = gm_l0;
            gm_t88rcucg1b7uk t141 = gm_l2;
            void * t142 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t143)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t143, gm_src7hmnlrtmirgt, 247, 46);
            gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t142) + (uintptr_t)((uint64_t)0ULL) * 4));
            void * t145 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t146)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t146, gm_src7hmnlrtmirgt, 247, 54);
            gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)t145) + (uintptr_t)((uint64_t)0ULL) * 16));
            gm_b16_8 t148 = gm_l6;
            gm_b16_8 t149 = gm_f_vec_shuffle_j6jmffl((void *)NULL, cx, NULL, t140, t141, t144, t147, t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t150;
            GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t150 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t150 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t150 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t150 + 32, 0);
            ((uint8_t *)&t150)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t150 + 8), t149);
            memmove(gm_out, ((uint8_t *)&t150), 40);
            goto gm_fdone;
        }
gm_lb27:;
        {
            void * t151 = gm_l0;
            gm_tl874t2atnlh3 t152 = gm_l1;
            gm_t88rcucg1b7uk t153 = gm_l2;
            gm_tvdg94bk7aa22 t154 = gm_l3;
            gm_b16_8 t155 = gm_l4;
            gm_b16_8 t156 = gm_l5;
            gm_b16_8 t157 = gm_f_vec_load_store_ng9gfpc((void *)NULL, cx, NULL, t151, t152, t153, t154, t155, t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t158;
            GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t158 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t158 + 32, 0);
            ((uint8_t *)&t158)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t158 + 8), t157);
            memmove(gm_out, ((uint8_t *)&t158), 40);
            goto gm_fdone;
        }
gm_lb28:;
        gm_trap("no match arm matched the scrutinee", gm_src7hmnlrtmirgt, 240, 5);
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
__attribute__((aligned(4))) void gm_f_vector_unary_value_o07noaa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, gm_b16_8 a4) {
    struct gm_fr_vector_unary_value_o07noaa F;
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t9qkh0rsooc8d gm_l3; /* op */
    gm_b16_8 gm_l4; /* v */
    gm_tismvprs8r13f gm_l5; /* vp */
    uint64_t gm_l6; /* n */
    gm_b16_8 gm_l10; /* a */
    gm_b16_8 gm_l11; /* r */
    gm_b16_8 gm_l12; /* u */
    gm_b16_8 gm_l13; /* s */
    GM_STACK_CHECK(&F, 1303u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t5o0tpv5jka2l gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    void * t4 = F.l0;
    gm_t88rcucg1b7uk t5 = gm_l2;
    gm_f_vec_parts_srl0rr9((void *)&gm_l5, t4, GM_CONV(gm_csguankko4lmhd, t5));
    uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    gm_l6 = t6;
    gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
    F.l7 = t7;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
    F.l8 = t8;
    {
        {
            gm_t9qkh0rsooc8d t9 = gm_l3;
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
            uint8_t t11 = (uint8_t)((t10 == ((uint8_t)0ULL)));
            if (!t11) {
                goto gm_lb2;
            }
            void * t12 = F.l0;
            void * t13 = GM_LD(void *, ((uint8_t *)t12));
            void * t14 = (void *)(((uint8_t *)t13) + 8);
            gm_t88rcucg1b7uk t15 = F.l7;
            gm_t9qkh0rsooc8d t16;
            GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
            uint8_t t17 = gm_f_is_integer_eo6jkc5(t14, GM_CONV(gm_csguankko4lmhd, t15));
            if (!t17) {
                goto gm_lb2;
            }
            void * t18 = F.l0;
            void * t19 = GM_LD(void *, ((uint8_t *)t18));
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)t19) + 1136));
            if (!t20) {
                goto gm_lb2;
            }
            void * t21 = F.l0;
            gm_t88rcucg1b7uk t22 = gm_l2;
            gm_b16_8 t23 = gm_l4;
            gm_b16_8 t24 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t21, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l9 = t24;
            void * t25 = F.l0;
            gm_t88rcucg1b7uk t26 = gm_l2;
            uint64_t t27 = gm_l6;
            gm_closure t28 = ((gm_closure){(void *)gm_f_fn_j0gskii, (void *)&F.h, cx});
            gm_f_vec_lane_loop_a3gvooh((void *)&gm_l14, (void *)NULL, cx, NULL, t25, t26, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (t29) {
            } else {
                gm_t08olq97rscn3 t30 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l14) + 8));
                gm_t5o0tpv5jka2l t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                ((uint8_t *)&t31)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t31 + 8), t30);
                memmove(gm_out, ((uint8_t *)&t31), 40);
                goto gm_fdone;
            }
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            gm_t5o0tpv5jka2l t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 32, 0);
            ((uint8_t *)&t33)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t33 + 8), t32);
            memmove(gm_out, ((uint8_t *)&t33), 40);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t34 = F.l0;
    gm_t88rcucg1b7uk t35 = gm_l2;
    gm_b16_8 t36 = gm_l4;
    gm_b16_8 t37 = gm_f_vec_load_8m1nglt((void *)NULL, cx, NULL, t34, t35, t36);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t37;
    gm_b16_8 t38;
    GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(0));
    gm_l11 = t38;
    gm_t9qkh0rsooc8d t39 = gm_l3;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
    uint8_t t41 = (uint8_t)((t40 == ((uint8_t)0ULL)));
    gm_t9qkh0rsooc8d t42;
    GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)0ULL));
    if (t41) {
        void * t43 = F.l0;
        void * t44 = GM_LD(void *, ((uint8_t *)t43));
        void * t45 = (void *)(((uint8_t *)t44) + 8);
        gm_t88rcucg1b7uk t46 = F.l7;
        uint8_t t47 = gm_f_is_float_gthejtb(t45, GM_CONV(gm_csguankko4lmhd, t46));
        if (t47) {
            gm_b16_8 t48 = gm_l10;
            gm_tnpifv20irnph t49;
            GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
            gm_f_format_dlemvkb((void *)&gm_l15, (void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
            if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src7hmnlrtmirgt, 123, 17);
            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
            gm_l11 = t52;
        } else {
            void * t53 = F.l0;
            uint64_t t54 = gm_l6;
            gm_t88rcucg1b7uk t55 = F.l7;
            gm_tqs5p4n2tq19v t56;
            GM_ST(uint8_t, ((uint8_t *)&t56), ((uint8_t)0));
            ((uint8_t *)&t56)[1] = 1;
            gm_b16_8 t57 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t53, t54, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t57;
            void * t58 = F.l0;
            uint64_t t59 = gm_l6;
            gm_t88rcucg1b7uk t60 = F.l7;
            gm_tqs5p4n2tq19v t61;
            memset(&t61, 0, sizeof t61);
            gm_b16_8 t62 = gm_f_vec_ty_adod8ur((void *)NULL, cx, NULL, t58, t59, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t62;
            gm_b16_8 t63 = gm_l13;
            gm_b16_8 t64 = gm_l12;
            gm_b16_8 t65 = gm_l12;
            gm_b16_8 t66 = gm_l10;
            gm_t0uuhaljsf410 t67;
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t63);
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 16), t64);
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 32), t65);
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 48), t66);
            gm_f_format_jo1gjr5((void *)&gm_l16, (void *)NULL, cx, NULL, &t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
            if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src7hmnlrtmirgt, 127, 17);
            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
            gm_l11 = t70;
        }
    } else {
        void * t71 = F.l0;
        void * t72 = GM_LD(void *, ((uint8_t *)t71));
        void * t73 = (void *)(((uint8_t *)t72) + 8);
        gm_t88rcucg1b7uk t74 = F.l7;
        gm_t53at80pfdus3 t75;
        gm_f_kind_ghvav22((void *)&t75, t73, GM_CONV(gm_csguankko4lmhd, t74));
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&t75) + 0));
        uint8_t t77 = (uint8_t)((t76 == ((uint8_t)4ULL)));
        gm_t53at80pfdus3 t78;
        memset(&t78, 0, sizeof t78);
        GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)4ULL));
        if (t77) {
            gm_b16_8 t79 = gm_l10;
            gm_tnpifv20irnph t80;
            GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t79);
            gm_f_format_njau13r((void *)&gm_l17, (void *)NULL, cx, NULL, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t81 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (!t81) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_src7hmnlrtmirgt, 130, 13);
            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
            gm_l11 = t83;
        } else {
            gm_b16_8 t84 = gm_l10;
            gm_tnpifv20irnph t85;
            GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t84);
            gm_f_format_nhrpa25((void *)&gm_l18, (void *)NULL, cx, NULL, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src7hmnlrtmirgt, 132, 13);
            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
            gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
            gm_l11 = t88;
        }
    }
    void * t89 = F.l0;
    gm_t88rcucg1b7uk t90 = gm_l2;
    gm_b16_8 t91 = gm_l11;
    gm_b16_8 t92 = gm_f_vec_store_6d0dm94((void *)NULL, cx, NULL, t89, t90, t91);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t5o0tpv5jka2l t93;
    GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t93 + 24, 0);
    GM_ST(uint64_t, (uint8_t *)&t93 + 32, 0);
    ((uint8_t *)&t93)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t93 + 8), t92);
    memmove(gm_out, ((uint8_t *)&t93), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
