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
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_ti0g3bmheuq9o v1; } f1; } gm_t171adjpb0m86;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tr8dvlu9ik4gj v1; } f1; } gm_t5detta1b4gnk;
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
typedef struct { gm_ti0g3bmheuq9o f0; uint8_t f1; uint8_t pe[7]; } gm_tmktnt0k5ner6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tin97tt4ud0s1 v0; gm_b16_8 v1; gm_b16_8 v2; gm_b16_8 v3; } f1; } gm_toq2f2q4opoa3;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
typedef struct { gm_tin97tt4ud0s1 f0; uint8_t f1; uint8_t pe[7]; } gm_tv37d3dpt2opu;
_Static_assert(sizeof(gm_t171adjpb0m86) == 160 && _Alignof(gm_t171adjpb0m86) == 8 && offsetof(gm_t171adjpb0m86, f0) == 0 && offsetof(gm_t171adjpb0m86, f1) == 8, "layout of `string!BuildSettings`");
typedef struct { gm_tv37d3dpt2opu f0; gm_trqfvmjtt4sij f1; gm_tr8dvlu9ik4gj f2; } gm_t4p8mfn47994q;
_Static_assert(sizeof(gm_t5detta1b4gnk) == 128 && _Alignof(gm_t5detta1b4gnk) == 8 && offsetof(gm_t5detta1b4gnk, f0) == 0 && offsetof(gm_t5detta1b4gnk, f1) == 8, "layout of `string!Extras`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tmktnt0k5ner6 v1; } f1; } gm_t9h00sl1nspi5;
typedef struct { gm_b16_8 f0; gm_toq2f2q4opoa3 f1; gm_th2lqbo77pvsc f2; } gm_te5jk64pjl28k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tikairc198460;
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_tmktnt0k5ner6) == 160 && _Alignof(gm_tmktnt0k5ner6) == 8 && offsetof(gm_tmktnt0k5ner6, f0) == 0 && offsetof(gm_tmktnt0k5ner6, f1) == 152, "layout of `?BuildSettings`");
_Static_assert(sizeof(gm_toq2f2q4opoa3) == 48 && _Alignof(gm_toq2f2q4opoa3) == 8 && offsetof(gm_toq2f2q4opoa3, f0) == 0 && offsetof(gm_toq2f2q4opoa3, f1) == 8, "layout of `GitSpec`");
_Static_assert(sizeof(gm_tv37d3dpt2opu) == 48 && _Alignof(gm_tv37d3dpt2opu) == 8 && offsetof(gm_tv37d3dpt2opu, f0) == 0 && offsetof(gm_tv37d3dpt2opu, f1) == 40, "layout of `?Resolution`");
_Static_assert(sizeof(gm_t4p8mfn47994q) == 208 && _Alignof(gm_t4p8mfn47994q) == 8 && offsetof(gm_t4p8mfn47994q, f0) == 0 && offsetof(gm_t4p8mfn47994q, f1) == 48 && offsetof(gm_t4p8mfn47994q, f2) == 88, "layout of `Collector`");
_Static_assert(sizeof(gm_t9h00sl1nspi5) == 168 && _Alignof(gm_t9h00sl1nspi5) == 8 && offsetof(gm_t9h00sl1nspi5, f0) == 0 && offsetof(gm_t9h00sl1nspi5, f1) == 8, "layout of `string!?BuildSettings`");
_Static_assert(sizeof(gm_te5jk64pjl28k) == 88 && _Alignof(gm_te5jk64pjl28k) == 8 && offsetof(gm_te5jk64pjl28k, f0) == 0 && offsetof(gm_te5jk64pjl28k, f1) == 16 && offsetof(gm_te5jk64pjl28k, f2) == 64, "layout of `GitSource`");
_Static_assert(sizeof(gm_tikairc198460) == 328 && _Alignof(gm_tikairc198460) == 8 && offsetof(gm_tikairc198460, f0) == 0 && offsetof(gm_tikairc198460, f1) == 8, "layout of `string!Manifest`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_te5jk64pjl28k v1; gm_tt008mgut8glb v2; } f1; } gm_tnlfbo6r4vlhg;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_tnlfbo6r4vlhg f2; } gm_t7tiv6vif86qg;
_Static_assert(sizeof(gm_tnlfbo6r4vlhg) == 96 && _Alignof(gm_tnlfbo6r4vlhg) == 8 && offsetof(gm_tnlfbo6r4vlhg, f0) == 0 && offsetof(gm_tnlfbo6r4vlhg, f1) == 8, "layout of `Source`");
_Static_assert(sizeof(gm_t7tiv6vif86qg) == 120 && _Alignof(gm_t7tiv6vif86qg) == 8 && offsetof(gm_t7tiv6vif86qg, f0) == 0 && offsetof(gm_t7tiv6vif86qg, f1) == 16 && offsetof(gm_t7tiv6vif86qg, f2) == 24, "layout of `Dependency`");
struct gm_fr_canonicalize_epo7cln {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_collect_0f0qu2r {
    gm_frame h;
    gm_tv37d3dpt2opu l1; /* resolution */
    gm_tm43kqe3e8b4a l4; /* manifest */
    gm_ti0g3bmheuq9o l6; /* settings */
    gm_t4p8mfn47994q l7; /* c */
    gm_t5detta1b4gnk ret;
};
struct gm_fr_dep_path_h9fcj1s {
    gm_frame h;
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fklmfmo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extras_joquqpb {
    gm_frame h;
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_find_root_kjnm6hf {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_format_f6rro2t {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_icvpspa {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_u6l3ek2 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vmb8nb9 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_is_absolute_8fbem8j {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_empty_8l7niee {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_0k0up67 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_sb1m5es {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_7h11c55 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_86t0l9s {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_mgis1jm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_resolve_path_kpnoes9 {
    gm_frame h;
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_root_settings_cs4jjnk {
    gm_frame h;
    gm_tm43kqe3e8b4a l4; /* manifest */
    gm_ti0g3bmheuq9o l7; /* settings */
    gm_t9h00sl1nspi5 ret;
};
struct gm_fr_same_strings_0sh4jt2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_split_root_len_eksvdj7 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_starts_with_ahdikpt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_visit_n1kqfoj {
    gm_frame h;
    gm_ti0g3bmheuq9o l4; /* settings */
    gm_trqfvmjtt4sij l6; /* include_dirs */
    gm_t7tiv6vif86qg l35; /* dep */
    gm_tin97tt4ud0s1 l38; /* res */
    gm_trqfvmjtt4sij l39; /* deps */
    gm_tm43kqe3e8b4a l50; /* m */
    gm_tl572d7579hns ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_build_settings_brkieib(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_collect_0f0qu2r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_dep_path_h9fcj1s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_deps_of_i4no6j9(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fklmfmo(void);
inline __attribute__((aligned(4))) void gm_f_extras_joquqpb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_icvpspa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_u6l3ek2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_vmb8nb9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_absolute_8fbem8j(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_8l7niee(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_0k0up67(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_list_7h11c55(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_bch4n2l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_86t0l9s(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_read_manifest_u9uajvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_mgis1jm(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_resolve_path_kpnoes9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) void gm_f_root_settings_cs4jjnk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_same_strings_0sh4jt2(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint64_t gm_f_split_root_len_eksvdj7(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_vl3t0h4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_visit_n1kqfoj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_b16_8 a3, const void * a4, uint8_t a5);
extern const uint8_t gm_s4t1alq7juu7a[67];
extern const uint8_t gm_s60n1k0cqjuaq[144];
extern const uint8_t gm_s6ac0uac1dtsl[62];
extern const uint8_t gm_s8gf7bnv27fi9[7];
extern const uint8_t gm_sdbqk29a3n8jm[13];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_spec9er6f839v[17];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src9gqs06mvt4n2[];
extern const char gm_srcc2qutubarto1[];
extern const uint8_t gm_srcf8l485c5uq[10];
extern const char gm_srcne58m9cqp42d[];
extern const char gm_srcoj9bno4fi1q4[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_svkmjs6p8bhtg[9];
extern const gm_tmpl gm_tpl24fci6dm83eo;
extern const gm_tmpl gm_tpl4476c5bksft1;
extern const gm_tmpl gm_tploik9nhlf98dm;
extern const gm_tmpl gm_tpltpd3ap2ql1ip;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonicalize_epo7cln F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_f_canonicalize_n3e3qri((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fklmfmo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fklmfmo F;
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
inline __attribute__((aligned(4))) void gm_f_extras_joquqpb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_extras_joquqpb F;
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_7h11c55((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_epvducg((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t4;
    gm_f_list_epvducg((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tr8dvlu9ik4gj t5;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t5 + 0), t2);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t5 + 40), t3);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t5 + 80), t4);
    memmove(gm_out, ((uint8_t *)&t5), 120);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_find_root_kjnm6hf F;
    gm_b16_8 gm_l0; /* start */
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    gm_th2lqbo77pvsc gm_l2;
    gm_b16_8 t5 = gm_l0;
    gm_f_canonicalize_epo7cln((void *)&gm_l2, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
            if (t6) {
                goto gm_lb2;
            }
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l1 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 11, 8);
    }
gm_lb1:;
    uint8_t t7 = gm_l1;
    if (t7) {
        gm_th2lqbo77pvsc t8;
        memset(&t8, 0, sizeof t8);
        memmove(gm_out, ((uint8_t *)&t8), 24);
        goto gm_fdone;
    }
    gm_b16_8 t9 = gm_l0;
    gm_f_find_package_root_fs4fuqj((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t9);
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
inline __attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_f6rro2t F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8gf7bnv27fi9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4476c5bksft1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_icvpspa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_icvpspa F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4t1alq7juu7a));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(66));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpltpd3ap2ql1ip), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_u6l3ek2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_u6l3ek2 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s6ac0uac1dtsl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(61));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl24fci6dm83eo), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_vmb8nb9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vmb8nb9 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s60n1k0cqjuaq));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(143));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tploik9nhlf98dm), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_absolute_8fbem8j(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_absolute_8fbem8j F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    uint64_t t3 = gm_f_split_root_len_eksvdj7(t2);
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_8l7niee(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_empty_8l7niee F;
    void * gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
    if (t4) {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 40) + 8));
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        gm_l2 = t7;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t8 = gm_l2;
    if (t8) {
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t9) + 80) + 8));
        uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
        gm_l1 = t11;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t12 = gm_l1;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_0k0up67(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_0k0up67 F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    uint8_t t3 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_tcuv9rv F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 183u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tih6h4avv5nrb gm_l2;
    gm_t5j14m8ru132p gm_l3;
    gm_b16_8 t2 = gm_l0;
    gm_node *pn3 = gm_find(cx, 2126215853);
    if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 234, 13);
    gm_tih6h4avv5nrb t4;
    ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[5])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t5) {
        memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l2) + 8), 32);
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l3), 32);
    gm_t9qkh0rsooc8d t6 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 8));
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    uint8_t t8 = (uint8_t)((t7 == ((uint8_t)0ULL)));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)0ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_join_sb1m5es F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_7h11c55(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7h11c55 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fklmfmo();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_86t0l9s(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_86t0l9s F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_fklmfmo();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_mgis1jm(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_mgis1jm(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_mgis1jm F;
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
inline __attribute__((aligned(4))) uint64_t gm_f_split_root_len_eksvdj7(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_split_root_len_eksvdj7 F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t gm_l1;
    {
        {
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t5 = (t4 > ((uint64_t)0ULL));
            if (!t5) {
                goto gm_lb2;
            }
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t9 = ((t8 == ((uint8_t)47ULL)));
            if (!t9) {
                goto gm_lb2;
            }
            gm_l1 = ((uint64_t)1ULL);
            goto gm_lb1;
        }
gm_lb2:;
        gm_l1 = ((uint64_t)0ULL);
    }
gm_lb1:;
    uint64_t t10 = gm_l1;
    F.ret = t10;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_starts_with_ahdikpt F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* base */
    GM_STACK_CHECK(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_th2lqbo77pvsc gm_l3;
    gm_b16_8 t5 = gm_l0;
    gm_b16_8 t6 = gm_l1;
    gm_f_strip_prefix_vl3t0h4((void *)&gm_l3, (void *)NULL, cx, NULL, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (t7) {
                goto gm_lb2;
            }
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcne58m9cqp42d, 120, 72);
    }
gm_lb1:;
    uint8_t t8 = gm_l2;
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
inline __attribute__((aligned(4))) void gm_f_collect_0f0qu2r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, const void * a2) {
    struct gm_fr_collect_0f0qu2r F;
    gm_b16_8 gm_l0; /* input */
    gm_th2lqbo77pvsc gm_l2; /* profile */
    gm_b16_8 gm_l3; /* root */
    gm_b16_8 gm_l5; /* e */
    gm_b16_8 gm_l8; /* name */
    GM_STACK_CHECK(&F, 2683u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l9;
    gm_b16_8 gm_l10;
    gm_tikairc198460 gm_l11;
    gm_t171adjpb0m86 gm_l12;
    gm_ti0g3bmheuq9o gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_b16_8 gm_l16;
    gm_tl572d7579hns gm_l17;
    gm_b16_8 t2 = gm_l0;
    gm_f_find_root_kjnm6hf((void *)&gm_l9, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
    if (t3) {
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
        gm_l10 = t4;
    } else {
        gm_tr8dvlu9ik4gj t5;
        gm_f_extras_joquqpb((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5detta1b4gnk t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
        ((uint8_t *)&t6)[0] = 0;
        GM_ST(gm_tr8dvlu9ik4gj, ((uint8_t *)&t6 + 8), t5);
        memmove(gm_out, ((uint8_t *)&t6), 128);
        goto gm_fdone;
    }
    gm_b16_8 t7 = gm_l10;
    gm_l3 = t7;
    gm_b16_8 t8 = gm_l3;
    gm_f_read_manifest_u9uajvn((void *)&gm_l11, (void *)NULL, cx, NULL, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (t9) {
    } else {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        gm_t5detta1b4gnk t11;
        memset(&t11, 0, sizeof t11);
        ((uint8_t *)&t11)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t11 + 8), t10);
        memmove(gm_out, ((uint8_t *)&t11), 128);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l11) + 8), 320);
    void * t12 = (void *)((uint8_t *)&F.l4);
    gm_th2lqbo77pvsc t13 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a2));
    gm_f_build_settings_brkieib((void *)&gm_l12, (void *)NULL, cx, NULL, t12, &t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (t14) {
        memmove(((uint8_t *)&gm_l13), (((uint8_t *)&gm_l12) + 8), 152);
    } else {
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_l5 = t15;
        gm_b16_8 t16 = gm_l3;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_se24c7hpp401n));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(10));
        gm_b16_8 t18 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t19 = gm_l5;
        gm_t6j6smj7qoctu t20;
        GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t18);
        GM_ST(gm_b16_8, ((uint8_t *)&t20 + 16), t19);
        gm_f_format_f6rro2t((void *)&gm_l14, (void *)NULL, cx, NULL, &t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src9gqs06mvt4n2, 150, 77);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        gm_t5detta1b4gnk t24;
        memset(&t24, 0, sizeof t24);
        ((uint8_t *)&t24)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t24 + 8), t23);
        memmove(gm_out, ((uint8_t *)&t24), 128);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l6), ((uint8_t *)&gm_l13), 152);
    gm_tv37d3dpt2opu t25 = GM_LD(gm_tv37d3dpt2opu, ((uint8_t *)a1));
    gm_trqfvmjtt4sij t26;
    gm_f_list_epvducg((void *)&t26, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tr8dvlu9ik4gj t27;
    gm_f_extras_joquqpb((void *)&t27, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4p8mfn47994q t28;
    GM_ST(gm_tv37d3dpt2opu, ((uint8_t *)&t28 + 0), t25);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t28 + 48), t26);
    GM_ST(gm_tr8dvlu9ik4gj, ((uint8_t *)&t28 + 88), t27);
    F.l7 = t28;
    void * t29 = (void *)(((uint8_t *)&F.l7) + 48);
    gm_b16_8 t30 = gm_l3;
    gm_tftb1odc739t5 t31 = gm_f_push_3ef746b(t29, t30);
    gm_l15 = t31;
    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_src9gqs06mvt4n2, 152, 5);
    uint8_t t33 = (uint8_t)((((uint8_t *)&F.l4))[16] != 0);
    if (t33) {
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_l16 = t34;
    } else {
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_spec9er6f839v));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(16));
        gm_l16 = t35;
    }
    gm_b16_8 t36 = gm_l16;
    gm_l8 = t36;
    void * t37 = (void *)((uint8_t *)&F.l7);
    gm_b16_8 t38 = gm_l3;
    void * t39 = (void *)((uint8_t *)&F.l4);
    gm_b16_8 t40 = gm_l8;
    gm_ti0g3bmheuq9o t41 = GM_LD(gm_ti0g3bmheuq9o, ((uint8_t *)&F.l6));
    gm_f_visit_n1kqfoj((void *)&gm_l17, (void *)NULL, cx, NULL, t37, t38, t39, t40, &t41, ((uint8_t)1));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (t42) {
    } else {
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
        gm_t5detta1b4gnk t44;
        memset(&t44, 0, sizeof t44);
        ((uint8_t *)&t44)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t44 + 8), t43);
        memmove(gm_out, ((uint8_t *)&t44), 128);
        goto gm_fdone;
    }
    gm_tr8dvlu9ik4gj t45 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)&F.l7) + 88));
    gm_t5detta1b4gnk t46;
    GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
    ((uint8_t *)&t46)[0] = 0;
    GM_ST(gm_tr8dvlu9ik4gj, ((uint8_t *)&t46 + 8), t45);
    memmove(gm_out, ((uint8_t *)&t46), 128);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_dep_path_h9fcj1s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_dep_path_h9fcj1s F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* rel */
    gm_b16_8 gm_l2; /* key */
    gm_b16_8 gm_l3; /* pkg */
    gm_b16_8 gm_l4; /* joined */
    GM_STACK_CHECK(&F, 414u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t5;
    gm_b16_8 t6 = gm_l1;
    uint8_t t7 = gm_f_is_absolute_8fbem8j(t6);
    if (t7) {
        gm_l5 = ((uint8_t)1);
    } else {
        gm_b16_8 t8 = gm_l4;
        gm_b16_8 t9 = gm_l0;
        uint8_t t10 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t11 = (uint8_t)!t10;
        gm_l5 = t11;
    }
    uint8_t t12 = gm_l5;
    if (t12) {
        gm_b16_8 t13 = gm_l2;
        gm_b16_8 t14 = gm_l3;
        gm_b16_8 t15 = gm_l1;
        gm_tfeab279c16rr t16;
        GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t13);
        GM_ST(gm_b16_8, ((uint8_t *)&t16 + 16), t14);
        GM_ST(gm_b16_8, ((uint8_t *)&t16 + 32), t15);
        gm_f_format_vmb8nb9((void *)&gm_l6, (void *)NULL, cx, NULL, &t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_src9gqs06mvt4n2, 51, 17);
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_t3ujhu6qph7de t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
        ((uint8_t *)&t20)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t20 + 8), t19);
        memmove(gm_out, ((uint8_t *)&t20), 24);
        goto gm_fdone;
    }
    gm_b16_8 t21 = gm_l4;
    gm_t3ujhu6qph7de t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
    ((uint8_t *)&t22)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t22 + 8), t21);
    memmove(gm_out, ((uint8_t *)&t22), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_extras_joquqpb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) uint8_t gm_f_is_empty_8l7niee(void * a0);
