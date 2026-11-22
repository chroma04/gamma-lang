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
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[12]; unsigned __int128 f1; } gm_tl5jbgvjjt15g;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; } gm_tpfol4ocdvoij;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_tfbnciphf3261 v1; gm_tl5jbgvjjt15g v2; gm_t88rcucg1b7uk v3; gm_t88rcucg1b7uk v4; } f1; } gm_t7b7esi3n9r90;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, bits: u64)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl5jbgvjjt15g) == 32 && _Alignof(gm_tl5jbgvjjt15g) == 16 && offsetof(gm_tl5jbgvjjt15g, f0) == 0 && offsetof(gm_tl5jbgvjjt15g, f1) == 16, "layout of `(ty: Ty, bits: u128)`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { void * f0; void * f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; uint32_t f7; uint8_t pe[4]; } gm_tp47bve3dmstt;
_Static_assert(sizeof(gm_tpfol4ocdvoij) == 32 && _Alignof(gm_tpfol4ocdvoij) == 8 && offsetof(gm_tpfol4ocdvoij, f0) == 0 && offsetof(gm_tpfol4ocdvoij, f1) == 16 && offsetof(gm_tpfol4ocdvoij, f2) == 28, "layout of `(name: string, span: Span, ty: Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tcc1rq4n1avb2 v1; uint64_t v2; gm_tpfol4ocdvoij v3; uint32_t v4; } f1; } gm_tu78tssgij60g;
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_t7b7esi3n9r90 f0; uint64_t f1; uint8_t pe[8]; } gm_t592joge1j4uv;
_Static_assert(sizeof(gm_t7b7esi3n9r90) == 48 && _Alignof(gm_t7b7esi3n9r90) == 16 && offsetof(gm_t7b7esi3n9r90, f0) == 0 && offsetof(gm_t7b7esi3n9r90, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t419mo45g7j0a v0; gm_t7b7esi3n9r90 v1; } f1; } gm_t9i04n6j2d1gi;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint32_t f0; uint8_t p1[12]; gm_t7b7esi3n9r90 f1; } gm_tf4lk4crue8vt;
typedef struct { gm_t7b7esi3n9r90 f0; uint8_t f1; uint8_t pe[15]; } gm_tf8kfhcnval78;
typedef struct { gm_tu78tssgij60g f0; gm_b16_8 f1; } gm_tlg23kb3oj23s;
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_t7b7esi3n9r90 v1; gm_t419mo45g7j0a v2; } f1; } gm_tllthq21h2f0k;
typedef struct { gm_b16_8 f0; gm_t7b7esi3n9r90 f1; } gm_tmvmieab1acso;
_Static_assert(sizeof(gm_tp47bve3dmstt) == 224 && _Alignof(gm_tp47bve3dmstt) == 8 && offsetof(gm_tp47bve3dmstt, f0) == 0 && offsetof(gm_tp47bve3dmstt, f1) == 8 && offsetof(gm_tp47bve3dmstt, f2) == 16 && offsetof(gm_tp47bve3dmstt, f3) == 56 && offsetof(gm_tp47bve3dmstt, f4) == 96 && offsetof(gm_tp47bve3dmstt, f5) == 136 && offsetof(gm_tp47bve3dmstt, f6) == 176 && offsetof(gm_tp47bve3dmstt, f7) == 216, "layout of `Verifier`");
typedef struct { gm_t7b7esi3n9r90 f0; gm_t7b7esi3n9r90 f1; } gm_tthvfhsvm2g0s;
_Static_assert(sizeof(gm_tu78tssgij60g) == 40 && _Alignof(gm_tu78tssgij60g) == 8 && offsetof(gm_tu78tssgij60g, f0) == 0 && offsetof(gm_tu78tssgij60g, f1) == 8, "layout of `Base`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t592joge1j4uv) == 64 && _Alignof(gm_t592joge1j4uv) == 16 && offsetof(gm_t592joge1j4uv, f0) == 0 && offsetof(gm_t592joge1j4uv, f1) == 48, "layout of `(index: Value, size: u64)`");
typedef struct { gm_t7b7esi3n9r90 f0; gm_tlg23kb3oj23s f1; uint8_t f2; uint8_t pe[7]; } gm_t6j9ipvj0d6k5;
_Static_assert(sizeof(gm_t9i04n6j2d1gi) == 64 && _Alignof(gm_t9i04n6j2d1gi) == 16 && offsetof(gm_t9i04n6j2d1gi, f0) == 0 && offsetof(gm_t9i04n6j2d1gi, f1) == 16, "layout of `Sink`");
_Static_assert(sizeof(gm_tf4lk4crue8vt) == 64 && _Alignof(gm_tf4lk4crue8vt) == 16 && offsetof(gm_tf4lk4crue8vt, f0) == 0 && offsetof(gm_tf4lk4crue8vt, f1) == 16, "layout of `(dst: u32, v: Value)`");
_Static_assert(sizeof(gm_tf8kfhcnval78) == 64 && _Alignof(gm_tf8kfhcnval78) == 16 && offsetof(gm_tf8kfhcnval78, f0) == 0 && offsetof(gm_tf8kfhcnval78, f1) == 48, "layout of `?Value`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tb31k5l794t7p v0; gm_t592joge1j4uv v1; gm_t592joge1j4uv v2; gm_tlcm4lvu3g1a8 v3; } f1; } gm_tfqmtuprvunod;
typedef struct { gm_t7b7esi3n9r90 f0; gm_t7b7esi3n9r90 f1; gm_tf8kfhcnval78 f2; uint8_t f3; uint8_t pe[15]; } gm_thvv7nb91atip;
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; gm_tf4lk4crue8vt v1; gm_tmvmieab1acso v2; } f1; } gm_tkgci6o4gktbf;
_Static_assert(sizeof(gm_tlg23kb3oj23s) == 56 && _Alignof(gm_tlg23kb3oj23s) == 8 && offsetof(gm_tlg23kb3oj23s, f0) == 0 && offsetof(gm_tlg23kb3oj23s, f1) == 40, "layout of `Place`");
_Static_assert(sizeof(gm_tllthq21h2f0k) == 64 && _Alignof(gm_tllthq21h2f0k) == 16 && offsetof(gm_tllthq21h2f0k, f0) == 0 && offsetof(gm_tllthq21h2f0k, f1) == 16, "layout of `Callee`");
_Static_assert(sizeof(gm_tmvmieab1acso) == 64 && _Alignof(gm_tmvmieab1acso) == 16 && offsetof(gm_tmvmieab1acso, f0) == 0 && offsetof(gm_tmvmieab1acso, f1) == 16, "layout of `(decls: []u32, v: Value)`");
_Static_assert(sizeof(gm_tthvfhsvm2g0s) == 96 && _Alignof(gm_tthvfhsvm2g0s) == 16 && offsetof(gm_tthvfhsvm2g0s, f0) == 0 && offsetof(gm_tthvfhsvm2g0s, f1) == 48, "layout of `(index: Value, len: Value)`");
_Static_assert(sizeof(gm_t6j9ipvj0d6k5) == 112 && _Alignof(gm_t6j9ipvj0d6k5) == 16 && offsetof(gm_t6j9ipvj0d6k5, f0) == 0 && offsetof(gm_t6j9ipvj0d6k5, f1) == 48 && offsetof(gm_t6j9ipvj0d6k5, f2) == 104, "layout of `(present: Value, place: Place, result: bool)`");
_Static_assert(sizeof(gm_tfqmtuprvunod) == 80 && _Alignof(gm_tfqmtuprvunod) == 16 && offsetof(gm_tfqmtuprvunod, f0) == 0 && offsetof(gm_tfqmtuprvunod, f1) == 16, "layout of `Proj`");
_Static_assert(sizeof(gm_thvv7nb91atip) == 176 && _Alignof(gm_thvv7nb91atip) == 16 && offsetof(gm_thvv7nb91atip, f0) == 0 && offsetof(gm_thvv7nb91atip, f1) == 48 && offsetof(gm_thvv7nb91atip, f2) == 96 && offsetof(gm_thvv7nb91atip, f3) == 160, "layout of `(lo: Value, hi: Value, len: ?Value, inclusive: bool)`");
_Static_assert(sizeof(gm_tkgci6o4gktbf) == 80 && _Alignof(gm_tkgci6o4gktbf) == 16 && offsetof(gm_tkgci6o4gktbf, f0) == 0 && offsetof(gm_tkgci6o4gktbf, f1) == 16, "layout of `Handler`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_tthvfhsvm2g0s v0; gm_thvv7nb91atip v1; gm_t6j9ipvj0d6k5 v2; } f1; } gm_tkj0hg83g38nr;
_Static_assert(sizeof(gm_tkj0hg83g38nr) == 192 && _Alignof(gm_tkj0hg83g38nr) == 16 && offsetof(gm_tkj0hg83g38nr, f0) == 0 && offsetof(gm_tkj0hg83g38nr, f1) == 16, "layout of `Check`");
struct gm_fr_check_define_00335ri {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_check_inst_l48k34t {
    gm_frame h;
    gm_t7b7esi3n9r90 l4; /* x */
    gm_tlg23kb3oj23s l6; /* p */
    gm_tlg23kb3oj23s l7; /* p */
    gm_tlg23kb3oj23s l8; /* p */
    gm_tlg23kb3oj23s l9; /* p */
    gm_t7b7esi3n9r90 l10; /* a */
    gm_t7b7esi3n9r90 l11; /* b */
    gm_t7b7esi3n9r90 l12; /* a */
    gm_t7b7esi3n9r90 l13; /* b */
    gm_t7b7esi3n9r90 l14; /* a */
    gm_t7b7esi3n9r90 l15; /* a */
    gm_t7b7esi3n9r90 l20; /* x */
    gm_tllthq21h2f0k l21; /* c */
    gm_t7b7esi3n9r90 l23; /* x */
    gm_t7b7esi3n9r90 l26; /* x */
    gm_tf8kfhcnval78 l27; /* x */
    gm_tf8kfhcnval78 l28; /* x */
    gm_t7b7esi3n9r90 l29; /* y */
    gm_tlg23kb3oj23s l30; /* p */
    gm_t7b7esi3n9r90 l31; /* x */
    gm_tkj0hg83g38nr l32; /* c */
    gm_t7b7esi3n9r90 l33; /* a */
    gm_t7b7esi3n9r90 l34; /* b */
    gm_t7b7esi3n9r90 l36; /* lo */
    gm_t7b7esi3n9r90 l37; /* hi */
    gm_tf8kfhcnval78 l38; /* len */
    gm_t7b7esi3n9r90 l39; /* x */
    gm_t7b7esi3n9r90 l40; /* present */
    gm_tlg23kb3oj23s l41; /* p */
    gm_t7b7esi3n9r90 l42; /* x */
    gm_t7b7esi3n9r90 l43; /* ptr */
    gm_t7b7esi3n9r90 l44; /* lo */
    gm_t7b7esi3n9r90 l45; /* hi */
    gm_t9i04n6j2d1gi l46; /* sink */
    gm_tf8kfhcnval78 l48; /* table */
    gm_t7b7esi3n9r90 l49; /* x */
    gm_t7b7esi3n9r90 l52; /* x */
    gm_t7b7esi3n9r90 l53; /* x */
    gm_t7b7esi3n9r90 l58; /* c */
    gm_t7b7esi3n9r90 l62; /* c */
    gm_tf8kfhcnval78 l64; /* x */
    gm_t7b7esi3n9r90 l65; /* y */
    gm_t7b7esi3n9r90 l67; /* x */
    gm_tf8kfhcnval78 l68; /* x */
    gm_t7b7esi3n9r90 l69; /* y */
    gm_t7b7esi3n9r90 l70; /* a */
    gm_t7b7esi3n9r90 l71; /* b */
    gm_t7b7esi3n9r90 l72; /* a */
    gm_t7b7esi3n9r90 l73; /* b */
    gm_t7b7esi3n9r90 l74; /* a */
    gm_tf8kfhcnval78 l78; /* on */
    gm_t7b7esi3n9r90 l80; /* x */
    gm_tkgci6o4gktbf l81; /* h */
    gm_t7b7esi3n9r90 l82; /* x */
    gm_t7b7esi3n9r90 l83; /* x */
    gm_tf8kfhcnval78 l84; /* x */
    gm_t7b7esi3n9r90 l85; /* y */
    gm_t7b7esi3n9r90 l86; /* k */
    gm_t7b7esi3n9r90 l87; /* x */
    gm_t7b7esi3n9r90 l88; /* k */
    gm_b0_1 ret;
};
struct gm_fr_check_list_0iaa8b5 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* made */
    gm_b0_1 ret;
};
struct gm_fr_check_place_bjmhj7l {
    gm_frame h;
    gm_tlg23kb3oj23s l1; /* p */
    gm_tfqmtuprvunod l6; /* x */
    gm_t7b7esi3n9r90 l7; /* i */
    gm_t7b7esi3n9r90 l8; /* i */
    gm_b0_1 ret;
};
struct gm_fr_check_read_t3fid3k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_check_target_flebqpd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_check_value_5jn6ef5 {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* x */
    gm_b0_1 ret;
};
struct gm_fr_d_type_g8gg2r0 {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fault_c0tu7v0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_2usi0he {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4itqon1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5496f10 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_akc2snl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bg3na43 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fkd5jf8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_g1tf15c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gi0picn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q3eb2dn {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sbobj14 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
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
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_run_verifier_g10feug {
    gm_frame h;
    gm_tp47bve3dmstt l2; /* v */
    gm_b16_8 ret;
};
struct gm_fr_same_type_8j3dhk8 {
    gm_frame h;
    gm_tlg23kb3oj23s l2; /* p */
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
struct gm_fr_typed_5uool0u {
    gm_frame h;
    gm_t7b7esi3n9r90 l1; /* x */
    uint8_t ret;
};
struct gm_fr_verify_i7mfe5h {
    gm_frame h;
    gm_b16_8 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_define_00335ri(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_inst_l48k34t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_list_0iaa8b5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_place_bjmhj7l(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_read_t3fid3k(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_target_flebqpd(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_value_5jn6ef5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_d_type_g8gg2r0(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_dst_of_c0rv8d0(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fault_c0tu7v0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_format_2usi0he(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_4itqon1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_5496f10(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_akc2snl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_bg3na43(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_fkd5jf8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_g1tf15c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_gi0picn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_q3eb2dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_sbobj14(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_place_type_5jm3hnm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_run_verifier_g10feug(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_same_type_8j3dhk8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_typed_5uool0u(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_typed_5uool0u_c(void * a0, gm_t7b7esi3n9r90 a1);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_value_type_p69quvj(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_verify_i7mfe5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern uint8_t gm_ct3dn92ug12[4] __attribute__((aligned(4)));
extern const uint8_t gm_s0963m82avtn1[23];
extern const uint8_t gm_s0ugop4l95o1j[6];
extern const uint8_t gm_s2s14jtsb4380[21];
extern const uint8_t gm_s7jf1bc28tar9[38];
extern const uint8_t gm_s94loquj8r6hq[19];
extern const uint8_t gm_s9sakn2pk9s4e[19];
extern const uint8_t gm_sasnane8pan26[33];
extern const uint8_t gm_sjcsd1e8rp9jn[43];
extern const uint8_t gm_sjrj6qdrion32[35];
extern const uint8_t gm_sk8hc0njn930q[43];
extern const uint8_t gm_sq5ab39u4btdn[41];
extern const uint8_t gm_sqbstqdssta1a[48];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcpafnhrjbrdqq[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_stcivomuinkhh[21];
extern const gm_tmpl gm_tpl265m7jpg5oed;
extern const gm_tmpl gm_tpl5cgoa6ddohcv;
extern const gm_tmpl gm_tpl63djfct0mcad;
extern const gm_tmpl gm_tpl6r9reg6pnruq;
extern const gm_tmpl gm_tple4r1aumd134l;
extern const gm_tmpl gm_tplfmqnc0q3nc41;
extern const gm_tmpl gm_tplkinjhvj7vqlj;
extern const gm_tmpl gm_tpll408251sp58f;
extern const gm_tmpl gm_tplrd8stfk49gik;
extern const gm_tmpl gm_tpls77qni9k09vc;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_value_5jn6ef5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_check_value_5jn6ef5 F;
    void * gm_l0; /* v */
    uint32_t gm_l2; /* t */
    GM_STACK_CHECK(&F, 30u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t5 = (uint8_t)(t4 == ((uint8_t)0ULL));
            if (!t5) {
                goto gm_lb2;
            }
            uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
            gm_l2 = t6;
            void * t7 = gm_l0;
            uint32_t t8 = gm_l2;
            gm_b0_1 t9 = gm_f_check_read_t3fid3k((void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fault_c0tu7v0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fault_c0tu7v0 F;
    void * gm_l0; /* v */
    gm_b16_8 gm_l1; /* text */
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 16);
    gm_b16_8 t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcpafnhrjbrdqq, 52, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_2usi0he(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_2usi0he F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2s14jtsb4380));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpll408251sp58f), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_4itqon1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_4itqon1 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s94loquj8r6hq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl63djfct0mcad), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5496f10(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_5496f10 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stcivomuinkhh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl265m7jpg5oed), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_akc2snl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_akc2snl F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0963m82avtn1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tple4r1aumd134l), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bg3na43(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bg3na43 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7jf1bc28tar9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpls77qni9k09vc), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fkd5jf8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_fkd5jf8 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq5ab39u4btdn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkinjhvj7vqlj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_g1tf15c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_g1tf15c F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk8hc0njn930q));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(42));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrd8stfk49gik), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gi0picn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_gi0picn F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjcsd1e8rp9jn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(42));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6r9reg6pnruq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q3eb2dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q3eb2dn F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sasnane8pan26));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfmqnc0q3nc41), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_sbobj14(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_sbobj14 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9sakn2pk9s4e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5cgoa6ddohcv), t3);
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
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_s1lbe5s();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vd1290p F;
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
        gm_b16_8 t9 = gm_f_empty_slice_s1lbe5s();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_hkmvuvc(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hkmvuvc F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_typed_5uool0u(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_typed_5uool0u F;
    void * gm_l0; /* v */
    uint32_t gm_l2; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t5 = (uint8_t)(t4 == ((uint8_t)0ULL));
            if (!t5) {
                goto gm_lb2;
            }
            uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
            gm_l2 = t6;
            uint32_t t7 = gm_l2;
            uint64_t t8 = (uint64_t)t7;
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 8));
            uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t10) + 24) + 8));
            uint8_t t12 = (t8 < t11);
            F.ret = t12;
            goto gm_fret;
        }
gm_lb2:;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_verify_i7mfe5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_verify_i7mfe5h F;
    void * gm_l0; /* s */
    void * gm_l1; /* f */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    gm_b16_8 t4 = gm_f_run_verifier_g10feug((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_define_00335ri(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_check_define_00335ri F;
    void * gm_l0; /* v */
    uint32_t gm_l1; /* l */
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    gm_tabb710d9mq2e gm_l3;
    uint32_t t2 = gm_l1;
    uint64_t t3 = (uint64_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 136) + 8));
    uint8_t t6 = (t3 >= t5);
    if (t6) {
        void * t7 = gm_l0;
        uint32_t t8 = gm_l1;
        gm_t88rcucg1b7uk t9;
        GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t8);
        gm_f_format_4itqon1((void *)&gm_l2, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcpafnhrjbrdqq, 201, 17);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_b0_1 t13 = gm_f_fault_c0tu7v0((void *)NULL, cx, NULL, t7, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, (((uint8_t *)t14) + 136));
    uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 136) + 8));
    uint32_t t17 = gm_l1;
    uint64_t t18 = (uint64_t)t17;
    if ((uintptr_t)t18 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t18, t16, gm_srcpafnhrjbrdqq, 204, 8);
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t15) + (uintptr_t)t18 * 1));
    if (t19) {
        void * t20 = gm_l0;
        uint32_t t21 = gm_l1;
        gm_t88rcucg1b7uk t22;
        GM_ST(uint32_t, ((uint8_t *)&t22 + 0), t21);
        gm_f_format_5496f10((void *)&gm_l3, (void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcpafnhrjbrdqq, 204, 43);
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
        gm_b0_1 t26 = gm_f_fault_c0tu7v0((void *)NULL, cx, NULL, t20, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t27 = gm_l0;
    void * t28 = GM_LD(void *, (((uint8_t *)t27) + 136));
    uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)t27) + 136) + 8));
    uint32_t t30 = gm_l1;
    uint64_t t31 = (uint64_t)t30;
    if ((uintptr_t)t31 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t31, t29, gm_srcpafnhrjbrdqq, 205, 5);
    GM_ST(uint8_t, (((uint8_t *)t28) + (uintptr_t)t31 * 1), ((uint8_t)1));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_inst_l48k34t(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_check_inst_l48k34t F;
    void * gm_l0; /* v */
    void * gm_l1; /* i */
    uint8_t gm_l2; /* block_body */
    uint32_t gm_l3; /* t */
    uint32_t gm_l5; /* d */
    gm_b16_8 gm_l16; /* args */
    gm_b16_8 gm_l17; /* args */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_b16_8 gm_l22; /* args */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    void * gm_l35; /* s */
    gm_b16_8 gm_l47; /* args */
    gm_b16_8 gm_l50; /* <tmp> */
    uint64_t gm_l51; /* <tmp> */
    uint32_t gm_l54; /* l */
    gm_b16_8 gm_l55; /* body */
    uint32_t gm_l56; /* l */
    gm_b16_8 gm_l57; /* body */
    gm_b16_8 gm_l59; /* then */
    gm_b16_8 gm_l60; /* else_ */
    uint32_t gm_l61; /* l */
    uint32_t gm_l63; /* l */
    gm_b16_8 gm_l66; /* body */
    gm_b16_8 gm_l75; /* body */
    uint32_t gm_l76; /* l */
    gm_b16_8 gm_l77; /* install */
    gm_b16_8 gm_l79; /* body */
    GM_STACK_CHECK(&F, 2859u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t9805qtonbsag gm_l89;
    gm_tabb710d9mq2e gm_l90;
    gm_tabb710d9mq2e gm_l91;
    gm_tabb710d9mq2e gm_l92;
    gm_tabb710d9mq2e gm_l93;
    gm_tftb1odc739t5 gm_l94;
    gm_tftb1odc739t5 gm_l95;
    gm_tftb1odc739t5 gm_l96;
    gm_tftb1odc739t5 gm_l97;
    {
        {
            {
                void * t113 = gm_l1;
                void * t114 = (void *)((uint8_t *)t113);
                gm_t9805qtonbsag t115 = gm_f_dst_of_c0rv8d0(t114);
                gm_l89 = t115;
                uint8_t t116 = (uint8_t)((((uint8_t *)&gm_l89))[4] != 0);
                if (!t116) {
                    goto gm_lb2;
                }
                uint32_t t117 = GM_LD(uint32_t, ((uint8_t *)&gm_l89));
                gm_l3 = t117;
                uint32_t t118 = gm_l3;
                uint64_t t119 = (uint64_t)t118;
                void * t120 = gm_l0;
                uint64_t t121 = GM_LD(uint64_t, ((((uint8_t *)t120) + 56) + 8));
                uint8_t t122 = (t119 >= t121);
                if (t122) {
                    void * t123 = gm_l0;
                    uint32_t t124 = gm_l3;
                    gm_t88rcucg1b7uk t125;
                    GM_ST(uint32_t, ((uint8_t *)&t125 + 0), t124);
                    gm_f_format_akc2snl((void *)&gm_l90, (void *)NULL, cx, NULL, t125);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t126 = (uint8_t)!((((uint8_t *)&gm_l90))[0] != 0);
                    if (!t126) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l90) + 8, gm_srcpafnhrjbrdqq, 74, 21);
                    gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l90) + 8));
                    gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
                    gm_b0_1 t129 = gm_f_fault_c0tu7v0((void *)NULL, cx, NULL, t123, t128);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                } else {
                    void * t130 = gm_l0;
                    void * t131 = GM_LD(void *, (((uint8_t *)t130) + 56));
                    uint64_t t132 = GM_LD(uint64_t, ((((uint8_t *)t130) + 56) + 8));
                    uint32_t t133 = gm_l3;
                    uint64_t t134 = (uint64_t)t133;
                    if ((uintptr_t)t134 >= (uintptr_t)(t132)) gm_trap_index((uintptr_t)t134, t132, gm_srcpafnhrjbrdqq, 75, 19);
                    uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)t131) + (uintptr_t)t134 * 1));
                    if (t135) {
                        void * t136 = gm_l0;
                        uint32_t t137 = gm_l3;
                        gm_t88rcucg1b7uk t138;
                        GM_ST(uint32_t, ((uint8_t *)&t138 + 0), t137);
                        gm_f_format_2usi0he((void *)&gm_l91, (void *)NULL, cx, NULL, t138);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t139 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                        if (!t139) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 8, gm_srcpafnhrjbrdqq, 76, 21);
                        gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l91) + 8));
                        gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
                        gm_b0_1 t142 = gm_f_fault_c0tu7v0((void *)NULL, cx, NULL, t136, t141);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    } else {
                        void * t143 = gm_l0;
                        void * t144 = GM_LD(void *, (((uint8_t *)t143) + 56));
                        uint64_t t145 = GM_LD(uint64_t, ((((uint8_t *)t143) + 56) + 8));
                        uint32_t t146 = gm_l3;
                        uint64_t t147 = (uint64_t)t146;
                        if ((uintptr_t)t147 >= (uintptr_t)(t145)) gm_trap_index((uintptr_t)t147, t145, gm_srcpafnhrjbrdqq, 78, 13);
                        GM_ST(uint8_t, (((uint8_t *)t144) + (uintptr_t)t147 * 1), ((uint8_t)1));
                    }
                }
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t148 = gm_l1;
        {
            {
                {
                    uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t150 = (uint8_t)(t149 == ((uint8_t)0ULL));
                    if (!t150) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l4), ((((uint8_t *)t148) + 16) + 16), 48);
                    void * t151 = gm_l0;
                    gm_b0_1 t152 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t151, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l4)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t154 = (uint8_t)(t153 == ((uint8_t)1ULL));
                    if (!t154) {
                        goto gm_lb5;
                    }
                    uint32_t t155 = GM_LD(uint32_t, (((uint8_t *)t148) + 16));
                    gm_l5 = t155;
                    memmove(((uint8_t *)&F.l6), ((((uint8_t *)t148) + 16) + 8), 56);
                    void * t156 = gm_l0;
                    gm_b0_1 t157 = gm_f_check_place_bjmhj7l((void *)NULL, cx, NULL, t156, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l6)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t158 = gm_l0;
                    gm_b16_8 t159;
                    GM_ST(void *, (uint8_t *)&t159, (void *)(gm_sqfrv3g5v9m2n));
                    GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)(4));
                    void * t160 = gm_l0;
                    uint32_t t161 = gm_l5;
                    gm_tlg23kb3oj23s t162 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l6));
                    gm_tkn42u7na3t81 t163 = gm_f_d_type_g8gg2r0(t160, t161);
                    gm_b0_1 t164 = gm_f_same_type_8j3dhk8((void *)NULL, cx, NULL, t158, t159, &t162, t163);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    {
                        {
                            uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t166 = (uint8_t)(t165 == ((uint8_t)2ULL));
                            if (!t166) {
                                goto gm_lb8;
                            }
                            memmove(((uint8_t *)&F.l7), ((((uint8_t *)t148) + 16) + 8), 56);
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t168 = (uint8_t)(t167 == ((uint8_t)23ULL));
                            if (!t168) {
                                goto gm_lb9;
                            }
                            memmove(((uint8_t *)&F.l7), ((((uint8_t *)t148) + 16) + 8), 56);
                            goto gm_lb7;
                        }
gm_lb9:;
                        {
                            uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t170 = (uint8_t)(t169 == ((uint8_t)24ULL));
                            if (!t170) {
                                goto gm_lb10;
                            }
                            memmove(((uint8_t *)&F.l7), ((((uint8_t *)t148) + 16) + 8), 56);
                            goto gm_lb7;
                        }
gm_lb10:;
                        goto gm_lb6;
                    }
