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
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; void * f2; } gm_t5r2drn9pjqkt;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t5r2drn9pjqkt) == 24 && _Alignof(gm_t5r2drn9pjqkt) == 8 && offsetof(gm_t5r2drn9pjqkt, f0) == 0 && offsetof(gm_t5r2drn9pjqkt, f1) == 8 && offsetof(gm_t5r2drn9pjqkt, f2) == 16, "layout of `PendingBody`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tammvh1iespi3;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint64_t f0; gm_tiilmfd16v3fc f1; } gm_t4f5fr60fo50r;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tammvh1iespi3) == 56 && _Alignof(gm_tammvh1iespi3) == 8 && offsetof(gm_tammvh1iespi3, f0) == 0 && offsetof(gm_tammvh1iespi3, f1) == 16 && offsetof(gm_tammvh1iespi3, f2) == 24 && offsetof(gm_tammvh1iespi3, f3) == 40, "layout of `EffectInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_t08olq97rscn3 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; void * f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tc0sbs1tp6d7f;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
typedef struct { gm_tnpifv20irnph f0; uint8_t f1; uint8_t pe[7]; } gm_teseu4tn5o422;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
typedef struct { gm_tiilmfd16v3fc a[1]; } gm_tfe2ufaqq8re9;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { uint64_t f0; gm_t5v0c8ia6a5mo f1; } gm_tgsvmddj3e85e;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tpv68m88f9lcr;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4f5fr60fo50r) == 32 && _Alignof(gm_t4f5fr60fo50r) == 8 && offsetof(gm_t4f5fr60fo50r, f0) == 0 && offsetof(gm_t4f5fr60fo50r, f1) == 8, "layout of `(i: usize, x: EffectRef)`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tc0sbs1tp6d7f) == 80 && _Alignof(gm_tc0sbs1tp6d7f) == 8 && offsetof(gm_tc0sbs1tp6d7f, f0) == 0 && offsetof(gm_tc0sbs1tp6d7f, f1) == 32 && offsetof(gm_tc0sbs1tp6d7f, f2) == 40 && offsetof(gm_tc0sbs1tp6d7f, f3) == 56 && offsetof(gm_tc0sbs1tp6d7f, f4) == 64, "layout of `OpDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teseu4tn5o422) == 24 && _Alignof(gm_teseu4tn5o422) == 8 && offsetof(gm_teseu4tn5o422, f0) == 0 && offsetof(gm_teseu4tn5o422, f1) == 16, "layout of `?EffectSet`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tfe2ufaqq8re9) == 24 && _Alignof(gm_tfe2ufaqq8re9) == 8 && offsetof(gm_tfe2ufaqq8re9, a) == 0, "layout of `[1]EffectRef`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpv68m88f9lcr) == 56 && _Alignof(gm_tpv68m88f9lcr) == 8 && offsetof(gm_tpv68m88f9lcr, f0) == 0 && offsetof(gm_tpv68m88f9lcr, f1) == 32 && offsetof(gm_tpv68m88f9lcr, f2) == 40, "layout of `Constraint`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { gm_tqqhoivahirr0 f0; uint8_t f1; uint8_t pe[7]; } gm_tg93b272d11vo;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tg93b272d11vo) == 144 && _Alignof(gm_tg93b272d11vo) == 8 && offsetof(gm_tg93b272d11vo, f0) == 0 && offsetof(gm_tg93b272d11vo, f1) == 136, "layout of `?FnSig`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
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
struct gm_fr_bare_effect_ref_a5ofmji {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* args */
    gm_tiilmfd16v3fc ret;
};
struct gm_fr_bodiless_def_u9kqo09 {
    gm_frame h;
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_check_distinct_d4svq2h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_collect_terms_n8okg7m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_declare_effect_9n12hc8 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* params */
    gm_tf2orqcrphmek l9; /* p */
    gm_trqfvmjtt4sij l18; /* ops */
    gm_tc0sbs1tp6d7f l21; /* op */
    gm_trqfvmjtt4sij l22; /* op_params */
    gm_tf2orqcrphmek l25; /* p */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_declare_fn_6piolnr {
    gm_frame h;
    gm_tqqhoivahirr0 l4; /* sig */
    gm_t5v0c8ia6a5mo l7; /* p */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_effect_param_named_h2jeoo7 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_effect_terms_bphj7th {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_1fu8oa9 {
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
struct gm_fr_empty_slice_d0lhrvs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_eoluluh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ito0pap {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kgfehvr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lp0ck0u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_osbc35j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ot5ttb7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p6mn4us {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
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
struct gm_fr_extend_74ac4hb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_format_37dbjte {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4e7kcl2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_askmr98 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kjp7rmf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n6tblha {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uvukia4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vilpuor {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_fresh_effect_2msfvhg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_splice_80pgqas {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_effect_sum_dd9le16 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_effects_kind_uutbf9u {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_site_8sr7vam {
    gm_frame h;
    gm_tet9guhev5r4u l2; /* i */
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
struct gm_fr_list_a7rbkus {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qs7fohu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vmvl81b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_may_coincide_f6v7r98 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_named_effect_set_s5gn1lr {
    gm_frame h;
    gm_teseu4tn5o422 ret;
};
struct gm_fr_new_literal_fn_oqjp4lg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_new_nested_fn_r5u4ioo {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_provably_distinct_q4efcr7 {
    gm_frame h;
    gm_t53at80pfdus3 l3; /* ka */
    gm_t53at80pfdus3 l4; /* kb */
    uint8_t ret;
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
struct gm_fr_push_57kepgg {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_700doiq {
    gm_frame h;
    gm_tv0k2vg3054a6 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d1r4h19 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_hv483nm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o022ckl {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o8u01sh {
    gm_frame h;
    gm_t5v0c8ia6a5mo l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_puil3ql {
    gm_frame h;
    gm_tammvh1iespi3 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_roqi806 {
    gm_frame h;
    gm_tdgfduqjheqdm l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_s5vr2a2 {
    gm_frame h;
    gm_toa2vhe3l4hfr l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_sp4si0f {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_12ta4fr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4mbcm9u {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4mifc6m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_7sj41hq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_9a4nica {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_9c84ki2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_icepq2q {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_iloqstk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_lm3950p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_m244brh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_sh70ltr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_effect_arg_4h6q4qv {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_resolve_effect_list_uhjlh6o {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* terms */
    gm_tnpifv20irnph ret;
};
struct gm_fr_resolve_effect_ref_bggirs2 {
    gm_frame h;
    gm_trqfvmjtt4sij l13; /* targs */
    gm_tc3oht4ksth9d l19; /* t */
    gm_tqu3m2l3n8mnc ret;
};
struct gm_fr_resolve_effect_set_o7rc5od {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* v */
    gm_tnpifv20irnph ret;
};
struct gm_fr_resolve_fn_sig_pamuhve {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* params */
    gm_trqfvmjtt4sij l7; /* generics */
    gm_tf2orqcrphmek l12; /* p */
    gm_t5v0c8ia6a5mo l37; /* p */
    gm_t5v0c8ia6a5mo l40; /* q */
    gm_tpv68m88f9lcr l47; /* c */
    gm_tg93b272d11vo ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
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
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_splice_tj8iedl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* args */
    gm_tiilmfd16v3fc ret;
};
struct gm_fr_template_9o92cvv {
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
struct gm_fr_type_ty_9ph2gbr {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_bare_effect_ref_a5ofmji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_tdgfduqjheqdm gm_f_bodiless_def_u9kqo09_c(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_distinct_d4svq2h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_terms_n8okg7m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_constraint_s1juggf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_declare_effect_9n12hc8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_declare_fn_6piolnr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_method_candidate_e20bo1i(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_effect_param_named_h2jeoo7(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0lhrvs(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_eoluluh(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kgfehvr(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lp0ck0u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p6mn4us(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_constraint_defer_52jt3m9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_74ac4hb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_37dbjte(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_4e7kcl2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_askmr98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_kjp7rmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_n6tblha(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_uvukia4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vilpuor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_effect_2msfvhg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_splice_80pgqas(gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_effect_sum_dd9le16(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_a7rbkus(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qs7fohu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_may_coincide_f6v7r98(void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_named_effect_set_s5gn1lr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_names_error_oet8t6b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) uint32_t gm_f_new_literal_fn_oqjp4lg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, uint64_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_tnpifv20irnph a6);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) uint32_t gm_f_new_param_7010dt4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tnnjeerolos0a a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_provably_distinct_q4efcr7(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_472i7vl(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_57kepgg(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_700doiq(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o022ckl(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_puil3ql(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_roqi806(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sp4si0f(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_12ta4fr(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4mbcm9u(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4mifc6m(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9a4nica(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9c84ki2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_icepq2q(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_iloqstk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_lm3950p(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m244brh(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_sh70ltr(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_effect_arg_4h6q4qv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_list_uhjlh6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_set_o7rc5od(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_fn_sig_pamuhve(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_splice_tj8iedl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
extern const uint8_t gm_s0ad3307s8jmo[105];
extern const uint8_t gm_s0sivi9n1gpid[74];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s43j3q24pq7de[26];
extern const uint8_t gm_s5agga61vhqnt[20];
extern const uint8_t gm_s8p6h6db1ftub[32];
extern const uint8_t gm_sa7gjgqof9s56[44];
extern const uint8_t gm_sa9e77lq9q04p[16];
extern const uint8_t gm_sba4cjt08ps1i[58];
extern const uint8_t gm_sdsd71bjtv81c[98];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfvsgjcg86bbl[45];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_sgmqtrfjl8vp3[52];
extern const uint8_t gm_sjljm9omp36ob[52];
extern const uint8_t gm_sn0ac8j2fho01[22];
extern const uint8_t gm_snjq35j2j9d3c[113];
extern const uint8_t gm_snnov1h58rig5[52];
extern const uint8_t gm_sp929adkqo1cd[93];
extern const uint8_t gm_spfda095eoqse[34];
extern const uint8_t gm_spskd7qirpoe3[25];
extern const uint8_t gm_sq5p8t05ccn5h[24];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcmhupam4se3h5[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sshv7blujc475[52];
extern const uint8_t gm_sua99rsdk6d1r[51];
extern const uint8_t gm_svurqcoqldl32[83];
extern const gm_tmpl gm_tpl43224j8old0s;
extern const gm_tmpl gm_tplegelorakvcn7;
extern const gm_tmpl gm_tplem02qig8laoi;
extern const gm_tmpl gm_tplj4gqj9p0fguq;
extern const gm_tmpl gm_tpll1t63hmppo21;
extern const gm_tmpl gm_tplp7gdl2lerimh;
extern const gm_tmpl gm_tplukjghf5ng2te;
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
inline __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bodiless_def_u9kqo09 F;
    gm_t9qkh0rsooc8d gm_l0; /* kind */
    GM_STACK_CHECK_SHALLOW(&F, 106u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t2 = gm_l0;
    gm_b16_8 t3 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t4 = gm_f_empty_slice_6trg8r7();
    gm_t9805qtonbsag t5;
    memset(&t5, 0, sizeof t5);
    gm_tdgfduqjheqdm t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t6 + 57, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 58, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 60, 0);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t6 + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 8), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 24), t4);
    GM_ST(void *, ((uint8_t *)&t6 + 40), ((void *)0));
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t6 + 48), t5);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 56), ((uint8_t)0));
    memmove(gm_out, ((uint8_t *)&t6), 64);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_name_prpp0ip F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 216));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 216) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 283, 62);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_effect_terms_bphj7th F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_trqfvmjtt4sij t2;
    gm_f_list_qs7fohu((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = gm_l0;
    void * t4 = (void *)((uint8_t *)&F.l1);
    gm_b0_1 t5 = gm_f_collect_terms_n8okg7m((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0lhrvs(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d0lhrvs F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_eoluluh(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_eoluluh F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ito0pap F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kgfehvr(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kgfehvr F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lp0ck0u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lp0ck0u F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_osbc35j F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p6mn4us(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p6mn4us F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_74ac4hb(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_74ac4hb F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_m244brh(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 24);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
inline __attribute__((aligned(4))) void gm_f_format_37dbjte(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_37dbjte F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjljm9omp36ob));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplegelorakvcn7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_4e7kcl2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4e7kcl2 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_snjq35j2j9d3c));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(112));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplukjghf5ng2te), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_askmr98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_askmr98 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sua99rsdk6d1r));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(50));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplem02qig8laoi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_kjp7rmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kjp7rmf F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5agga61vhqnt));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpll1t63hmppo21), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n6tblha(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n6tblha F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s43j3q24pq7de));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(25));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplj4gqj9p0fguq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_uvukia4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_uvukia4 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgmqtrfjl8vp3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplp7gdl2lerimh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vilpuor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vilpuor F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn0ac8j2fho01));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl43224j8old0s), t3);
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_effect_2msfvhg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_effect_2msfvhg F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 216);
    gm_b16_8 t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_src0ih77f43mhf2, 279, 5);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 216) + 8));
    uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
    if (o10) gm_trap("integer overflow in `-`", gm_src0ih77f43mhf2, 280, 13);
    uint64_t t11 = r9;
    uint32_t t12 = (uint32_t)t11;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_effects_kind_uutbf9u F;
    void * gm_l0; /* t */
    gm_t08olq97rscn3 gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)6ULL));
            if (!t6) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l1), ((((uint8_t *)t4) + 8) + 8), 32);
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sgbumttiou4de));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(7));
            uint8_t t9 = (gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
            F.ret = t9;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_site_8sr7vam F;
    void * gm_l0; /* p */
    void * gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * gm_l3;
    void * t4 = gm_l0;
    uint8_t t5 = (uint8_t)(GM_LD(void *, (((uint8_t *)t4) + 32)) != 0);
    if (t5) {
        void * t6 = GM_LD(void *, (((uint8_t *)t4) + 32));
        gm_l3 = t6;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t7 = gm_l3;
    gm_l1 = t7;
    {
        {
            void * t8 = gm_l1;
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)49ULL));
            if (!t10) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), ((((uint8_t *)t8) + 8) + 8), 56);
            gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sa9e77lq9q04p));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(15));
            uint8_t t13 = (gm_eq0iqiubfm7e(((uint8_t *)&t11), ((uint8_t *)&t12)));
            F.ret = t13;
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
inline __attribute__((aligned(4))) void gm_f_list_a7rbkus(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_a7rbkus F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_p6mn4us();
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
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_af74s0q F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_he774nl();
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
inline __attribute__((aligned(4))) void gm_f_list_qs7fohu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qs7fohu F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lp0ck0u();
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
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vmvl81b F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fh7svmj();
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
inline __attribute__((aligned(4))) uint32_t gm_f_new_literal_fn_oqjp4lg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, uint64_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_tnpifv20irnph a6) {
    struct gm_fr_new_literal_fn_oqjp4lg F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tl874t2atnlh3 gm_l2; /* span */
    uint64_t gm_l3; /* module */
    gm_b16_8 gm_l4; /* params */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    gm_tnpifv20irnph gm_l6; /* effects */
    GM_STACK_CHECK(&F, 173u);
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
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    uint64_t t5 = gm_l3;
    gm_b16_8 t6 = gm_l4;
    gm_t88rcucg1b7uk t7 = gm_l5;
    gm_tnpifv20irnph t8 = gm_l6;
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)1ULL));
    uint32_t t10 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t2, t9, t3, t4, t5, t6, t7, t8);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_param_n8kgsdm F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)19ULL));
    GM_ST(uint32_t, ((uint8_t *)&t4 + 8), t3);
    gm_t88rcucg1b7uk t5 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t4);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_57kepgg(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_57kepgg F;
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
        gm_b16_8 t9 = gm_f_empty_slice_lp0ck0u();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_sh70ltr(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_700doiq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_700doiq F;
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
        gm_b16_8 t9 = gm_f_empty_slice_p6mn4us();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_9c84ki2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 48), ((uint8_t *)a1), 48);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_hv483nm F;
    void * gm_l0; /* l */
    gm_tep2drsl9408b gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_osbc35j();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_12ta4fr(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o022ckl(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o022ckl F;
    void * gm_l0; /* l */
    gm_t5r2drn9pjqkt gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_kgfehvr();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4mifc6m(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o8u01sh F;
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
        gm_b16_8 t9 = gm_f_empty_slice_he774nl();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_7sj41hq(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 56), ((uint8_t *)&F.l1), 56);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_puil3ql(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_puil3ql F;
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
        gm_b16_8 t9 = gm_f_empty_slice_eoluluh();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_iloqstk(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_s5vr2a2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_fh7svmj();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4mbcm9u(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sp4si0f(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_sp4si0f F;
    void * gm_l0; /* l */
    gm_tommf4j9upomc gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_d0lhrvs();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_lm3950p(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_12ta4fr(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_12ta4fr F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4mbcm9u(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4mbcm9u F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4mifc6m(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4mifc6m F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_7sj41hq F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9c84ki2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_9c84ki2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_iloqstk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_iloqstk F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_lm3950p(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_lm3950p F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_sh70ltr(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_sh70ltr F;
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
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scope_mark_i3b2mvo F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1608) + 8));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 1648) + 8));
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t6) + 1728) + 8));
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 1688) + 8));
    gm_t66j1im9ip128 t10;
    GM_ST(uint64_t, ((uint8_t *)&t10 + 0), t3);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 8), t5);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 16), t7);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 24), t9);
    memmove(gm_out, ((uint8_t *)&t10), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_ref_aqt1t7c F;
    void * gm_l0; /* t */
    void * gm_l1; /* e */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 200u);
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
    void * t7 = gm_l1;
    gm_b0_1 t8 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t6, t7);
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
inline __attribute__((aligned(4))) void gm_f_splice_tj8iedl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_splice_tj8iedl F;
    gm_t88rcucg1b7uk gm_l0; /* p */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l2;
    gm_trqfvmjtt4sij t2;
    gm_f_list_vmvl81b((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_t88rcucg1b7uk t4 = gm_l0;
    gm_toa2vhe3l4hfr t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 8), t4);
    gm_tftb1odc739t5 t6 = gm_f_push_s5vr2a2(t3, &t5);
    gm_l2 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_src0ih77f43mhf2, 469, 5);
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_tiilmfd16v3fc t10;
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t10 + 0), ((uint32_t)0ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 8), t9);
    memmove(gm_out, ((uint8_t *)&t10), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 176));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 176) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t3) + (uintptr_t)t6 * 4));
    F.ret = t7;
    goto gm_fret;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_type_ty_9ph2gbr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_type_ty_9ph2gbr F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)6ULL));
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
extern __attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_bare_effect_ref_a5ofmji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_bare_effect_ref_a5ofmji F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* eff */
    gm_b16_8 gm_l2; /* params */
    uint8_t gm_l3; /* all_sets */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* p */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 653u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l10;
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, (((uint8_t *)t12) + 1048));
    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 1048) + 8));
    uint32_t t15 = gm_l1;
    uint64_t t16 = (uint64_t)t15;
    if ((uintptr_t)t16 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t16, t14, gm_srcmhupam4se3h5, 370, 15);
    gm_b16_8 t17 = GM_LD(gm_b16_8, ((((uint8_t *)t13) + (uintptr_t)t16 * 56) + 24));
    gm_l2 = t17;
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t19 = (t18 > ((uint64_t)0ULL));
    gm_l3 = t19;
    gm_b16_8 t20 = gm_l2;
    gm_l4 = t20;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t21 = gm_l5;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t23 = (t21 < t22);
            if (!t23) {
                goto gm_lb1;
            }
            {
                void * t24 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t26 = gm_l5;
                {
                    {
                        uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t24) + (uintptr_t)t26 * 4));
                        gm_l6 = t27;
                        void * t28 = gm_l0;
                        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 1128));
                        uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t28) + 1128) + 8));
                        uint32_t t31 = gm_l6;
                        uint64_t t32 = (uint64_t)t31;
                        if ((uintptr_t)t32 >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)t32, t30, gm_srcmhupam4se3h5, 373, 12);
                        gm_tnnjeerolos0a t33 = GM_LD(gm_tnnjeerolos0a, (((uint8_t *)t29) + (uintptr_t)t32 * 48));
                        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t33) + 0));
                        uint8_t t35 = (uint8_t)(!(t34 == ((uint8_t)1ULL)));
                        gm_tnnjeerolos0a t36;
                        GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)1ULL));
                        if (t35) {
                            gm_l3 = ((uint8_t)0);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 372, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t37 = gm_l5;
            uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
            uint64_t t40 = r38;
            gm_l5 = t40;
        }
    }