inline __attribute__((aligned(4))) void gm_f_resolve_path_kpnoes9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, uint8_t a4) {
    struct gm_fr_resolve_path_kpnoes9 F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* rel */
    gm_b16_8 gm_l2; /* key */
    gm_b16_8 gm_l3; /* name */
    uint8_t gm_l4; /* root */
    GM_STACK_CHECK(&F, 218u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t t2 = gm_l4;
    if (t2) {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4 = gm_l1;
        gm_b16_8 t5 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t6 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3ujhu6qph7de t7;
        GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
        ((uint8_t *)&t7)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t6);
        memmove(gm_out, ((uint8_t *)&t7), 24);
        goto gm_fdone;
    }
    gm_b16_8 t8 = gm_l0;
    gm_b16_8 t9 = gm_l1;
    gm_b16_8 t10 = gm_l2;
    gm_b16_8 t11 = gm_l3;
    gm_f_dep_path_h9fcj1s((void *)&(*(gm_t3ujhu6qph7de *)gm_out), (void *)NULL, cx, NULL, t8, t9, t10, t11);
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
inline __attribute__((aligned(4))) void gm_f_root_settings_cs4jjnk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_root_settings_cs4jjnk F;
    gm_b16_8 gm_l0; /* input */
    gm_th2lqbo77pvsc gm_l1; /* profile */
    gm_b16_8 gm_l2; /* p */
    gm_b16_8 gm_l3; /* root */
    gm_b16_8 gm_l5; /* path */
    gm_b16_8 gm_l6; /* e */
    GM_STACK_CHECK(&F, 2534u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_th2lqbo77pvsc gm_l8;
    gm_b16_8 gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tikairc198460 gm_l11;
    gm_t171adjpb0m86 gm_l12;
    gm_ti0g3bmheuq9o gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_b16_8 t4 = gm_l0;
    gm_f_find_root_kjnm6hf((void *)&gm_l8, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l8))[16] != 0);
    if (t5) {
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
        gm_l9 = t6;
    } else {
        {
            {
                uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l1))[16] != 0);
                if (!t7) {
                    goto gm_lb2;
                }
                gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
                gm_l2 = t8;
                gm_b16_8 t9 = gm_l2;
                gm_b16_8 t10 = gm_l0;
                gm_t6j6smj7qoctu t11;
                GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t9);
                GM_ST(gm_b16_8, ((uint8_t *)&t11 + 16), t10);
                gm_f_format_icvpspa((void *)&gm_l10, (void *)NULL, cx, NULL, &t11);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src9gqs06mvt4n2, 36, 21);
                gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
                gm_t9h00sl1nspi5 t15;
                memset(&t15, 0, sizeof t15);
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t15 + 8), t14);
                memmove(gm_out, ((uint8_t *)&t15), 168);
                goto gm_fdone;
            }