gm_lb7:;
                    void * t171 = gm_l0;
                    gm_b0_1 t172 = gm_f_check_place_bjmhj7l((void *)NULL, cx, NULL, t171, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l7)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    {
                        {
                            uint8_t t173 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t174 = (uint8_t)(t173 == ((uint8_t)3ULL));
                            if (!t174) {
                                goto gm_lb13;
                            }
                            memmove(((uint8_t *)&F.l10), ((((uint8_t *)t148) + 16) + 16), 48);
                            memmove(((uint8_t *)&F.l11), ((((uint8_t *)t148) + 16) + 64), 48);
                            goto gm_lb12;
                        }
gm_lb13:;
                        {
                            uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t176 = (uint8_t)(t175 == ((uint8_t)5ULL));
                            if (!t176) {
                                goto gm_lb14;
                            }
                            memmove(((uint8_t *)&F.l10), ((((uint8_t *)t148) + 16) + 16), 48);
                            memmove(((uint8_t *)&F.l11), ((((uint8_t *)t148) + 16) + 64), 48);
                            goto gm_lb12;
                        }
gm_lb14:;
                        goto gm_lb11;
                    }
gm_lb12:;
                    void * t177 = gm_l0;
                    gm_b0_1 t178 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t177, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l10)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t179 = gm_l0;
                    gm_b0_1 t180 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t179, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l11)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb11:;
            }
            {
                {
                    {
                        {
                            uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t182 = (uint8_t)(t181 == ((uint8_t)4ULL));
                            if (!t182) {
                                goto gm_lb17;
                            }
                            memmove(((uint8_t *)&F.l14), ((((uint8_t *)t148) + 16) + 16), 48);
                            goto gm_lb16;
                        }
gm_lb17:;
                        {
                            uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t184 = (uint8_t)(t183 == ((uint8_t)6ULL));
                            if (!t184) {
                                goto gm_lb18;
                            }
                            memmove(((uint8_t *)&F.l14), ((((uint8_t *)t148) + 16) + 16), 48);
                            goto gm_lb16;
                        }
gm_lb18:;
                        goto gm_lb15;
                    }
gm_lb16:;
                    void * t185 = gm_l0;
                    gm_b0_1 t186 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t185, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l14)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb15:;
                {
                    {
                        {
                            uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t188 = (uint8_t)(t187 == ((uint8_t)7ULL));
                            if (!t188) {
                                goto gm_lb21;
                            }
                            gm_b16_8 t189 = GM_LD(gm_b16_8, ((((uint8_t *)t148) + 16) + 24));
                            gm_l16 = t189;
                            goto gm_lb20;
                        }
gm_lb21:;
                        {
                            uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t191 = (uint8_t)(t190 == ((uint8_t)9ULL));
                            if (!t191) {
                                goto gm_lb22;
                            }
                            gm_b16_8 t192 = GM_LD(gm_b16_8, ((((uint8_t *)t148) + 16) + 8));
                            gm_l16 = t192;
                            goto gm_lb20;
                        }
gm_lb22:;
                        goto gm_lb19;
                    }
gm_lb20:;
                    gm_b16_8 t193 = gm_l16;
                    gm_l18 = t193;
                    gm_l19 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t194 = gm_l19;
                            uint64_t t195 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                            uint8_t t196 = (t194 < t195);
                            if (!t196) {
                                goto gm_lb23;
                            }
                            {
                                void * t197 = GM_LD(void *, ((uint8_t *)&gm_l18));
                                uint64_t t198 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                uint64_t t199 = gm_l19;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l20), (((uint8_t *)t197) + (uintptr_t)t199 * 48), 48);
                                        void * t200 = gm_l0;
                                        gm_b0_1 t201 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t200, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l20)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb26;
                                    }