gm_lb1:;
    uint8_t t41 = gm_l3;
    uint8_t t42 = (uint8_t)!t41;
    if (t42) {
        uint32_t t43 = gm_l1;
        gm_b16_8 t44 = gm_f_empty_slice_fh7svmj();
        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
        gm_tiilmfd16v3fc t46;
        GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t46 + 0), t43);
        GM_ST(gm_b16_8, ((uint8_t *)&t46 + 8), t45);
        memmove(gm_out, ((uint8_t *)&t46), 24);
        goto gm_fdone;
    }
    gm_trqfvmjtt4sij t47;
    gm_f_list_vmvl81b((void *)&t47, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l7 = t47;
    gm_b16_8 t48 = gm_l2;
    gm_l8 = t48;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t49 = gm_l9;
            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t51 = (t49 < t50);
            if (!t51) {
                goto gm_lb6;
            }
            {
                void * t52 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t54 = gm_l9;
                {
                    {
                        void * t55 = (void *)((uint8_t *)&F.l7);
                        void * t56 = gm_l0;
                        void * t57 = (void *)(((uint8_t *)t56) + 8);
                        gm_tnpifv20irnph t58 = gm_f_empty_effects_r0iv3pv();
                        gm_t53at80pfdus3 t59;
                        memset(&t59, 0, sizeof t59);
                        GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)24ULL));
                        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t59 + 8), t58);
                        gm_t88rcucg1b7uk t60 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t57, &t59);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_toa2vhe3l4hfr t61;
                        memset(&t61, 0, sizeof t61);
                        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t61 + 8), t60);
                        gm_tftb1odc739t5 t62 = gm_f_push_s5vr2a2(t55, &t61);
                        gm_l10 = t62;
                        uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcmhupam4se3h5, 377, 23);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 377, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t64 = gm_l9;
            uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
            uint64_t t67 = r65;
            gm_l9 = t67;
        }
    }
gm_lb6:;
    uint32_t t68 = gm_l1;
    gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
    gm_tiilmfd16v3fc t71;
    GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t71 + 0), t68);
    GM_ST(gm_b16_8, ((uint8_t *)&t71 + 8), t70);
    memmove(gm_out, ((uint8_t *)&t71), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_tdgfduqjheqdm gm_f_bodiless_def_u9kqo09_c(gm_t9qkh0rsooc8d a0) { gm_tdgfduqjheqdm gm_r; gm_f_bodiless_def_u9kqo09(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_check_distinct_d4svq2h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_check_distinct_d4svq2h F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* refs */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_tiilmfd16v3fc gm_l6; /* a */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tiilmfd16v3fc gm_l9; /* b */
    GM_STACK_CHECK(&F, 660u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t4f5fr60fo50r gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_b16_8 t12 = gm_l2;
    gm_l3 = t12;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t13 = gm_l4;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                uint64_t t16 = gm_l4;
                void * t17 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t19 = gm_l4;
                gm_tiilmfd16v3fc t20 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)t17) + (uintptr_t)t19 * 24));
                gm_t4f5fr60fo50r t21;
                GM_ST(uint64_t, ((uint8_t *)&t21 + 0), t16);
                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t21 + 8), t20);
                gm_l10 = t21;
                {
                    {
                        uint64_t t22 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                        gm_l5 = t22;
                        memmove(((uint8_t *)&gm_l6), (((uint8_t *)&gm_l10) + 8), 24);
                        void * t23 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t25 = gm_l5;
                        uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                        if (o27) gm_trap("integer overflow in `+`", gm_srcmhupam4se3h5, 154, 23);
                        uint64_t t28 = r26;
                        uint64_t t29 = (uintptr_t)t28;
                        uint64_t t30 = (uintptr_t)t24;
                        if (t29 > t30 || t30 > (uintptr_t)t24) gm_trap_slice(t29, t30, t24, gm_srcmhupam4se3h5, 154, 18);
                        uint64_t t31 = ((uintptr_t)t24) - (uintptr_t)t28;
                        gm_b16_8 t32;
                        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t23 + (uintptr_t)t28 * 24)));
                        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31));
                        gm_l7 = t32;
                        gm_l8 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t33 = gm_l8;
                                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                uint8_t t35 = (t33 < t34);
                                if (!t35) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t36 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                    uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                    uint64_t t38 = gm_l8;
                                    {
                                        {
                                            memmove(((uint8_t *)&gm_l9), (((uint8_t *)t36) + (uintptr_t)t38 * 24), 24);
                                            void * t39 = gm_l0;
                                            void * t40 = (void *)((uint8_t *)&gm_l6);
                                            void * t41 = (void *)((uint8_t *)&gm_l9);
                                            uint8_t t42 = gm_f_may_coincide_f6v7r98(t39, t40, t41);
                                            if (t42) {
                                                void * t43 = gm_l0;
                                                gm_tl874t2atnlh3 t44 = gm_l1;
                                                void * t45 = gm_l0;
                                                void * t46 = (void *)(((uint8_t *)t45) + 8);
                                                void * t47 = (void *)((uint8_t *)&gm_l6);
                                                gm_b16_8 t48 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t46, t47);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t49 = gm_l0;
                                                void * t50 = (void *)(((uint8_t *)t49) + 8);
                                                void * t51 = (void *)((uint8_t *)&gm_l9);
                                                gm_b16_8 t52 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t50, t51);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_t6j6smj7qoctu t53;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t48);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t53 + 16), t52);
                                                gm_f_format_4e7kcl2((void *)&gm_l11, (void *)NULL, cx, NULL, &t53);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                                if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcmhupam4se3h5, 156, 31);
                                                gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                                                gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
                                                gm_b0_1 t57 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t43, t44, t56);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 154, 9);
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
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 153, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t62 = gm_l4;
            uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
            uint64_t t65 = r63;
            gm_l4 = t65;
        }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_collect_terms_n8okg7m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_collect_terms_n8okg7m F;
    void * gm_l0; /* e */
    void * gm_l1; /* out */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    GM_STACK_CHECK(&F, 115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l4;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)13ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint8_t t7 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)0ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint8_t t9 = GM_LD(uint8_t, ((((((uint8_t *)t4) + 8) + 8) + 1) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)0ULL));
            if (!t10) {
                goto gm_lb2;
            }
            void * t11 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
            gm_l2 = t11;
            void * t12 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 16));
            gm_l3 = t12;
            void * t13 = gm_l2;
            void * t14 = gm_l1;
            gm_b0_1 t15 = gm_f_collect_terms_n8okg7m((void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t16 = gm_l3;
            void * t17 = gm_l1;
            gm_b0_1 t18 = gm_f_collect_terms_n8okg7m((void *)NULL, cx, NULL, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        void * t19 = gm_l1;
        void * t20 = gm_l0;
        gm_tftb1odc739t5 t21 = gm_f_push_57kepgg(t19, t20);
        gm_l4 = t21;
        uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcmhupam4se3h5, 221, 9);
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
__attribute__((aligned(4))) void gm_f_declare_effect_9n12hc8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4) {
    struct gm_fr_declare_effect_9n12hc8 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    uint64_t gm_l2; /* decl */
    gm_t08olq97rscn3 gm_l3; /* name */
    void * gm_l4; /* lit */
    uint32_t gm_l5; /* eff */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint8_t gm_l10; /* is_type */
    uint8_t gm_l11; /* is_effects */
    void * gm_l12; /* t */
    gm_t08olq97rscn3 gm_l13; /* i */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t08olq97rscn3 gm_l16; /* n */
    uint32_t gm_l17; /* pid */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_t88rcucg1b7uk gm_l26; /* ty */
    void * gm_l27; /* t */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_t08olq97rscn3 gm_l30; /* n */
    gm_t88rcucg1b7uk gm_l31; /* ret */
    void * gm_l32; /* r */
    GM_STACK_CHECK(&F, 2463u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_b16_8 gm_l33;
    gm_tnnjeerolos0a gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    {
        void * t37 = gm_l0;
        void * t38 = (void *)(((uint8_t *)t37) + 8);
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)a3));
        uint32_t t40 = gm_f_fresh_effect_2msfvhg((void *)NULL, cx, NULL, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t40;
        gm_trqfvmjtt4sij t41;
        gm_f_list_go2qg0b((void *)&t41, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t41;
        void * t42 = gm_l4;
        uint8_t t43 = (uint8_t)((((uint8_t *)t42))[16] != 0);
        if (t43) {
            gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)t42));
            gm_l33 = t44;
        } else {
            gm_b16_8 t45 = gm_f_empty_slice_ito0pap();
            gm_l33 = t45;
        }
        gm_b16_8 t46 = gm_l33;
        gm_l7 = t46;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t47 = gm_l8;
                uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t49 = (t47 < t48);
                if (!t49) {
                    goto gm_lb1;
                }
                {
                    void * t50 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t52 = gm_l8;
                    {
                        {
                            memmove(((uint8_t *)&F.l9), (((uint8_t *)t50) + (uintptr_t)t52 * 56), 56);
                            gm_l10 = ((uint8_t)0);
                            gm_l11 = ((uint8_t)0);
                            {
                                {
                                    uint8_t t53 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l9) + 24)) != 0);
                                    if (!t53) {
                                        goto gm_lb7;
                                    }
                                    void * t54 = GM_LD(void *, (((uint8_t *)&F.l9) + 24));
                                    gm_l12 = t54;
                                    {
                                        {
                                            void * t55 = gm_l12;
                                            uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t55) + 8) + 0));
                                            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)6ULL));
                                            if (!t57) {
                                                goto gm_lb9;
                                            }
                                            memmove(((uint8_t *)&gm_l13), ((((uint8_t *)t55) + 8) + 8), 32);
                                            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                                            gm_b16_8 t59;
                                            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s2737i1iq57nf));
                                            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(4));
                                            uint8_t t60 = (gm_eq0iqiubfm7e(((uint8_t *)&t58), ((uint8_t *)&t59)));
                                            gm_l10 = t60;
                                            goto gm_lb8;
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                    void * t61 = gm_l12;
                                    uint8_t t62 = gm_f_is_effects_kind_uutbf9u(t61);
                                    gm_l11 = t62;
                                    goto gm_lb6;
                                }
gm_lb7:;
                            }
gm_lb6:;
                            {
                                {
                                    uint8_t t63 = gm_l10;
                                    uint8_t t64 = (uint8_t)!t63;
                                    if (!t64) {
                                        goto gm_lb11;
                                    }
                                    uint8_t t65 = gm_l11;
                                    uint8_t t66 = (uint8_t)!t65;
                                    if (!t66) {
                                        goto gm_lb11;
                                    }
                                    void * t67 = gm_l0;
                                    gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l9) + 40));
                                    gm_b16_8 t69;
                                    GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sp929adkqo1cd));
                                    GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(92));
                                    gm_b0_1 t70 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t67, t68, t69);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb10;
                                }
gm_lb11:;
                            }
gm_lb10:;
                            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                            gm_l14 = t71;
                            gm_l15 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t72 = gm_l15;
                                    uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                    uint8_t t74 = (t72 < t73);
                                    if (!t74) {
                                        goto gm_lb12;
                                    }
                                    {
                                        void * t75 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                        uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                        uint64_t t77 = gm_l15;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l16), (((uint8_t *)t75) + (uintptr_t)t77 * 32), 32);
                                                void * t78 = gm_l0;
                                                gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
                                                uint8_t t80 = gm_l11;
                                                if (t80) {
                                                    gm_tnnjeerolos0a t81;
                                                    GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)1ULL));
                                                    gm_l34 = t81;
                                                } else {
                                                    gm_tnnjeerolos0a t82;
                                                    GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)0ULL));
                                                    gm_l34 = t82;
                                                }
                                                gm_tnnjeerolos0a t83 = gm_l34;
                                                uint32_t t84 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t78, t79, t83);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l17 = t84;
                                                void * t85 = (void *)((uint8_t *)&F.l6);
                                                uint32_t t86 = gm_l17;
                                                gm_tftb1odc739t5 t87 = gm_f_push_im9d4ro(t85, t86);
                                                gm_l35 = t87;
                                                uint8_t t88 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                                if (!t88) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_srcmhupam4se3h5, 419, 13);
                                                void * t89 = gm_l0;
                                                void * t90 = (void *)(((uint8_t *)t89) + 1608);
                                                gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
                                                void * t92 = gm_l0;
                                                void * t93 = (void *)(((uint8_t *)t92) + 8);
                                                uint32_t t94 = gm_l17;
                                                gm_t88rcucg1b7uk t95 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t93, t94);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_tep2drsl9408b t96;
                                                GM_ST(uint32_t, (uint8_t *)&t96 + 20, 0);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t96 + 0), t91);
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t96 + 16), t95);
                                                gm_tftb1odc739t5 t97 = gm_f_push_hv483nm(t90, &t96);
                                                gm_l36 = t97;
                                                uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                                if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srcmhupam4se3h5, 420, 13);
                                                goto gm_lb15;
                                            }
gm_lb16:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 417, 9);
                                        }
gm_lb15:;
                                    }
gm_lb14:;
                                    uint64_t t99 = gm_l15;
                                    uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                                    uint64_t t102 = r100;
                                    gm_l15 = t102;
                                }
                            }
gm_lb12:;
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 407, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t103 = gm_l8;
                uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                uint64_t t106 = r104;
                gm_l8 = t106;
            }
        }
gm_lb1:;
    }
    {
        void * t107 = gm_l0;
        void * t108 = (void *)(((uint8_t *)t107) + 1048);
        gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)a3));
        uint64_t t110 = gm_l1;
        gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_b16_8 t112 = gm_f_empty_slice_p6mn4us();
        gm_tammvh1iespi3 t113;
        GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t109);
        GM_ST(uint64_t, ((uint8_t *)&t113 + 16), t110);
        GM_ST(gm_b16_8, ((uint8_t *)&t113 + 24), t111);
        GM_ST(gm_b16_8, ((uint8_t *)&t113 + 40), t112);
        gm_tftb1odc739t5 t114 = gm_f_push_puil3ql(t108, &t113);
        gm_l37 = t114;
        uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
        if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srcmhupam4se3h5, 424, 5);
        void * t116 = gm_l0;
        uint64_t t117 = gm_l2;
        uint32_t t118 = gm_l5;
        gm_t6qooom8qa2do t119;
        memset(&t119, 0, sizeof t119);
        GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)2ULL));
        GM_ST(uint32_t, ((uint8_t *)&t119 + 8), t118);
        gm_tagrkbqh054h7 t120;
        GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t120 + 8), t119);
        gm_b0_1 t121 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t116, t117, &t120);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t122;
        gm_f_list_a7rbkus((void *)&t122, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l18 = t122;
        void * t123 = gm_l4;
        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)t123) + 24));
        gm_l19 = t124;
        gm_l20 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t125 = gm_l20;
                uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                uint8_t t127 = (t125 < t126);
                if (!t127) {
                    goto gm_lb17;
                }
                {
                    void * t128 = GM_LD(void *, ((uint8_t *)&gm_l19));
                    uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                    uint64_t t130 = gm_l20;
                    {
                        {
                            memmove(((uint8_t *)&F.l21), (((uint8_t *)t128) + (uintptr_t)t130 * 80), 80);
                            gm_trqfvmjtt4sij t131;
                            gm_f_list_af74s0q((void *)&t131, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l22 = t131;
                            gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 40));
                            gm_l23 = t132;
                            gm_l24 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t133 = gm_l24;
                                    uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                    uint8_t t135 = (t133 < t134);
                                    if (!t135) {
                                        goto gm_lb22;
                                    }
                                    {
                                        void * t136 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                        uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                        uint64_t t138 = gm_l24;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l25), (((uint8_t *)t136) + (uintptr_t)t138 * 56), 56);
                                                void * t139 = gm_l0;
                                                void * t140 = (void *)(((uint8_t *)t139) + 8);
                                                gm_t88rcucg1b7uk t141 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t140);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l26 = t141;
                                                {
                                                    {
                                                        uint8_t t142 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l25) + 24)) != 0);
                                                        if (!t142) {
                                                            goto gm_lb28;
                                                        }
                                                        void * t143 = GM_LD(void *, (((uint8_t *)&F.l25) + 24));
                                                        gm_l27 = t143;
                                                        void * t144 = gm_l0;
                                                        uint64_t t145 = gm_l1;
                                                        void * t146 = gm_l27;
                                                        gm_t88rcucg1b7uk t147 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t144, t145, t146);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l26 = t147;
                                                        goto gm_lb27;
                                                    }
gm_lb28:;
                                                    void * t148 = gm_l0;
                                                    gm_tl874t2atnlh3 t149 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l25) + 40));
                                                    gm_b16_8 t150;
                                                    GM_ST(void *, (uint8_t *)&t150, (void *)(gm_s8p6h6db1ftub));
                                                    GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(31));
                                                    gm_b0_1 t151 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t148, t149, t150);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
gm_lb27:;
                                                gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
                                                gm_l28 = t152;
                                                gm_l29 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t153 = gm_l29;
                                                        uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                                        uint8_t t155 = (t153 < t154);
                                                        if (!t155) {
                                                            goto gm_lb29;
                                                        }
                                                        {
                                                            void * t156 = GM_LD(void *, ((uint8_t *)&gm_l28));
                                                            uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                                            uint64_t t158 = gm_l29;
                                                            {
                                                                {
                                                                    memmove(((uint8_t *)&gm_l30), (((uint8_t *)t156) + (uintptr_t)t158 * 32), 32);
                                                                    void * t159 = (void *)((uint8_t *)&F.l22);
                                                                    gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l30));
                                                                    gm_t88rcucg1b7uk t161 = gm_l26;
                                                                    gm_tl874t2atnlh3 t162 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l30) + 16));
                                                                    gm_t9805qtonbsag t163;
                                                                    memset(&t163, 0, sizeof t163);
                                                                    gm_t5v0c8ia6a5mo t164;
                                                                    GM_ST(uint8_t, (uint8_t *)&t164 + 49, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t164 + 50, 0);
                                                                    GM_ST(uint32_t, (uint8_t *)&t164 + 52, 0);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t164 + 0), t160);
                                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t164 + 16), t161);
                                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t164 + 20), t162);
                                                                    GM_ST(void *, ((uint8_t *)&t164 + 32), ((void *)0));
                                                                    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t164 + 40), t163);
                                                                    GM_ST(uint8_t, ((uint8_t *)&t164 + 48), ((uint8_t)0));
                                                                    gm_tftb1odc739t5 t165 = gm_f_push_o8u01sh(t159, &t164);
                                                                    gm_l38 = t165;
                                                                    uint8_t t166 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                                                    if (!t166) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srcmhupam4se3h5, 437, 17);
                                                                    goto gm_lb32;
                                                                }
gm_lb33:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 436, 13);
                                                            }
gm_lb32:;
                                                        }
gm_lb31:;
                                                        uint64_t t167 = gm_l29;
                                                        uint64_t r168; bool o169 = __builtin_add_overflow(t167, ((uint64_t)1ULL), &r168);
                                                        uint64_t t170 = r168;
                                                        gm_l29 = t170;
                                                    }
                                                }
gm_lb29:;
                                                goto gm_lb25;
                                            }
gm_lb26:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 429, 9);
                                        }
gm_lb25:;
                                    }
gm_lb24:;
                                    uint64_t t171 = gm_l24;
                                    uint64_t r172; bool o173 = __builtin_add_overflow(t171, ((uint64_t)1ULL), &r172);
                                    uint64_t t174 = r172;
                                    gm_l24 = t174;
                                }
                            }
gm_lb22:;
                            void * t175 = gm_l0;
                            void * t176 = (void *)(((uint8_t *)t175) + 8);
                            gm_t88rcucg1b7uk t177 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t176);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l31 = t177;
                            {
                                {
                                    uint8_t t178 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l21) + 56)) != 0);
                                    if (!t178) {
                                        goto gm_lb35;
                                    }
                                    void * t179 = GM_LD(void *, (((uint8_t *)&F.l21) + 56));
                                    gm_l32 = t179;
                                    void * t180 = gm_l0;
                                    uint64_t t181 = gm_l1;
                                    void * t182 = gm_l32;
                                    gm_t88rcucg1b7uk t183 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t180, t181, t182);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l31 = t183;
                                    goto gm_lb34;
                                }