gm_lb2:;
        }
gm_lb1:;
        gm_tmktnt0k5ner6 t16;
        memset(&t16, 0, sizeof t16);
        gm_t9h00sl1nspi5 t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
        ((uint8_t *)&t17)[0] = 0;
        GM_ST(gm_tmktnt0k5ner6, ((uint8_t *)&t17 + 8), t16);
        memmove(gm_out, ((uint8_t *)&t17), 168);
        goto gm_fdone;
    }
    gm_b16_8 t18 = gm_l9;
    gm_l3 = t18;
    gm_b16_8 t19 = gm_l3;
    gm_f_read_manifest_u9uajvn((void *)&gm_l11, (void *)NULL, cx, NULL, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (t20) {
    } else {
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        gm_t9h00sl1nspi5 t22;
        memset(&t22, 0, sizeof t22);
        ((uint8_t *)&t22)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 8), t21);
        memmove(gm_out, ((uint8_t *)&t22), 168);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l11) + 8), 320);
    gm_b16_8 t23 = gm_l3;
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_se24c7hpp401n));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(10));
    gm_b16_8 t25 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t23, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t25;
    void * t26 = (void *)((uint8_t *)&F.l4);
    gm_th2lqbo77pvsc t27 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l1));
    gm_f_build_settings_brkieib((void *)&gm_l12, (void *)NULL, cx, NULL, t26, &t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (t28) {
        memmove(((uint8_t *)&gm_l13), (((uint8_t *)&gm_l12) + 8), 152);
    } else {
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_l6 = t29;
        gm_b16_8 t30 = gm_l5;
        gm_b16_8 t31 = gm_l6;
        gm_t6j6smj7qoctu t32;
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t30);
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 16), t31);
        gm_f_format_f6rro2t((void *)&gm_l14, (void *)NULL, cx, NULL, &t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src9gqs06mvt4n2, 42, 77);
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
        gm_t9h00sl1nspi5 t36;
        memset(&t36, 0, sizeof t36);
        ((uint8_t *)&t36)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t36 + 8), t35);
        memmove(gm_out, ((uint8_t *)&t36), 168);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l7), ((uint8_t *)&gm_l13), 152);
    gm_ti0g3bmheuq9o t37 = GM_LD(gm_ti0g3bmheuq9o, ((uint8_t *)&F.l7));
    gm_tmktnt0k5ner6 t38;
    GM_ST(uint8_t, (uint8_t *)&t38 + 153, 0);
    GM_ST(uint16_t, (uint8_t *)&t38 + 154, 0);
    GM_ST(uint32_t, (uint8_t *)&t38 + 156, 0);
    GM_ST(gm_ti0g3bmheuq9o, ((uint8_t *)&t38), t37);
    ((uint8_t *)&t38)[152] = 1;
    gm_t9h00sl1nspi5 t39;
    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
    ((uint8_t *)&t39)[0] = 0;
    GM_ST(gm_tmktnt0k5ner6, ((uint8_t *)&t39 + 8), t38);
    memmove(gm_out, ((uint8_t *)&t39), 168);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_same_strings_0sh4jt2(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_strings_0sh4jt2 F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t9 = (!(t7 == t8));
    if (t9) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_l3 = ((uint64_t)0ULL);
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l2 = t10;
    {
        for (;;) {
            uint64_t t11 = gm_l3;
            uint64_t t12 = gm_l2;
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t14 = gm_l3;
                        gm_l4 = t14;
                        void * t15 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t17 = gm_l4;
                        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t15) + (uintptr_t)t17 * 16));
                        void * t19 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t21 = gm_l4;
                        if ((uintptr_t)t21 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t21, t20, gm_src9gqs06mvt4n2, 134, 24);
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t19) + (uintptr_t)t21 * 16));
                        uint8_t t23 = (!gm_eq0iqiubfm7e(((uint8_t *)&t18), ((uint8_t *)&t22)));
                        if (t23) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src9gqs06mvt4n2, 133, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t24 = gm_l3;
            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
            uint64_t t27 = r25;
            gm_l3 = t27;
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
__attribute__((aligned(4))) void gm_f_visit_n1kqfoj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_b16_8 a3, const void * a4, uint8_t a5) {
    struct gm_fr_visit_n1kqfoj F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* dir */
    void * gm_l2; /* manifest */
    gm_b16_8 gm_l3; /* name */
    uint8_t gm_l5; /* root */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* d */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l12; /* s */
    gm_b16_8 gm_l13; /* path */
    uint8_t gm_l14; /* dup */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_t6j6smj7qoctu gm_l17; /* x */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_b16_8 gm_l20; /* l */
    uint8_t gm_l21; /* dup */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_b16_8 gm_l24; /* x */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_b16_8 gm_l27; /* d */
    gm_b16_8 gm_l28; /* p */
    uint8_t gm_l29; /* dup */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    gm_b16_8 gm_l32; /* x */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    gm_b16_8 gm_l36; /* target */
    gm_th2lqbo77pvsc gm_l37; /* resolved */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_t6j6smj7qoctu gm_l42; /* d */
    gm_b16_8 gm_l43; /* t */
    gm_b16_8 gm_l44; /* p */
    gm_b16_8 gm_l45; /* canon */
    uint8_t gm_l46; /* seen */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    gm_b16_8 gm_l49; /* s */
    GM_STACK_CHECK(&F, 3972u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&F.l4, a4, sizeof F.l4);
    gm_l5 = a5;
    gm_b16_8 gm_l51;
    gm_t3ujhu6qph7de gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_b16_8 gm_l54;
    gm_t3ujhu6qph7de gm_l55;
    gm_tabb710d9mq2e gm_l56;
    gm_tftb1odc739t5 gm_l57;
    gm_b16_8 gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_b16_8 gm_l60;
    gm_t3ujhu6qph7de gm_l61;
    gm_tftb1odc739t5 gm_l62;
    gm_tqetrgo0q24b5 gm_l63;
    gm_th2lqbo77pvsc gm_l64;
    gm_b16_8 gm_l65;
    gm_tftb1odc739t5 gm_l66;
    gm_tikairc198460 gm_l67;
    gm_tl572d7579hns gm_l68;
    {
        gm_trqfvmjtt4sij t63;
        gm_f_list_epvducg((void *)&t63, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t63;
        uint8_t t64 = (uint8_t)(((((uint8_t *)&F.l4) + 80))[16] != 0);
        if (t64) {
            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 80));
            gm_l51 = t65;
        } else {
            gm_b16_8 t66 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
            gm_l51 = t67;
        }
        gm_b16_8 t68 = gm_l51;
        gm_l7 = t68;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t69 = gm_l8;
                uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t71 = (t69 < t70);
                if (!t71) {
                    goto gm_lb1;
                }
                {
                    void * t72 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t74 = gm_l8;
                    {
                        {
                            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)t72) + (uintptr_t)t74 * 16));
                            gm_l9 = t75;
                            void * t76 = (void *)((uint8_t *)&F.l6);
                            gm_b16_8 t77 = gm_l1;
                            gm_b16_8 t78 = gm_l9;
                            gm_b16_8 t79;
                            GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sdbqk29a3n8jm));
                            GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(12));
                            gm_b16_8 t80 = gm_l3;
                            uint8_t t81 = gm_l5;
                            gm_f_resolve_path_kpnoes9((void *)&gm_l52, (void *)NULL, cx, NULL, t77, t78, t79, t80, t81);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                            if (t82) {
                            } else {
                                gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                                gm_tl572d7579hns t84;
                                GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
                                ((uint8_t *)&t84)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t84 + 8), t83);
                                memmove(gm_out, ((uint8_t *)&t84), 24);
                                goto gm_fdone;
                            }
                            gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                            gm_tftb1odc739t5 t86 = gm_f_push_3ef746b(t76, t85);
                            gm_l53 = t86;
                            uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                            if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_src9gqs06mvt4n2, 70, 9);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src9gqs06mvt4n2, 69, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t88 = gm_l8;
                uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
                uint64_t t91 = r89;
                gm_l8 = t91;
            }
        }