gm_lb27:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcpafnhrjbrdqq, 93, 54);
                                }
gm_lb26:;
                            }
gm_lb25:;
                            uint64_t t202 = gm_l19;
                            uint64_t r203; bool o204 = __builtin_add_overflow(t202, ((uint64_t)1ULL), &r203);
                            uint64_t t205 = r203;
                            gm_l19 = t205;
                        }
                    }
gm_lb23:;
                    goto gm_lb3;
                }
gm_lb19:;
                {
                    uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t207 = (uint8_t)(t206 == ((uint8_t)8ULL));
                    if (!t207) {
                        goto gm_lb28;
                    }
                    memmove(((uint8_t *)&F.l21), ((((uint8_t *)t148) + 16) + 16), 64);
                    gm_b16_8 t208 = GM_LD(gm_b16_8, ((((uint8_t *)t148) + 16) + 80));
                    gm_l22 = t208;
                    {
                        {
                            uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)&F.l21) + 0));
                            uint8_t t210 = (uint8_t)(t209 == ((uint8_t)1ULL));
                            if (!t210) {
                                goto gm_lb30;
                            }
                            memmove(((uint8_t *)&F.l23), (((uint8_t *)&F.l21) + 16), 48);
                            void * t211 = gm_l0;
                            gm_b0_1 t212 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t211, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l23)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb29;
                        }
gm_lb30:;
                    }
gm_lb29:;
                    gm_b16_8 t213 = gm_l22;
                    gm_l24 = t213;
                    gm_l25 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t214 = gm_l25;
                            uint64_t t215 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                            uint8_t t216 = (t214 < t215);
                            if (!t216) {
                                goto gm_lb31;
                            }
                            {
                                void * t217 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                uint64_t t219 = gm_l25;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l26), (((uint8_t *)t217) + (uintptr_t)t219 * 48), 48);
                                        void * t220 = gm_l0;
                                        gm_b0_1 t221 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t220, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l26)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb34;
                                    }
gm_lb35:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcpafnhrjbrdqq, 96, 13);
                                }
gm_lb34:;
                            }