gm_lb35:;
                            }
gm_lb34:;
                            uint64_t t184 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 8));
                            uint8_t t185 = (t184 > ((uint64_t)0ULL));
                            if (t185) {
                                void * t186 = gm_l0;
                                uint64_t t187 = gm_l1;
                                void * t188 = GM_LD(void *, ((uint8_t *)&F.l22));
                                uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t189)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t189, gm_srcmhupam4se3h5, 442, 82);
                                gm_t88rcucg1b7uk t190 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t188) + (uintptr_t)((uint64_t)0ULL) * 56) + 16));
                                gm_b0_1 t191 = gm_f_declare_method_candidate_e20bo1i((void *)NULL, cx, NULL, t186, t187, &(*(gm_t08olq97rscn3 *)((uint8_t *)&F.l21)), t190);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            void * t192 = (void *)((uint8_t *)&F.l18);
                            gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
                            gm_t9qkh0rsooc8d t194 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l21) + 32));
                            gm_b16_8 t195 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
                            gm_t88rcucg1b7uk t196 = gm_l31;
                            gm_tv0k2vg3054a6 t197;
                            GM_ST(uint8_t, (uint8_t *)&t197 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t197 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t197 + 20, 0);
                            GM_ST(uint32_t, (uint8_t *)&t197 + 44, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t197 + 0), t193);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t197 + 16), t194);
                            GM_ST(gm_b16_8, ((uint8_t *)&t197 + 24), t195);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t197 + 40), t196);
                            gm_tftb1odc739t5 t198 = gm_f_push_700doiq(t192, &t197);
                            gm_l39 = t198;
                            uint8_t t199 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                            if (!t199) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_srcmhupam4se3h5, 443, 9);
                            goto gm_lb20;
                        }
gm_lb21:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 427, 5);
                    }
gm_lb20:;
                }
gm_lb19:;
                uint64_t t200 = gm_l20;
                uint64_t r201; bool o202 = __builtin_add_overflow(t200, ((uint64_t)1ULL), &r201);
                uint64_t t203 = r201;
                gm_l20 = t203;
            }
        }
gm_lb17:;
    }
    {
        void * t204 = gm_l0;
        void * t205 = (void *)(((uint8_t *)t204) + 1608);
        void * t206 = gm_l0;
        uint64_t t207 = GM_LD(uint64_t, ((((uint8_t *)t206) + 1608) + 8));
        uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
        uint64_t r209; bool o210 = __builtin_sub_overflow(t207, t208, &r209);
        if (o210) gm_trap("integer overflow in `-`", gm_srcmhupam4se3h5, 445, 28);
        uint64_t t211 = r209;
        gm_f_truncate_7r782uc(t205, t211);
        void * t212 = gm_l0;
        void * t213 = GM_LD(void *, (((uint8_t *)t212) + 1048));
        uint64_t t214 = GM_LD(uint64_t, ((((uint8_t *)t212) + 1048) + 8));
        uint32_t t215 = gm_l5;
        uint64_t t216 = (uint64_t)t215;
        if ((uintptr_t)t216 >= (uintptr_t)(t214)) gm_trap_index((uintptr_t)t216, t214, gm_srcmhupam4se3h5, 446, 5);
        gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
        GM_ST(gm_b16_8, ((((uint8_t *)t213) + (uintptr_t)t216 * 56) + 40), t217);
        uint32_t t218 = gm_l5;
        gm_t6qooom8qa2do t219;
        memset(&t219, 0, sizeof t219);
        GM_ST(uint8_t, ((uint8_t *)&t219 + 0), ((uint8_t)2ULL));
        GM_ST(uint32_t, ((uint8_t *)&t219 + 8), t218);
        memmove(gm_out, ((uint8_t *)&t219), 208);
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
inline __attribute__((aligned(4))) void gm_f_declare_fn_6piolnr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3) {
    struct gm_fr_declare_fn_6piolnr F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* lit */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l8; /* id */
    GM_STACK_CHECK(&F, 1351u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_tg93b272d11vo gm_l9;
    gm_tqqhoivahirr0 gm_l10;
    uint8_t gm_l11;
    gm_tftb1odc739t5 gm_l12;
    void * t10 = gm_l0;
    uint64_t t11 = gm_l1;
    void * t12 = gm_l3;
    void * t13 = (void *)((uint8_t *)t12);
    void * t14 = gm_l3;
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 72));
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
    gm_f_resolve_fn_sig_pamuhve((void *)&gm_l9, (void *)NULL, cx, NULL, t10, t11, t13, ((uint8_t)1), t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l9))[136] != 0);
    if (t17) {
        memmove(((uint8_t *)&gm_l10), ((uint8_t *)&gm_l9), 136);
    } else {
        gm_t6qooom8qa2do t18;
        memset(&t18, 0, sizeof t18);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)11ULL));
        memmove(gm_out, ((uint8_t *)&t18), 208);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l4), ((uint8_t *)&gm_l10), 136);
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
    gm_l5 = t19;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t20 = gm_l6;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t25 = gm_l6;
                {
                    {
                        memmove(((uint8_t *)&F.l7), (((uint8_t *)t23) + (uintptr_t)t25 * 56), 56);
                        {
                            {
                                uint8_t t26 = (uint8_t)(((((uint8_t *)&F.l7) + 40))[4] != 0);
                                if (t26) {
                                    goto gm_lb7;
                                }
                                gm_l11 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l11 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 7, 12);
                        }
gm_lb6:;
                        uint8_t t27 = gm_l11;
                        if (t27) {
                            void * t28 = gm_l0;
                            uint64_t t29 = gm_l1;
                            gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
                            gm_b0_1 t31 = gm_f_declare_method_candidate_e20bo1i((void *)NULL, cx, NULL, t28, t29, &(*(gm_t08olq97rscn3 *)((uint8_t *)a2)), t30);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb1;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 6, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t32 = gm_l6;
            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
            uint64_t t35 = r33;
            gm_l6 = t35;
        }
    }
gm_lb1:;
    void * t36 = gm_l0;
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)a2));
    gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
    uint64_t t39 = gm_l1;
    gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
    gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
    gm_tnpifv20irnph t42 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l4) + 64));
    gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 80));
    gm_tqu3m2l3n8mnc t44;
    memset(&t44, 0, sizeof t44);
    gm_tqqhoivahirr0 t45;
    GM_ST(uint32_t, (uint8_t *)&t45 + 28, 0);
    GM_ST(uint32_t, (uint8_t *)&t45 + 60, 0);
    GM_ST(uint8_t, (uint8_t *)&t45 + 129, 0);
    GM_ST(uint16_t, (uint8_t *)&t45 + 130, 0);
    GM_ST(uint32_t, (uint8_t *)&t45 + 132, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t45 + 0), t37);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t45 + 16), t38);
    GM_ST(uint64_t, ((uint8_t *)&t45 + 32), t39);
    GM_ST(gm_b16_8, ((uint8_t *)&t45 + 40), t40);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t45 + 56), t41);
    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t45 + 64), t42);
    GM_ST(gm_b16_8, ((uint8_t *)&t45 + 80), t43);
    GM_ST(gm_tqu3m2l3n8mnc, ((uint8_t *)&t45 + 96), t44);
    GM_ST(uint8_t, ((uint8_t *)&t45 + 128), ((uint8_t)0));
    gm_t9qkh0rsooc8d t46;
    GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)0ULL));
    gm_tdgfduqjheqdm t47;
    gm_f_bodiless_def_u9kqo09((void *)&t47, t46);
    uint32_t t48 = gm_f_add_fn_t4t83qh((void *)NULL, cx, NULL, t36, &t45, &t47);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t48;
    void * t49 = gm_l0;
    void * t50 = (void *)(((uint8_t *)t49) + 424);
    uint32_t t51 = gm_l8;
    uint64_t t52 = gm_l1;
    void * t53 = gm_l3;
    gm_t5r2drn9pjqkt t54;
    GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t54 + 0), t51);
    GM_ST(uint64_t, ((uint8_t *)&t54 + 8), t52);
    GM_ST(void *, ((uint8_t *)&t54 + 16), t53);
    gm_tftb1odc739t5 t55 = gm_f_push_o022ckl(t50, &t54);
    gm_l12 = t55;
    uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcmhupam4se3h5, 13, 5);
    uint32_t t57 = gm_l8;
    gm_t6qooom8qa2do t58;
    memset(&t58, 0, sizeof t58);
    GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)5ULL));
    GM_ST(uint32_t, ((uint8_t *)&t58 + 8), t57);
    memmove(gm_out, ((uint8_t *)&t58), 208);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_effect_param_named_h2jeoo7(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_param_named_h2jeoo7 F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    gm_t08olq97rscn3 gm_l2; /* id */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint32_t gm_l4; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 315u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tkn42u7na3t81 gm_l5;
    gm_t88rcucg1b7uk gm_l6;
    gm_t53at80pfdus3 gm_l7;
    {
        {
            void * t6 = gm_l1;
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)6ULL));
            if (!t8) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t6) + 8) + 8), 32);
            void * t9 = gm_l0;
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_tkn42u7na3t81 t11 = gm_f_type_param_3av4r8i(t9, t10);
            gm_l5 = t11;
            uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l5))[4] != 0);
            if (t12) {
                gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l5));
                gm_l6 = t13;
            } else {
                gm_tkn42u7na3t81 t14;
                memset(&t14, 0, sizeof t14);
                F.ret = t14;
                goto gm_fret;
            }
            gm_t88rcucg1b7uk t15 = gm_l6;
            gm_l3 = t15;
            {
                {
                    void * t16 = gm_l0;
                    void * t17 = (void *)(((uint8_t *)t16) + 8);
                    gm_t88rcucg1b7uk t18 = gm_l3;
                    gm_f_kind_jcnt4hm((void *)&gm_l7, t17, GM_CONV(gm_csguankko4lmhd, t18));
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                    uint8_t t20 = (uint8_t)(t19 == ((uint8_t)19ULL));
                    if (!t20) {
                        goto gm_lb4;
                    }
                    uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 8));
                    gm_l4 = t21;
                    void * t22 = gm_l0;
                    void * t23 = GM_LD(void *, (((uint8_t *)t22) + 1128));
                    uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t22) + 1128) + 8));
                    uint32_t t25 = gm_l4;
                    uint64_t t26 = (uint64_t)t25;
                    if ((uintptr_t)t26 >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)t26, t24, gm_srcmhupam4se3h5, 386, 8);
                    gm_tnnjeerolos0a t27 = GM_LD(gm_tnnjeerolos0a, (((uint8_t *)t23) + (uintptr_t)t26 * 48));
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
                    uint8_t t29 = (uint8_t)(!(t28 == ((uint8_t)1ULL)));
                    gm_tnnjeerolos0a t30;
                    GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)1ULL));
                    if (t29) {
                        gm_tkn42u7na3t81 t31;
                        memset(&t31, 0, sizeof t31);
                        F.ret = t31;
                        goto gm_fret;
                    }
                    gm_t88rcucg1b7uk t32 = gm_l3;
                    gm_tkn42u7na3t81 t33;
                    GM_ST(uint8_t, (uint8_t *)&t33 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t33 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t33), t32);
                    ((uint8_t *)&t33)[4] = 1;
                    F.ret = t33;
                    goto gm_fret;
                }
gm_lb4:;
                gm_tkn42u7na3t81 t34;
                memset(&t34, 0, sizeof t34);
                F.ret = t34;
                goto gm_fret;
            }
gm_lb3:;
        }
gm_lb2:;
        gm_tkn42u7na3t81 t35;
        memset(&t35, 0, sizeof t35);
        F.ret = t35;
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_splice_80pgqas(gm_tnpifv20irnph a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_splice_80pgqas F;
    gm_tnpifv20irnph gm_l0; /* set */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_tiilmfd16v3fc gm_l3; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t7;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t8 = gm_l2;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t13 = gm_l2;
                {
                    {
                        memmove(((uint8_t *)&gm_l3), (((uint8_t *)t11) + (uintptr_t)t13 * 24), 24);
                        uint32_t t14 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                        uint8_t t15 = ((t14 == ((uint32_t)0ULL)));
                        if (t15) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 227, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t16 = gm_l2;
            uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
            uint64_t t19 = r17;
            gm_l2 = t19;
        }
    }
gm_lb1:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_effect_sum_dd9le16(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_is_effect_sum_dd9le16 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    void * gm_l3; /* first */
    void * gm_l4; /* head */
    void * gm_l5; /* callee */
    gm_tf5srh4mp4b3i gm_l6; /* d */
    gm_t08olq97rscn3 gm_l7; /* id */
    void * gm_l8; /* base */
    gm_t08olq97rscn3 gm_l9; /* name */
    uint64_t gm_l10; /* m */
    uint64_t gm_l11; /* decl */
    GM_STACK_CHECK(&F, 698u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 gm_l12;
    gm_tf5srh4mp4b3i gm_l13;
    uint64_t gm_l14;
    uint8_t gm_l15;
    gm_t6qooom8qa2do gm_l16;
    void * t15 = gm_l2;
    gm_b16_8 t16 = gm_f_effect_terms_bphj7th((void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t16;
    void * t17 = GM_LD(void *, ((uint8_t *)&gm_l12));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t18, gm_srcmhupam4se3h5, 261, 14);
    void * t19 = GM_LD(void *, (((uint8_t *)t17) + (uintptr_t)((uint64_t)0ULL) * 8));
    gm_l3 = t19;
    void * t20 = gm_l3;
    gm_l4 = t20;
    {
        {
            void * t21 = gm_l3;
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t21) + 8) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)19ULL));
            if (!t23) {
                goto gm_lb2;
            }
            void * t24 = GM_LD(void *, ((((uint8_t *)t21) + 8) + 8));
            gm_l5 = t24;
            void * t25 = gm_l5;
            gm_l4 = t25;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_tf5srh4mp4b3i t26;
    memset(&t26, 0, sizeof t26);
    gm_l6 = t26;
    void * t27 = gm_l4;
    {
        {
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)6ULL));
            if (!t29) {
                goto gm_lb4;
            }
            memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t27) + 8) + 8), 32);
            void * t30 = gm_l0;
            uint64_t t31 = gm_l1;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
            gm_tf5srh4mp4b3i t33 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t30, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t33;
            goto gm_lb3;
        }
gm_lb4:;
        {
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)22ULL));
            if (!t35) {
                goto gm_lb5;
            }
            void * t36 = GM_LD(void *, ((((uint8_t *)t27) + 8) + 8));
            gm_l8 = t36;
            memmove(((uint8_t *)&gm_l9), (((((uint8_t *)t27) + 8) + 8) + 8), 32);
            {
                {
                    void * t37 = gm_l0;
                    uint64_t t38 = gm_l1;
                    void * t39 = gm_l8;
                    gm_tf5srh4mp4b3i t40 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t37, t38, t39);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t40;
                    uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l13))[8] != 0);
                    if (!t41) {
                        goto gm_lb7;
                    }
                    uint64_t t42 = GM_LD(uint64_t, ((uint8_t *)&gm_l13));
                    gm_l10 = t42;
                    void * t43 = gm_l0;
                    uint64_t t44 = gm_l10;
                    uint64_t t45 = gm_l1;
                    gm_tf5srh4mp4b3i t46 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t43, t44, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l9)), t45);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l6 = t46;
                    goto gm_lb6;
                }
gm_lb7:;
            }
gm_lb6:;
            goto gm_lb3;
        }
gm_lb5:;
        {
            goto gm_lb3;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 265, 5);
    }
gm_lb3:;
    uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l6))[8] != 0);
    if (t47) {
        uint64_t t48 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
        gm_l14 = t48;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint64_t t49 = gm_l14;
    gm_l11 = t49;
    void * t50 = gm_l0;
    uint64_t t51 = gm_l11;
    gm_f_resolve_decl_pus042v((void *)&gm_l16, (void *)NULL, cx, NULL, t50, t51);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)2ULL));
            if (!t53) {
                goto gm_lb10;
            }
            gm_l15 = ((uint8_t)1);
            goto gm_lb9;
        }
gm_lb10:;
        {
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)3ULL));
            if (!t55) {
                goto gm_lb11;
            }
            gm_l15 = ((uint8_t)1);
            goto gm_lb9;
        }
gm_lb11:;
        {
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)4ULL));
            if (!t57) {
                goto gm_lb12;
            }
            gm_l15 = ((uint8_t)1);
            goto gm_lb9;
        }
gm_lb12:;
        {
            gm_l15 = ((uint8_t)0);
            goto gm_lb9;
        }
gm_lb13:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 273, 12);
    }
gm_lb9:;
    uint8_t t58 = gm_l15;
    F.ret = t58;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_may_coincide_f6v7r98(void * a0, void * a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_may_coincide_f6v7r98 F;
    void * gm_l0; /* s */
    void * gm_l1; /* a */
    void * gm_l2; /* b */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_t88rcucg1b7uk gm_l6; /* x */
    gm_t88rcucg1b7uk gm_l7; /* y */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l8;
    void * t9 = gm_l1;
    uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)t9));
    void * t11 = gm_l2;
    uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)t11));
    uint8_t t13 = (!(t10 == t12));
    if (t13) {
        gm_l8 = ((uint8_t)1);
    } else {
        void * t14 = gm_l1;
        void * t15 = gm_l2;
        int32_t t16 = gm_f_cmp_ref_8ntrqll(t14, t15);
        uint8_t t17 = ((t16 == ((int32_t)0LL)));
        gm_l8 = t17;
    }
    uint8_t t18 = gm_l8;
    if (t18) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_l4 = ((uint64_t)0ULL);
    void * t19 = gm_l1;
    uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)t19) + 8) + 8));
    void * t21 = gm_l2;
    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t21) + 8) + 8));
    uint64_t t23 = (t20 < t22 ? t20 : t22);
    gm_l3 = t23;
    {
        for (;;) {
            uint64_t t24 = gm_l4;
            uint64_t t25 = gm_l3;
            uint8_t t26 = (t24 < t25);
            if (!t26) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t27 = gm_l4;
                        gm_l5 = t27;
                        {
                            {
                                void * t28 = gm_l1;
                                void * t29 = GM_LD(void *, (((uint8_t *)t28) + 8));
                                uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t28) + 8) + 8));
                                uint64_t t31 = gm_l5;
                                if ((uintptr_t)t31 >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)t31, t30, gm_srcmhupam4se3h5, 165, 24);
                                uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t29) + (uintptr_t)t31 * 40) + 0));
                                uint8_t t33 = (uint8_t)(t32 == ((uint8_t)0ULL));
                                if (!t33) {
                                    goto gm_lb7;
                                }
                                gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t29) + (uintptr_t)t31 * 40) + 8));
                                gm_l6 = t34;
                                void * t35 = gm_l2;
                                void * t36 = GM_LD(void *, (((uint8_t *)t35) + 8));
                                uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)t35) + 8) + 8));
                                uint64_t t38 = gm_l5;
                                if ((uintptr_t)t38 >= (uintptr_t)(t37)) gm_trap_index((uintptr_t)t38, t37, gm_srcmhupam4se3h5, 165, 50);
                                uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t36) + (uintptr_t)t38 * 40) + 0));
                                uint8_t t40 = (uint8_t)(t39 == ((uint8_t)0ULL));
                                if (!t40) {
                                    goto gm_lb7;
                                }
                                gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t36) + (uintptr_t)t38 * 40) + 8));
                                gm_l7 = t41;
                                void * t42 = gm_l0;
                                gm_t88rcucg1b7uk t43 = gm_l6;
                                gm_t88rcucg1b7uk t44 = gm_l7;
                                uint8_t t45 = gm_f_provably_distinct_q4efcr7(t42, GM_CONV(gm_csguankko4lmhd, t43), GM_CONV(gm_csguankko4lmhd, t44));
                                if (t45) {
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 164, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t46 = gm_l4;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
            uint64_t t49 = r47;
            gm_l4 = t49;
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
inline __attribute__((aligned(4))) void gm_f_named_effect_set_s5gn1lr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_named_effect_set_s5gn1lr F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    uint64_t gm_l3; /* d */
    gm_t08olq97rscn3 gm_l4; /* id */
    void * gm_l5; /* base */
    gm_t08olq97rscn3 gm_l6; /* name */
    uint64_t gm_l7; /* m */
    gm_tnpifv20irnph gm_l8; /* set */
    GM_STACK_CHECK(&F, 801u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l9;
    uint64_t gm_l10;
    gm_tf5srh4mp4b3i gm_l11;
    uint64_t gm_l12;
    gm_tf5srh4mp4b3i gm_l13;
    uint64_t gm_l14;
    gm_t6qooom8qa2do gm_l15;
    gm_l3 = ((uint64_t)0ULL);
    void * t8 = gm_l2;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)6ULL));
            if (!t10) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t8) + 8) + 8), 32);
            void * t11 = gm_l0;
            uint64_t t12 = gm_l1;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_tf5srh4mp4b3i t14 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t11, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t14;
            uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l9))[8] != 0);
            if (t15) {
                uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                gm_l10 = t16;
            } else {
                gm_teseu4tn5o422 t17;
                memset(&t17, 0, sizeof t17);
                memmove(gm_out, ((uint8_t *)&t17), 24);
                goto gm_fdone;
            }
            uint64_t t18 = gm_l10;
            gm_l3 = t18;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)22ULL));
            if (!t20) {
                goto gm_lb3;
            }
            void * t21 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
            gm_l5 = t21;
            memmove(((uint8_t *)&gm_l6), (((((uint8_t *)t8) + 8) + 8) + 8), 32);
            void * t22 = gm_l0;
            uint64_t t23 = gm_l1;
            void * t24 = gm_l5;
            gm_tf5srh4mp4b3i t25 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t22, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t25;
            uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
            if (t26) {
                uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
                gm_l12 = t27;
            } else {
                gm_teseu4tn5o422 t28;
                memset(&t28, 0, sizeof t28);
                memmove(gm_out, ((uint8_t *)&t28), 24);
                goto gm_fdone;
            }
            uint64_t t29 = gm_l12;
            gm_l7 = t29;
            void * t30 = gm_l0;
            uint64_t t31 = gm_l7;
            uint64_t t32 = gm_l1;
            gm_tf5srh4mp4b3i t33 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t30, t31, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l6)), t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t33;
            uint8_t t34 = (uint8_t)((((uint8_t *)&gm_l13))[8] != 0);
            if (t34) {
                uint64_t t35 = GM_LD(uint64_t, ((uint8_t *)&gm_l13));
                gm_l14 = t35;
            } else {
                gm_teseu4tn5o422 t36;
                memset(&t36, 0, sizeof t36);
                memmove(gm_out, ((uint8_t *)&t36), 24);
                goto gm_fdone;
            }
            uint64_t t37 = gm_l14;
            gm_l3 = t37;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_teseu4tn5o422 t38;
            memset(&t38, 0, sizeof t38);
            memmove(gm_out, ((uint8_t *)&t38), 24);
            goto gm_fdone;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 279, 5);
    }