gm_lb1:;
        uint8_t t92 = (uint8_t)(((((uint8_t *)&F.l4) + 56))[16] != 0);
        if (t92) {
            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 56));
            gm_l54 = t93;
        } else {
            gm_b16_8 t94 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
            gm_l54 = t95;
        }
        gm_b16_8 t96 = gm_l54;
        gm_l10 = t96;
        gm_l11 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t97 = gm_l11;
                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint8_t t99 = (t97 < t98);
                if (!t99) {
                    goto gm_lb6;
                }
                {
                    void * t100 = GM_LD(void *, ((uint8_t *)&gm_l10));
                    uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint64_t t102 = gm_l11;
                    {
                        {
                            gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)t100) + (uintptr_t)t102 * 16));
                            gm_l12 = t103;
                            gm_b16_8 t104 = gm_l1;
                            gm_b16_8 t105 = gm_l12;
                            gm_b16_8 t106;
                            GM_ST(void *, (uint8_t *)&t106, (void *)(gm_srcf8l485c5uq));
                            GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(9));
                            gm_b16_8 t107 = gm_l3;
                            uint8_t t108 = gm_l5;
                            gm_f_resolve_path_kpnoes9((void *)&gm_l55, (void *)NULL, cx, NULL, t104, t105, t106, t107, t108);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                            if (t109) {
                            } else {
                                gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
                                gm_tl572d7579hns t111;
                                GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                                ((uint8_t *)&t111)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t111 + 8), t110);
                                memmove(gm_out, ((uint8_t *)&t111), 24);
                                goto gm_fdone;
                            }
                            gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
                            gm_l13 = t112;
                            gm_b16_8 t113 = gm_l13;
                            uint8_t t114 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t113);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t115 = (uint8_t)!t114;
                            if (t115) {
                                gm_b16_8 t116 = gm_l1;
                                gm_b16_8 t117;
                                GM_ST(void *, (uint8_t *)&t117, (void *)(gm_se24c7hpp401n));
                                GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(10));
                                gm_b16_8 t118 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t116, t117);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t119 = gm_l3;
                                gm_b16_8 t120 = gm_l12;
                                gm_tfeab279c16rr t121;
                                GM_ST(gm_b16_8, ((uint8_t *)&t121 + 0), t118);
                                GM_ST(gm_b16_8, ((uint8_t *)&t121 + 16), t119);
                                GM_ST(gm_b16_8, ((uint8_t *)&t121 + 32), t120);
                                gm_f_format_u6l3ek2((void *)&gm_l56, (void *)NULL, cx, NULL, &t121);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 8, gm_src9gqs06mvt4n2, 75, 21);
                                gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l56) + 8));
                                gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&t123));
                                gm_tl572d7579hns t125;
                                GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                                ((uint8_t *)&t125)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t125 + 8), t124);
                                memmove(gm_out, ((uint8_t *)&t125), 24);
                                goto gm_fdone;
                            }
                            gm_l14 = ((uint8_t)0);
                            void * t126 = gm_l0;
                            gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)t126) + 88));
                            gm_l15 = t127;
                            gm_l16 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t128 = gm_l16;
                                    uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                    uint8_t t130 = (t128 < t129);
                                    if (!t130) {
                                        goto gm_lb11;
                                    }
                                    {
                                        void * t131 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                        uint64_t t132 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                        uint64_t t133 = gm_l16;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l17), (((uint8_t *)t131) + (uintptr_t)t133 * 32), 32);
                                                {
                                                    {
                                                        gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
                                                        gm_b16_8 t135 = gm_l13;
                                                        uint8_t t136 = (gm_eq0iqiubfm7e(((uint8_t *)&t134), ((uint8_t *)&t135)));
                                                        if (!t136) {
                                                            goto gm_lb17;
                                                        }
                                                        gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 16));
                                                        gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                                                        gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
                                                        uint8_t t140 = gm_f_same_strings_0sh4jt2(t137, t139);
                                                        if (!t140) {
                                                            goto gm_lb17;
                                                        }
                                                        gm_l14 = ((uint8_t)1);
                                                        goto gm_lb16;
                                                    }