gm_lb33:;
                            uint64_t t222 = gm_l25;
                            uint64_t r223; bool o224 = __builtin_add_overflow(t222, ((uint64_t)1ULL), &r223);
                            uint64_t t225 = r223;
                            gm_l25 = t225;
                        }
                    }
gm_lb31:;
                    goto gm_lb3;
                }
gm_lb28:;
            }
            {
                {
                    {
                        {
                            uint8_t t226 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t227 = (uint8_t)(t226 == ((uint8_t)14ULL));
                            if (!t227) {
                                goto gm_lb38;
                            }
                            memmove(((uint8_t *)&F.l27), ((((uint8_t *)t148) + 16) + 16), 64);
                            goto gm_lb37;
                        }
gm_lb38:;
                        {
                            uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t229 = (uint8_t)(t228 == ((uint8_t)15ULL));
                            if (!t229) {
                                goto gm_lb39;
                            }
                            memmove(((uint8_t *)&F.l27), ((((uint8_t *)t148) + 16) + 16), 64);
                            goto gm_lb37;
                        }
gm_lb39:;
                        goto gm_lb36;
                    }
gm_lb37:;
                    {
                        {
                            uint8_t t230 = (uint8_t)((((uint8_t *)&F.l27))[48] != 0);
                            if (!t230) {
                                goto gm_lb41;
                            }
                            memmove(((uint8_t *)&F.l29), ((uint8_t *)&F.l27), 48);
                            void * t231 = gm_l0;
                            gm_b0_1 t232 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t231, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l29)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb40;
                        }
gm_lb41:;
                    }
gm_lb40:;
                    goto gm_lb3;
                }
gm_lb36:;
                {
                    uint8_t t233 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t234 = (uint8_t)(t233 == ((uint8_t)25ULL));
                    if (!t234) {
                        goto gm_lb42;
                    }
                    memmove(((uint8_t *)&F.l30), (((uint8_t *)t148) + 16), 56);
                    memmove(((uint8_t *)&F.l31), ((((uint8_t *)t148) + 16) + 64), 48);
                    void * t235 = gm_l0;
                    gm_b0_1 t236 = gm_f_check_place_bjmhj7l((void *)NULL, cx, NULL, t235, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l30)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t237 = gm_l0;
                    gm_b0_1 t238 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t237, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l31)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t239 = gm_l0;
                    uint8_t t240 = gm_f_typed_5uool0u(t239, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l31)));
                    if (t240) {
                        void * t241 = gm_l0;
                        gm_b16_8 t242;
                        GM_ST(void *, (uint8_t *)&t242, (void *)(gm_s0ugop4l95o1j));
                        GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(5));
                        void * t243 = gm_l0;
                        void * t244 = GM_LD(void *, (((uint8_t *)t243) + 8));
                        gm_tlg23kb3oj23s t245 = GM_LD(gm_tlg23kb3oj23s, ((uint8_t *)&F.l30));
                        gm_t88rcucg1b7uk t246 = GM_CONV(gm_t88rcucg1b7uk, gm_f_value_type_p69quvj(t244, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l31))));
                        gm_tkn42u7na3t81 t247;
                        GM_ST(uint8_t, (uint8_t *)&t247 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t247 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t247), t246);
                        ((uint8_t *)&t247)[4] = 1;
                        gm_b0_1 t248 = gm_f_same_type_8j3dhk8((void *)NULL, cx, NULL, t241, t242, &t245, t247);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb3;
                }
gm_lb42:;
                {
                    uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t250 = (uint8_t)(t249 == ((uint8_t)26ULL));
                    if (!t250) {
                        goto gm_lb43;
                    }
                    memmove(((uint8_t *)&F.l32), (((uint8_t *)t148) + 16), 192);
                    {
                        {
                            uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 0));
                            uint8_t t252 = (uint8_t)(t251 == ((uint8_t)0ULL));
                            if (!t252) {
                                goto gm_lb45;
                            }
                            memmove(((uint8_t *)&F.l33), (((uint8_t *)&F.l32) + 16), 48);
                            memmove(((uint8_t *)&F.l34), ((((uint8_t *)&F.l32) + 16) + 48), 48);
                            void * t253 = gm_l0;
                            gm_b0_1 t254 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t253, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l33)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t255 = gm_l0;
                            gm_b0_1 t256 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t255, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l34)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                {
                                    void * t257 = gm_l0;
                                    uint8_t t258 = (uint8_t)(GM_LD(void *, ((uint8_t *)t257)) != 0);
                                    if (!t258) {
                                        goto gm_lb47;
                                    }
                                    void * t259 = GM_LD(void *, ((uint8_t *)t257));
                                    gm_l35 = t259;
                                    {
                                        {
                                            void * t260 = gm_l0;
                                            uint8_t t261 = gm_f_typed_5uool0u(t260, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l33)));
                                            if (!t261) {
                                                goto gm_lb49;
                                            }
                                            void * t262 = gm_l35;
                                            void * t263 = (void *)(((uint8_t *)t262) + 8);
                                            void * t264 = gm_l0;
                                            void * t265 = GM_LD(void *, (((uint8_t *)t264) + 8));
                                            gm_t88rcucg1b7uk t266 = GM_CONV(gm_t88rcucg1b7uk, gm_f_value_type_p69quvj(t265, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l33))));
                                            uint8_t t267 = gm_f_is_integer_8d4f9ek(t263, GM_CONV(gm_csguankko4lmhd, t266));
                                            uint8_t t268 = (uint8_t)!t267;
                                            if (!t268) {
                                                goto gm_lb49;
                                            }
                                            void * t269 = gm_l0;
                                            void * t270 = gm_l35;
                                            void * t271 = gm_l0;
                                            void * t272 = GM_LD(void *, (((uint8_t *)t271) + 8));
                                            gm_t88rcucg1b7uk t273 = GM_CONV(gm_t88rcucg1b7uk, gm_f_value_type_p69quvj(t272, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l33))));
                                            gm_b16_8 t274 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t270, t273);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_tnpifv20irnph t275;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t275 + 0), t274);
                                            gm_f_format_bg3na43((void *)&gm_l92, (void *)NULL, cx, NULL, t275);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t276 = (uint8_t)!((((uint8_t *)&gm_l92))[0] != 0);
                                            if (!t276) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l92) + 8, gm_srcpafnhrjbrdqq, 109, 90);
                                            gm_b16_8 t277 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l92) + 8));
                                            gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)&t277));
                                            gm_b0_1 t279 = gm_f_fault_c0tu7v0((void *)NULL, cx, NULL, t269, t278);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_lb48;
                                        }
gm_lb49:;
                                    }
gm_lb48:;
                                    {
                                        {
                                            void * t280 = gm_l0;
                                            uint8_t t281 = gm_f_typed_5uool0u(t280, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l34)));
                                            if (!t281) {
                                                goto gm_lb51;
                                            }
                                            void * t282 = gm_l35;
                                            void * t283 = (void *)(((uint8_t *)t282) + 8);
                                            void * t284 = gm_l0;
                                            void * t285 = GM_LD(void *, (((uint8_t *)t284) + 8));
                                            gm_t88rcucg1b7uk t286 = GM_CONV(gm_t88rcucg1b7uk, gm_f_value_type_p69quvj(t285, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l34))));
                                            gm_t53at80pfdus3 t287;
                                            gm_f_kind_jcnt4hm((void *)&t287, t283, GM_CONV(gm_csguankko4lmhd, t286));
                                            uint8_t t288 = GM_LD(uint8_t, (((uint8_t *)&t287) + 0));
                                            uint8_t t289 = (uint8_t)(!(t288 == ((uint8_t)1ULL)));
                                            if (!t289) {
                                                goto gm_lb51;
                                            }
                                            void * t290 = gm_l0;
                                            void * t291 = gm_l35;
                                            void * t292 = gm_l0;
                                            void * t293 = GM_LD(void *, (((uint8_t *)t292) + 8));
                                            gm_t53at80pfdus3 t294;
                                            memset(&t294, 0, sizeof t294);
                                            GM_ST(uint8_t, ((uint8_t *)&t294 + 0), ((uint8_t)1ULL));
                                            gm_t88rcucg1b7uk t295 = GM_CONV(gm_t88rcucg1b7uk, gm_f_value_type_p69quvj(t293, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l34))));
                                            gm_b16_8 t296 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t291, t295);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_tnpifv20irnph t297;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t297 + 0), t296);
                                            gm_f_format_gi0picn((void *)&gm_l93, (void *)NULL, cx, NULL, t297);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t298 = (uint8_t)!((((uint8_t *)&gm_l93))[0] != 0);
                                            if (!t298) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l93) + 8, gm_srcpafnhrjbrdqq, 110, 94);
                                            gm_b16_8 t299 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l93) + 8));
                                            gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&t299));
                                            gm_b0_1 t301 = gm_f_fault_c0tu7v0((void *)NULL, cx, NULL, t290, t300);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_lb50;
                                        }
gm_lb51:;
                                    }
gm_lb50:;
                                    goto gm_lb46;
                                }
gm_lb47:;
                            }
gm_lb46:;
                            goto gm_lb44;
                        }
gm_lb45:;
                        {
                            uint8_t t302 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 0));
                            uint8_t t303 = (uint8_t)(t302 == ((uint8_t)1ULL));
                            if (!t303) {
                                goto gm_lb52;
                            }
                            memmove(((uint8_t *)&F.l36), (((uint8_t *)&F.l32) + 16), 48);
                            memmove(((uint8_t *)&F.l37), ((((uint8_t *)&F.l32) + 16) + 48), 48);
                            memmove(((uint8_t *)&F.l38), ((((uint8_t *)&F.l32) + 16) + 96), 64);
                            void * t304 = gm_l0;
                            gm_b0_1 t305 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t304, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l36)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t306 = gm_l0;
                            gm_b0_1 t307 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t306, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l37)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                {
                                    uint8_t t308 = (uint8_t)((((uint8_t *)&F.l38))[48] != 0);
                                    if (!t308) {
                                        goto gm_lb54;
                                    }
                                    memmove(((uint8_t *)&F.l39), ((uint8_t *)&F.l38), 48);
                                    void * t309 = gm_l0;
                                    gm_b0_1 t310 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t309, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l39)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb53;
                                }
gm_lb54:;
                            }
gm_lb53:;
                            goto gm_lb44;
                        }
gm_lb52:;
                        {
                            uint8_t t311 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 0));
                            uint8_t t312 = (uint8_t)(t311 == ((uint8_t)2ULL));
                            if (!t312) {
                                goto gm_lb55;
                            }
                            memmove(((uint8_t *)&F.l40), (((uint8_t *)&F.l32) + 16), 48);
                            memmove(((uint8_t *)&F.l41), ((((uint8_t *)&F.l32) + 16) + 48), 56);
                            void * t313 = gm_l0;
                            gm_b0_1 t314 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t313, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l40)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t315 = gm_l0;
                            gm_b0_1 t316 = gm_f_check_place_bjmhj7l((void *)NULL, cx, NULL, t315, &(*(gm_tlg23kb3oj23s *)((uint8_t *)&F.l41)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb44;
                        }
gm_lb55:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpafnhrjbrdqq, 104, 22);
                    }
gm_lb44:;
                    goto gm_lb3;
                }