gm_lb1:;
    {
        {
            void * t39 = gm_l0;
            uint64_t t40 = gm_l3;
            gm_f_resolve_decl_pus042v((void *)&gm_l15, (void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)4ULL));
            if (!t42) {
                goto gm_lb6;
            }
            gm_tnpifv20irnph t43 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&gm_l15) + 8));
            gm_l8 = t43;
            gm_tnpifv20irnph t44 = gm_l8;
            gm_teseu4tn5o422 t45;
            GM_ST(uint8_t, (uint8_t *)&t45 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t45 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t45 + 20, 0);
            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t45), t44);
            ((uint8_t *)&t45)[16] = 1;
            memmove(gm_out, ((uint8_t *)&t45), 24);
            goto gm_fdone;
        }
gm_lb6:;
        gm_teseu4tn5o422 t46;
        memset(&t46, 0, sizeof t46);
        memmove(gm_out, ((uint8_t *)&t46), 24);
        goto gm_fdone;
    }
gm_lb5:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) uint32_t gm_f_new_literal_fn_oqjp4lg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, uint64_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_tnpifv20irnph a6);
inline __attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7) {
    struct gm_fr_new_nested_fn_r5u4ioo F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l1; /* kind */
    gm_b16_8 gm_l2; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint64_t gm_l4; /* module */
    gm_b16_8 gm_l5; /* params */
    gm_t88rcucg1b7uk gm_l6; /* ret */
    gm_tnpifv20irnph gm_l7; /* effects */
    GM_STACK_CHECK(&F, 422u);
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
    gm_l7 = a7;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l2;
    gm_tl874t2atnlh3 t4 = gm_l3;
    uint64_t t5 = gm_l4;
    gm_b16_8 t6 = gm_l5;
    gm_t88rcucg1b7uk t7 = gm_l6;
    gm_tnpifv20irnph t8 = gm_l7;
    gm_b16_8 t9 = gm_f_empty_slice_d914i5u();
    gm_tqu3m2l3n8mnc t10;
    memset(&t10, 0, sizeof t10);
    gm_tqqhoivahirr0 t11;
    GM_ST(uint32_t, (uint8_t *)&t11 + 28, 0);
    GM_ST(uint32_t, (uint8_t *)&t11 + 60, 0);
    GM_ST(uint8_t, (uint8_t *)&t11 + 129, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 130, 0);
    GM_ST(uint32_t, (uint8_t *)&t11 + 132, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t3);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t11 + 16), t4);
    GM_ST(uint64_t, ((uint8_t *)&t11 + 32), t5);
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 40), t6);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t11 + 56), t7);
    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t11 + 64), t8);
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 80), t9);
    GM_ST(gm_tqu3m2l3n8mnc, ((uint8_t *)&t11 + 96), t10);
    GM_ST(uint8_t, ((uint8_t *)&t11 + 128), ((uint8_t)0));
    gm_t9qkh0rsooc8d t12 = gm_l1;
    gm_tdgfduqjheqdm t13;
    gm_f_bodiless_def_u9kqo09((void *)&t13, t12);
    uint32_t t14 = gm_f_add_fn_t4t83qh((void *)NULL, cx, NULL, t2, &t11, &t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t14;
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
__attribute__((aligned(4))) uint8_t gm_f_provably_distinct_q4efcr7(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_provably_distinct_q4efcr7 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* a */
    gm_t88rcucg1b7uk gm_l2; /* b */
    gm_t88rcucg1b7uk gm_l5; /* x */
    gm_t88rcucg1b7uk gm_l6; /* y */
    gm_t88rcucg1b7uk gm_l7; /* x */
    gm_t88rcucg1b7uk gm_l8; /* y */
    gm_t88rcucg1b7uk gm_l9; /* x */
    gm_t88rcucg1b7uk gm_l10; /* y */
    gm_t88rcucg1b7uk gm_l11; /* x */
    gm_t88rcucg1b7uk gm_l12; /* y */
    uint32_t gm_l13; /* x */
    gm_b16_8 gm_l14; /* xs */
    uint32_t gm_l15; /* y */
    gm_b16_8 gm_l16; /* ys */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    gm_t88rcucg1b7uk gm_l20; /* p */
    gm_t88rcucg1b7uk gm_l21; /* q */
    gm_b16_8 gm_l22; /* x */
    gm_b16_8 gm_l23; /* y */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* i */
    GM_STACK_CHECK(&F, 914u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    uint8_t gm_l27;
    uint8_t gm_l28;
    uint8_t gm_l29;
    {
        {
            {
                void * t42 = gm_l0;
                void * t43 = (void *)(((uint8_t *)t42) + 8);
                gm_t88rcucg1b7uk t44 = gm_l1;
                uint8_t t45 = gm_f_has_param_u9t0efs(t43, GM_CONV(gm_csguankko4lmhd, t44));
                uint8_t t46 = (uint8_t)!t45;
                if (!t46) {
                    goto gm_lb2;
                }
                void * t47 = gm_l0;
                void * t48 = (void *)(((uint8_t *)t47) + 8);
                gm_t88rcucg1b7uk t49 = gm_l2;
                uint8_t t50 = gm_f_has_param_u9t0efs(t48, GM_CONV(gm_csguankko4lmhd, t49));
                uint8_t t51 = (uint8_t)!t50;
                if (!t51) {
                    goto gm_lb2;
                }
                gm_t88rcucg1b7uk t52 = gm_l1;
                gm_t88rcucg1b7uk t53 = gm_l2;
                uint8_t t54 = (!gm_eq9m6mvtmgde(((uint8_t *)&t52), ((uint8_t *)&t53)));
                F.ret = t54;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t55 = gm_l0;
        void * t56 = (void *)(((uint8_t *)t55) + 8);
        gm_t88rcucg1b7uk t57 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&F.l3, t56, GM_CONV(gm_csguankko4lmhd, t57));
        void * t58 = gm_l0;
        void * t59 = (void *)(((uint8_t *)t58) + 8);
        gm_t88rcucg1b7uk t60 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&F.l4, t59, GM_CONV(gm_csguankko4lmhd, t60));
        {
            {
                uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t62 = (uint8_t)(t61 == ((uint8_t)19ULL));
                if (!t62) {
                    goto gm_lb4;
                }
                gm_l28 = ((uint8_t)1);
                goto gm_lb3;
            }
gm_lb4:;
            {
                gm_l28 = ((uint8_t)0);
                goto gm_lb3;
            }
gm_lb5:;
            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 176, 8);
        }
gm_lb3:;
        uint8_t t63 = gm_l28;
        if (t63) {
            gm_l27 = ((uint8_t)1);
        } else {
            {
                {
                    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t65 = (uint8_t)(t64 == ((uint8_t)19ULL));
                    if (!t65) {
                        goto gm_lb7;
                    }
                    gm_l29 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    gm_l29 = ((uint8_t)0);
                    goto gm_lb6;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 176, 27);
            }
gm_lb6:;
            uint8_t t66 = gm_l29;
            gm_l27 = t66;
        }
        uint8_t t67 = gm_l27;
        if (t67) {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        {
            {
                uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t69 = (uint8_t)(t68 == ((uint8_t)8ULL));
                if (!t69) {
                    goto gm_lb10;
                }
                gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
                gm_l5 = t70;
                {
                    {
                        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t72 = (uint8_t)(t71 == ((uint8_t)8ULL));
                        if (!t72) {
                            goto gm_lb12;
                        }
                        gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                        gm_l6 = t73;
                        void * t74 = gm_l0;
                        gm_t88rcucg1b7uk t75 = gm_l5;
                        gm_t88rcucg1b7uk t76 = gm_l6;
                        uint8_t t77 = gm_f_provably_distinct_q4efcr7(t74, GM_CONV(gm_csguankko4lmhd, t75), GM_CONV(gm_csguankko4lmhd, t76));
                        F.ret = t77;
                        goto gm_fret;
                    }
gm_lb12:;
                }
gm_lb11:;
                goto gm_lb9;
            }
gm_lb10:;
            {
                uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t79 = (uint8_t)(t78 == ((uint8_t)10ULL));
                if (!t79) {
                    goto gm_lb13;
                }
                gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
                gm_l7 = t80;
                {
                    {
                        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t82 = (uint8_t)(t81 == ((uint8_t)10ULL));
                        if (!t82) {
                            goto gm_lb15;
                        }
                        gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                        gm_l8 = t83;
                        void * t84 = gm_l0;
                        gm_t88rcucg1b7uk t85 = gm_l7;
                        gm_t88rcucg1b7uk t86 = gm_l8;
                        uint8_t t87 = gm_f_provably_distinct_q4efcr7(t84, GM_CONV(gm_csguankko4lmhd, t85), GM_CONV(gm_csguankko4lmhd, t86));
                        F.ret = t87;
                        goto gm_fret;
                    }
gm_lb15:;
                }
gm_lb14:;
                goto gm_lb9;
            }
gm_lb13:;
            {
                uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t89 = (uint8_t)(t88 == ((uint8_t)9ULL));
                if (!t89) {
                    goto gm_lb16;
                }
                gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l3) + 8) + 4));
                gm_l9 = t90;
                {
                    {
                        uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t92 = (uint8_t)(t91 == ((uint8_t)9ULL));
                        if (!t92) {
                            goto gm_lb18;
                        }
                        gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                        gm_l10 = t93;
                        void * t94 = gm_l0;
                        gm_t88rcucg1b7uk t95 = gm_l9;
                        gm_t88rcucg1b7uk t96 = gm_l10;
                        uint8_t t97 = gm_f_provably_distinct_q4efcr7(t94, GM_CONV(gm_csguankko4lmhd, t95), GM_CONV(gm_csguankko4lmhd, t96));
                        F.ret = t97;
                        goto gm_fret;
                    }
gm_lb18:;
                }
gm_lb17:;
                goto gm_lb9;
            }
gm_lb16:;
            {
                uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t99 = (uint8_t)(t98 == ((uint8_t)13ULL));
                if (!t99) {
                    goto gm_lb19;
                }
                gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 8));
                gm_l11 = t100;
                {
                    {
                        uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t102 = (uint8_t)(t101 == ((uint8_t)13ULL));
                        if (!t102) {
                            goto gm_lb21;
                        }
                        gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
                        gm_l12 = t103;
                        void * t104 = gm_l0;
                        gm_t88rcucg1b7uk t105 = gm_l11;
                        gm_t88rcucg1b7uk t106 = gm_l12;
                        uint8_t t107 = gm_f_provably_distinct_q4efcr7(t104, GM_CONV(gm_csguankko4lmhd, t105), GM_CONV(gm_csguankko4lmhd, t106));
                        F.ret = t107;
                        goto gm_fret;
                    }
gm_lb21:;
                }
gm_lb20:;
                goto gm_lb9;
            }
gm_lb19:;
            {
                uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t109 = (uint8_t)(t108 == ((uint8_t)17ULL));
                if (!t109) {
                    goto gm_lb22;
                }
                uint32_t t110 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
                gm_l13 = t110;
                gm_b16_8 t111 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 8) + 8));
                gm_l14 = t111;
                {
                    {
                        uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t113 = (uint8_t)(t112 == ((uint8_t)17ULL));
                        if (!t113) {
                            goto gm_lb24;
                        }
                        uint32_t t114 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 8));
                        gm_l15 = t114;
                        gm_b16_8 t115 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 8));
                        gm_l16 = t115;
                        void * t116 = gm_l0;
                        void * t117 = (void *)(((uint8_t *)t116) + 8);
                        uint32_t t118 = gm_l13;
                        uint32_t t119 = gm_f_template_9o92cvv(t117, t118);
                        void * t120 = gm_l0;
                        void * t121 = (void *)(((uint8_t *)t120) + 8);
                        uint32_t t122 = gm_l15;
                        uint32_t t123 = gm_f_template_9o92cvv(t121, t122);
                        uint8_t t124 = (!(t119 == t123));
                        if (t124) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        gm_l18 = ((uint64_t)0ULL);
                        uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                        uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                        uint64_t t127 = (t125 < t126 ? t125 : t126);
                        gm_l17 = t127;
                        {
                            for (;;) {
                                uint64_t t128 = gm_l18;
                                uint64_t t129 = gm_l17;
                                uint8_t t130 = (t128 < t129);
                                if (!t130) {
                                    goto gm_lb25;
                                }
                                {
                                    {
                                        {
                                            uint64_t t131 = gm_l18;
                                            gm_l19 = t131;
                                            {
                                                {
                                                    void * t132 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                                    uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                                    uint64_t t134 = gm_l19;
                                                    if ((uintptr_t)t134 >= (uintptr_t)(t133)) gm_trap_index((uintptr_t)t134, t133, gm_srcmhupam4se3h5, 185, 32);
                                                    uint8_t t135 = GM_LD(uint8_t, ((((uint8_t *)t132) + (uintptr_t)t134 * 40) + 0));
                                                    uint8_t t136 = (uint8_t)(t135 == ((uint8_t)0ULL));
                                                    if (!t136) {
                                                        goto gm_lb31;
                                                    }
                                                    gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t132) + (uintptr_t)t134 * 40) + 8));
                                                    gm_l20 = t137;
                                                    void * t138 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                                    uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                                    uint64_t t140 = gm_l19;
                                                    if ((uintptr_t)t140 >= (uintptr_t)(t139)) gm_trap_index((uintptr_t)t140, t139, gm_srcmhupam4se3h5, 185, 54);
                                                    uint8_t t141 = GM_LD(uint8_t, ((((uint8_t *)t138) + (uintptr_t)t140 * 40) + 0));
                                                    uint8_t t142 = (uint8_t)(t141 == ((uint8_t)0ULL));
                                                    if (!t142) {
                                                        goto gm_lb31;
                                                    }
                                                    gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t138) + (uintptr_t)t140 * 40) + 8));
                                                    gm_l21 = t143;
                                                    void * t144 = gm_l0;
                                                    gm_t88rcucg1b7uk t145 = gm_l20;
                                                    gm_t88rcucg1b7uk t146 = gm_l21;
                                                    uint8_t t147 = gm_f_provably_distinct_q4efcr7(t144, GM_CONV(gm_csguankko4lmhd, t145), GM_CONV(gm_csguankko4lmhd, t146));
                                                    if (t147) {
                                                        F.ret = ((uint8_t)1);
                                                        goto gm_fret;
                                                    }
                                                    goto gm_lb30;
                                                }
gm_lb31:;
                                            }
gm_lb30:;
                                            goto gm_lb28;
                                        }
gm_lb29:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 184, 13);
                                    }
gm_lb28:;
                                }
gm_lb27:;
                                uint64_t t148 = gm_l18;
                                uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                                uint64_t t151 = r149;
                                gm_l18 = t151;
                            }
                        }
gm_lb25:;
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
gm_lb24:;
                }
gm_lb23:;
                goto gm_lb9;
            }
gm_lb22:;
            {
                uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                uint8_t t153 = (uint8_t)(t152 == ((uint8_t)12ULL));
                if (!t153) {
                    goto gm_lb32;
                }
                gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
                gm_l22 = t154;
                {
                    {
                        uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        uint8_t t156 = (uint8_t)(t155 == ((uint8_t)12ULL));
                        if (!t156) {
                            goto gm_lb34;
                        }
                        gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                        gm_l23 = t157;
                        uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                        uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                        uint8_t t160 = (!(t158 == t159));
                        if (t160) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        gm_l25 = ((uint64_t)0ULL);
                        uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                        gm_l24 = t161;
                        {
                            for (;;) {
                                uint64_t t162 = gm_l25;
                                uint64_t t163 = gm_l24;
                                uint8_t t164 = (t162 < t163);
                                if (!t164) {
                                    goto gm_lb35;
                                }
                                {
                                    {
                                        {
                                            uint64_t t165 = gm_l25;
                                            gm_l26 = t165;
                                            void * t166 = gm_l0;
                                            void * t167 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                            uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                            uint64_t t169 = gm_l26;
                                            gm_t88rcucg1b7uk t170 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t167) + (uintptr_t)t169 * 4));
                                            void * t171 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                            uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                            uint64_t t173 = gm_l26;
                                            if ((uintptr_t)t173 >= (uintptr_t)(t172)) gm_trap_index((uintptr_t)t173, t172, gm_srcmhupam4se3h5, 194, 46);
                                            gm_t88rcucg1b7uk t174 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t171) + (uintptr_t)t173 * 4));
                                            uint8_t t175 = gm_f_provably_distinct_q4efcr7(t166, GM_CONV(gm_csguankko4lmhd, t170), GM_CONV(gm_csguankko4lmhd, t174));
                                            if (t175) {
                                                F.ret = ((uint8_t)1);
                                                goto gm_fret;
                                            }
                                            goto gm_lb38;
                                        }
gm_lb39:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 193, 13);
                                    }
gm_lb38:;
                                }
gm_lb37:;
                                uint64_t t176 = gm_l25;
                                uint64_t r177; bool o178 = __builtin_add_overflow(t176, ((uint64_t)1ULL), &r177);
                                uint64_t t179 = r177;
                                gm_l25 = t179;
                            }
                        }
gm_lb35:;
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
gm_lb34:;
                }
gm_lb33:;
                goto gm_lb9;
            }
gm_lb32:;
            {
                goto gm_lb9;
            }
gm_lb40:;
            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 177, 5);
        }