gm_lb17:;
                                                }
gm_lb16:;
                                                goto gm_lb14;
                                            }
gm_lb15:;
                                            gm_trap("no match arm matched the scrutinee", gm_src9gqs06mvt4n2, 78, 9);
                                        }
gm_lb14:;
                                    }
gm_lb13:;
                                    uint64_t t141 = gm_l16;
                                    uint64_t r142; bool o143 = __builtin_add_overflow(t141, ((uint64_t)1ULL), &r142);
                                    uint64_t t144 = r142;
                                    gm_l16 = t144;
                                }
                            }
gm_lb11:;
                            uint8_t t145 = gm_l14;
                            uint8_t t146 = (uint8_t)!t145;
                            if (t146) {
                                void * t147 = gm_l0;
                                void * t148 = (void *)(((uint8_t *)t147) + 88);
                                gm_b16_8 t149 = gm_l13;
                                gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                                gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                                gm_t6j6smj7qoctu t152;
                                GM_ST(gm_b16_8, ((uint8_t *)&t152 + 0), t149);
                                GM_ST(gm_b16_8, ((uint8_t *)&t152 + 16), t151);
                                gm_tftb1odc739t5 t153 = gm_f_push_86t0l9s(t148, &t152);
                                gm_l57 = t153;
                                uint8_t t154 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                if (!t154) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_src9gqs06mvt4n2, 81, 22);
                            }
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_src9gqs06mvt4n2, 72, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t155 = gm_l11;
                uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
                uint64_t t158 = r156;
                gm_l11 = t158;
            }
        }