gm_lb43:;
            }
            {
                {
                    {
                        {
                            uint8_t t317 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t318 = (uint8_t)(t317 == ((uint8_t)10ULL));
                            if (!t318) {
                                goto gm_lb58;
                            }
                            goto gm_lb57;
                        }
gm_lb58:;
                        {
                            uint8_t t319 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t320 = (uint8_t)(t319 == ((uint8_t)19ULL));
                            if (!t320) {
                                goto gm_lb59;
                            }
                            goto gm_lb57;
                        }
gm_lb59:;
                        {
                            uint8_t t321 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t322 = (uint8_t)(t321 == ((uint8_t)16ULL));
                            if (!t322) {
                                goto gm_lb60;
                            }
                            goto gm_lb57;
                        }
gm_lb60:;
                        {
                            uint8_t t323 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t324 = (uint8_t)(t323 == ((uint8_t)17ULL));
                            if (!t324) {
                                goto gm_lb61;
                            }
                            goto gm_lb57;
                        }
gm_lb61:;
                        {
                            uint8_t t325 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t326 = (uint8_t)(t325 == ((uint8_t)18ULL));
                            if (!t326) {
                                goto gm_lb62;
                            }
                            goto gm_lb57;
                        }
gm_lb62:;
                        {
                            uint8_t t327 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t328 = (uint8_t)(t327 == ((uint8_t)42ULL));
                            if (!t328) {
                                goto gm_lb63;
                            }
                            goto gm_lb57;
                        }
gm_lb63:;
                        {
                            uint8_t t329 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t330 = (uint8_t)(t329 == ((uint8_t)43ULL));
                            if (!t330) {
                                goto gm_lb64;
                            }
                            goto gm_lb57;
                        }
gm_lb64:;
                        {
                            uint8_t t331 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t332 = (uint8_t)(t331 == ((uint8_t)44ULL));
                            if (!t332) {
                                goto gm_lb65;
                            }
                            goto gm_lb57;
                        }
gm_lb65:;
                        goto gm_lb56;
                    }
gm_lb57:;
                    goto gm_lb3;
                }
gm_lb56:;
                {
                    uint8_t t333 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t334 = (uint8_t)(t333 == ((uint8_t)11ULL));
                    if (!t334) {
                        goto gm_lb66;
                    }
                    memmove(((uint8_t *)&F.l42), ((((uint8_t *)t148) + 16) + 16), 48);
                    void * t335 = gm_l0;
                    gm_b0_1 t336 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t335, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l42)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb66:;
                {
                    uint8_t t337 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t338 = (uint8_t)(t337 == ((uint8_t)12ULL));
                    if (!t338) {
                        goto gm_lb67;
                    }
                    memmove(((uint8_t *)&F.l43), ((((uint8_t *)t148) + 16) + 16), 48);
                    memmove(((uint8_t *)&F.l44), ((((uint8_t *)t148) + 16) + 64), 48);
                    memmove(((uint8_t *)&F.l45), ((((uint8_t *)t148) + 16) + 112), 48);
                    void * t339 = gm_l0;
                    gm_b0_1 t340 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t339, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l43)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t341 = gm_l0;
                    gm_b0_1 t342 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t341, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l44)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t343 = gm_l0;
                    gm_b0_1 t344 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t343, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l45)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb67:;
            }
            {
                {
                    uint8_t t345 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t346 = (uint8_t)(t345 == ((uint8_t)13ULL));
                    if (!t346) {
                        goto gm_lb68;
                    }
                    memmove(((uint8_t *)&F.l46), (((uint8_t *)t148) + 16), 64);
                    gm_b16_8 t347 = GM_LD(gm_b16_8, ((((uint8_t *)t148) + 16) + 96));
                    gm_l47 = t347;
                    memmove(((uint8_t *)&F.l48), ((((uint8_t *)t148) + 16) + 112), 64);
                    {
                        {
                            uint8_t t348 = GM_LD(uint8_t, (((uint8_t *)&F.l46) + 0));
                            uint8_t t349 = (uint8_t)(t348 == ((uint8_t)1ULL));
                            if (!t349) {
                                goto gm_lb70;
                            }
                            memmove(((uint8_t *)&F.l49), (((uint8_t *)&F.l46) + 16), 48);
                            void * t350 = gm_l0;
                            gm_b0_1 t351 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t350, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l49)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb69;
                        }
gm_lb70:;
                    }
gm_lb69:;
                    gm_b16_8 t352 = gm_l47;
                    gm_l50 = t352;
                    gm_l51 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t353 = gm_l51;
                            uint64_t t354 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                            uint8_t t355 = (t353 < t354);
                            if (!t355) {
                                goto gm_lb71;
                            }
                            {
                                void * t356 = GM_LD(void *, ((uint8_t *)&gm_l50));
                                uint64_t t357 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                                uint64_t t358 = gm_l51;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l52), (((uint8_t *)t356) + (uintptr_t)t358 * 48), 48);
                                        void * t359 = gm_l0;
                                        gm_b0_1 t360 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t359, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l52)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb74;
                                    }
gm_lb75:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcpafnhrjbrdqq, 132, 13);
                                }
gm_lb74:;
                            }
gm_lb73:;
                            uint64_t t361 = gm_l51;
                            uint64_t r362; bool o363 = __builtin_add_overflow(t361, ((uint64_t)1ULL), &r362);
                            uint64_t t364 = r362;
                            gm_l51 = t364;
                        }
                    }
gm_lb71:;
                    {
                        {
                            uint8_t t365 = (uint8_t)((((uint8_t *)&F.l48))[48] != 0);
                            if (!t365) {
                                goto gm_lb77;
                            }
                            memmove(((uint8_t *)&F.l53), ((uint8_t *)&F.l48), 48);
                            void * t366 = gm_l0;
                            gm_b0_1 t367 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t366, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l53)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb76;
                        }
gm_lb77:;
                    }
gm_lb76:;
                    goto gm_lb3;
                }
gm_lb68:;
                {
                    uint8_t t368 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t369 = (uint8_t)(t368 == ((uint8_t)27ULL));
                    if (!t369) {
                        goto gm_lb78;
                    }
                    uint32_t t370 = GM_LD(uint32_t, (((uint8_t *)t148) + 16));
                    gm_l54 = t370;
                    gm_b16_8 t371 = GM_LD(gm_b16_8, ((((uint8_t *)t148) + 16) + 8));
                    gm_l55 = t371;
                    void * t372 = gm_l0;
                    uint32_t t373 = gm_l54;
                    gm_b0_1 t374 = gm_f_check_define_00335ri((void *)NULL, cx, NULL, t372, t373);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t375 = gm_l0;
                    void * t376 = (void *)(((uint8_t *)t375) + 176);
                    uint32_t t377 = gm_l54;
                    gm_tftb1odc739t5 t378 = gm_f_push_im9d4ro(t376, t377);
                    gm_l94 = t378;
                    uint8_t t379 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
                    if (!t379) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l94) + 1, gm_srcpafnhrjbrdqq, 137, 13);
                    void * t380 = gm_l0;
                    gm_b16_8 t381 = gm_l55;
                    gm_b16_8 t382 = GM_LD(gm_b16_8, ((uint8_t *)&t381));
                    gm_b0_1 t383 = gm_f_check_list_0iaa8b5((void *)NULL, cx, NULL, t380, t382, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t384 = gm_l0;
                    void * t385 = (void *)(((uint8_t *)t384) + 176);
                    gm_t9805qtonbsag t386 = gm_f_pop_570g79n(t385);
                    goto gm_lb3;
                }
gm_lb78:;
            }
            {
                {
                    uint8_t t387 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t388 = (uint8_t)(t387 == ((uint8_t)28ULL));
                    if (!t388) {
                        goto gm_lb79;
                    }
                    uint32_t t389 = GM_LD(uint32_t, (((uint8_t *)t148) + 16));
                    gm_l56 = t389;
                    gm_b16_8 t390 = GM_LD(gm_b16_8, ((((uint8_t *)t148) + 16) + 8));
                    gm_l57 = t390;
                    void * t391 = gm_l0;
                    uint32_t t392 = gm_l56;
                    gm_b0_1 t393 = gm_f_check_define_00335ri((void *)NULL, cx, NULL, t391, t392);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t394 = gm_l0;
                    void * t395 = (void *)(((uint8_t *)t394) + 176);
                    uint32_t t396 = GM_LD(uint32_t, ((uint8_t *)gm_ct3dn92ug12));
                    uint32_t r397; bool o398 = __builtin_sub_overflow(t396, ((uint32_t)1ULL), &r397);
                    if (o398) gm_trap("integer overflow in `-`", gm_srcpafnhrjbrdqq, 144, 25);
                    uint32_t t399 = r397;
                    gm_tftb1odc739t5 t400 = gm_f_push_im9d4ro(t395, t399);
                    gm_l95 = t400;
                    uint8_t t401 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
                    if (!t401) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l95) + 1, gm_srcpafnhrjbrdqq, 144, 13);
                    void * t402 = gm_l0;
                    gm_b16_8 t403 = gm_l57;
                    gm_b16_8 t404 = GM_LD(gm_b16_8, ((uint8_t *)&t403));
                    gm_b0_1 t405 = gm_f_check_list_0iaa8b5((void *)NULL, cx, NULL, t402, t404, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t406 = gm_l0;
                    void * t407 = (void *)(((uint8_t *)t406) + 176);
                    gm_t9805qtonbsag t408 = gm_f_pop_570g79n(t407);
                    goto gm_lb3;
                }