gm_lb9:;
    }
    {
        gm_t53at80pfdus3 t180 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)&F.l3));
        uint8_t t181 = GM_LD(uint8_t, ((uint8_t *)&t180));
        gm_t53at80pfdus3 t182 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)&F.l4));
        uint8_t t183 = GM_LD(uint8_t, ((uint8_t *)&t182));
        uint8_t t184 = (!(t181 == t183));
        F.ret = t184;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_effect_arg_4h6q4qv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_effect_arg_4h6q4qv F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* p */
    gm_tnpifv20irnph gm_l4; /* set */
    gm_tnpifv20irnph gm_l5; /* set */
    gm_tiilmfd16v3fc gm_l6; /* r */
    gm_tfe2ufaqq8re9 gm_l7; /* <temporary array> */
    GM_STACK_CHECK(&F, 783u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tkn42u7na3t81 gm_l8;
    gm_teseu4tn5o422 gm_l9;
    uint8_t gm_l10;
    gm_tqu3m2l3n8mnc gm_l11;
    gm_tiilmfd16v3fc gm_l12;
    {
        {
            void * t9 = gm_l0;
            void * t10 = gm_l2;
            gm_tkn42u7na3t81 t11 = gm_f_effect_param_named_h2jeoo7(t9, t10);
            gm_l8 = t11;
            uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l8))[4] != 0);
            if (!t12) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l8));
            gm_l3 = t13;
            gm_t88rcucg1b7uk t14 = gm_l3;
            F.ret = t14;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t15 = gm_l0;
            uint64_t t16 = gm_l1;
            void * t17 = gm_l2;
            gm_f_named_effect_set_s5gn1lr((void *)&gm_l9, (void *)NULL, cx, NULL, t15, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
            if (!t18) {
                goto gm_lb4;
            }
            gm_tnpifv20irnph t19 = GM_LD(gm_tnpifv20irnph, ((uint8_t *)&gm_l9));
            gm_l4 = t19;
            void * t20 = gm_l0;
            void * t21 = (void *)(((uint8_t *)t20) + 8);
            gm_tnpifv20irnph t22 = gm_l4;
            gm_t53at80pfdus3 t23;
            memset(&t23, 0, sizeof t23);
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)24ULL));
            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t23 + 8), t22);
            gm_t88rcucg1b7uk t24 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t21, &t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t24;
            goto gm_fret;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t25 = gm_l2;
    {
        {
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)13ULL));
            if (!t27) {
                goto gm_lb6;
            }
            uint8_t t28 = GM_LD(uint8_t, (((((uint8_t *)t25) + 8) + 8) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)0ULL));
            if (!t29) {
                goto gm_lb6;
            }
            uint8_t t30 = GM_LD(uint8_t, ((((((uint8_t *)t25) + 8) + 8) + 1) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)0ULL));
            if (!t31) {
                goto gm_lb6;
            }
            gm_l10 = ((uint8_t)1);
            goto gm_lb5;
        }
gm_lb6:;
        {
            gm_l10 = ((uint8_t)0);
            goto gm_lb5;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 395, 8);
    }
gm_lb5:;
    uint8_t t32 = gm_l10;
    if (t32) {
        void * t33 = gm_l0;
        uint64_t t34 = gm_l1;
        void * t35 = gm_l2;
        gm_b16_8 t36 = gm_f_effect_terms_bphj7th((void *)NULL, cx, NULL, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        gm_tnpifv20irnph t38 = gm_f_resolve_effect_set_o7rc5od((void *)NULL, cx, NULL, t33, t34, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t38;
        void * t39 = gm_l0;
        void * t40 = (void *)(((uint8_t *)t39) + 8);
        gm_tnpifv20irnph t41 = gm_l5;
        gm_t53at80pfdus3 t42;
        memset(&t42, 0, sizeof t42);
        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)24ULL));
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t42 + 8), t41);
        gm_t88rcucg1b7uk t43 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t40, &t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t43;
        goto gm_fret;
    }
    void * t44 = gm_l0;
    uint64_t t45 = gm_l1;
    void * t46 = gm_l2;
    gm_f_resolve_effect_ref_bggirs2((void *)&gm_l11, (void *)NULL, cx, NULL, t44, t45, t46);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l11))[24] != 0);
    if (t47) {
        memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l11), 24);
    } else {
        void * t48 = gm_l0;
        void * t49 = (void *)(((uint8_t *)t48) + 8);
        gm_t88rcucg1b7uk t50 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t50;
        goto gm_fret;
    }
    memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l12), 24);
    void * t51 = gm_l0;
    void * t52 = (void *)(((uint8_t *)t51) + 8);
    gm_tiilmfd16v3fc t53 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l6));
    gm_tfe2ufaqq8re9 t54;
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t54 + 0), t53);
    gm_l7 = t54;
    void * t55 = (void *)((uint8_t *)&gm_l7);
    uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t57 = (uintptr_t)((uint64_t)1ULL);
    if (t56 > t57 || t57 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t56, t57, ((uint64_t)1ULL), gm_srcmhupam4se3h5, 400, 51);
    uint64_t t58 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t59;
    GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t55 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(t58));
    gm_tnpifv20irnph t60 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t59);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t53at80pfdus3 t61;
    memset(&t61, 0, sizeof t61);
    GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)24ULL));
    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t61 + 8), t60);
    gm_t88rcucg1b7uk t62 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t52, &t61);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_list_uhjlh6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_resolve_effect_list_uhjlh6o F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* list_ */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 286u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l7;
    gm_trqfvmjtt4sij t7;
    gm_f_list_qs7fohu((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t7;
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_l4 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l5;
            uint64_t t10 = gm_l4;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t12 = gm_l5;
                        gm_l6 = t12;
                        void * t13 = (void *)((uint8_t *)&F.l3);
                        void * t14 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t16 = gm_l6;
                        void * t17 = (void *)(((uint8_t *)t14) + (uintptr_t)t16 * 88);
                        gm_tftb1odc739t5 t18 = gm_f_push_57kepgg(t13, t17);
                        gm_l7 = t18;
                        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcmhupam4se3h5, 254, 29);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 254, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t20 = gm_l5;
            uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
            uint64_t t23 = r21;
            gm_l5 = t23;
        }
    }
gm_lb1:;
    void * t24 = gm_l0;
    uint64_t t25 = gm_l1;
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
    gm_tnpifv20irnph t28 = gm_f_resolve_effect_set_o7rc5od((void *)NULL, cx, NULL, t24, t25, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t28;
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
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_resolve_effect_ref_bggirs2 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* p */
    void * gm_l4; /* callee */
    gm_b16_8 gm_l5; /* args */
    gm_tiilmfd16v3fc gm_l6; /* base */
    gm_b16_8 gm_l7; /* params */
    uint64_t gm_l8; /* required */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    uint32_t gm_l12; /* p */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint32_t gm_l17; /* p */
    void * gm_l18; /* a */
    gm_t08olq97rscn3 gm_l20; /* id */
    uint64_t gm_l21; /* d */
    uint32_t gm_l22; /* eff */
    gm_tiilmfd16v3fc gm_l23; /* r */
    void * gm_l24; /* base */
    gm_t08olq97rscn3 gm_l25; /* name */
    uint64_t gm_l26; /* m */
    uint64_t gm_l27; /* d */
    uint32_t gm_l28; /* eff */
    gm_tiilmfd16v3fc gm_l29; /* r */
    GM_STACK_CHECK(&F, 3744u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tkn42u7na3t81 gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tqu3m2l3n8mnc gm_l32;
    gm_tiilmfd16v3fc gm_l33;
    gm_tn68el8d14vku gm_l34;
    uint8_t gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_tn68el8d14vku gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tf5srh4mp4b3i gm_l41;
    uint64_t gm_l42;
    gm_tabb710d9mq2e gm_l43;
    gm_t6qooom8qa2do gm_l44;
    gm_tabb710d9mq2e gm_l45;
    gm_tabb710d9mq2e gm_l46;
    gm_tf5srh4mp4b3i gm_l47;
    gm_tf5srh4mp4b3i gm_l48;
    uint64_t gm_l49;
    gm_t6qooom8qa2do gm_l50;
    gm_tabb710d9mq2e gm_l51;
    {
        {
            {
                void * t35 = gm_l0;
                void * t36 = gm_l2;
                gm_tkn42u7na3t81 t37 = gm_f_effect_param_named_h2jeoo7(t35, t36);
                gm_l30 = t37;
                uint8_t t38 = (uint8_t)((((uint8_t *)&gm_l30))[4] != 0);
                if (!t38) {
                    goto gm_lb2;
                }
                gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l30));
                gm_l3 = t39;
                void * t40 = gm_l0;
                void * t41 = gm_l2;
                gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t41) + 72));
                void * t43 = gm_l0;
                gm_t88rcucg1b7uk t44 = gm_l3;
                gm_b16_8 t45 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t43, t44);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t46;
                GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
                gm_f_format_uvukia4((void *)&gm_l31, (void *)NULL, cx, NULL, t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srcmhupam4se3h5, 293, 25);
                gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                gm_b0_1 t50 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t40, t42, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqu3m2l3n8mnc t51;
                memset(&t51, 0, sizeof t51);
                memmove(gm_out, ((uint8_t *)&t51), 32);
                goto gm_fdone;
            }
gm_lb2:;
        }
gm_lb1:;
        {
            {
                void * t52 = gm_l2;
                uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t52) + 8) + 0));
                uint8_t t54 = (uint8_t)(t53 == ((uint8_t)19ULL));
                if (!t54) {
                    goto gm_lb4;
                }
                void * t55 = GM_LD(void *, ((((uint8_t *)t52) + 8) + 8));
                gm_l4 = t55;
                gm_b16_8 t56 = GM_LD(gm_b16_8, (((((uint8_t *)t52) + 8) + 8) + 8));
                gm_l5 = t56;
                void * t57 = gm_l0;
                uint64_t t58 = gm_l1;
                void * t59 = gm_l4;
                gm_f_resolve_effect_ref_bggirs2((void *)&gm_l32, (void *)NULL, cx, NULL, t57, t58, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t60 = (uint8_t)((((uint8_t *)&gm_l32))[24] != 0);
                if (t60) {
                    memmove(((uint8_t *)&gm_l33), ((uint8_t *)&gm_l32), 24);
                } else {
                    gm_tqu3m2l3n8mnc t61;
                    memset(&t61, 0, sizeof t61);
                    memmove(gm_out, ((uint8_t *)&t61), 32);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l33), 24);
                void * t62 = gm_l0;
                void * t63 = GM_LD(void *, (((uint8_t *)t62) + 1048));
                uint64_t t64 = GM_LD(uint64_t, ((((uint8_t *)t62) + 1048) + 8));
                uint32_t t65 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
                uint64_t t66 = (uint64_t)t65;
                if ((uintptr_t)t66 >= (uintptr_t)(t64)) gm_trap_index((uintptr_t)t66, t64, gm_srcmhupam4se3h5, 299, 19);
                gm_b16_8 t67 = GM_LD(gm_b16_8, ((((uint8_t *)t63) + (uintptr_t)t66 * 56) + 24));
                gm_l7 = t67;
                gm_l8 = ((uint64_t)0ULL);
                gm_b16_8 t68 = gm_l7;
                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                gm_l9 = t69;
                gm_l10 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t70 = gm_l10;
                        uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                        uint8_t t72 = (t70 < t71);
                        if (!t72) {
                            goto gm_lb5;
                        }
                        {
                            uint64_t t73 = gm_l10;
                            void * t74 = GM_LD(void *, ((uint8_t *)&gm_l9));
                            uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                            uint64_t t76 = gm_l10;
                            uint32_t t77 = GM_LD(uint32_t, (((uint8_t *)t74) + (uintptr_t)t76 * 4));
                            gm_tn68el8d14vku t78;
                            GM_ST(uint32_t, (uint8_t *)&t78 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t78 + 0), t73);
                            GM_ST(uint32_t, ((uint8_t *)&t78 + 8), t77);
                            gm_l34 = t78;
                            {
                                {
                                    uint64_t t79 = GM_LD(uint64_t, ((uint8_t *)&gm_l34));
                                    gm_l11 = t79;
                                    uint32_t t80 = GM_LD(uint32_t, (((uint8_t *)&gm_l34) + 8));
                                    gm_l12 = t80;
                                    void * t81 = gm_l0;
                                    void * t82 = GM_LD(void *, (((uint8_t *)t81) + 1128));
                                    uint64_t t83 = GM_LD(uint64_t, ((((uint8_t *)t81) + 1128) + 8));
                                    uint32_t t84 = gm_l12;
                                    uint64_t t85 = (uint64_t)t84;
                                    if ((uintptr_t)t85 >= (uintptr_t)(t83)) gm_trap_index((uintptr_t)t85, t83, gm_srcmhupam4se3h5, 303, 16);
                                    gm_tnnjeerolos0a t86 = GM_LD(gm_tnnjeerolos0a, (((uint8_t *)t82) + (uintptr_t)t85 * 48));
                                    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&t86) + 0));
                                    uint8_t t88 = (uint8_t)(!(t87 == ((uint8_t)1ULL)));
                                    gm_tnnjeerolos0a t89;
                                    GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)1ULL));
                                    if (t88) {
                                        uint64_t t90 = gm_l11;
                                        uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                                        if (o92) gm_trap("integer overflow in `+`", gm_srcmhupam4se3h5, 303, 75);
                                        uint64_t t93 = r91;
                                        gm_l8 = t93;
                                    }
                                    goto gm_lb8;
                                }
gm_lb9:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 302, 9);
                            }
gm_lb8:;
                        }
gm_lb7:;
                        uint64_t t94 = gm_l10;
                        uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                        uint64_t t97 = r95;
                        gm_l10 = t97;
                    }
                }
gm_lb5:;
                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t100 = (t98 > t99);
                if (t100) {
                    gm_l35 = ((uint8_t)1);
                } else {
                    uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint64_t t102 = gm_l8;
                    uint8_t t103 = (t101 < t102);
                    gm_l35 = t103;
                }
                uint8_t t104 = gm_l35;
                if (t104) {
                    void * t105 = gm_l0;
                    void * t106 = gm_l2;
                    gm_tl874t2atnlh3 t107 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t106) + 72));
                    void * t108 = gm_l0;
                    void * t109 = (void *)(((uint8_t *)t108) + 8);
                    uint32_t t110 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
                    gm_b16_8 t111 = gm_f_effect_name_prpp0ip(t109, t110);
                    uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    gm_t3877pk7lv748 t113;
                    GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t111);
                    GM_ST(uint64_t, ((uint8_t *)&t113 + 16), t112);
                    gm_f_format_n6tblha((void *)&gm_l36, (void *)NULL, cx, NULL, &t113);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                    if (!t114) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_srcmhupam4se3h5, 306, 29);
                    gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                    gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
                    gm_b0_1 t117 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t105, t107, t116);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqu3m2l3n8mnc t118;
                    memset(&t118, 0, sizeof t118);
                    memmove(gm_out, ((uint8_t *)&t118), 32);
                    goto gm_fdone;
                }
                gm_trqfvmjtt4sij t119;
                gm_f_list_vmvl81b((void *)&t119, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l13 = t119;
                gm_b16_8 t120 = gm_l7;
                gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                gm_l14 = t121;
                gm_l15 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t122 = gm_l15;
                        uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                        uint8_t t124 = (t122 < t123);
                        if (!t124) {
                            goto gm_lb10;
                        }
                        {
                            uint64_t t125 = gm_l15;
                            void * t126 = GM_LD(void *, ((uint8_t *)&gm_l14));
                            uint64_t t127 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                            uint64_t t128 = gm_l15;
                            uint32_t t129 = GM_LD(uint32_t, (((uint8_t *)t126) + (uintptr_t)t128 * 4));
                            gm_tn68el8d14vku t130;
                            GM_ST(uint32_t, (uint8_t *)&t130 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t130 + 0), t125);
                            GM_ST(uint32_t, ((uint8_t *)&t130 + 8), t129);
                            gm_l37 = t130;
                            {
                                {
                                    uint64_t t131 = GM_LD(uint64_t, ((uint8_t *)&gm_l37));
                                    gm_l16 = t131;
                                    uint32_t t132 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 8));
                                    gm_l17 = t132;
                                    uint64_t t133 = gm_l16;
                                    uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                    uint8_t t135 = (t133 >= t134);
                                    if (t135) {
                                        void * t136 = (void *)((uint8_t *)&F.l13);
                                        void * t137 = gm_l0;
                                        void * t138 = (void *)(((uint8_t *)t137) + 8);
                                        gm_tnpifv20irnph t139 = gm_f_empty_effects_r0iv3pv();
                                        gm_t53at80pfdus3 t140;
                                        memset(&t140, 0, sizeof t140);
                                        GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)24ULL));
                                        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t140 + 8), t139);
                                        gm_t88rcucg1b7uk t141 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t138, &t140);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_toa2vhe3l4hfr t142;
                                        memset(&t142, 0, sizeof t142);
                                        GM_ST(uint8_t, ((uint8_t *)&t142 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t142 + 8), t141);
                                        gm_tftb1odc739t5 t143 = gm_f_push_s5vr2a2(t136, &t142);
                                        gm_l38 = t143;
                                        uint8_t t144 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                        if (!t144) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srcmhupam4se3h5, 312, 17);
                                        goto gm_lb12;
                                    }
                                    void * t145 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                    uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                    uint64_t t147 = gm_l16;
                                    void * t148 = (void *)(((uint8_t *)t145) + (uintptr_t)t147 * 144);
                                    gm_l18 = t148;
                                    {
                                        {
                                            void * t149 = gm_l18;
                                            uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)t149) + 0));
                                            uint8_t t151 = (uint8_t)(t150 == ((uint8_t)0ULL));
                                            if (!t151) {
                                                goto gm_lb16;
                                            }
                                            memmove(((uint8_t *)&F.l19), (((uint8_t *)t149) + 8), 88);
                                            void * t152 = gm_l0;
                                            void * t153 = GM_LD(void *, (((uint8_t *)t152) + 1128));
                                            uint64_t t154 = GM_LD(uint64_t, ((((uint8_t *)t152) + 1128) + 8));
                                            uint32_t t155 = gm_l17;
                                            uint64_t t156 = (uint64_t)t155;
                                            if ((uintptr_t)t156 >= (uintptr_t)(t154)) gm_trap_index((uintptr_t)t156, t154, gm_srcmhupam4se3h5, 320, 16);
                                            gm_tnnjeerolos0a t157 = GM_LD(gm_tnnjeerolos0a, (((uint8_t *)t153) + (uintptr_t)t156 * 48));
                                            uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&t157) + 0));
                                            uint8_t t159 = (uint8_t)((t158 == ((uint8_t)1ULL)));
                                            gm_tnnjeerolos0a t160;
                                            GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)1ULL));
                                            if (t159) {
                                                void * t161 = (void *)((uint8_t *)&F.l13);
                                                void * t162 = gm_l0;
                                                uint64_t t163 = gm_l1;
                                                void * t164 = (void *)((uint8_t *)&F.l19);
                                                gm_t88rcucg1b7uk t165 = gm_f_resolve_effect_arg_4h6q4qv((void *)NULL, cx, NULL, t162, t163, t164);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_toa2vhe3l4hfr t166;
                                                memset(&t166, 0, sizeof t166);
                                                GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)0ULL));
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t166 + 8), t165);
                                                gm_tftb1odc739t5 t167 = gm_f_push_s5vr2a2(t161, &t166);
                                                gm_l39 = t167;
                                                uint8_t t168 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                                if (!t168) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_srcmhupam4se3h5, 321, 17);
                                            } else {
                                                void * t169 = (void *)((uint8_t *)&F.l13);
                                                void * t170 = gm_l0;
                                                uint64_t t171 = gm_l1;
                                                void * t172 = (void *)((uint8_t *)&F.l19);
                                                gm_t88rcucg1b7uk t173 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t170, t171, t172);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_toa2vhe3l4hfr t174;
                                                memset(&t174, 0, sizeof t174);
                                                GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)0ULL));
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t174 + 8), t173);
                                                gm_tftb1odc739t5 t175 = gm_f_push_s5vr2a2(t169, &t174);
                                                gm_l40 = t175;
                                                uint8_t t176 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                                if (!t176) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srcmhupam4se3h5, 323, 17);
                                            }
                                            goto gm_lb15;
                                        }