gm_lb6:;
    }
    {
        uint8_t t159 = (uint8_t)(((((uint8_t *)&F.l4) + 104))[16] != 0);
        if (t159) {
            gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 104));
            gm_l58 = t160;
        } else {
            gm_b16_8 t161 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
            gm_l58 = t162;
        }
        gm_b16_8 t163 = gm_l58;
        gm_l18 = t163;
        gm_l19 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t164 = gm_l19;
                uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                uint8_t t166 = (t164 < t165);
                if (!t166) {
                    goto gm_lb18;
                }
                {
                    void * t167 = GM_LD(void *, ((uint8_t *)&gm_l18));
                    uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint64_t t169 = gm_l19;
                    {
                        {
                            gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)t167) + (uintptr_t)t169 * 16));
                            gm_l20 = t170;
                            gm_l21 = ((uint8_t)0);
                            void * t171 = gm_l0;
                            gm_b16_8 t172 = GM_LD(gm_b16_8, ((((uint8_t *)t171) + 88) + 40));
                            gm_l22 = t172;
                            gm_l23 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t173 = gm_l23;
                                    uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                    uint8_t t175 = (t173 < t174);
                                    if (!t175) {
                                        goto gm_lb23;
                                    }
                                    {
                                        void * t176 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                        uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                        uint64_t t178 = gm_l23;
                                        {
                                            {
                                                gm_b16_8 t179 = GM_LD(gm_b16_8, (((uint8_t *)t176) + (uintptr_t)t178 * 16));
                                                gm_l24 = t179;
                                                gm_b16_8 t180 = gm_l24;
                                                gm_b16_8 t181 = gm_l20;
                                                uint8_t t182 = (gm_eq0iqiubfm7e(((uint8_t *)&t180), ((uint8_t *)&t181)));
                                                if (t182) {
                                                    gm_l21 = ((uint8_t)1);
                                                }
                                                goto gm_lb26;
                                            }
gm_lb27:;
                                            gm_trap("no match arm matched the scrutinee", gm_src9gqs06mvt4n2, 85, 9);
                                        }
gm_lb26:;
                                    }
gm_lb25:;
                                    uint64_t t183 = gm_l23;
                                    uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
                                    uint64_t t186 = r184;
                                    gm_l23 = t186;
                                }
                            }
gm_lb23:;
                            uint8_t t187 = gm_l21;
                            uint8_t t188 = (uint8_t)!t187;
                            if (t188) {
                                void * t189 = gm_l0;
                                void * t190 = (void *)((((uint8_t *)t189) + 88) + 40);
                                gm_b16_8 t191 = gm_l20;
                                gm_tftb1odc739t5 t192 = gm_f_push_3ef746b(t190, t191);
                                gm_l59 = t192;
                                uint8_t t193 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                if (!t193) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_src9gqs06mvt4n2, 88, 22);
                            }
                            goto gm_lb21;
                        }
gm_lb22:;
                        gm_trap("no match arm matched the scrutinee", gm_src9gqs06mvt4n2, 83, 5);
                    }
gm_lb21:;
                }
gm_lb20:;
                uint64_t t194 = gm_l19;
                uint64_t r195; bool o196 = __builtin_add_overflow(t194, ((uint64_t)1ULL), &r195);
                uint64_t t197 = r195;
                gm_l19 = t197;
            }
        }
gm_lb18:;
    }
    {
        uint8_t t198 = (uint8_t)(((((uint8_t *)&F.l4) + 128))[16] != 0);
        if (t198) {
            gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 128));
            gm_l60 = t199;
        } else {
            gm_b16_8 t200 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
            gm_l60 = t201;
        }
        gm_b16_8 t202 = gm_l60;
        gm_l25 = t202;
        gm_l26 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t203 = gm_l26;
                uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                uint8_t t205 = (t203 < t204);
                if (!t205) {
                    goto gm_lb28;
                }
                {
                    void * t206 = GM_LD(void *, ((uint8_t *)&gm_l25));
                    uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                    uint64_t t208 = gm_l26;
                    {
                        {
                            gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)t206) + (uintptr_t)t208 * 16));
                            gm_l27 = t209;
                            gm_b16_8 t210 = gm_l1;
                            gm_b16_8 t211 = gm_l27;
                            gm_b16_8 t212;
                            GM_ST(void *, (uint8_t *)&t212, (void *)(gm_svkmjs6p8bhtg));
                            GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(8));
                            gm_b16_8 t213 = gm_l3;
                            uint8_t t214 = gm_l5;
                            gm_f_resolve_path_kpnoes9((void *)&gm_l61, (void *)NULL, cx, NULL, t210, t211, t212, t213, t214);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t215 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                            if (t215) {
                            } else {
                                gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                                gm_tl572d7579hns t217;
                                GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t217 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t217 + 4, 0);
                                ((uint8_t *)&t217)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t217 + 8), t216);
                                memmove(gm_out, ((uint8_t *)&t217), 24);
                                goto gm_fdone;
                            }
                            gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                            gm_l28 = t218;
                            gm_l29 = ((uint8_t)0);
                            void * t219 = gm_l0;
                            gm_b16_8 t220 = GM_LD(gm_b16_8, ((((uint8_t *)t219) + 88) + 80));
                            gm_l30 = t220;
                            gm_l31 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t221 = gm_l31;
                                    uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                    uint8_t t223 = (t221 < t222);
                                    if (!t223) {
                                        goto gm_lb33;
                                    }
                                    {
                                        void * t224 = GM_LD(void *, ((uint8_t *)&gm_l30));
                                        uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                        uint64_t t226 = gm_l31;
                                        {
                                            {
                                                gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)t224) + (uintptr_t)t226 * 16));
                                                gm_l32 = t227;
                                                gm_b16_8 t228 = gm_l32;
                                                gm_b16_8 t229 = gm_l28;
                                                uint8_t t230 = (gm_eq0iqiubfm7e(((uint8_t *)&t228), ((uint8_t *)&t229)));
                                                if (t230) {
                                                    gm_l29 = ((uint8_t)1);
                                                }
                                                goto gm_lb36;
                                            }