gm_lb79:;
                {
                    uint8_t t409 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t410 = (uint8_t)(t409 == ((uint8_t)29ULL));
                    if (!t410) {
                        goto gm_lb80;
                    }
                    memmove(((uint8_t *)&F.l58), (((uint8_t *)t148) + 16), 48);
                    gm_b16_8 t411 = GM_LD(gm_b16_8, ((((uint8_t *)t148) + 16) + 48));
                    gm_l59 = t411;
                    gm_b16_8 t412 = GM_LD(gm_b16_8, ((((uint8_t *)t148) + 16) + 64));
                    gm_l60 = t412;
                    void * t413 = gm_l0;
                    gm_b0_1 t414 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t413, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l58)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t415 = gm_l0;
                    gm_b16_8 t416 = gm_l59;
                    gm_b16_8 t417 = GM_LD(gm_b16_8, ((uint8_t *)&t416));
                    gm_b0_1 t418 = gm_f_check_list_0iaa8b5((void *)NULL, cx, NULL, t415, t417, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t419 = gm_l0;
                    gm_b16_8 t420 = gm_l60;
                    gm_b16_8 t421 = GM_LD(gm_b16_8, ((uint8_t *)&t420));
                    gm_b0_1 t422 = gm_f_check_list_0iaa8b5((void *)NULL, cx, NULL, t419, t421, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb80:;
                {
                    uint8_t t423 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t424 = (uint8_t)(t423 == ((uint8_t)30ULL));
                    if (!t424) {
                        goto gm_lb81;
                    }
                    uint32_t t425 = GM_LD(uint32_t, (((uint8_t *)t148) + 16));
                    gm_l61 = t425;
                    void * t426 = gm_l0;
                    uint32_t t427 = gm_l61;
                    gm_b0_1 t428 = gm_f_check_target_flebqpd((void *)NULL, cx, NULL, t426, t427);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb81:;
                {
                    uint8_t t429 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t430 = (uint8_t)(t429 == ((uint8_t)31ULL));
                    if (!t430) {
                        goto gm_lb82;
                    }
                    memmove(((uint8_t *)&F.l62), (((uint8_t *)t148) + 16), 48);
                    uint32_t t431 = GM_LD(uint32_t, ((((uint8_t *)t148) + 16) + 52));
                    gm_l63 = t431;
                    void * t432 = gm_l0;
                    gm_b0_1 t433 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t432, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l62)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t434 = gm_l0;
                    uint32_t t435 = gm_l63;
                    gm_b0_1 t436 = gm_f_check_target_flebqpd((void *)NULL, cx, NULL, t434, t435);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb82:;
            }
            {
                {
                    uint8_t t437 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t438 = (uint8_t)(t437 == ((uint8_t)32ULL));
                    if (!t438) {
                        goto gm_lb83;
                    }
                    memmove(((uint8_t *)&F.l64), (((uint8_t *)t148) + 16), 64);
                    {
                        {
                            uint8_t t439 = (uint8_t)((((uint8_t *)&F.l64))[48] != 0);
                            if (!t439) {
                                goto gm_lb85;
                            }
                            memmove(((uint8_t *)&F.l65), ((uint8_t *)&F.l64), 48);
                            void * t440 = gm_l0;
                            gm_b0_1 t441 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t440, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l65)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb84;
                        }
gm_lb85:;
                    }
gm_lb84:;
                    goto gm_lb3;
                }
gm_lb83:;
                {
                    uint8_t t442 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t443 = (uint8_t)(t442 == ((uint8_t)33ULL));
                    if (!t443) {
                        goto gm_lb86;
                    }
                    gm_b16_8 t444 = GM_LD(gm_b16_8, ((((uint8_t *)t148) + 16) + 8));
                    gm_l66 = t444;
                    uint8_t t445 = gm_l2;
                    uint8_t t446 = (uint8_t)!t445;
                    if (t446) {
                        void * t447 = gm_l0;
                        gm_b16_8 t448;
                        GM_ST(void *, (uint8_t *)&t448, (void *)(gm_sjrj6qdrion32));
                        GM_ST(uintptr_t, (uint8_t *)&t448 + 8, (uintptr_t)(34));
                        gm_b0_1 t449 = gm_f_fault_c0tu7v0((void *)NULL, cx, NULL, t447, t448);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t450 = gm_l0;
                    void * t451 = (void *)(((uint8_t *)t450) + 176);
                    uint32_t t452 = GM_LD(uint32_t, ((uint8_t *)gm_ct3dn92ug12));
                    gm_tftb1odc739t5 t453 = gm_f_push_im9d4ro(t451, t452);
                    gm_l96 = t453;
                    uint8_t t454 = (uint8_t)!((((uint8_t *)&gm_l96))[0] != 0);
                    if (!t454) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l96) + 1, gm_srcpafnhrjbrdqq, 161, 13);
                    void * t455 = gm_l0;
                    gm_b16_8 t456 = gm_l66;
                    gm_b16_8 t457 = GM_LD(gm_b16_8, ((uint8_t *)&t456));
                    gm_b0_1 t458 = gm_f_check_list_0iaa8b5((void *)NULL, cx, NULL, t455, t457, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t459 = gm_l0;
                    void * t460 = (void *)(((uint8_t *)t459) + 176);
                    gm_t9805qtonbsag t461 = gm_f_pop_570g79n(t460);
                    goto gm_lb3;
                }
gm_lb86:;
                {
                    uint8_t t462 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t463 = (uint8_t)(t462 == ((uint8_t)34ULL));
                    if (!t463) {
                        goto gm_lb87;
                    }
                    memmove(((uint8_t *)&F.l67), (((uint8_t *)t148) + 16), 48);
                    void * t464 = gm_l0;
                    gm_b0_1 t465 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t464, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l67)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb87:;
                {
                    uint8_t t466 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t467 = (uint8_t)(t466 == ((uint8_t)35ULL));
                    if (!t467) {
                        goto gm_lb88;
                    }
                    memmove(((uint8_t *)&F.l68), (((uint8_t *)t148) + 16), 64);
                    {
                        {
                            uint8_t t468 = (uint8_t)((((uint8_t *)&F.l68))[48] != 0);
                            if (!t468) {
                                goto gm_lb90;
                            }
                            memmove(((uint8_t *)&F.l69), ((uint8_t *)&F.l68), 48);
                            void * t469 = gm_l0;
                            gm_b0_1 t470 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t469, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l69)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb89;
                        }
gm_lb90:;
                    }
gm_lb89:;
                    goto gm_lb3;
                }
gm_lb88:;
            }
            {
                {
                    {
                        {
                            uint8_t t471 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t472 = (uint8_t)(t471 == ((uint8_t)20ULL));
                            if (!t472) {
                                goto gm_lb93;
                            }
                            memmove(((uint8_t *)&F.l70), ((((uint8_t *)t148) + 16) + 16), 48);
                            memmove(((uint8_t *)&F.l71), ((((uint8_t *)t148) + 16) + 64), 48);
                            goto gm_lb92;
                        }
gm_lb93:;
                        {
                            uint8_t t473 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                            uint8_t t474 = (uint8_t)(t473 == ((uint8_t)22ULL));
                            if (!t474) {
                                goto gm_lb94;
                            }
                            memmove(((uint8_t *)&F.l70), ((((uint8_t *)t148) + 16) + 16), 48);
                            memmove(((uint8_t *)&F.l71), ((((uint8_t *)t148) + 16) + 80), 48);
                            goto gm_lb92;
                        }
gm_lb94:;
                        goto gm_lb91;
                    }
gm_lb92:;
                    void * t475 = gm_l0;
                    gm_b0_1 t476 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t475, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l70)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t477 = gm_l0;
                    gm_b0_1 t478 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t477, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l71)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb91:;
                {
                    uint8_t t479 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t480 = (uint8_t)(t479 == ((uint8_t)21ULL));
                    if (!t480) {
                        goto gm_lb95;
                    }
                    memmove(((uint8_t *)&F.l74), ((((uint8_t *)t148) + 16) + 16), 48);
                    void * t481 = gm_l0;
                    gm_b0_1 t482 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t481, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l74)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb95:;
                {
                    uint8_t t483 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t484 = (uint8_t)(t483 == ((uint8_t)36ULL));
                    if (!t484) {
                        goto gm_lb96;
                    }
                    gm_b16_8 t485 = GM_LD(gm_b16_8, (((uint8_t *)t148) + 16));
                    gm_l75 = t485;
                    void * t486 = gm_l0;
                    gm_b16_8 t487 = gm_l75;
                    gm_b16_8 t488 = GM_LD(gm_b16_8, ((uint8_t *)&t487));
                    gm_b0_1 t489 = gm_f_check_list_0iaa8b5((void *)NULL, cx, NULL, t486, t488, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb96:;
                {
                    uint8_t t490 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t491 = (uint8_t)(t490 == ((uint8_t)37ULL));
                    if (!t491) {
                        goto gm_lb97;
                    }
                    uint32_t t492 = GM_LD(uint32_t, (((uint8_t *)t148) + 16));
                    gm_l76 = t492;
                    gm_b16_8 t493 = GM_LD(gm_b16_8, ((((uint8_t *)t148) + 16) + 8));
                    gm_l77 = t493;
                    memmove(((uint8_t *)&F.l78), ((((uint8_t *)t148) + 16) + 32), 64);
                    gm_b16_8 t494 = GM_LD(gm_b16_8, ((((uint8_t *)t148) + 16) + 96));
                    gm_l79 = t494;
                    {
                        {
                            uint8_t t495 = (uint8_t)((((uint8_t *)&F.l78))[48] != 0);
                            if (!t495) {
                                goto gm_lb99;
                            }
                            memmove(((uint8_t *)&F.l80), ((uint8_t *)&F.l78), 48);
                            void * t496 = gm_l0;
                            gm_b0_1 t497 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t496, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l80)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb98;
                        }
gm_lb99:;
                    }
gm_lb98:;
                    void * t498 = gm_l0;
                    uint32_t t499 = gm_l76;
                    gm_b0_1 t500 = gm_f_check_define_00335ri((void *)NULL, cx, NULL, t498, t499);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t501 = gm_l0;
                    void * t502 = (void *)(((uint8_t *)t501) + 176);
                    uint32_t t503 = gm_l76;
                    gm_tftb1odc739t5 t504 = gm_f_push_im9d4ro(t502, t503);
                    gm_l97 = t504;
                    uint8_t t505 = (uint8_t)!((((uint8_t *)&gm_l97))[0] != 0);
                    if (!t505) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l97) + 1, gm_srcpafnhrjbrdqq, 176, 13);
                    void * t506 = gm_l0;
                    void * t507 = gm_l0;
                    uint32_t t508 = GM_LD(uint32_t, (((uint8_t *)t507) + 216));
                    uint32_t r509; bool o510 = __builtin_add_overflow(t508, ((uint32_t)1ULL), &r509);
                    if (o510) gm_trap("integer overflow in `+`", gm_srcpafnhrjbrdqq, 177, 13);
                    uint32_t t511 = r509;
                    GM_ST(uint32_t, (((uint8_t *)t506) + 216), t511);
                    void * t512 = gm_l0;
                    gm_b16_8 t513 = gm_l77;
                    gm_b16_8 t514 = GM_LD(gm_b16_8, ((uint8_t *)&t513));
                    gm_b0_1 t515 = gm_f_check_list_0iaa8b5((void *)NULL, cx, NULL, t512, t514, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t516 = gm_l0;
                    void * t517 = gm_l0;
                    uint32_t t518 = GM_LD(uint32_t, (((uint8_t *)t517) + 216));
                    uint32_t r519; bool o520 = __builtin_sub_overflow(t518, ((uint32_t)1ULL), &r519);
                    if (o520) gm_trap("integer overflow in `-`", gm_srcpafnhrjbrdqq, 179, 13);
                    uint32_t t521 = r519;
                    GM_ST(uint32_t, (((uint8_t *)t516) + 216), t521);
                    void * t522 = gm_l0;
                    gm_b16_8 t523 = gm_l79;
                    gm_b16_8 t524 = GM_LD(gm_b16_8, ((uint8_t *)&t523));
                    gm_b0_1 t525 = gm_f_check_list_0iaa8b5((void *)NULL, cx, NULL, t522, t524, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t526 = gm_l0;
                    void * t527 = (void *)(((uint8_t *)t526) + 176);
                    gm_t9805qtonbsag t528 = gm_f_pop_570g79n(t527);
                    goto gm_lb3;
                }
gm_lb97:;
            }
            {
                {
                    uint8_t t529 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t530 = (uint8_t)(t529 == ((uint8_t)38ULL));
                    if (!t530) {
                        goto gm_lb100;
                    }
                    memmove(((uint8_t *)&F.l81), (((uint8_t *)t148) + 16), 80);
                    void * t531 = gm_l0;
                    uint32_t t532 = GM_LD(uint32_t, (((uint8_t *)t531) + 216));
                    uint8_t t533 = ((t532 == ((uint32_t)0ULL)));
                    if (t533) {
                        void * t534 = gm_l0;
                        gm_b16_8 t535;
                        GM_ST(void *, (uint8_t *)&t535, (void *)(gm_sqbstqdssta1a));
                        GM_ST(uintptr_t, (uint8_t *)&t535 + 8, (uintptr_t)(47));
                        gm_b0_1 t536 = gm_f_fault_c0tu7v0((void *)NULL, cx, NULL, t534, t535);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        {
                            uint8_t t537 = GM_LD(uint8_t, (((uint8_t *)&F.l81) + 0));
                            uint8_t t538 = (uint8_t)(t537 == ((uint8_t)0ULL));
                            if (!t538) {
                                goto gm_lb102;
                            }
                            goto gm_lb101;
                        }
gm_lb102:;
                        {
                            {
                                {
                                    uint8_t t539 = GM_LD(uint8_t, (((uint8_t *)&F.l81) + 0));
                                    uint8_t t540 = (uint8_t)(t539 == ((uint8_t)1ULL));
                                    if (!t540) {
                                        goto gm_lb105;
                                    }
                                    memmove(((uint8_t *)&F.l82), ((((uint8_t *)&F.l81) + 16) + 16), 48);
                                    goto gm_lb104;
                                }
gm_lb105:;
                                {
                                    uint8_t t541 = GM_LD(uint8_t, (((uint8_t *)&F.l81) + 0));
                                    uint8_t t542 = (uint8_t)(t541 == ((uint8_t)2ULL));
                                    if (!t542) {
                                        goto gm_lb106;
                                    }
                                    memmove(((uint8_t *)&F.l82), ((((uint8_t *)&F.l81) + 16) + 16), 48);
                                    goto gm_lb104;
                                }
gm_lb106:;
                                goto gm_lb103;
                            }
gm_lb104:;
                            void * t543 = gm_l0;
                            gm_b0_1 t544 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t543, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l82)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb101;
                        }
gm_lb103:;
                        gm_trap("no match arm matched the scrutinee", gm_srcpafnhrjbrdqq, 185, 13);
                    }
gm_lb101:;
                    goto gm_lb3;
                }