gm_lb16:;
                                        void * t177 = gm_l0;
                                        void * t178 = gm_l18;
                                        gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t178) + 128));
                                        gm_b16_8 t180;
                                        GM_ST(void *, (uint8_t *)&t180, (void *)(gm_spskd7qirpoe3));
                                        GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(24));
                                        gm_b0_1 t181 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t177, t179, t180);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tqu3m2l3n8mnc t182;
                                        memset(&t182, 0, sizeof t182);
                                        memmove(gm_out, ((uint8_t *)&t182), 32);
                                        goto gm_fdone;
                                    }
gm_lb15:;
                                    goto gm_lb13;
                                }
gm_lb14:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 310, 9);
                            }
gm_lb13:;
                        }
gm_lb12:;
                        uint64_t t183 = gm_l15;
                        uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
                        uint64_t t186 = r184;
                        gm_l15 = t186;
                    }
                }
gm_lb10:;
                uint32_t t187 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
                gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)&t188));
                gm_tiilmfd16v3fc t190;
                GM_ST(uint32_t, (uint8_t *)&t190 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t190 + 0), t187);
                GM_ST(gm_b16_8, ((uint8_t *)&t190 + 8), t189);
                gm_tqu3m2l3n8mnc t191;
                GM_ST(uint8_t, (uint8_t *)&t191 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t191 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t191 + 28, 0);
                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t191), t190);
                ((uint8_t *)&t191)[24] = 1;
                memmove(gm_out, ((uint8_t *)&t191), 32);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    {
        {
            {
                void * t192 = gm_l2;
                uint8_t t193 = GM_LD(uint8_t, ((((uint8_t *)t192) + 8) + 0));
                uint8_t t194 = (uint8_t)(t193 == ((uint8_t)6ULL));
                if (!t194) {
                    goto gm_lb18;
                }
                memmove(((uint8_t *)&gm_l20), ((((uint8_t *)t192) + 8) + 8), 32);
                void * t195 = gm_l0;
                uint64_t t196 = gm_l1;
                gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                gm_tf5srh4mp4b3i t198 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t195, t196, t197);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l41 = t198;
                uint8_t t199 = (uint8_t)((((uint8_t *)&gm_l41))[8] != 0);
                if (t199) {
                    uint64_t t200 = GM_LD(uint64_t, ((uint8_t *)&gm_l41));
                    gm_l42 = t200;
                } else {
                    void * t201 = gm_l0;
                    void * t202 = gm_l2;
                    gm_tl874t2atnlh3 t203 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t202) + 72));
                    gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                    gm_tnpifv20irnph t205;
                    GM_ST(gm_b16_8, ((uint8_t *)&t205 + 0), t204);
                    gm_f_format_kjp7rmf((void *)&gm_l43, (void *)NULL, cx, NULL, t205);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t206 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                    if (!t206) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 8, gm_srcmhupam4se3h5, 330, 29);
                    gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
                    gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)&t207));
                    gm_b0_1 t209 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t201, t203, t208);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqu3m2l3n8mnc t210;
                    memset(&t210, 0, sizeof t210);
                    memmove(gm_out, ((uint8_t *)&t210), 32);
                    goto gm_fdone;
                }
                uint64_t t211 = gm_l42;
                gm_l21 = t211;
                void * t212 = gm_l0;
                uint64_t t213 = gm_l21;
                gm_f_resolve_decl_pus042v((void *)&gm_l44, (void *)NULL, cx, NULL, t212, t213);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                {
                    {
                        uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l44) + 0));
                        uint8_t t215 = (uint8_t)(t214 == ((uint8_t)2ULL));
                        if (!t215) {
                            goto gm_lb20;
                        }
                        uint32_t t216 = GM_LD(uint32_t, (((uint8_t *)&gm_l44) + 8));
                        gm_l22 = t216;
                        void * t217 = gm_l0;
                        uint32_t t218 = gm_l22;
                        gm_tiilmfd16v3fc t219;
                        gm_f_bare_effect_ref_a5ofmji((void *)&t219, (void *)NULL, cx, NULL, t217, t218);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tqu3m2l3n8mnc t220;
                        GM_ST(uint8_t, (uint8_t *)&t220 + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&t220 + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&t220 + 28, 0);
                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t220), t219);
                        ((uint8_t *)&t220)[24] = 1;
                        memmove(gm_out, ((uint8_t *)&t220), 32);
                        goto gm_fdone;
                    }
gm_lb20:;
                    {
                        uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&gm_l44) + 0));
                        uint8_t t222 = (uint8_t)(t221 == ((uint8_t)3ULL));
                        if (!t222) {
                            goto gm_lb21;
                        }
                        memmove(((uint8_t *)&gm_l23), (((uint8_t *)&gm_l44) + 8), 24);
                        gm_tiilmfd16v3fc t223 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l23));
                        gm_tqu3m2l3n8mnc t224;
                        GM_ST(uint8_t, (uint8_t *)&t224 + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&t224 + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&t224 + 28, 0);
                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t224), t223);
                        ((uint8_t *)&t224)[24] = 1;
                        memmove(gm_out, ((uint8_t *)&t224), 32);
                        goto gm_fdone;
                    }
gm_lb21:;
                    {
                        uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l44) + 0));
                        uint8_t t226 = (uint8_t)(t225 == ((uint8_t)11ULL));
                        if (!t226) {
                            goto gm_lb22;
                        }
                        gm_tqu3m2l3n8mnc t227;
                        memset(&t227, 0, sizeof t227);
                        memmove(gm_out, ((uint8_t *)&t227), 32);
                        goto gm_fdone;
                    }
gm_lb22:;
                    {
                        uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&gm_l44) + 0));
                        uint8_t t229 = (uint8_t)(t228 == ((uint8_t)4ULL));
                        if (!t229) {
                            goto gm_lb23;
                        }
                        void * t230 = gm_l0;
                        void * t231 = gm_l2;
                        gm_tl874t2atnlh3 t232 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t231) + 72));
                        gm_b16_8 t233 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                        gm_tnpifv20irnph t234;
                        GM_ST(gm_b16_8, ((uint8_t *)&t234 + 0), t233);
                        gm_f_format_37dbjte((void *)&gm_l45, (void *)NULL, cx, NULL, t234);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t235 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                        if (!t235) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 8, gm_srcmhupam4se3h5, 338, 33);
                        gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                        gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
                        gm_b0_1 t238 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t230, t232, t237);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tqu3m2l3n8mnc t239;
                        memset(&t239, 0, sizeof t239);
                        memmove(gm_out, ((uint8_t *)&t239), 32);
                        goto gm_fdone;
                    }
gm_lb23:;
                    {
                        void * t240 = gm_l0;
                        void * t241 = gm_l2;
                        gm_tl874t2atnlh3 t242 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t241) + 72));
                        gm_b16_8 t243 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                        gm_tnpifv20irnph t244;
                        GM_ST(gm_b16_8, ((uint8_t *)&t244 + 0), t243);
                        gm_f_format_vilpuor((void *)&gm_l46, (void *)NULL, cx, NULL, t244);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t245 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                        if (!t245) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 8, gm_srcmhupam4se3h5, 342, 33);
                        gm_b16_8 t246 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                        gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)&t246));
                        gm_b0_1 t248 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t240, t242, t247);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tqu3m2l3n8mnc t249;
                        memset(&t249, 0, sizeof t249);
                        memmove(gm_out, ((uint8_t *)&t249), 32);
                        goto gm_fdone;
                    }
gm_lb24:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 333, 9);
                }
gm_lb19:;
            }
gm_lb18:;
        }
gm_lb17:;
    }
    {
        {
            {
                void * t250 = gm_l2;
                uint8_t t251 = GM_LD(uint8_t, ((((uint8_t *)t250) + 8) + 0));
                uint8_t t252 = (uint8_t)(t251 == ((uint8_t)22ULL));
                if (!t252) {
                    goto gm_lb26;
                }
                void * t253 = GM_LD(void *, ((((uint8_t *)t250) + 8) + 8));
                gm_l24 = t253;
                memmove(((uint8_t *)&gm_l25), (((((uint8_t *)t250) + 8) + 8) + 8), 32);
                {
                    {
                        void * t254 = gm_l0;
                        uint64_t t255 = gm_l1;
                        void * t256 = gm_l24;
                        gm_tf5srh4mp4b3i t257 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t254, t255, t256);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l47 = t257;
                        uint8_t t258 = (uint8_t)((((uint8_t *)&gm_l47))[8] != 0);
                        if (!t258) {
                            goto gm_lb28;
                        }
                        uint64_t t259 = GM_LD(uint64_t, ((uint8_t *)&gm_l47));
                        gm_l26 = t259;
                        void * t260 = gm_l0;
                        uint64_t t261 = gm_l26;
                        uint64_t t262 = gm_l1;
                        gm_tf5srh4mp4b3i t263 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t260, t261, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l25)), t262);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l48 = t263;
                        uint8_t t264 = (uint8_t)((((uint8_t *)&gm_l48))[8] != 0);
                        if (t264) {
                            uint64_t t265 = GM_LD(uint64_t, ((uint8_t *)&gm_l48));
                            gm_l49 = t265;
                        } else {
                            gm_tqu3m2l3n8mnc t266;
                            memset(&t266, 0, sizeof t266);
                            memmove(gm_out, ((uint8_t *)&t266), 32);
                            goto gm_fdone;
                        }
                        uint64_t t267 = gm_l49;
                        gm_l27 = t267;
                        void * t268 = gm_l0;
                        uint64_t t269 = gm_l27;
                        gm_f_resolve_decl_pus042v((void *)&gm_l50, (void *)NULL, cx, NULL, t268, t269);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        {
                            {
                                uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                                uint8_t t271 = (uint8_t)(t270 == ((uint8_t)2ULL));
                                if (!t271) {
                                    goto gm_lb30;
                                }
                                uint32_t t272 = GM_LD(uint32_t, (((uint8_t *)&gm_l50) + 8));
                                gm_l28 = t272;
                                void * t273 = gm_l0;
                                uint32_t t274 = gm_l28;
                                gm_tiilmfd16v3fc t275;
                                gm_f_bare_effect_ref_a5ofmji((void *)&t275, (void *)NULL, cx, NULL, t273, t274);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tqu3m2l3n8mnc t276;
                                GM_ST(uint8_t, (uint8_t *)&t276 + 25, 0);
                                GM_ST(uint16_t, (uint8_t *)&t276 + 26, 0);
                                GM_ST(uint32_t, (uint8_t *)&t276 + 28, 0);
                                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t276), t275);
                                ((uint8_t *)&t276)[24] = 1;
                                memmove(gm_out, ((uint8_t *)&t276), 32);
                                goto gm_fdone;
                            }
gm_lb30:;
                            {
                                uint8_t t277 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                                uint8_t t278 = (uint8_t)(t277 == ((uint8_t)3ULL));
                                if (!t278) {
                                    goto gm_lb31;
                                }
                                memmove(((uint8_t *)&gm_l29), (((uint8_t *)&gm_l50) + 8), 24);
                                gm_tiilmfd16v3fc t279 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l29));
                                gm_tqu3m2l3n8mnc t280;
                                GM_ST(uint8_t, (uint8_t *)&t280 + 25, 0);
                                GM_ST(uint16_t, (uint8_t *)&t280 + 26, 0);
                                GM_ST(uint32_t, (uint8_t *)&t280 + 28, 0);
                                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t280), t279);
                                ((uint8_t *)&t280)[24] = 1;
                                memmove(gm_out, ((uint8_t *)&t280), 32);
                                goto gm_fdone;
                            }
gm_lb31:;
                            {
                                uint8_t t281 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                                uint8_t t282 = (uint8_t)(t281 == ((uint8_t)11ULL));
                                if (!t282) {
                                    goto gm_lb32;
                                }
                                gm_tqu3m2l3n8mnc t283;
                                memset(&t283, 0, sizeof t283);
                                memmove(gm_out, ((uint8_t *)&t283), 32);
                                goto gm_fdone;
                            }
gm_lb32:;
                            {
                                void * t284 = gm_l0;
                                void * t285 = gm_l2;
                                gm_tl874t2atnlh3 t286 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t285) + 72));
                                gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l25));
                                gm_tnpifv20irnph t288;
                                GM_ST(gm_b16_8, ((uint8_t *)&t288 + 0), t287);
                                gm_f_format_vilpuor((void *)&gm_l51, (void *)NULL, cx, NULL, t288);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t289 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                                if (!t289) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 8, gm_srcmhupam4se3h5, 356, 37);
                                gm_b16_8 t290 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                                gm_b16_8 t291 = GM_LD(gm_b16_8, ((uint8_t *)&t290));
                                gm_b0_1 t292 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t284, t286, t291);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tqu3m2l3n8mnc t293;
                                memset(&t293, 0, sizeof t293);
                                memmove(gm_out, ((uint8_t *)&t293), 32);
                                goto gm_fdone;
                            }
gm_lb33:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 351, 13);
                        }
gm_lb29:;
                    }
gm_lb28:;
                }
gm_lb27:;
                void * t294 = gm_l0;
                uint64_t t295 = gm_l1;
                void * t296 = gm_l24;
                uint8_t t297 = gm_f_names_error_oet8t6b((void *)NULL, cx, NULL, t294, t295, t296);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t297) {
                    gm_tqu3m2l3n8mnc t298;
                    memset(&t298, 0, sizeof t298);
                    memmove(gm_out, ((uint8_t *)&t298), 32);
                    goto gm_fdone;
                }
                goto gm_lb25;
            }
gm_lb26:;
        }
gm_lb25:;
    }
    {
        void * t299 = gm_l0;
        void * t300 = gm_l2;
        gm_tl874t2atnlh3 t301 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t300) + 72));
        gm_b16_8 t302;
        GM_ST(void *, (uint8_t *)&t302, (void *)(gm_sq5p8t05ccn5h));
        GM_ST(uintptr_t, (uint8_t *)&t302 + 8, (uintptr_t)(23));
        gm_b0_1 t303 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t299, t301, t302);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqu3m2l3n8mnc t304;
        memset(&t304, 0, sizeof t304);
        memmove(gm_out, ((uint8_t *)&t304), 32);
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_set_o7rc5od(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_resolve_effect_set_o7rc5od F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* terms */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l6; /* e */
    gm_t88rcucg1b7uk gm_l7; /* p */
    gm_tnpifv20irnph gm_l8; /* set */
    gm_tiilmfd16v3fc gm_l9; /* r */
    GM_STACK_CHECK(&F, 551u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tkn42u7na3t81 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_teseu4tn5o422 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tqu3m2l3n8mnc gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_trqfvmjtt4sij t13;
    gm_f_list_084fcd6((void *)&t13, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t13;
    gm_b16_8 t14 = gm_l2;
    gm_l4 = t14;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t15 = gm_l5;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t20 = gm_l5;
                {
                    {
                        void * t21 = GM_LD(void *, (((uint8_t *)t18) + (uintptr_t)t20 * 8));
                        gm_l6 = t21;
                        {
                            {
                                void * t22 = gm_l0;
                                void * t23 = gm_l6;
                                gm_tkn42u7na3t81 t24 = gm_f_effect_param_named_h2jeoo7(t22, t23);
                                gm_l10 = t24;
                                uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l10))[4] != 0);
                                if (!t25) {
                                    goto gm_lb7;
                                }
                                gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l10));
                                gm_l7 = t26;
                                void * t27 = (void *)((uint8_t *)&F.l3);
                                gm_t88rcucg1b7uk t28 = gm_l7;
                                gm_tiilmfd16v3fc t29;
                                gm_f_splice_tj8iedl((void *)&t29, (void *)NULL, cx, NULL, t28);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t30 = gm_f_push_d1r4h19(t27, &t29);
                                gm_l11 = t30;
                                uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcmhupam4se3h5, 239, 13);
                                goto gm_lb3;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        {
                            {
                                void * t32 = gm_l0;
                                uint64_t t33 = gm_l1;
                                void * t34 = gm_l6;
                                gm_f_named_effect_set_s5gn1lr((void *)&gm_l12, (void *)NULL, cx, NULL, t32, t33, t34);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l12))[16] != 0);
                                if (!t35) {
                                    goto gm_lb9;
                                }
                                gm_tnpifv20irnph t36 = GM_LD(gm_tnpifv20irnph, ((uint8_t *)&gm_l12));
                                gm_l8 = t36;
                                void * t37 = (void *)((uint8_t *)&F.l3);
                                gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
                                gm_tftb1odc739t5 t39 = gm_f_extend_74ac4hb(t37, t38);
                                gm_l13 = t39;
                                uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcmhupam4se3h5, 243, 13);
                                goto gm_lb3;
                            }
gm_lb9:;
                        }
gm_lb8:;
                        {
                            {
                                void * t41 = gm_l0;
                                uint64_t t42 = gm_l1;
                                void * t43 = gm_l6;
                                gm_f_resolve_effect_ref_bggirs2((void *)&gm_l14, (void *)NULL, cx, NULL, t41, t42, t43);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t44 = (uint8_t)((((uint8_t *)&gm_l14))[24] != 0);
                                if (!t44) {
                                    goto gm_lb11;
                                }
                                memmove(((uint8_t *)&gm_l9), ((uint8_t *)&gm_l14), 24);
                                void * t45 = (void *)((uint8_t *)&F.l3);
                                gm_tftb1odc739t5 t46 = gm_f_push_d1r4h19(t45, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l9)));
                                gm_l15 = t46;
                                uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcmhupam4se3h5, 246, 51);
                                goto gm_lb10;
                            }
gm_lb11:;
                        }
gm_lb10:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 237, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t48 = gm_l5;
            uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
            uint64_t t51 = r49;
            gm_l5 = t51;
        }
    }