gm_lb37:;
                                            gm_trap("no match arm matched the scrutinee", gm_src9gqs06mvt4n2, 93, 9);
                                        }
gm_lb36:;
                                    }
gm_lb35:;
                                    uint64_t t231 = gm_l31;
                                    uint64_t r232; bool o233 = __builtin_add_overflow(t231, ((uint64_t)1ULL), &r232);
                                    uint64_t t234 = r232;
                                    gm_l31 = t234;
                                }
                            }
gm_lb33:;
                            uint8_t t235 = gm_l29;
                            uint8_t t236 = (uint8_t)!t235;
                            if (t236) {
                                void * t237 = gm_l0;
                                void * t238 = (void *)((((uint8_t *)t237) + 88) + 80);
                                gm_b16_8 t239 = gm_l28;
                                gm_tftb1odc739t5 t240 = gm_f_push_3ef746b(t238, t239);
                                gm_l62 = t240;
                                uint8_t t241 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                if (!t241) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_src9gqs06mvt4n2, 96, 22);
                            }
                            goto gm_lb31;
                        }
gm_lb32:;
                        gm_trap("no match arm matched the scrutinee", gm_src9gqs06mvt4n2, 90, 5);
                    }
gm_lb31:;
                }
gm_lb30:;
                uint64_t t242 = gm_l26;
                uint64_t r243; bool o244 = __builtin_add_overflow(t242, ((uint64_t)1ULL), &r243);
                uint64_t t245 = r243;
                gm_l26 = t245;
            }
        }
gm_lb28:;
    }
    {
        void * t246 = gm_l2;
        gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)t246) + 48));
        gm_l33 = t247;
        gm_l34 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t248 = gm_l34;
                uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                uint8_t t250 = (t248 < t249);
                if (!t250) {
                    goto gm_lb38;
                }
                {
                    void * t251 = GM_LD(void *, ((uint8_t *)&gm_l33));
                    uint64_t t252 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                    uint64_t t253 = gm_l34;
                    {
                        {
                            memmove(((uint8_t *)&F.l35), (((uint8_t *)t251) + (uintptr_t)t253 * 120), 120);
                            gm_b16_8 t254;
                            GM_ST(void *, (uint8_t *)&t254, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t254 + 8, (uintptr_t)(0));
                            gm_l36 = t254;
                            gm_th2lqbo77pvsc t255;
                            memset(&t255, 0, sizeof t255);
                            gm_l37 = t255;
                            {
                                {
                                    void * t256 = gm_l0;
                                    uint8_t t257 = (uint8_t)((((uint8_t *)t256))[40] != 0);
                                    if (!t257) {
                                        goto gm_lb44;
                                    }
                                    memmove(((uint8_t *)&F.l38), ((uint8_t *)t256), 40);
                                    {
                                        {
                                            void * t258 = (void *)((uint8_t *)&F.l38);
                                            gm_b16_8 t259 = gm_l1;
                                            gm_f_deps_of_i4no6j9((void *)&gm_l63, t258, t259);
                                            uint8_t t260 = (uint8_t)((((uint8_t *)&gm_l63))[40] != 0);
                                            if (!t260) {
                                                goto gm_lb46;
                                            }
                                            memmove(((uint8_t *)&F.l39), ((uint8_t *)&gm_l63), 40);
                                            gm_b16_8 t261 = GM_LD(gm_b16_8, ((uint8_t *)&F.l39));
                                            gm_l40 = t261;
                                            gm_l41 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t262 = gm_l41;
                                                    uint64_t t263 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                                    uint8_t t264 = (t262 < t263);
                                                    if (!t264) {
                                                        goto gm_lb47;
                                                    }
                                                    {
                                                        void * t265 = GM_LD(void *, ((uint8_t *)&gm_l40));
                                                        uint64_t t266 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                                        uint64_t t267 = gm_l41;
                                                        {
                                                            {
                                                                memmove(((uint8_t *)&gm_l42), (((uint8_t *)t265) + (uintptr_t)t267 * 32), 32);
                                                                gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l42));
                                                                gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&F.l35));
                                                                uint8_t t270 = (gm_eq0iqiubfm7e(((uint8_t *)&t268), ((uint8_t *)&t269)));
                                                                if (t270) {
                                                                    gm_b16_8 t271 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l42) + 16));
                                                                    gm_th2lqbo77pvsc t272;
                                                                    GM_ST(uint8_t, (uint8_t *)&t272 + 17, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t272 + 18, 0);
                                                                    GM_ST(uint32_t, (uint8_t *)&t272 + 20, 0);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t272), t271);
                                                                    ((uint8_t *)&t272)[16] = 1;
                                                                    gm_l37 = t272;
                                                                }
                                                                goto gm_lb50;
                                                            }
gm_lb51:;
                                                            gm_trap("no match arm matched the scrutinee", gm_src9gqs06mvt4n2, 103, 17);
                                                        }
gm_lb50:;
                                                    }
gm_lb49:;
                                                    uint64_t t273 = gm_l41;
                                                    uint64_t r274; bool o275 = __builtin_add_overflow(t273, ((uint64_t)1ULL), &r274);
                                                    uint64_t t276 = r274;
                                                    gm_l41 = t276;
                                                }
                                            }
gm_lb47:;
                                            goto gm_lb45;
                                        }
gm_lb46:;
                                    }
gm_lb45:;
                                    goto gm_lb43;
                                }
gm_lb44:;
                            }