gm_lb100:;
                {
                    uint8_t t545 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t546 = (uint8_t)(t545 == ((uint8_t)39ULL));
                    if (!t546) {
                        goto gm_lb107;
                    }
                    memmove(((uint8_t *)&F.l84), ((((uint8_t *)t148) + 16) + 16), 64);
                    {
                        {
                            uint8_t t547 = (uint8_t)((((uint8_t *)&F.l84))[48] != 0);
                            if (!t547) {
                                goto gm_lb109;
                            }
                            memmove(((uint8_t *)&F.l85), ((uint8_t *)&F.l84), 48);
                            void * t548 = gm_l0;
                            gm_b0_1 t549 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t548, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l85)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb108;
                        }
gm_lb109:;
                    }
gm_lb108:;
                    goto gm_lb3;
                }
gm_lb107:;
                {
                    uint8_t t550 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t551 = (uint8_t)(t550 == ((uint8_t)40ULL));
                    if (!t551) {
                        goto gm_lb110;
                    }
                    memmove(((uint8_t *)&F.l86), (((uint8_t *)t148) + 16), 48);
                    memmove(((uint8_t *)&F.l87), ((((uint8_t *)t148) + 16) + 48), 48);
                    void * t552 = gm_l0;
                    gm_b0_1 t553 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t552, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l86)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t554 = gm_l0;
                    gm_b0_1 t555 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t554, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l87)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb110:;
            }
            {
                {
                    uint8_t t556 = GM_LD(uint8_t, (((uint8_t *)t148) + 0));
                    uint8_t t557 = (uint8_t)(t556 == ((uint8_t)41ULL));
                    if (!t557) {
                        goto gm_lb111;
                    }
                    memmove(((uint8_t *)&F.l88), (((uint8_t *)t148) + 16), 48);
                    void * t558 = gm_l0;
                    gm_b0_1 t559 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t558, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l88)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb3;
                }
gm_lb111:;
                gm_trap("no match arm matched the scrutinee", gm_srcpafnhrjbrdqq, 81, 5);
            }
        }
gm_lb3:;
    }
    {
        F.ret = GM_ZERO(gm_b0_1);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_list_0iaa8b5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_check_list_0iaa8b5 F;
    void * gm_l0; /* v */
    gm_b16_8 gm_l1; /* insts */
    uint8_t gm_l2; /* block_body */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    uint32_t gm_l7; /* t */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint32_t gm_l10; /* t */
    GM_STACK_CHECK(&F, 459u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t9805qtonbsag gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_trqfvmjtt4sij t14;
    gm_f_list_go2qg0b((void *)&t14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t14;
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    gm_l4 = t15;
    {
        for (;;) {
            uint64_t t16 = gm_l5;
            uint64_t t17 = gm_l4;
            uint8_t t18 = (t16 < t17);
            if (!t18) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t19 = gm_l5;
                        gm_l6 = t19;
                        void * t20 = gm_l0;
                        void * t21 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t23 = gm_l6;
                        void * t24 = (void *)(((uint8_t *)t21) + (uintptr_t)t23 * 224);
                        uint8_t t25 = gm_l2;
                        gm_b0_1 t26 = gm_f_check_inst_l48k34t((void *)NULL, cx, NULL, t20, t24, t25);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        {
                            {
                                void * t27 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                uint64_t t29 = gm_l6;
                                void * t30 = (void *)(((uint8_t *)t27) + (uintptr_t)t29 * 224);
                                gm_t9805qtonbsag t31 = gm_f_dst_of_c0rv8d0(t30);
                                gm_l11 = t31;
                                uint8_t t32 = (uint8_t)((((uint8_t *)&gm_l11))[4] != 0);
                                if (!t32) {
                                    goto gm_lb7;
                                }
                                uint32_t t33 = GM_LD(uint32_t, ((uint8_t *)&gm_l11));
                                gm_l7 = t33;
                                uint32_t t34 = gm_l7;
                                uint64_t t35 = (uint64_t)t34;
                                void * t36 = gm_l0;
                                uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)t36) + 96) + 8));
                                uint8_t t38 = (t35 < t37);
                                if (t38) {
                                    void * t39 = gm_l0;
                                    void * t40 = GM_LD(void *, (((uint8_t *)t39) + 96));
                                    uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 96) + 8));
                                    uint32_t t42 = gm_l7;
                                    uint64_t t43 = (uint64_t)t42;
                                    if ((uintptr_t)t43 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t43, t41, gm_srcpafnhrjbrdqq, 63, 17);
                                    GM_ST(uint8_t, (((uint8_t *)t40) + (uintptr_t)t43 * 1), ((uint8_t)1));
                                    void * t44 = (void *)((uint8_t *)&F.l3);
                                    uint32_t t45 = gm_l7;
                                    gm_tftb1odc739t5 t46 = gm_f_push_im9d4ro(t44, t45);
                                    gm_l12 = t46;
                                    uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                    if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcpafnhrjbrdqq, 64, 17);
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpafnhrjbrdqq, 59, 5);
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
    gm_l8 = t52;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t53 = gm_l9;
            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t55 = (t53 < t54);
            if (!t55) {
                goto gm_lb8;
            }
            {
                void * t56 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t58 = gm_l9;
                {
                    {
                        uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)t56) + (uintptr_t)t58 * 4));
                        gm_l10 = t59;
                        void * t60 = gm_l0;
                        void * t61 = GM_LD(void *, (((uint8_t *)t60) + 96));
                        uint64_t t62 = GM_LD(uint64_t, ((((uint8_t *)t60) + 96) + 8));
                        uint32_t t63 = gm_l10;
                        uint64_t t64 = (uint64_t)t63;
                        if ((uintptr_t)t64 >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)t64, t62, gm_srcpafnhrjbrdqq, 68, 27);
                        GM_ST(uint8_t, (((uint8_t *)t61) + (uintptr_t)t64 * 1), ((uint8_t)0));
                        goto gm_lb11;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpafnhrjbrdqq, 68, 5);
                }
gm_lb11:;
            }
gm_lb10:;
            uint64_t t65 = gm_l9;
            uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
            uint64_t t68 = r66;
            gm_l9 = t68;
        }
    }
gm_lb8:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_place_bjmhj7l(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_check_place_bjmhj7l F;
    void * gm_l0; /* v */
    uint32_t gm_l2; /* l */
    uint32_t gm_l3; /* t */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l9;
    {
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)0ULL));
            if (!t22) {
                goto gm_lb2;
            }
            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)a1) + 8));
            gm_l2 = t23;
            uint32_t t24 = gm_l2;
            uint64_t t25 = (uint64_t)t24;
            void * t26 = gm_l0;
            void * t27 = GM_LD(void *, (((uint8_t *)t26) + 8));
            uint64_t t28 = GM_LD(uint64_t, ((((uint8_t *)t27) + 8) + 8));
            uint8_t t29 = (t25 >= t28);
            if (t29) {
                void * t30 = gm_l0;
                uint32_t t31 = gm_l2;
                gm_t88rcucg1b7uk t32;
                GM_ST(uint32_t, ((uint8_t *)&t32 + 0), t31);
                gm_f_format_sbobj14((void *)&gm_l9, (void *)NULL, cx, NULL, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcpafnhrjbrdqq, 235, 62);
                gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
                gm_b0_1 t36 = gm_f_fault_c0tu7v0((void *)NULL, cx, NULL, t30, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)4ULL));
            if (!t38) {
                goto gm_lb3;
            }
            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)a1) + 8));
            gm_l3 = t39;
            void * t40 = gm_l0;
            uint32_t t41 = gm_l3;
            gm_b0_1 t42 = gm_f_check_read_t3fid3k((void *)NULL, cx, NULL, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            {
                {
                    uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t44 = (uint8_t)(t43 == ((uint8_t)1ULL));
                    if (!t44) {
                        goto gm_lb6;
                    }
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t46 = (uint8_t)(t45 == ((uint8_t)2ULL));
                    if (!t46) {
                        goto gm_lb7;
                    }
                    goto gm_lb5;
                }
gm_lb7:;
                {
                    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
                    uint8_t t48 = (uint8_t)(t47 == ((uint8_t)3ULL));
                    if (!t48) {
                        goto gm_lb8;
                    }
                    goto gm_lb5;
                }
gm_lb8:;
                goto gm_lb4;
            }
gm_lb5:;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcpafnhrjbrdqq, 234, 5);
    }
gm_lb1:;
    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 40));
    gm_l4 = t49;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t50 = gm_l5;
            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t52 = (t50 < t51);
            if (!t52) {
                goto gm_lb9;
            }
            {
                void * t53 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t55 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t53) + (uintptr_t)t55 * 80), 80);
                        {
                            {
                                {
                                    {
                                        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                        uint8_t t57 = (uint8_t)(t56 == ((uint8_t)1ULL));
                                        if (!t57) {
                                            goto gm_lb17;
                                        }
                                        memmove(((uint8_t *)&F.l7), (((uint8_t *)&F.l6) + 16), 48);
                                        goto gm_lb16;
                                    }
gm_lb17:;
                                    {
                                        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                        uint8_t t59 = (uint8_t)(t58 == ((uint8_t)2ULL));
                                        if (!t59) {
                                            goto gm_lb18;
                                        }
                                        memmove(((uint8_t *)&F.l7), (((uint8_t *)&F.l6) + 16), 48);
                                        goto gm_lb16;
                                    }
gm_lb18:;
                                    goto gm_lb15;
                                }
gm_lb16:;
                                void * t60 = gm_l0;
                                gm_b0_1 t61 = gm_f_check_value_5jn6ef5((void *)NULL, cx, NULL, t60, &(*(gm_t7b7esi3n9r90 *)((uint8_t *)&F.l7)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb14;
                            }
gm_lb15:;
                            {
                                goto gm_lb14;
                            }
gm_lb19:;
                            gm_trap("no match arm matched the scrutinee", gm_srcpafnhrjbrdqq, 240, 9);
                        }
gm_lb14:;
                        goto gm_lb12;
                    }
gm_lb13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpafnhrjbrdqq, 239, 5);
                }