gm_lb1:;
    gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
    gm_tnpifv20irnph t54 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t53);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t54;
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
__attribute__((aligned(4))) void gm_f_resolve_fn_sig_pamuhve(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, uint8_t a3, gm_b16_8 a4) {
    struct gm_fr_resolve_fn_sig_pamuhve F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* sig */
    uint8_t gm_l3; /* literal */
    gm_b16_8 gm_l4; /* where_ */
    uint8_t gm_l5; /* ok */
    uint8_t gm_l8; /* seen_default */
    gm_t66j1im9ip128 gm_l9; /* mark */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    void * gm_l13; /* ty_expr */
    gm_t88rcucg1b7uk gm_l14; /* ty */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_t08olq97rscn3 gm_l17; /* n */
    uint32_t gm_l18; /* pid */
    void * gm_l19; /* ty_expr */
    uint8_t gm_l20; /* is_effects */
    uint8_t gm_l21; /* is_type */
    gm_t08olq97rscn3 gm_l22; /* i */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_t08olq97rscn3 gm_l25; /* n */
    uint32_t gm_l26; /* pid */
    gm_t88rcucg1b7uk gm_l27; /* ty */
    uint8_t gm_l28; /* rest */
    gm_b16_8 gm_l29; /* names */
    uint8_t gm_l30; /* ok_ty */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    gm_t08olq97rscn3 gm_l33; /* n */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* r */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    gm_t88rcucg1b7uk gm_l41; /* ret */
    void * gm_l42; /* r */
    gm_tnpifv20irnph gm_l43; /* effects */
    gm_b16_8 gm_l44; /* list_ */
    gm_b16_8 gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    GM_STACK_CHECK(&F, 3979u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * gm_l48;
    uint8_t gm_l49;
    gm_t53at80pfdus3 gm_l50;
    gm_tabb710d9mq2e gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tftb1odc739t5 gm_l54;
    void * gm_l55;
    uint8_t gm_l56;
    gm_tnnjeerolos0a gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_tftb1odc739t5 gm_l60;
    uint8_t gm_l61;
    gm_tftb1odc739t5 gm_l62;
    uint8_t gm_l63;
    gm_t53at80pfdus3 gm_l64;
    uint8_t gm_l65;
    uint8_t gm_l66;
    uint8_t gm_l67;
    gm_tftb1odc739t5 gm_l68;
    gm_tgsvmddj3e85e gm_l69;
    uint8_t gm_l70;
    uint8_t gm_l71;
    gm_t53at80pfdus3 gm_l72;
    {
        gm_l5 = ((uint8_t)1);
        gm_trqfvmjtt4sij t68;
        gm_f_list_af74s0q((void *)&t68, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t68;
        gm_trqfvmjtt4sij t69;
        gm_f_list_go2qg0b((void *)&t69, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t69;
        gm_l8 = ((uint8_t)0);
        void * t70 = gm_l0;
        void * t71 = gm_l0;
        uint32_t t72 = GM_LD(uint32_t, (((uint8_t *)t71) + 1888));
        uint32_t r73; bool o74 = __builtin_add_overflow(t72, ((uint32_t)1ULL), &r73);
        if (o74) gm_trap("integer overflow in `+`", gm_srcmhupam4se3h5, 31, 5);
        uint32_t t75 = r73;
        GM_ST(uint32_t, (((uint8_t *)t70) + 1888), t75);
        void * t76 = gm_l0;
        gm_f_scope_mark_i3b2mvo((void *)&gm_l9, t76);
        void * t77 = gm_l2;
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)t77));
        gm_l10 = t78;
        gm_l11 = ((uint64_t)0ULL);
        {
            for (;;) {
                {
                    uint64_t t79 = gm_l11;
                    uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint8_t t81 = (t79 < t80);
                    if (!t81) {
                        goto gm_lb1;
                    }
                    {
                        {
                            void * t82 = GM_LD(void *, ((uint8_t *)&gm_l10));
                            uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint64_t t84 = gm_l11;
                            {
                                {
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l12), (((uint8_t *)t82) + (uintptr_t)t84 * 56), 56);
                                            uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 16));
                                            if (t85) {
                                                uint8_t t86 = gm_l3;
                                                uint8_t t87 = (uint8_t)!t86;
                                                if (t87) {
                                                    void * t88 = gm_l0;
                                                    gm_tl874t2atnlh3 t89 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                                    gm_b16_8 t90;
                                                    GM_ST(void *, (uint8_t *)&t90, (void *)(gm_sshv7blujc475));
                                                    GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(51));
                                                    gm_b0_1 t91 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t88, t89, t90);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l5 = ((uint8_t)0);
                                                    goto gm_lb3;
                                                }
                                                uint8_t t92 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l12) + 24)) != 0);
                                                if (t92) {
                                                    void * t93 = GM_LD(void *, (((uint8_t *)&F.l12) + 24));
                                                    gm_l48 = t93;
                                                } else {
                                                    void * t94 = gm_l0;
                                                    gm_tl874t2atnlh3 t95 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                                    gm_b16_8 t96;
                                                    GM_ST(void *, (uint8_t *)&t96, (void *)(gm_snnov1h58rig5));
                                                    GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(51));
                                                    gm_b0_1 t97 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t94, t95, t96);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l5 = ((uint8_t)0);
                                                    goto gm_lb3;
                                                }
                                                void * t98 = gm_l48;
                                                gm_l13 = t98;
                                                void * t99 = gm_l0;
                                                uint64_t t100 = gm_l1;
                                                void * t101 = gm_l13;
                                                gm_t88rcucg1b7uk t102 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t99, t100, t101);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l14 = t102;
                                                void * t103 = gm_l0;
                                                void * t104 = (void *)(((uint8_t *)t103) + 8);
                                                gm_t88rcucg1b7uk t105 = gm_l14;
                                                gm_f_kind_jcnt4hm((void *)&gm_l50, t104, GM_CONV(gm_csguankko4lmhd, t105));
                                                {
                                                    {
                                                        uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                                                        uint8_t t107 = (uint8_t)(t106 == ((uint8_t)15ULL));
                                                        if (!t107) {
                                                            goto gm_lb7;
                                                        }
                                                        gm_l49 = ((uint8_t)1);
                                                        goto gm_lb6;
                                                    }
gm_lb7:;
                                                    {
                                                        uint8_t t108 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                                                        uint8_t t109 = (uint8_t)(t108 == ((uint8_t)25ULL));
                                                        if (!t109) {
                                                            goto gm_lb8;
                                                        }
                                                        gm_l49 = ((uint8_t)1);
                                                        goto gm_lb6;
                                                    }
gm_lb8:;
                                                    {
                                                        uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                                                        uint8_t t111 = (uint8_t)(t110 == ((uint8_t)6ULL));
                                                        if (!t111) {
                                                            goto gm_lb9;
                                                        }
                                                        gm_l49 = ((uint8_t)1);
                                                        goto gm_lb6;
                                                    }
gm_lb9:;
                                                    {
                                                        gm_l49 = ((uint8_t)0);
                                                        goto gm_lb6;
                                                    }
gm_lb10:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 47, 16);
                                                }
gm_lb6:;
                                                uint8_t t112 = gm_l49;
                                                if (t112) {
                                                    void * t113 = gm_l0;
                                                    void * t114 = gm_l13;
                                                    gm_tl874t2atnlh3 t115 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t114) + 72));
                                                    void * t116 = gm_l0;
                                                    gm_t88rcucg1b7uk t117 = gm_l14;
                                                    gm_b16_8 t118 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t116, t117);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tnpifv20irnph t119;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t119 + 0), t118);
                                                    gm_f_format_askmr98((void *)&gm_l51, (void *)NULL, cx, NULL, t119);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                                                    if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 8, gm_srcmhupam4se3h5, 48, 39);
                                                    gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                                                    gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
                                                    gm_b0_1 t123 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t113, t115, t122);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l5 = ((uint8_t)0);
                                                    goto gm_lb3;
                                                }
                                                gm_b16_8 t124 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                                gm_l15 = t124;
                                                gm_l16 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t125 = gm_l16;
                                                        uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                        uint8_t t127 = (t125 < t126);
                                                        if (!t127) {
                                                            goto gm_lb11;
                                                        }
                                                        {
                                                            void * t128 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                                            uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                                            uint64_t t130 = gm_l16;
                                                            {
                                                                {
                                                                    memmove(((uint8_t *)&gm_l17), (((uint8_t *)t128) + (uintptr_t)t130 * 32), 32);
                                                                    void * t131 = gm_l0;
                                                                    gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
                                                                    gm_t88rcucg1b7uk t133 = gm_l14;
                                                                    gm_tnnjeerolos0a t134;
                                                                    GM_ST(uint8_t, (uint8_t *)&t134 + 1, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t134 + 2, 0);
                                                                    GM_ST(uint8_t, ((uint8_t *)&t134 + 0), ((uint8_t)2ULL));
                                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t134 + 4), t133);
                                                                    uint32_t t135 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t131, t132, t134);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l18 = t135;
                                                                    void * t136 = (void *)((uint8_t *)&F.l7);
                                                                    uint32_t t137 = gm_l18;
                                                                    gm_tftb1odc739t5 t138 = gm_f_push_im9d4ro(t136, t137);
                                                                    gm_l52 = t138;
                                                                    uint8_t t139 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                                                                    if (!t139) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_srcmhupam4se3h5, 54, 17);
                                                                    void * t140 = gm_l0;
                                                                    void * t141 = (void *)(((uint8_t *)t140) + 1648);
                                                                    gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
                                                                    uint32_t t143 = gm_l18;
                                                                    gm_tommf4j9upomc t144;
                                                                    GM_ST(uint32_t, (uint8_t *)&t144 + 20, 0);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t144 + 0), t142);
                                                                    GM_ST(uint32_t, ((uint8_t *)&t144 + 16), t143);
                                                                    gm_tftb1odc739t5 t145 = gm_f_push_sp4si0f(t141, &t144);
                                                                    gm_l53 = t145;
                                                                    uint8_t t146 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                                                                    if (!t146) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_srcmhupam4se3h5, 55, 17);
                                                                    void * t147 = (void *)((uint8_t *)&F.l6);
                                                                    gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
                                                                    gm_t88rcucg1b7uk t149 = gm_l14;
                                                                    gm_tl874t2atnlh3 t150 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l17) + 16));
                                                                    void * t151 = GM_LD(void *, (((uint8_t *)&F.l12) + 32));
                                                                    uint32_t t152 = gm_l18;
                                                                    gm_t9805qtonbsag t153;
                                                                    GM_ST(uint8_t, (uint8_t *)&t153 + 5, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t153 + 6, 0);
                                                                    GM_ST(uint32_t, ((uint8_t *)&t153), t152);
                                                                    ((uint8_t *)&t153)[4] = 1;
                                                                    gm_t5v0c8ia6a5mo t154;
                                                                    GM_ST(uint8_t, (uint8_t *)&t154 + 49, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t154 + 50, 0);
                                                                    GM_ST(uint32_t, (uint8_t *)&t154 + 52, 0);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t154 + 0), t148);
                                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t154 + 16), t149);
                                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t154 + 20), t150);
                                                                    GM_ST(void *, ((uint8_t *)&t154 + 32), t151);
                                                                    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t154 + 40), t153);
                                                                    GM_ST(uint8_t, ((uint8_t *)&t154 + 48), ((uint8_t)0));
                                                                    gm_tftb1odc739t5 t155 = gm_f_push_o8u01sh(t147, &t154);
                                                                    gm_l54 = t155;
                                                                    uint8_t t156 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                                                                    if (!t156) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_srcmhupam4se3h5, 56, 17);
                                                                    goto gm_lb14;
                                                                }
gm_lb15:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 52, 13);
                                                            }
gm_lb14:;
                                                        }
gm_lb13:;
                                                        uint64_t t157 = gm_l16;
                                                        uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
                                                        uint64_t t160 = r158;
                                                        gm_l16 = t160;
                                                    }
                                                }
gm_lb11:;
                                                goto gm_lb3;
                                            }
                                        }
                                        {
                                            uint8_t t161 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l12) + 24)) != 0);
                                            if (t161) {
                                                void * t162 = GM_LD(void *, (((uint8_t *)&F.l12) + 24));
                                                gm_l55 = t162;
                                            } else {
                                                void * t163 = gm_l0;
                                                gm_tl874t2atnlh3 t164 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                                gm_b16_8 t165;
                                                GM_ST(void *, (uint8_t *)&t165, (void *)(gm_spfda095eoqse));
                                                GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(33));
                                                gm_b0_1 t166 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t163, t164, t165);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l5 = ((uint8_t)0);
                                                goto gm_lb3;
                                            }
                                            void * t167 = gm_l55;
                                            gm_l19 = t167;
                                            void * t168 = gm_l19;
                                            uint8_t t169 = gm_f_is_effects_kind_uutbf9u(t168);
                                            gm_l20 = t169;
                                            gm_l21 = ((uint8_t)0);
                                            {
                                                {
                                                    void * t170 = gm_l19;
                                                    uint8_t t171 = GM_LD(uint8_t, ((((uint8_t *)t170) + 8) + 0));
                                                    uint8_t t172 = (uint8_t)(t171 == ((uint8_t)6ULL));
                                                    if (!t172) {
                                                        goto gm_lb17;
                                                    }
                                                    memmove(((uint8_t *)&gm_l22), ((((uint8_t *)t170) + 8) + 8), 32);
                                                    gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
                                                    gm_b16_8 t174;
                                                    GM_ST(void *, (uint8_t *)&t174, (void *)(gm_s2737i1iq57nf));
                                                    GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(4));
                                                    uint8_t t175 = (gm_eq0iqiubfm7e(((uint8_t *)&t173), ((uint8_t *)&t174)));
                                                    gm_l21 = t175;
                                                    goto gm_lb16;
                                                }
gm_lb17:;
                                            }
gm_lb16:;
                                            uint8_t t176 = gm_l20;
                                            if (t176) {
                                                gm_l56 = ((uint8_t)1);
                                            } else {
                                                uint8_t t177 = gm_l21;
                                                gm_l56 = t177;
                                            }
                                            uint8_t t178 = gm_l56;
                                            if (t178) {
                                                uint8_t t179 = gm_l3;
                                                uint8_t t180 = (uint8_t)!t179;
                                                if (t180) {
                                                    void * t181 = gm_l0;
                                                    void * t182 = gm_l19;
                                                    gm_tl874t2atnlh3 t183 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t182) + 72));
                                                    gm_b16_8 t184;
                                                    GM_ST(void *, (uint8_t *)&t184, (void *)(gm_sa7gjgqof9s56));
                                                    GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(43));
                                                    gm_b0_1 t185 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t181, t183, t184);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l5 = ((uint8_t)0);
                                                    goto gm_lb3;
                                                }
                                                gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                                gm_l23 = t186;
                                                gm_l24 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t187 = gm_l24;
                                                        uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                        uint8_t t189 = (t187 < t188);
                                                        if (!t189) {
                                                            goto gm_lb18;
                                                        }
                                                        {
                                                            void * t190 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                                            uint64_t t191 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                            uint64_t t192 = gm_l24;
                                                            {
                                                                {
                                                                    memmove(((uint8_t *)&gm_l25), (((uint8_t *)t190) + (uintptr_t)t192 * 32), 32);
                                                                    void * t193 = gm_l0;
                                                                    gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l25));
                                                                    uint8_t t195 = gm_l20;
                                                                    if (t195) {
                                                                        gm_tnnjeerolos0a t196;
                                                                        GM_ST(uint8_t, (uint8_t *)&t196 + 1, 0);
                                                                        GM_ST(uint16_t, (uint8_t *)&t196 + 2, 0);
                                                                        GM_ST(uint32_t, (uint8_t *)&t196 + 4, 0);
                                                                        GM_ST(uint8_t, ((uint8_t *)&t196 + 0), ((uint8_t)1ULL));
                                                                        gm_l57 = t196;
                                                                    } else {
                                                                        gm_tnnjeerolos0a t197;
                                                                        GM_ST(uint8_t, (uint8_t *)&t197 + 1, 0);
                                                                        GM_ST(uint16_t, (uint8_t *)&t197 + 2, 0);
                                                                        GM_ST(uint32_t, (uint8_t *)&t197 + 4, 0);
                                                                        GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)0ULL));
                                                                        gm_l57 = t197;
                                                                    }
                                                                    gm_tnnjeerolos0a t198 = gm_l57;
                                                                    uint32_t t199 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t193, t194, t198);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l26 = t199;
                                                                    void * t200 = (void *)((uint8_t *)&F.l7);
                                                                    uint32_t t201 = gm_l26;
                                                                    gm_tftb1odc739t5 t202 = gm_f_push_im9d4ro(t200, t201);
                                                                    gm_l58 = t202;
                                                                    uint8_t t203 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                                                    if (!t203) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_srcmhupam4se3h5, 78, 17);
                                                                    void * t204 = gm_l0;
                                                                    void * t205 = (void *)(((uint8_t *)t204) + 1608);
                                                                    gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l25));
                                                                    void * t207 = gm_l0;
                                                                    void * t208 = (void *)(((uint8_t *)t207) + 8);
                                                                    uint32_t t209 = gm_l26;
                                                                    gm_t88rcucg1b7uk t210 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t208, t209);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_tep2drsl9408b t211;
                                                                    GM_ST(uint32_t, (uint8_t *)&t211 + 20, 0);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t211 + 0), t206);
                                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t211 + 16), t210);
                                                                    gm_tftb1odc739t5 t212 = gm_f_push_hv483nm(t205, &t211);
                                                                    gm_l59 = t212;
                                                                    uint8_t t213 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                                                    if (!t213) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srcmhupam4se3h5, 79, 17);
                                                                    void * t214 = (void *)((uint8_t *)&F.l6);
                                                                    gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l25));
                                                                    void * t216 = gm_l0;
                                                                    void * t217 = (void *)(((uint8_t *)t216) + 8);
                                                                    gm_t88rcucg1b7uk t218 = gm_f_type_ty_9ph2gbr((void *)NULL, cx, NULL, t217);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_tl874t2atnlh3 t219 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l25) + 16));
                                                                    uint32_t t220 = gm_l26;
                                                                    gm_t9805qtonbsag t221;
                                                                    GM_ST(uint8_t, (uint8_t *)&t221 + 5, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t221 + 6, 0);
                                                                    GM_ST(uint32_t, ((uint8_t *)&t221), t220);
                                                                    ((uint8_t *)&t221)[4] = 1;
                                                                    gm_t5v0c8ia6a5mo t222;
                                                                    GM_ST(uint8_t, (uint8_t *)&t222 + 49, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t222 + 50, 0);
                                                                    GM_ST(uint32_t, (uint8_t *)&t222 + 52, 0);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t222 + 0), t215);
                                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t222 + 16), t218);
                                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t222 + 20), t219);
                                                                    GM_ST(void *, ((uint8_t *)&t222 + 32), ((void *)0));
                                                                    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t222 + 40), t221);
                                                                    GM_ST(uint8_t, ((uint8_t *)&t222 + 48), ((uint8_t)0));
                                                                    gm_tftb1odc739t5 t223 = gm_f_push_o8u01sh(t214, &t222);
                                                                    gm_l60 = t223;
                                                                    uint8_t t224 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                                                    if (!t224) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_srcmhupam4se3h5, 80, 17);
                                                                    goto gm_lb21;
                                                                }
gm_lb22:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 76, 13);
                                                            }
gm_lb21:;
                                                        }
gm_lb20:;
                                                        uint64_t t225 = gm_l24;
                                                        uint64_t r226; bool o227 = __builtin_add_overflow(t225, ((uint64_t)1ULL), &r226);
                                                        uint64_t t228 = r226;
                                                        gm_l24 = t228;
                                                    }
                                                }
gm_lb18:;
                                                goto gm_lb3;
                                            }
                                        }
                                        {
                                            void * t229 = gm_l0;
                                            uint64_t t230 = gm_l1;
                                            void * t231 = gm_l19;
                                            gm_t88rcucg1b7uk t232 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t229, t230, t231);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l27 = t232;
                                            {
                                                {
                                                    uint8_t t233 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l12) + 32)) != 0);
                                                    if (t233) {
                                                        goto gm_lb24;
                                                    }
                                                    gm_l61 = ((uint8_t)0);
                                                    goto gm_lb23;
                                                }
gm_lb24:;
                                                {
                                                    gm_l61 = ((uint8_t)1);
                                                    goto gm_lb23;
                                                }
gm_lb25:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 85, 12);
                                            }
gm_lb23:;
                                            uint8_t t234 = gm_l61;
                                            if (t234) {
                                                gm_l8 = ((uint8_t)1);
                                            } else {
                                                {
                                                    {
                                                        uint8_t t235 = gm_l8;
                                                        if (!t235) {
                                                            goto gm_lb27;
                                                        }
                                                        uint8_t t236 = gm_l3;
                                                        if (!t236) {
                                                            goto gm_lb27;
                                                        }
                                                        void * t237 = gm_l0;
                                                        gm_tl874t2atnlh3 t238 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                                        gm_b16_8 t239;
                                                        GM_ST(void *, (uint8_t *)&t239, (void *)(gm_s0sivi9n1gpid));
                                                        GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(73));
                                                        gm_b0_1 t240 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t237, t238, t239);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        goto gm_lb26;
                                                    }
gm_lb27:;
                                                }
gm_lb26:;
                                            }
                                            uint64_t t241 = GM_LD(uint64_t, (((uint8_t *)&F.l12) + 8));
                                            uint8_t t242 = ((t241 == ((uint64_t)0ULL)));
                                            if (t242) {
                                                void * t243 = (void *)((uint8_t *)&F.l6);
                                                gm_b16_8 t244;
                                                GM_ST(void *, (uint8_t *)&t244, (void *)(gm_sfdbfrv46lihd));
                                                GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)(1));
                                                gm_t88rcucg1b7uk t245 = gm_l27;
                                                gm_tl874t2atnlh3 t246 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                                gm_t9805qtonbsag t247;
                                                memset(&t247, 0, sizeof t247);
                                                gm_t5v0c8ia6a5mo t248;
                                                GM_ST(uint8_t, (uint8_t *)&t248 + 49, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t248 + 50, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t248 + 52, 0);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t248 + 0), t244);
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t248 + 16), t245);
                                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t248 + 20), t246);
                                                GM_ST(void *, ((uint8_t *)&t248 + 32), ((void *)0));
                                                GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t248 + 40), t247);
                                                GM_ST(uint8_t, ((uint8_t *)&t248 + 48), ((uint8_t)0));
                                                gm_tftb1odc739t5 t249 = gm_f_push_o8u01sh(t243, &t248);
                                                gm_l62 = t249;
                                                uint8_t t250 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                                if (!t250) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_srcmhupam4se3h5, 91, 13);
                                            }
                                            gm_l28 = ((uint8_t)0);
                                            uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 17));
                                            if (t251) {
                                                void * t252 = gm_l0;
                                                void * t253 = (void *)(((uint8_t *)t252) + 8);
                                                gm_t88rcucg1b7uk t254 = gm_l27;
                                                gm_f_kind_jcnt4hm((void *)&gm_l64, t253, GM_CONV(gm_csguankko4lmhd, t254));
                                                {
                                                    {
                                                        uint8_t t255 = GM_LD(uint8_t, (((uint8_t *)&gm_l64) + 0));
                                                        uint8_t t256 = (uint8_t)(t255 == ((uint8_t)12ULL));
                                                        if (!t256) {
                                                            goto gm_lb29;
                                                        }
                                                        gm_b16_8 t257 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l64) + 8) + 16));
                                                        gm_l29 = t257;
                                                        uint64_t t258 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                                        uint8_t t259 = ((t258 == ((uint64_t)0ULL)));
                                                        gm_l63 = t259;
                                                        goto gm_lb28;
                                                    }