gm_lb43:;
                            {
                                {
                                    uint8_t t277 = (uint8_t)((((uint8_t *)&gm_l37))[16] != 0);
                                    if (!t277) {
                                        goto gm_lb53;
                                    }
                                    gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l37));
                                    gm_l43 = t278;
                                    gm_b16_8 t279 = gm_l43;
                                    gm_l36 = t279;
                                    goto gm_lb52;
                                }
gm_lb53:;
                                {
                                    {
                                        uint8_t t280 = GM_LD(uint8_t, ((((uint8_t *)&F.l35) + 24) + 0));
                                        uint8_t t281 = (uint8_t)(t280 == ((uint8_t)0ULL));
                                        if (!t281) {
                                            goto gm_lb55;
                                        }
                                        gm_b16_8 t282 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l35) + 24) + 8));
                                        gm_l44 = t282;
                                        gm_b16_8 t283 = gm_l1;
                                        gm_b16_8 t284 = gm_l44;
                                        gm_b16_8 t285 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t283, t284);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t286 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t285);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l36 = t286;
                                        goto gm_lb54;
                                    }
gm_lb55:;
                                    {
                                        goto gm_lb40;
                                    }
gm_lb56:;
                                    gm_trap("no match arm matched the scrutinee", gm_src9gqs06mvt4n2, 111, 13);
                                }
gm_lb54:;
                            }
gm_lb52:;
                            gm_b16_8 t287 = gm_l36;
                            gm_f_canonicalize_epo7cln((void *)&gm_l64, (void *)NULL, cx, NULL, t287);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t288 = (uint8_t)((((uint8_t *)&gm_l64))[16] != 0);
                            if (t288) {
                                gm_b16_8 t289 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l64));
                                gm_l65 = t289;
                            } else {
                                goto gm_lb40;
                            }
                            gm_b16_8 t290 = gm_l65;
                            gm_l45 = t290;
                            gm_b16_8 t291 = gm_l45;
                            gm_b16_8 t292;
                            GM_ST(void *, (uint8_t *)&t292, (void *)(gm_se24c7hpp401n));
                            GM_ST(uintptr_t, (uint8_t *)&t292 + 8, (uintptr_t)(10));
                            gm_b16_8 t293 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t291, t292);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t294 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t293);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t295 = (uint8_t)!t294;
                            if (t295) {
                                goto gm_lb40;
                            }
                            gm_l46 = ((uint8_t)0);
                            void * t296 = gm_l0;
                            gm_b16_8 t297 = GM_LD(gm_b16_8, (((uint8_t *)t296) + 48));
                            gm_l47 = t297;
                            gm_l48 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t298 = gm_l48;
                                    uint64_t t299 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                                    uint8_t t300 = (t298 < t299);
                                    if (!t300) {
                                        goto gm_lb57;
                                    }
                                    {
                                        void * t301 = GM_LD(void *, ((uint8_t *)&gm_l47));
                                        uint64_t t302 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                                        uint64_t t303 = gm_l48;
                                        {
                                            {
                                                gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)t301) + (uintptr_t)t303 * 16));
                                                gm_l49 = t304;
                                                gm_b16_8 t305 = gm_l49;
                                                gm_b16_8 t306 = gm_l45;
                                                uint8_t t307 = (gm_eq0iqiubfm7e(((uint8_t *)&t305), ((uint8_t *)&t306)));
                                                if (t307) {
                                                    gm_l46 = ((uint8_t)1);
                                                }
                                                goto gm_lb60;
                                            }
gm_lb61:;
                                            gm_trap("no match arm matched the scrutinee", gm_src9gqs06mvt4n2, 119, 9);
                                        }
gm_lb60:;
                                    }
gm_lb59:;
                                    uint64_t t308 = gm_l48;
                                    uint64_t r309; bool o310 = __builtin_add_overflow(t308, ((uint64_t)1ULL), &r309);
                                    uint64_t t311 = r309;
                                    gm_l48 = t311;
                                }
                            }
gm_lb57:;
                            uint8_t t312 = gm_l46;
                            if (t312) {
                                goto gm_lb40;
                            }
                            void * t313 = gm_l0;
                            void * t314 = (void *)(((uint8_t *)t313) + 48);
                            gm_b16_8 t315 = gm_l45;
                            gm_tftb1odc739t5 t316 = gm_f_push_3ef746b(t314, t315);
                            gm_l66 = t316;
                            uint8_t t317 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                            if (!t317) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_src9gqs06mvt4n2, 123, 9);
                            gm_b16_8 t318 = gm_l45;
                            gm_f_read_manifest_u9uajvn((void *)&gm_l67, (void *)NULL, cx, NULL, t318);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t319 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                            if (t319) {
                            } else {
                                gm_b16_8 t320 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                                gm_tl572d7579hns t321;
                                GM_ST(uint8_t, (uint8_t *)&t321 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t321 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t321 + 4, 0);
                                ((uint8_t *)&t321)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t321 + 8), t320);
                                memmove(gm_out, ((uint8_t *)&t321), 24);
                                goto gm_fdone;
                            }
                            memmove(((uint8_t *)&F.l50), (((uint8_t *)&gm_l67) + 8), 320);
                            void * t322 = gm_l0;
                            gm_b16_8 t323 = gm_l45;
                            void * t324 = (void *)((uint8_t *)&F.l50);
                            gm_b16_8 t325 = GM_LD(gm_b16_8, ((uint8_t *)&F.l35));
                            gm_ti0g3bmheuq9o t326 = GM_LD(gm_ti0g3bmheuq9o, (((uint8_t *)&F.l50) + 88));
                            gm_f_visit_n1kqfoj((void *)&gm_l68, (void *)NULL, cx, NULL, t322, t323, t324, t325, &t326, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t327 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                            if (t327) {
                            } else {
                                gm_b16_8 t328 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 8));
                                gm_tl572d7579hns t329;
                                GM_ST(uint8_t, (uint8_t *)&t329 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t329 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t329 + 4, 0);
                                ((uint8_t *)&t329)[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&t329 + 8), t328);
                                memmove(gm_out, ((uint8_t *)&t329), 24);
                                goto gm_fdone;
                            }
                            goto gm_lb41;
                        }
gm_lb42:;
                        gm_trap("no match arm matched the scrutinee", gm_src9gqs06mvt4n2, 98, 5);
                    }
gm_lb41:;
                }
gm_lb40:;
                uint64_t t330 = gm_l34;
                uint64_t r331; bool o332 = __builtin_add_overflow(t330, ((uint64_t)1ULL), &r331);
                uint64_t t333 = r331;
                gm_l34 = t333;
            }
        }
gm_lb38:;
    }
    {
        gm_tl572d7579hns t334;
        GM_ST(uint8_t, (uint8_t *)&t334 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t334 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t334 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t334 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t334 + 16, 0);
        ((uint8_t *)&t334)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t334 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t334), 24);
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