gm_lb12:;
            }
gm_lb11:;
            uint64_t t62 = gm_l5;
            uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
            uint64_t t65 = r63;
            gm_l5 = t65;
        }
    }
gm_lb9:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_read_t3fid3k(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_check_read_t3fid3k F;
    void * gm_l0; /* v */
    uint32_t gm_l1; /* t */
    GM_STACK_CHECK(&F, 225u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    gm_tabb710d9mq2e gm_l3;
    uint32_t t2 = gm_l1;
    uint64_t t3 = (uint64_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 96) + 8));
    uint8_t t6 = (t3 >= t5);
    if (t6) {
        void * t7 = gm_l0;
        uint32_t t8 = gm_l1;
        gm_t88rcucg1b7uk t9;
        GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t8);
        gm_f_format_akc2snl((void *)&gm_l2, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcpafnhrjbrdqq, 227, 17);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_b0_1 t13 = gm_f_fault_c0tu7v0((void *)NULL, cx, NULL, t7, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 96));
        uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 96) + 8));
        uint32_t t17 = gm_l1;
        uint64_t t18 = (uint64_t)t17;
        if ((uintptr_t)t18 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t18, t16, gm_srcpafnhrjbrdqq, 228, 19);
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t15) + (uintptr_t)t18 * 1));
        uint8_t t20 = (uint8_t)!t19;
        if (t20) {
            void * t21 = gm_l0;
            uint32_t t22 = gm_l1;
            gm_t88rcucg1b7uk t23;
            GM_ST(uint32_t, ((uint8_t *)&t23 + 0), t22);
            gm_f_format_g1tf15c((void *)&gm_l3, (void *)NULL, cx, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcpafnhrjbrdqq, 229, 17);
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            gm_b0_1 t27 = gm_f_fault_c0tu7v0((void *)NULL, cx, NULL, t21, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_target_flebqpd(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_check_target_flebqpd F;
    void * gm_l0; /* v */
    uint32_t gm_l1; /* l */
    uint64_t gm_l2; /* i */
    uint32_t gm_l3; /* x */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l4;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 176) + 8));
    gm_l2 = t6;
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            {
                uint64_t t9 = gm_l2;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcpafnhrjbrdqq, 212, 9);
                uint64_t t12 = r10;
                gm_l2 = t12;
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, (((uint8_t *)t13) + 176));
                uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 176) + 8));
                uint64_t t16 = gm_l2;
                if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcpafnhrjbrdqq, 213, 14);
                uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t14) + (uintptr_t)t16 * 4));
                gm_l3 = t17;
                uint32_t t18 = gm_l3;
                uint32_t t19 = GM_LD(uint32_t, ((uint8_t *)gm_ct3dn92ug12));
                uint8_t t20 = ((t18 == t19));
                if (t20) {
                    goto gm_lb1;
                }
                uint32_t t21 = gm_l3;
                uint32_t t22 = gm_l1;
                uint8_t t23 = ((t21 == t22));
                if (t23) {
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t24 = gm_l0;
    uint32_t t25 = gm_l1;
    gm_t88rcucg1b7uk t26;
    GM_ST(uint32_t, ((uint8_t *)&t26 + 0), t25);
    gm_f_format_fkd5jf8((void *)&gm_l4, (void *)NULL, cx, NULL, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcpafnhrjbrdqq, 217, 13);
    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
    gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
    gm_b0_1 t30 = gm_f_fault_c0tu7v0((void *)NULL, cx, NULL, t24, t29);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_check_value_5jn6ef5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_d_type_g8gg2r0(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_d_type_g8gg2r0 F;
    void * gm_l0; /* v */
    uint32_t gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tkn42u7na3t81 gm_l2;
    uint32_t t2 = gm_l1;
    uint64_t t3 = (uint64_t)t2;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 8));
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 24) + 8));
    uint8_t t7 = (t3 < t6);
    if (t7) {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 8));
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 24));
        uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t9) + 24) + 8));
        uint32_t t12 = gm_l1;
        uint64_t t13 = (uint64_t)t12;
        if ((uintptr_t)t13 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t13, t11, gm_srcpafnhrjbrdqq, 254, 76);
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + (uintptr_t)t13 * 4));
        gm_tkn42u7na3t81 t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[4] = 1;
        gm_l2 = t15;
    } else {
        gm_tkn42u7na3t81 t16;
        memset(&t16, 0, sizeof t16);
        gm_l2 = t16;
    }
    gm_tkn42u7na3t81 t17 = gm_l2;
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fault_c0tu7v0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_run_verifier_g10feug(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_run_verifier_g10feug F;
    void * gm_l0; /* s */
    void * gm_l1; /* f */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 693u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    void * t12 = gm_l0;
    void * t13 = gm_l1;
    gm_trqfvmjtt4sij t14;
    gm_f_list_epvducg((void *)&t14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t15;
    gm_f_list_s83jn2q((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t16;
    gm_f_list_s83jn2q((void *)&t16, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t17;
    gm_f_list_s83jn2q((void *)&t17, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t18;
    gm_f_list_go2qg0b((void *)&t18, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tp47bve3dmstt t19;
    GM_ST(uint32_t, (uint8_t *)&t19 + 220, 0);
    GM_ST(void *, ((uint8_t *)&t19 + 0), t12);
    GM_ST(void *, ((uint8_t *)&t19 + 8), t13);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t19 + 16), t14);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t19 + 56), t15);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t19 + 96), t16);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t19 + 136), t17);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t19 + 176), t18);
    GM_ST(uint32_t, ((uint8_t *)&t19 + 216), ((uint32_t)0ULL));
    F.l2 = t19;
    gm_l4 = ((uint64_t)0ULL);
    void * t20 = gm_l1;
    uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t20) + 24) + 8));
    gm_l3 = t21;
    {
        for (;;) {
            uint64_t t22 = gm_l4;
            uint64_t t23 = gm_l3;
            uint8_t t24 = (t22 < t23);
            if (!t24) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t25 = (void *)(((uint8_t *)&F.l2) + 56);
                        gm_tftb1odc739t5 t26 = gm_f_push_vd1290p(t25, ((uint8_t)0));
                        gm_l7 = t26;
                        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcpafnhrjbrdqq, 23, 9);
                        void * t28 = (void *)(((uint8_t *)&F.l2) + 96);
                        gm_tftb1odc739t5 t29 = gm_f_push_vd1290p(t28, ((uint8_t)0));
                        gm_l8 = t29;
                        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcpafnhrjbrdqq, 24, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpafnhrjbrdqq, 22, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t31 = gm_l4;
            uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
            uint64_t t34 = r32;
            gm_l4 = t34;
        }
    }
gm_lb1:;
    gm_l6 = ((uint32_t)0ULL);
    void * t35 = gm_l1;
    uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)t35) + 64));
    gm_l5 = t36;
    {
        for (;;) {
            uint32_t t37 = gm_l6;
            uint32_t t38 = gm_l5;
            uint8_t t39 = (t37 < t38);
            if (!t39) {
                goto gm_lb6;
            }
            {
                {
                    {
                        void * t40 = (void *)(((uint8_t *)&F.l2) + 136);
                        gm_tftb1odc739t5 t41 = gm_f_push_vd1290p(t40, ((uint8_t)0));
                        gm_l9 = t41;
                        uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcpafnhrjbrdqq, 26, 28);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcpafnhrjbrdqq, 26, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint32_t t43 = gm_l6;
            uint32_t r44; bool o45 = __builtin_add_overflow(t43, ((uint32_t)1ULL), &r44);
            uint32_t t46 = r44;
            gm_l6 = t46;
        }
    }
gm_lb6:;
    void * t47 = (void *)((uint8_t *)&F.l2);
    void * t48 = gm_l1;
    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t48) + 48));
    gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
    gm_b0_1 t51 = gm_f_check_list_0iaa8b5((void *)NULL, cx, NULL, t47, t50, ((uint8_t)1));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 16));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_same_type_8j3dhk8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_same_type_8j3dhk8 F;
    void * gm_l0; /* v */
    gm_b16_8 gm_l1; /* what */
    gm_tkn42u7na3t81 gm_l3; /* t */
    void * gm_l4; /* s */
    gm_t88rcucg1b7uk gm_l5; /* vt */
    gm_t88rcucg1b7uk gm_l6; /* pt */
    GM_STACK_CHECK(&F, 349u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    void * gm_l7;
    gm_t88rcucg1b7uk gm_l8;
    gm_tkn42u7na3t81 gm_l9;
    gm_t88rcucg1b7uk gm_l10;
    gm_tabb710d9mq2e gm_l11;
    void * t2 = gm_l0;
    uint8_t t3 = (uint8_t)(GM_LD(void *, ((uint8_t *)t2)) != 0);
    if (t3) {
        void * t4 = GM_LD(void *, ((uint8_t *)t2));
        gm_l7 = t4;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t5 = gm_l7;
    gm_l4 = t5;
    uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
    if (t6) {
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
        gm_l8 = t7;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_t88rcucg1b7uk t8 = gm_l8;
    gm_l5 = t8;
    void * t9 = gm_l4;
    void * t10 = gm_l0;
    void * t11 = GM_LD(void *, (((uint8_t *)t10) + 8));
    void * t12 = (void *)((uint8_t *)&F.l2);
    gm_tkn42u7na3t81 t13 = gm_f_place_type_5jm3hnm((void *)NULL, cx, NULL, t9, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t13;
    uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l9))[4] != 0);
    if (t14) {
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l9));
        gm_l10 = t15;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_t88rcucg1b7uk t16 = gm_l10;
    gm_l6 = t16;
    gm_t88rcucg1b7uk t17 = gm_l6;
    gm_t88rcucg1b7uk t18 = gm_l5;
    uint8_t t19 = (!gm_eq9m6mvtmgde(((uint8_t *)&t17), ((uint8_t *)&t18)));
    if (t19) {
        void * t20 = gm_l0;
        gm_b16_8 t21 = gm_l1;
        void * t22 = gm_l4;
        gm_t88rcucg1b7uk t23 = gm_l5;
        gm_b16_8 t24 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t25 = gm_l4;
        gm_t88rcucg1b7uk t26 = gm_l6;
        gm_b16_8 t27 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfeab279c16rr t28;
        GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t21);
        GM_ST(gm_b16_8, ((uint8_t *)&t28 + 16), t24);
        GM_ST(gm_b16_8, ((uint8_t *)&t28 + 32), t27);
        gm_f_format_q3eb2dn((void *)&gm_l11, (void *)NULL, cx, NULL, &t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcpafnhrjbrdqq, 261, 31);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
        gm_b0_1 t32 = gm_f_fault_c0tu7v0((void *)NULL, cx, NULL, t20, t31);
        if (gm_unw.kind) {
            goto gm_lad;
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
extern __attribute__((aligned(4))) uint8_t gm_f_typed_5uool0u(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_typed_5uool0u_c(void * a0, gm_t7b7esi3n9r90 a1) { return gm_f_typed_5uool0u(a0, &a1); }
extern __attribute__((aligned(4))) gm_b16_8 gm_f_verify_i7mfe5h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