gm_lb29:;
                                                    {
                                                        {
                                                            {
                                                                uint8_t t260 = GM_LD(uint8_t, (((uint8_t *)&gm_l64) + 0));
                                                                uint8_t t261 = (uint8_t)(t260 == ((uint8_t)19ULL));
                                                                if (!t261) {
                                                                    goto gm_lb32;
                                                                }
                                                                goto gm_lb31;
                                                            }
gm_lb32:;
                                                            {
                                                                uint8_t t262 = GM_LD(uint8_t, (((uint8_t *)&gm_l64) + 0));
                                                                uint8_t t263 = (uint8_t)(t262 == ((uint8_t)26ULL));
                                                                if (!t263) {
                                                                    goto gm_lb33;
                                                                }
                                                                goto gm_lb31;
                                                            }
gm_lb33:;
                                                            goto gm_lb30;
                                                        }
gm_lb31:;
                                                        gm_l63 = ((uint8_t)1);
                                                        goto gm_lb28;
                                                    }
gm_lb30:;
                                                    {
                                                        gm_l63 = ((uint8_t)0);
                                                        goto gm_lb28;
                                                    }
gm_lb34:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 96, 22);
                                                }
gm_lb28:;
                                                uint8_t t264 = gm_l63;
                                                gm_l30 = t264;
                                                uint8_t t265 = gm_l3;
                                                uint8_t t266 = (uint8_t)!t265;
                                                if (t266) {
                                                    void * t267 = gm_l0;
                                                    gm_tl874t2atnlh3 t268 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                                    gm_b16_8 t269;
                                                    GM_ST(void *, (uint8_t *)&t269, (void *)(gm_sfvsgjcg86bbl));
                                                    GM_ST(uintptr_t, (uint8_t *)&t269 + 8, (uintptr_t)(44));
                                                    gm_b0_1 t270 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t267, t268, t269);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l5 = ((uint8_t)0);
                                                } else {
                                                    uint64_t t271 = GM_LD(uint64_t, (((uint8_t *)&F.l12) + 8));
                                                    uint8_t t272 = (!(t271 == ((uint64_t)1ULL)));
                                                    if (t272) {
                                                        gm_l66 = ((uint8_t)1);
                                                    } else {
                                                        {
                                                            {
                                                                uint8_t t273 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l12) + 32)) != 0);
                                                                if (t273) {
                                                                    goto gm_lb36;
                                                                }
                                                                gm_l67 = ((uint8_t)0);
                                                                goto gm_lb35;
                                                            }
gm_lb36:;
                                                            {
                                                                gm_l67 = ((uint8_t)1);
                                                                goto gm_lb35;
                                                            }
gm_lb37:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 104, 47);
                                                        }
gm_lb35:;
                                                        uint8_t t274 = gm_l67;
                                                        gm_l66 = t274;
                                                    }
                                                    uint8_t t275 = gm_l66;
                                                    if (t275) {
                                                        gm_l65 = ((uint8_t)1);
                                                    } else {
                                                        uint8_t t276 = gm_l30;
                                                        uint8_t t277 = (uint8_t)!t276;
                                                        gm_l65 = t277;
                                                    }
                                                    uint8_t t278 = gm_l65;
                                                    if (t278) {
                                                        void * t279 = gm_l0;
                                                        gm_tl874t2atnlh3 t280 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 40));
                                                        gm_b16_8 t281;
                                                        GM_ST(void *, (uint8_t *)&t281, (void *)(gm_s0ad3307s8jmo));
                                                        GM_ST(uintptr_t, (uint8_t *)&t281 + 8, (uintptr_t)(104));
                                                        gm_b0_1 t282 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t279, t280, t281);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l5 = ((uint8_t)0);
                                                    } else {
                                                        gm_l28 = ((uint8_t)1);
                                                    }
                                                }
                                            }
                                        }
                                        {
                                            gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                            gm_l31 = t283;
                                            gm_l32 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t284 = gm_l32;
                                                    uint64_t t285 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                                    uint8_t t286 = (t284 < t285);
                                                    if (!t286) {
                                                        goto gm_lb38;
                                                    }
                                                    {
                                                        void * t287 = GM_LD(void *, ((uint8_t *)&gm_l31));
                                                        uint64_t t288 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                                        uint64_t t289 = gm_l32;
                                                        {
                                                            {
                                                                memmove(((uint8_t *)&gm_l33), (((uint8_t *)t287) + (uintptr_t)t289 * 32), 32);
                                                                void * t290 = (void *)((uint8_t *)&F.l6);
                                                                gm_b16_8 t291 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l33));
                                                                gm_t88rcucg1b7uk t292 = gm_l27;
                                                                gm_tl874t2atnlh3 t293 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l33) + 16));
                                                                void * t294 = GM_LD(void *, (((uint8_t *)&F.l12) + 32));
                                                                gm_t9805qtonbsag t295;
                                                                memset(&t295, 0, sizeof t295);
                                                                uint8_t t296 = gm_l28;
                                                                gm_t5v0c8ia6a5mo t297;
                                                                GM_ST(uint8_t, (uint8_t *)&t297 + 49, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&t297 + 50, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&t297 + 52, 0);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t297 + 0), t291);
                                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t297 + 16), t292);
                                                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t297 + 20), t293);
                                                                GM_ST(void *, ((uint8_t *)&t297 + 32), t294);
                                                                GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t297 + 40), t295);
                                                                GM_ST(uint8_t, ((uint8_t *)&t297 + 48), t296);
                                                                gm_tftb1odc739t5 t298 = gm_f_push_o8u01sh(t290, &t297);
                                                                gm_l68 = t298;
                                                                uint8_t t299 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                                                                if (!t299) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_srcmhupam4se3h5, 112, 13);
                                                                goto gm_lb41;
                                                            }
gm_lb42:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 111, 9);
                                                        }
gm_lb41:;
                                                    }
gm_lb40:;
                                                    uint64_t t300 = gm_l32;
                                                    uint64_t r301; bool o302 = __builtin_add_overflow(t300, ((uint64_t)1ULL), &r301);
                                                    uint64_t t303 = r301;
                                                    gm_l32 = t303;
                                                }
                                            }
gm_lb38:;
                                            goto gm_lb4;
                                        }
                                    }
gm_lb5:;
                                }
                                {
                                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 33, 5);
                                }
                            }
gm_lb4:;
                        }
                    }
gm_lb3:;
                }
                {
                    uint64_t t304 = gm_l11;
                    uint64_t r305; bool o306 = __builtin_add_overflow(t304, ((uint64_t)1ULL), &r305);
                    uint64_t t307 = r305;
                    gm_l11 = t307;
                }
            }
        }
gm_lb1:;
    }
    {
        gm_b16_8 t308 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
        gm_l34 = t309;
        gm_l35 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t310 = gm_l35;
                uint64_t t311 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                uint8_t t312 = (t310 < t311);
                if (!t312) {
                    goto gm_lb43;
                }
                {
                    uint64_t t313 = gm_l35;
                    void * t314 = GM_LD(void *, ((uint8_t *)&gm_l34));
                    uint64_t t315 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                    uint64_t t316 = gm_l35;
                    gm_t5v0c8ia6a5mo t317 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t314) + (uintptr_t)t316 * 56));
                    gm_tgsvmddj3e85e t318;
                    GM_ST(uint64_t, ((uint8_t *)&t318 + 0), t313);
                    GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t318 + 8), t317);
                    gm_l69 = t318;
                    {
                        {
                            uint64_t t319 = GM_LD(uint64_t, ((uint8_t *)&gm_l69));
                            gm_l36 = t319;
                            memmove(((uint8_t *)&F.l37), (((uint8_t *)&gm_l69) + 8), 56);
                            uint8_t t320 = GM_LD(uint8_t, (((uint8_t *)&F.l37) + 48));
                            uint8_t t321 = (uint8_t)!t320;
                            if (t321) {
                                goto gm_lb45;
                            }
                            void * t322 = GM_LD(void *, ((uint8_t *)&F.l6));
                            uint64_t t323 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                            uint64_t t324 = gm_l36;
                            uint64_t r325; bool o326 = __builtin_add_overflow(t324, ((uint64_t)1ULL), &r325);
                            if (o326) gm_trap("integer overflow in `+`", gm_srcmhupam4se3h5, 118, 31);
                            uint64_t t327 = r325;
                            uint64_t t328 = (uintptr_t)t327;
                            uint64_t t329 = (uintptr_t)t323;
                            if (t328 > t329 || t329 > (uintptr_t)t323) gm_trap_slice(t328, t329, t323, gm_srcmhupam4se3h5, 118, 18);
                            uint64_t t330 = ((uintptr_t)t323) - (uintptr_t)t327;
                            gm_b16_8 t331;
                            GM_ST(void *, (uint8_t *)&t331, (void *)(((uint8_t *)t322 + (uintptr_t)t327 * 56)));
                            GM_ST(uintptr_t, (uint8_t *)&t331 + 8, (uintptr_t)(t330));
                            gm_l38 = t331;
                            gm_l39 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t332 = gm_l39;
                                    uint64_t t333 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                    uint8_t t334 = (t332 < t333);
                                    if (!t334) {
                                        goto gm_lb48;
                                    }
                                    {
                                        void * t335 = GM_LD(void *, ((uint8_t *)&gm_l38));
                                        uint64_t t336 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                                        uint64_t t337 = gm_l39;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l40), (((uint8_t *)t335) + (uintptr_t)t337 * 56), 56);
                                                uint8_t t338 = GM_LD(uint8_t, (((uint8_t *)&F.l40) + 48));
                                                if (t338) {
                                                    gm_l70 = ((uint8_t)1);
                                                } else {
                                                    void * t339 = (void *)((uint8_t *)&F.l40);
                                                    uint8_t t340 = gm_f_is_site_8sr7vam(t339);
                                                    uint8_t t341 = (uint8_t)!t340;
                                                    gm_l70 = t341;
                                                }
                                                uint8_t t342 = gm_l70;
                                                if (t342) {
                                                    void * t343 = gm_l0;
                                                    gm_tl874t2atnlh3 t344 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l37) + 20));
                                                    gm_b16_8 t345;
                                                    GM_ST(void *, (uint8_t *)&t345, (void *)(gm_svurqcoqldl32));
                                                    GM_ST(uintptr_t, (uint8_t *)&t345 + 8, (uintptr_t)(82));
                                                    gm_b0_1 t346 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t343, t344, t345);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l5 = ((uint8_t)0);
                                                    goto gm_lb48;
                                                }
                                                goto gm_lb51;
                                            }
gm_lb52:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 118, 9);
                                        }
gm_lb51:;
                                    }
gm_lb50:;
                                    uint64_t t347 = gm_l39;
                                    uint64_t r348; bool o349 = __builtin_add_overflow(t347, ((uint64_t)1ULL), &r348);
                                    uint64_t t350 = r348;
                                    gm_l39 = t350;
                                }
                            }
gm_lb48:;
                            goto gm_lb43;
                        }
gm_lb47:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 116, 5);
                    }
gm_lb46:;
                }
gm_lb45:;
                uint64_t t351 = gm_l35;
                uint64_t r352; bool o353 = __builtin_add_overflow(t351, ((uint64_t)1ULL), &r352);
                uint64_t t354 = r352;
                gm_l35 = t354;
            }
        }
gm_lb43:;
    }
    {
        {
            {
                void * t355 = gm_l2;
                uint8_t t356 = GM_LD(uint8_t, (((uint8_t *)t355) + 16));
                if (!t356) {
                    goto gm_lb54;
                }
                void * t357 = gm_l0;
                uint8_t t358 = GM_LD(uint8_t, (((uint8_t *)t357) + 1480));
                uint8_t t359 = (uint8_t)!t358;
                if (!t359) {
                    goto gm_lb54;
                }
                void * t360 = gm_l0;
                void * t361 = gm_l2;
                gm_tl874t2atnlh3 t362 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t361) + 56));
                gm_b16_8 t363;
                GM_ST(void *, (uint8_t *)&t363, (void *)(gm_sba4cjt08ps1i));
                GM_ST(uintptr_t, (uint8_t *)&t363 + 8, (uintptr_t)(57));
                gm_b0_1 t364 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t360, t362, t363);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l5 = ((uint8_t)0);
                goto gm_lb53;
            }
gm_lb54:;
        }
gm_lb53:;
        void * t365 = gm_l0;
        void * t366 = (void *)(((uint8_t *)t365) + 8);
        gm_t88rcucg1b7uk t367 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t366);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l41 = t367;
        {
            {
                void * t368 = gm_l2;
                uint8_t t369 = (uint8_t)(GM_LD(void *, (((uint8_t *)t368) + 24)) != 0);
                if (!t369) {
                    goto gm_lb56;
                }
                void * t370 = GM_LD(void *, (((uint8_t *)t368) + 24));
                gm_l42 = t370;
                void * t371 = gm_l0;
                uint64_t t372 = gm_l1;
                void * t373 = gm_l42;
                gm_t88rcucg1b7uk t374 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t371, t372, t373);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l41 = t374;
                void * t375 = gm_l0;
                void * t376 = (void *)(((uint8_t *)t375) + 8);
                gm_t88rcucg1b7uk t377 = gm_l41;
                gm_f_kind_jcnt4hm((void *)&gm_l72, t376, GM_CONV(gm_csguankko4lmhd, t377));
                {
                    {
                        uint8_t t378 = GM_LD(uint8_t, (((uint8_t *)&gm_l72) + 0));
                        uint8_t t379 = (uint8_t)(t378 == ((uint8_t)15ULL));
                        if (!t379) {
                            goto gm_lb58;
                        }
                        gm_l71 = ((uint8_t)1);
                        goto gm_lb57;
                    }
gm_lb58:;
                    {
                        gm_l71 = ((uint8_t)0);
                        goto gm_lb57;
                    }
gm_lb59:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 134, 12);
                }
gm_lb57:;
                uint8_t t380 = gm_l71;
                if (t380) {
                    void * t381 = gm_l0;
                    void * t382 = gm_l42;
                    gm_tl874t2atnlh3 t383 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t382) + 72));
                    gm_b16_8 t384;
                    GM_ST(void *, (uint8_t *)&t384, (void *)(gm_sdsd71bjtv81c));
                    GM_ST(uintptr_t, (uint8_t *)&t384 + 8, (uintptr_t)(97));
                    gm_b0_1 t385 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t381, t383, t384);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto gm_lb55;
            }
gm_lb56:;
        }
gm_lb55:;
        gm_tnpifv20irnph t386 = gm_f_empty_effects_r0iv3pv();
        gm_l43 = t386;
        {
            {
                void * t387 = gm_l2;
                uint8_t t388 = (uint8_t)(((((uint8_t *)t387) + 32))[16] != 0);
                if (!t388) {
                    goto gm_lb61;
                }
                gm_b16_8 t389 = GM_LD(gm_b16_8, (((uint8_t *)t387) + 32));
                gm_l44 = t389;
                void * t390 = gm_l0;
                uint64_t t391 = gm_l1;
                gm_b16_8 t392 = gm_l44;
                gm_b16_8 t393 = GM_LD(gm_b16_8, ((uint8_t *)&t392));
                gm_tnpifv20irnph t394 = gm_f_resolve_effect_list_uhjlh6o((void *)NULL, cx, NULL, t390, t391, t393);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l43 = t394;
                goto gm_lb60;
            }
gm_lb61:;
        }
gm_lb60:;
    }
    {
        gm_b16_8 t395 = gm_l4;
        gm_l45 = t395;
        gm_l46 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t396 = gm_l46;
                uint64_t t397 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                uint8_t t398 = (t396 < t397);
                if (!t398) {
                    goto gm_lb62;
                }
                {
                    void * t399 = GM_LD(void *, ((uint8_t *)&gm_l45));
                    uint64_t t400 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                    uint64_t t401 = gm_l46;
                    {
                        {
                            memmove(((uint8_t *)&F.l47), (((uint8_t *)t399) + (uintptr_t)t401 * 56), 56);
                            void * t402 = gm_l0;
                            uint64_t t403 = gm_l1;
                            void * t404 = (void *)((uint8_t *)&F.l47);
                            gm_b0_1 t405 = gm_f_declare_constraint_s1juggf((void *)NULL, cx, NULL, t402, t403, t404);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb65;
                        }
gm_lb66:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmhupam4se3h5, 140, 5);
                    }
gm_lb65:;
                }
gm_lb64:;
                uint64_t t406 = gm_l46;
                uint64_t r407; bool o408 = __builtin_add_overflow(t406, ((uint64_t)1ULL), &r407);
                uint64_t t409 = r407;
                gm_l46 = t409;
            }
        }
gm_lb62:;
        void * t410 = gm_l0;
        gm_b0_1 t411 = gm_f_end_constraint_defer_52jt3m9((void *)NULL, cx, NULL, t410);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t412 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
        uint8_t t413 = (t412 > ((uint64_t)0ULL));
        if (t413) {
            void * t414 = gm_l0;
            void * t415 = gm_l2;
            gm_tl874t2atnlh3 t416 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t415) + 56));
            gm_b16_8 t417 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l43));
            gm_b0_1 t418 = gm_f_check_distinct_d4svq2h((void *)NULL, cx, NULL, t414, t416, t417);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t419 = gm_l0;
        gm_f_restore_scope_edqq1v8(t419, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l9)));
        uint8_t t420 = gm_l5;
        uint8_t t421 = (uint8_t)!t420;
        if (t421) {
            gm_tg93b272d11vo t422;
            memset(&t422, 0, sizeof t422);
            memmove(gm_out, ((uint8_t *)&t422), 144);
            goto gm_fdone;
        }
        gm_b16_8 t423;
        GM_ST(void *, (uint8_t *)&t423, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t423 + 8, (uintptr_t)(0));
        void * t424 = gm_l2;
        gm_tl874t2atnlh3 t425 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t424) + 56));
        uint64_t t426 = gm_l1;
        gm_b16_8 t427 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_t88rcucg1b7uk t428 = gm_l41;
        gm_tnpifv20irnph t429 = gm_l43;
        gm_b16_8 t430 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_tqu3m2l3n8mnc t431;
        memset(&t431, 0, sizeof t431);
        gm_tqqhoivahirr0 t432;
        GM_ST(uint32_t, (uint8_t *)&t432 + 28, 0);
        GM_ST(uint32_t, (uint8_t *)&t432 + 60, 0);
        GM_ST(uint8_t, (uint8_t *)&t432 + 129, 0);
        GM_ST(uint16_t, (uint8_t *)&t432 + 130, 0);
        GM_ST(uint32_t, (uint8_t *)&t432 + 132, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t432 + 0), t423);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t432 + 16), t425);
        GM_ST(uint64_t, ((uint8_t *)&t432 + 32), t426);
        GM_ST(gm_b16_8, ((uint8_t *)&t432 + 40), t427);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t432 + 56), t428);
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t432 + 64), t429);
        GM_ST(gm_b16_8, ((uint8_t *)&t432 + 80), t430);
        GM_ST(gm_tqu3m2l3n8mnc, ((uint8_t *)&t432 + 96), t431);
        GM_ST(uint8_t, ((uint8_t *)&t432 + 128), ((uint8_t)0));
        gm_tg93b272d11vo t433;
        GM_ST(uint8_t, (uint8_t *)&t433 + 137, 0);
        GM_ST(uint16_t, (uint8_t *)&t433 + 138, 0);
        GM_ST(uint32_t, (uint8_t *)&t433 + 140, 0);
        GM_ST(gm_tqqhoivahirr0, ((uint8_t *)&t433), t432);
        ((uint8_t *)&t433)[136] = 1;
        memmove(gm_out, ((uint8_t *)&t433), 144);
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
