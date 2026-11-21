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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; } f1; } gm_t8c5h7u09n10a;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t a[4096]; } gm_tpbv7nqggnvqr;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint64_t f0; uint64_t f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0nqnmillkbs6;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t8c5h7u09n10a) == 16 && _Alignof(gm_t8c5h7u09n10a) == 8 && offsetof(gm_t8c5h7u09n10a, f0) == 0 && offsetof(gm_t8c5h7u09n10a, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; } gm_tk6rvhh54g86p;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_tpbv7nqggnvqr) == 4096 && _Alignof(gm_tpbv7nqggnvqr) == 1 && offsetof(gm_tpbv7nqggnvqr, a) == 0, "layout of `[4096]u8`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0nqnmillkbs6) == 32 && _Alignof(gm_t0nqnmillkbs6) == 8 && offsetof(gm_t0nqnmillkbs6, f0) == 0 && offsetof(gm_t0nqnmillkbs6, f1) == 8 && offsetof(gm_t0nqnmillkbs6, f2) == 16, "layout of `Edge`");
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tk6rvhh54g86p) == 160 && _Alignof(gm_tk6rvhh54g86p) == 8 && offsetof(gm_tk6rvhh54g86p, f0) == 0 && offsetof(gm_tk6rvhh54g86p, f1) == 16 && offsetof(gm_tk6rvhh54g86p, f2) == 32 && offsetof(gm_tk6rvhh54g86p, f3) == 48 && offsetof(gm_tk6rvhh54g86p, f4) == 64 && offsetof(gm_tk6rvhh54g86p, f5) == 80 && offsetof(gm_tk6rvhh54g86p, f6) == 96 && offsetof(gm_tk6rvhh54g86p, f7) == 112 && offsetof(gm_tk6rvhh54g86p, f8) == 128 && offsetof(gm_tk6rvhh54g86p, f9) == 144, "layout of `(string, string, string, string, string, string, string, string, string, string)`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_t08olq97rscn3 f2; } gm_tqtt2php6e8lk;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint64_t f2; void * f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_tc0nbpdhofi37 f7; gm_trqfvmjtt4sij f8; gm_toceffb697p0u f9; gm_trqfvmjtt4sij f10; uint8_t f11; uint8_t pe[7]; } gm_trd6bg8is1v4n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; uint8_t f3; uint8_t f4; uint8_t p5[6]; gm_b16_8 f5; uint64_t f6; gm_trqfvmjtt4sij f7; } gm_tt0nlfbhb3hu6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_trqfvmjtt4sij v1; } f1; } gm_ttevl7jqjigkp;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { uint64_t f0; gm_tl7hpf5nlilqq f1; } gm_tm101l8077enc;
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
typedef struct { uint64_t f0; gm_tt0nlfbhb3hu6 f1; } gm_to8irbp065ot2;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tin97tt4ud0s1 v0; gm_b16_8 v1; gm_b16_8 v2; gm_b16_8 v3; } f1; } gm_toq2f2q4opoa3;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tqtt2php6e8lk) == 56 && _Alignof(gm_tqtt2php6e8lk) == 8 && offsetof(gm_tqtt2php6e8lk, f0) == 0 && offsetof(gm_tqtt2php6e8lk, f1) == 8 && offsetof(gm_tqtt2php6e8lk, f2) == 24, "layout of `Parsed`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_trd6bg8is1v4n) == 288 && _Alignof(gm_trd6bg8is1v4n) == 8 && offsetof(gm_trd6bg8is1v4n, f0) == 0 && offsetof(gm_trd6bg8is1v4n, f1) == 8 && offsetof(gm_trd6bg8is1v4n, f2) == 24 && offsetof(gm_trd6bg8is1v4n, f3) == 32 && offsetof(gm_trd6bg8is1v4n, f4) == 40 && offsetof(gm_trd6bg8is1v4n, f5) == 80 && offsetof(gm_trd6bg8is1v4n, f6) == 120 && offsetof(gm_trd6bg8is1v4n, f7) == 160 && offsetof(gm_trd6bg8is1v4n, f8) == 184 && offsetof(gm_trd6bg8is1v4n, f9) == 224 && offsetof(gm_trd6bg8is1v4n, f10) == 240 && offsetof(gm_trd6bg8is1v4n, f11) == 280, "layout of `Lexer`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
_Static_assert(sizeof(gm_tt0nlfbhb3hu6) == 184 && _Alignof(gm_tt0nlfbhb3hu6) == 8 && offsetof(gm_tt0nlfbhb3hu6, f0) == 0 && offsetof(gm_tt0nlfbhb3hu6, f1) == 16 && offsetof(gm_tt0nlfbhb3hu6, f2) == 56 && offsetof(gm_tt0nlfbhb3hu6, f3) == 112 && offsetof(gm_tt0nlfbhb3hu6, f4) == 113 && offsetof(gm_tt0nlfbhb3hu6, f5) == 120 && offsetof(gm_tt0nlfbhb3hu6, f6) == 136 && offsetof(gm_tt0nlfbhb3hu6, f7) == 144, "layout of `Module`");
_Static_assert(sizeof(gm_ttevl7jqjigkp) == 48 && _Alignof(gm_ttevl7jqjigkp) == 8 && offsetof(gm_ttevl7jqjigkp, f0) == 0 && offsetof(gm_ttevl7jqjigkp, f1) == 8, "layout of `IoError!List(string)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { gm_tin97tt4ud0s1 f0; uint8_t f1; uint8_t pe[7]; } gm_tv37d3dpt2opu;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t f4; uint8_t p5[6]; gm_trqfvmjtt4sij f5; gm_b16_8 f6; gm_t0rfe1ou14h21 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; gm_trqfvmjtt4sij f11; gm_trqfvmjtt4sij f12; gm_trqfvmjtt4sij f13; gm_tf5srh4mp4b3i f14; gm_trqfvmjtt4sij f15; gm_tv37d3dpt2opu f16; void * f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_trqfvmjtt4sij f20; } gm_tcuh1vih4g9cr;
typedef struct { gm_b16_8 f0; gm_toq2f2q4opoa3 f1; gm_th2lqbo77pvsc f2; } gm_te5jk64pjl28k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tjgmtdrk88t7m;
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tm101l8077enc) == 64 && _Alignof(gm_tm101l8077enc) == 8 && offsetof(gm_tm101l8077enc, f0) == 0 && offsetof(gm_tm101l8077enc, f1) == 8, "layout of `(i: usize, x: Fix)`");
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_to8irbp065ot2) == 192 && _Alignof(gm_to8irbp065ot2) == 8 && offsetof(gm_to8irbp065ot2, f0) == 0 && offsetof(gm_to8irbp065ot2, f1) == 8, "layout of `(i: usize, x: Module)`");
_Static_assert(sizeof(gm_toq2f2q4opoa3) == 48 && _Alignof(gm_toq2f2q4opoa3) == 8 && offsetof(gm_toq2f2q4opoa3, f0) == 0 && offsetof(gm_toq2f2q4opoa3, f1) == 8, "layout of `GitSpec`");
_Static_assert(sizeof(gm_tv37d3dpt2opu) == 48 && _Alignof(gm_tv37d3dpt2opu) == 8 && offsetof(gm_tv37d3dpt2opu, f0) == 0 && offsetof(gm_tv37d3dpt2opu, f1) == 40, "layout of `?Resolution`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { gm_t08olq97rscn3 f0; uint64_t f1; gm_tvpnktv5fo8nl f2; gm_t8c5h7u09n10a f3; } gm_t52j636u3vgk3;
_Static_assert(sizeof(gm_tcuh1vih4g9cr) == 552 && _Alignof(gm_tcuh1vih4g9cr) == 8 && offsetof(gm_tcuh1vih4g9cr, f0) == 0 && offsetof(gm_tcuh1vih4g9cr, f1) == 8 && offsetof(gm_tcuh1vih4g9cr, f2) == 16 && offsetof(gm_tcuh1vih4g9cr, f3) == 24 && offsetof(gm_tcuh1vih4g9cr, f4) == 25 && offsetof(gm_tcuh1vih4g9cr, f5) == 32 && offsetof(gm_tcuh1vih4g9cr, f6) == 72 && offsetof(gm_tcuh1vih4g9cr, f7) == 88 && offsetof(gm_tcuh1vih4g9cr, f8) == 152 && offsetof(gm_tcuh1vih4g9cr, f9) == 168 && offsetof(gm_tcuh1vih4g9cr, f10) == 184 && offsetof(gm_tcuh1vih4g9cr, f11) == 200 && offsetof(gm_tcuh1vih4g9cr, f12) == 240 && offsetof(gm_tcuh1vih4g9cr, f13) == 280 && offsetof(gm_tcuh1vih4g9cr, f14) == 320 && offsetof(gm_tcuh1vih4g9cr, f15) == 336 && offsetof(gm_tcuh1vih4g9cr, f16) == 376 && offsetof(gm_tcuh1vih4g9cr, f17) == 424 && offsetof(gm_tcuh1vih4g9cr, f18) == 432 && offsetof(gm_tcuh1vih4g9cr, f19) == 472 && offsetof(gm_tcuh1vih4g9cr, f20) == 512, "layout of `Loader`");
_Static_assert(sizeof(gm_te5jk64pjl28k) == 88 && _Alignof(gm_te5jk64pjl28k) == 8 && offsetof(gm_te5jk64pjl28k, f0) == 0 && offsetof(gm_te5jk64pjl28k, f1) == 16 && offsetof(gm_te5jk64pjl28k, f2) == 64, "layout of `GitSource`");
_Static_assert(sizeof(gm_tjgmtdrk88t7m) == 328 && _Alignof(gm_tjgmtdrk88t7m) == 8 && offsetof(gm_tjgmtdrk88t7m, f0) == 0 && offsetof(gm_tjgmtdrk88t7m, f1) == 8, "layout of `ManifestError!Manifest`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_te5jk64pjl28k v1; gm_tt008mgut8glb v2; } f1; } gm_tnlfbo6r4vlhg;
typedef struct { uint64_t f0; gm_tvpnktv5fo8nl f1; } gm_tp8h583g7s5hd;
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t52j636u3vgk3) == 216 && _Alignof(gm_t52j636u3vgk3) == 8 && offsetof(gm_t52j636u3vgk3, f0) == 0 && offsetof(gm_t52j636u3vgk3, f1) == 32 && offsetof(gm_t52j636u3vgk3, f2) == 40 && offsetof(gm_t52j636u3vgk3, f3) == 200, "layout of `Decl`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_tnlfbo6r4vlhg f2; } gm_t7tiv6vif86qg;
_Static_assert(sizeof(gm_tnlfbo6r4vlhg) == 96 && _Alignof(gm_tnlfbo6r4vlhg) == 8 && offsetof(gm_tnlfbo6r4vlhg, f0) == 0 && offsetof(gm_tnlfbo6r4vlhg, f1) == 8, "layout of `Source`");
_Static_assert(sizeof(gm_tp8h583g7s5hd) == 168 && _Alignof(gm_tp8h583g7s5hd) == 8 && offsetof(gm_tp8h583g7s5hd, f0) == 0 && offsetof(gm_tp8h583g7s5hd, f1) == 8, "layout of `OtherItem`");
_Static_assert(sizeof(gm_t7tiv6vif86qg) == 120 && _Alignof(gm_t7tiv6vif86qg) == 8 && offsetof(gm_t7tiv6vif86qg, f0) == 0 && offsetof(gm_t7tiv6vif86qg, f1) == 16 && offsetof(gm_t7tiv6vif86qg, f2) == 24, "layout of `Dependency`");
struct gm_fr_absolute_path_gj3a0ni {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_member_0jvs7ug {
    gm_frame h;
    gm_tvpnktv5fo8nl l3; /* item */
    uint64_t ret;
};
struct gm_fr_add_module_3cgsegf {
    gm_frame h;
    gm_tvpnktv5fo8nl l9; /* item */
    uint64_t ret;
};
struct gm_fr_add_named_i40bl11 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bad_rest_oc5mkat {
    gm_frame h;
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_canonical_path_q6lq4b6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_canonicalize_n3e3qri {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_check_import_cycles_2u7oqo0 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* stack */
    gm_b0_1 ret;
};
struct gm_fr_collect_item_d4tfq2h {
    gm_frame h;
    gm_tvpnktv5fo8nl l2; /* item */
    gm_tv2uf4leil5hq l3; /* b */
    gm_t4gut2c9fj1kt l6; /* d */
    gm_b0_1 ret;
};
struct gm_fr_collect_whens_ktf37vb {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* queue */
    gm_tvpnktv5fo8nl l4; /* item */
    gm_t7bjbmukv26e7 l5; /* w */
    gm_tvpnktv5fo8nl l9; /* it */
    gm_b0_1 ret;
};
struct gm_fr_contains_c222vll {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_dep_dir_viuo66n {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_deps_of_i4no6j9 {
    gm_frame h;
    gm_tl7hpf5nlilqq l4; /* p */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_dir_gm_files_0s2opd8 {
    gm_frame h;
    gm_b16_8 l0; /* dir */
    gm_trqfvmjtt4sij l1; /* paths */
    gm_ttevl7jqjigkp ret;
    gm_b0_1 w0;
};
struct gm_fr_dump_fugpeqi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_tt0nlfbhb3hu6 l5; /* m */
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_30da7i6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3dleea8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_738b90k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7hq6cu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_aiebmtn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpi80k4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n7sb6c8_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_nhbtm4l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_octbn8r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oq9aajs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pkfv52g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tjobtaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uphdt5g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_vogsd72 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_find_package_root_fs4fuqj {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fn_6gkpg0v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_6gkpg0v_2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_bq6tmqb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_fnpff4s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_m8t719h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_04pispc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8meo3jv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9i45r57 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ajjbrpr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_atch2pp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bl6khrb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cgpq8i6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n6eprd4 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pl7hkg4 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q451tst {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qpmmpss {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t2lplfr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_lljdp3h {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_import_decl_vuv14lf {
    gm_frame h;
    gm_t4gut2c9fj1kt l4; /* e */
    gm_t8c5h7u09n10a ret;
};
struct gm_fr_import_module_8345fv4 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_import_path_any_q8ttsvi {
    gm_frame h;
    gm_tv2uf4leil5hq l1; /* b */
    gm_b16_8 ret;
};
struct gm_fr_import_path_na25mjr {
    gm_frame h;
    gm_tv2uf4leil5hq l1; /* b */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_imports_of_5rkfdia {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_is_dir_6sd3mnh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_import_1c0aicu {
    gm_frame h;
    gm_tv2uf4leil5hq l1; /* b */
    uint8_t ret;
};
struct gm_fr_is_reserved_type_name_tuq4qsj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_lex_dc8mbmf {
    gm_frame h;
    gm_trd6bg8is1v4n l3; /* lx */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_list_016fo30_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0pjrgaq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_12lt7k0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5msmod6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6kkl75k {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_f0gjjbh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g6e4eeu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_j41vvuc {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_j6fu443 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_oo6l3qa {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vdg8k7c {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_load_dir_module_a75lmvr {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* paths */
    gm_trqfvmjtt4sij l10; /* files */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_load_file_rdflf4e {
    gm_frame h;
    gm_t1s75i08qtptd ret;
};
struct gm_fr_load_modules_2vujekg {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_load_package_modules_t4hh6gf {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* dirs */
    gm_b16_8 l9; /* dir */
    gm_trqfvmjtt4sij l10; /* subdirs */
    uint8_t l11; /* readable */
    gm_trqfvmjtt4sij l18; /* files */
    gm_b0_1 ret;
    gm_b0_1 w1;
};
struct gm_fr_load_root_af4ud7b {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* files */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_loader_on0up0a {
    gm_frame h;
    gm_t0rfe1ou14h21 l9; /* <tmp> */
    gm_trqfvmjtt4sij l10; /* <tmp> */
    gm_trqfvmjtt4sij l14; /* <tmp> */
    gm_trqfvmjtt4sij l15; /* <tmp> */
    gm_trqfvmjtt4sij l16; /* <tmp> */
    gm_trqfvmjtt4sij l17; /* <tmp> */
    gm_trqfvmjtt4sij l18; /* <tmp> */
    gm_trqfvmjtt4sij l19; /* <tmp> */
    gm_trqfvmjtt4sij l20; /* <tmp> */
    gm_tcuh1vih4g9cr l21; /* l */
    gm_tcuh1vih4g9cr ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_module_files_8j8gjpm {
    gm_frame h;
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_module_files_with_abkh6r4 {
    gm_frame h;
    gm_tqetrgo0q24b5 l2; /* paths */
    gm_trqfvmjtt4sij l4; /* out */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_overlay_text_haaaurk {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_package_of_hul50on {
    gm_frame h;
    gm_tl7hpf5nlilqq l12; /* p */
    gm_trqfvmjtt4sij l13; /* deps */
    uint64_t ret;
};
struct gm_fr_parse_text_j7gp86k {
    gm_frame h;
    gm_tr8dvlu9ik4gj l4; /* lexed */
    gm_t08olq97rscn3 ret;
};
struct gm_fr_pop_j4rpvm5 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_pop_vr08sii {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_push_1e500nc {
    gm_frame h;
    gm_tvpnktv5fo8nl l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_46vfpsn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_5h5ofgv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_88e5hkq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_89v6mq9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_9oh3uqv {
    gm_frame h;
    gm_tqtt2php6e8lk l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_i535kr1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_n0od5si {
    gm_frame h;
    gm_tl7hpf5nlilqq l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rda4o9n_2 {
    gm_frame h;
    gm_t52j636u3vgk3 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u9lban8 {
    gm_frame h;
    gm_tp8h583g7s5hd l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vhbtmrs {
    gm_frame h;
    gm_tt0nlfbhb3hu6 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_manifest_qr219nu {
    gm_frame h;
    gm_tin97tt4ud0s1 l2; /* res */
    gm_trqfvmjtt4sij l3; /* deps */
    gm_trqfvmjtt4sij l4; /* deps */
    gm_tm43kqe3e8b4a l9; /* manifest */
    gm_t7tiv6vif86qg l12; /* dep */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3iuca1r {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_46rs2i6_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_7vbp7tk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_c2r0eo7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_csardjj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_edp3chb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_flg4i7h {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_isbo8va {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_kqsscod {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o86m6d7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v1tfl0k {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_import_944uqa9 {
    gm_frame h;
    gm_t52j636u3vgk3 l2; /* d */
    gm_tv2uf4leil5hq l3; /* b */
    gm_b0_1 ret;
};
struct gm_fr_reverse_cgmjfos {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_same_import_dav5vf0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_show_khjih0n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_skipped_dir_orfng8m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sort_o401rjn {
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
struct gm_fr_starts_with_ahdikpt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_visit_fs6p4tu {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* names */
    gm_b0_1 ret;
};
struct gm_fr_working_dir_dj7eq93 {
    gm_frame h;
    gm_tpbv7nqggnvqr l0; /* buf */
    gm_b16_8 ret;
};
struct gm_fr_write_2l4ia51 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_gf33g1q {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_otk7ft7 {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqk27942ja0g(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_path_gj3a0ni(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint64_t gm_f_add_member_0jvs7ug(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, const void * a3);
__attribute__((aligned(4))) uint64_t gm_f_add_module_3cgsegf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_named_i40bl11(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) uint32_t gm_f_add_pva1tcq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_bad_rest_oc5mkat(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_canonical_path_q6lq4b6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_import_cycles_2u7oqo0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_item_d4tfq2h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_whens_ktf37vb(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_c222vll(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_dep_dir_viuo66n(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_dep_dir_viuo66n_c(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_deps_of_i4no6j9(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tqetrgo0q24b5 gm_f_deps_of_i4no6j9_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_describe_read_error_uikkr59(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
__attribute__((aligned(4))) void gm_f_dir_gm_files_0s2opd8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_dump_fugpeqi(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_30da7i6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3dleea8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_738b90k(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7hq6cu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_aiebmtn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n7sb6c8_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nhbtm4l(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_octbn8r(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oq9aajs(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pkfv52g(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tjobtaj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uphdt5g(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sbacjg5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_siirbaa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_bytes_25mo226(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_file_stem_v6vrs6i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v_2(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_bq6tmqb(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fnpff4s(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_m8t719h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_format_04pispc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8meo3jv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9i45r57(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ajjbrpr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_atch2pp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_b73b468(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_bl6khrb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cgpq8i6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_pl7hkg4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q451tst(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qpmmpss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_t2lplfr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_host_target_vsu78om(void *gm_out);
__attribute__((aligned(4))) gm_t8c5h7u09n10a gm_f_import_decl_vuv14lf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_import_module_8345fv4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_import_path_any_q8ttsvi(void * a0);
__attribute__((aligned(4))) void gm_f_import_path_na25mjr(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_import_path_na25mjr_c(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_imports_of_5rkfdia(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_s9trr09(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_introsort_6k4m7ds(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_r6p6ckl(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_import_1c0aicu(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) void gm_f_lex_recovering_obo2892(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_016fo30_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0pjrgaq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_12lt7k0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5msmod6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6kkl75k(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g6e4eeu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_j41vvuc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_j6fu443(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oo6l3qa(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vdg8k7c(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_load_dir_module_a75lmvr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_tf5srh4mp4b3i a3, const void * a4);
__attribute__((aligned(4))) void gm_f_load_file_rdflf4e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_load_modules_2vujekg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_load_package_modules_t4hh6gf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_load_root_af4ud7b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_loader_on0up0a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, uint8_t a4);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) void gm_f_module_files_8j8gjpm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_module_files_with_abkh6r4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_bch4n2l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_overlay_text_haaaurk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_package_of_hul50on(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_file_70g7vok(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_text_j7gp86k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_46vfpsn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_5h5ofgv(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_88e5hkq(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_89v6mq9(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9oh3uqv(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_i535kr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_n0od5si(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rda4o9n_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u9lban8(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vhbtmrs(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qoojoqv(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_read_manifest_qr219nu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3iuca1r(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_46rs2i6_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7vbp7tk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_c2r0eo7(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_csardjj(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_edp3chb(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_flg4i7h(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_isbo8va(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kqsscod(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v1tfl0k(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_import_944uqa9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_reverse_cgmjfos(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_reverse_range_kcrihc4(gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_same_import_dav5vf0(void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_khjih0n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_skipped_dir_orfng8m(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_slashes_0qtfkuu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_sort_o401rjn(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_sort_skeree7(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_stnipdi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_vl3t0h4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_visit_fs6p4tu(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_when_cond_9a46a5h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_working_dir_dj7eq93(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2l4ia51(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gf33g1q(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otk7ft7(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s3v41br(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cuqbj62gqhf_3[16] __attribute__((aligned(8)));
extern void *const gm_hl3gh4gojekg[];
extern void *const gm_hle6hel8vh71[];
extern void *const gm_hlrpdhe6kjfr[];
extern uint8_t gm_k5c0o81h2qv[40] __attribute__((aligned(8)));
extern uint8_t gm_kn7qjfrkcl7[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s0ucactj6hj0a[4];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s2c24uim36k06[36];
extern const uint8_t gm_s2urait735ofq[5];
extern const uint8_t gm_s3cv29r83pgn4[6];
extern const uint8_t gm_s5rb1e1n8ivr7[48];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s6lnp3h2ojl1f[77];
extern const uint8_t gm_s79s3h98matgr[6];
extern const uint8_t gm_s86s8nstj5qij[54];
extern const uint8_t gm_s8sv6qdepk5ba[4];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_sa0l93d5jbk7k[69];
extern const uint8_t gm_sad9qq5tf3nqa[36];
extern const uint8_t gm_san68r6np95il[6];
extern const uint8_t gm_sbg03hl2obk72[10];
extern const uint8_t gm_sbgl5dgur8udg[7];
extern const uint8_t gm_sdbvaa960ee0f[5];
extern const uint8_t gm_sdsu0imtl17e3[3];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_se3md9tm534o5[33];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sg9g5p2e4brnh[62];
extern const uint8_t gm_sh7dbt4gdrljc[19];
extern const uint8_t gm_shb5dhf7pftn9[17];
extern const uint8_t gm_shge5n743ua4h[3];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_shqn2dgvujf5o[13];
extern const uint8_t gm_si5knpkeaqca7[137];
extern const uint8_t gm_sjkg27kmogh66[7];
extern const uint8_t gm_sjq0dshsov83m[20];
extern const uint8_t gm_skqb5p67mhjp2[43];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_slkoo2ifkem47[62];
extern const uint8_t gm_slolcr6ikvg33[7];
extern const uint8_t gm_slprvoonnu2j2[5];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snoai0j6d9u36[7];
extern const uint8_t gm_so2pnermu207h[23];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_spq5ja4c7ciga[11];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcne58m9cqp42d[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srv05plrkjhf8[7];
extern const uint8_t gm_st2dnokm1h0jd[5];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_stn0r86gl3sc3[13];
extern const uint8_t gm_strcbt1c3upss[4];
extern const uint8_t gm_su5dv07glhc06[37];
extern const uint8_t gm_svi913vrigpr0[107];
extern const gm_tmpl gm_tpl4mhnbmvg0r80;
extern const gm_tmpl gm_tpl5d3ksa1ntdkn;
extern const gm_tmpl gm_tpl6cs6ch4uvrua;
extern const gm_tmpl gm_tpl9cnobsjbh0r2;
extern const gm_tmpl gm_tpl9p5406jtlms7;
extern const gm_tmpl gm_tplancpnb63qiq5;
extern const gm_tmpl gm_tplaoj5r4tauejk;
extern const gm_tmpl gm_tplbmf4neusqbo5;
extern const gm_tmpl gm_tpld4u3te7g9c5v;
extern const gm_tmpl gm_tplhsmbcv8t80v3;
extern const gm_tmpl gm_tpljucl1tv02je2;
extern const gm_tmpl gm_tploa995k06npbj;
extern const gm_tmpl gm_tplqf2cda88og0i;
extern const gm_tmpl gm_tplrnntb2r7ns40;
extern const gm_tmpl gm_tplt9d80smn1kum;
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
inline bool gm_eqk27942ja0g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_path_gj3a0ni(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_absolute_path_gj3a0ni F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_f_working_dir_dj7eq93((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t3 = gm_l0;
    gm_b16_8 t4 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 40);
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    gm_tbgd14esf0jvi t6;
    GM_ST(uint32_t, (uint8_t *)&t6 + 12, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t6);
    gm_l3 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcgfglnv8p059r, 92, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_bad_rest_oc5mkat(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bad_rest_oc5mkat F;
    gm_b16_8 gm_l0; /* rest */
    gm_b16_8 gm_l1; /* <tmp> */
    GM_STACK_CHECK(&F, 82u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = gm_l0;
    gm_l1 = t3;
    {
        gm_node wn4 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlrpdhe6kjfr, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t7 = gm_l1;
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(1));
        gm_b0_1 t9 = gm_f_split_obtinjv_stnipdi((void *)NULL, (&wn4), NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_ll6;
        }
    }
    goto gm_ls10;
gm_ll6:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we5; }
    goto gm_lad;
gm_ls10:;
gm_lb1:;
we5:;
    F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_c222vll(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_c222vll F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    void * t5 = gm_l0;
    void * t6 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t7 = gm_f_find_ovslcai(t5, t6);
    gm_l3 = t7;
    {
        {
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (t8) {
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
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 173, 12);
    }
gm_lb1:;
    uint8_t t9 = gm_l2;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_30da7i6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_30da7i6 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 168)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3dleea8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3dleea8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_738b90k(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_738b90k F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7hq6cu2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7hq6cu2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_aiebmtn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_aiebmtn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpi80k4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpi80k4 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 160)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n7sb6c8_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n7sb6c8_2 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 216)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nhbtm4l(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_nhbtm4l F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 184)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_octbn8r(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_octbn8r F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oq9aajs(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oq9aajs F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pkfv52g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_pkfv52g F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tjobtaj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tjobtaj F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uphdt5g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uphdt5g F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_vogsd72 F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_0jmnnpk(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 16);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 16)));
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
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_o15fqku F;
    void * gm_l0; /* sm */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srcv1nri2247d2r, 82, 75);
    void * t7 = (void *)(((uint8_t *)t3) + (uintptr_t)t6 * 80);
    F.ret = t7;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_6gkpg0v_2(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_6gkpg0v_2 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
    void * t4 = gm_l1;
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)t4));
    uint8_t t6 = (t3 < t5);
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_04pispc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_04pispc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sh7dbt4gdrljc));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqf2cda88og0i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_8meo3jv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8meo3jv F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so2pnermu207h));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9p5406jtlms7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9i45r57(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_9i45r57 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s86s8nstj5qij));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpljucl1tv02je2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ajjbrpr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ajjbrpr F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_si5knpkeaqca7));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(136));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tploa995k06npbj), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_atch2pp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_atch2pp F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2c24uim36k06));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpld4u3te7g9c5v), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bl6khrb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bl6khrb F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shb5dhf7pftn9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrnntb2r7ns40), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cgpq8i6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cgpq8i6 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5rb1e1n8ivr7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplt9d80smn1kum), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n6eprd4 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sbg03hl2obk72));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_tq3m5qgvkpje8 t3 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
    gm_f_format_b73b468((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbmf4neusqbo5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_pl7hkg4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_pl7hkg4 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sg9g5p2e4brnh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(61));
    gm_tq3m5qgvkpje8 t3 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
    gm_f_format_b73b468((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5d3ksa1ntdkn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q451tst(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q451tst F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_slkoo2ifkem47));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(61));
    gm_tq3m5qgvkpje8 t3 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
    gm_f_format_b73b468((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhsmbcv8t80v3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qpmmpss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qpmmpss F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svi913vrigpr0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(106));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplaoj5r4tauejk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t2lplfr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_t2lplfr F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skqb5p67mhjp2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(42));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4mhnbmvg0r80), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_errors_if7ai0m F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 40));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_lljdp3h(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_lljdp3h F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_7hq6cu2();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_dir_6sd3mnh F;
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
    if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 229, 13);
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
    uint8_t t8 = (uint8_t)((t7 == ((uint8_t)1ULL)));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)1ULL));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_import_1c0aicu(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_import_1c0aicu F;
    void * gm_l0; /* item */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    {
        {
            void * t7 = gm_l0;
            uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t7) + 64) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)0ULL));
            if (!t9) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l1), ((((uint8_t *)t7) + 64) + 8), 64);
            void * t10 = GM_LD(void *, (((uint8_t *)&F.l1) + 40));
            {
                {
                    uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
                    uint8_t t12 = (uint8_t)(t11 == ((uint8_t)38ULL));
                    if (!t12) {
                        goto gm_lb4;
                    }
                    gm_l2 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l2 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 214, 12);
            }
gm_lb3:;
            uint8_t t13 = gm_l2;
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
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_lex_dc8mbmf F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    GM_STACK_CHECK(&F, 352u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    void * t4 = gm_l2;
    gm_trd6bg8is1v4n t5;
    gm_f_new_lexer_dm12g12((void *)&t5, (void *)NULL, cx, NULL, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t5;
    void * t6 = (void *)((uint8_t *)&F.l3);
    gm_f_run_v26is74((void *)&(*(gm_tr8dvlu9ik4gj *)gm_out), (void *)NULL, cx, NULL, t6);
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
inline __attribute__((aligned(4))) void gm_f_list_016fo30_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_016fo30_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n7sb6c8_2();
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
inline __attribute__((aligned(4))) void gm_f_list_0pjrgaq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0pjrgaq F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_pkfv52g();
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
inline __attribute__((aligned(4))) void gm_f_list_12lt7k0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_12lt7k0 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_3dleea8();
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
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5dhgg4i F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_73v4ttb();
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
inline __attribute__((aligned(4))) void gm_f_list_5msmod6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5msmod6 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_nhbtm4l();
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
inline __attribute__((aligned(4))) void gm_f_list_6kkl75k(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6kkl75k F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_738b90k();
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
inline __attribute__((aligned(4))) void gm_f_list_f0gjjbh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_f0gjjbh F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lpi80k4();
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
inline __attribute__((aligned(4))) void gm_f_list_g6e4eeu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g6e4eeu F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_30da7i6();
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
inline __attribute__((aligned(4))) void gm_f_list_j41vvuc(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_j41vvuc F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_oq9aajs();
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
inline __attribute__((aligned(4))) void gm_f_list_j6fu443(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_j6fu443 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_uphdt5g();
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
inline __attribute__((aligned(4))) void gm_f_list_oo6l3qa(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oo6l3qa F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_aiebmtn();
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
inline __attribute__((aligned(4))) void gm_f_list_vdg8k7c(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vdg8k7c F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_tjobtaj();
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 200));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 200) + 8));
    uint64_t t5 = gm_l1;
    if ((uintptr_t)t5 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t5, t4, gm_srcc2qutubarto1, 547, 78);
    void * t6 = (void *)((((uint8_t *)t3) + (uintptr_t)t5 * 184) + 56);
    gm_b16_8 t7 = gm_l2;
    gm_tf5srh4mp4b3i t8 = gm_f_get_espvf96(t6, t7);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_j4rpvm5 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* x */
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
        gm_tf5srh4mp4b3i t6;
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
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t8) + (uintptr_t)t13 * 8));
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
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t15), t26);
    uint64_t t27 = gm_l2;
    gm_tf5srh4mp4b3i t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t28 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[8] = 1;
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_vr08sii F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* x */
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
        gm_th2lqbo77pvsc t6;
        memset(&t6, 0, sizeof t6);
        memmove(gm_out, ((uint8_t *)&t6), 24);
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
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t8) + (uintptr_t)t13 * 16));
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
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t15), t26);
    gm_b16_8 t27 = gm_l2;
    gm_th2lqbo77pvsc t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t28 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t28), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1e500nc(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1e500nc F;
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
        gm_b16_8 t9 = gm_f_empty_slice_lpi80k4();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_o86m6d7(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 160), ((uint8_t *)&F.l1), 160);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 160)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_46vfpsn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_46vfpsn F;
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
        gm_b16_8 t9 = gm_f_empty_slice_3dleea8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_csardjj(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_5h5ofgv(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_5h5ofgv F;
    void * gm_l0; /* l */
    gm_t08olq97rscn3 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_uphdt5g();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_flg4i7h(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_88e5hkq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_88e5hkq F;
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
        gm_b16_8 t9 = gm_f_empty_slice_aiebmtn();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_v1tfl0k(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_89v6mq9(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_89v6mq9 F;
    void * gm_l0; /* l */
    gm_t0nqnmillkbs6 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_738b90k();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_c2r0eo7(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9oh3uqv(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_9oh3uqv F;
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
        gm_b16_8 t9 = gm_f_empty_slice_octbn8r();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_kqsscod(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_i535kr1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_i535kr1 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_pkfv52g();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_isbo8va(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kiclmcn F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_jbu9fo3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8skjmj4(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_n0od5si(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_n0od5si F;
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
        gm_b16_8 t9 = gm_f_empty_slice_tjobtaj();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_edp3chb(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rda4o9n_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rda4o9n_2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_n7sb6c8_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_46rs2i6_2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 216), ((uint8_t *)a1), 216);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 216)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u9lban8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u9lban8 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_30da7i6();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_3iuca1r(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 168), ((uint8_t *)a1), 168);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 168)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ucgmlo2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_73v4ttb();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_k4j2gj3(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vhbtmrs(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vhbtmrs F;
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
        gm_b16_8 t9 = gm_f_empty_slice_nhbtm4l();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_7vbp7tk(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 184), ((uint8_t *)a1), 184);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 184)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3iuca1r(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3iuca1r F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)168ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_46rs2i6_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_46rs2i6_2 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)216ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7vbp7tk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_7vbp7tk F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)184ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_c2r0eo7(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_c2r0eo7 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_csardjj(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_csardjj F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_edp3chb(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_edp3chb F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_flg4i7h(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_flg4i7h F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_isbo8va(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_isbo8va F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_k4j2gj3 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kqsscod(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_kqsscod F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o86m6d7(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o86m6d7 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)160ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v1tfl0k(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_v1tfl0k F;
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
inline __attribute__((aligned(4))) void gm_f_reverse_cgmjfos(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reverse_cgmjfos F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_f_reverse_range_kcrihc4(t2, ((uint64_t)0ULL), t3);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_skipped_dir_orfng8m(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skipped_dir_orfng8m F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl3q52ep6aa53));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(1));
    uint8_t t4 = gm_f_starts_with_6vvt01e(t2, t3);
    if (t4) {
        gm_l2 = ((uint8_t)1);
    } else {
        gm_b16_8 t5 = gm_l0;
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_snoai0j6d9u36));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(6));
        uint8_t t7 = (gm_eq0iqiubfm7e(((uint8_t *)&t5), ((uint8_t *)&t6)));
        gm_l2 = t7;
    }
    uint8_t t8 = gm_l2;
    if (t8) {
        gm_l1 = ((uint8_t)1);
    } else {
        gm_b16_8 t9 = gm_l0;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_shqn2dgvujf5o));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(12));
        uint8_t t11 = (gm_eq0iqiubfm7e(((uint8_t *)&t9), ((uint8_t *)&t10)));
        gm_l1 = t11;
    }
    uint8_t t12 = gm_l1;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sort_o401rjn(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_o401rjn F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = ((gm_closure){GM_PTR_TAG(gm_f_fn_6gkpg0v_2), NULL, cx});
    gm_f_introsort_6k4m7ds(t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_trim_start_179crak(t2);
    gm_b16_8 t4 = gm_f_trim_end_6sjv6rg(t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2l4ia51(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_2l4ia51 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_slprvoonnu2j2));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(4));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl9cnobsjbh0r2), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gf33g1q(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_gf33g1q F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_stn0r86gl3sc3));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(12));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl6cs6ch4uvrua), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otk7ft7(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_otk7ft7 F;
    void * gm_l0; /* l */
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 39u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se3md9tm534o5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
    gm_tftb1odc739t5 t4 = gm_f_write_s3v41br(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplancpnb63qiq5), t3);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_path_gj3a0ni(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_add_member_0jvs7ug(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, const void * a3) {
    struct gm_fr_add_member_0jvs7ug F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    uint64_t gm_l4; /* id */
    GM_STACK_CHECK(&F, 597u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l5;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 240);
    uint64_t t4 = gm_l1;
    gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a2));
    gm_tvpnktv5fo8nl t6 = GM_LD(gm_tvpnktv5fo8nl, ((uint8_t *)a3));
    gm_t8c5h7u09n10a t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)0ULL));
    gm_t52j636u3vgk3 t8;
    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t8 + 0), t5);
    GM_ST(uint64_t, ((uint8_t *)&t8 + 32), t4);
    GM_ST(gm_tvpnktv5fo8nl, ((uint8_t *)&t8 + 40), t6);
    GM_ST(gm_t8c5h7u09n10a, ((uint8_t *)&t8 + 200), t7);
    gm_tftb1odc739t5 t9 = gm_f_push_rda4o9n_2(t3, &t8);
    gm_l5 = t9;
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcc2qutubarto1, 813, 5);
    void * t11 = gm_l0;
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t11) + 240) + 8));
    uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
    if (o14) gm_trap("integer overflow in `-`", gm_srcc2qutubarto1, 814, 11);
    uint64_t t15 = r13;
    gm_l4 = t15;
    void * t16 = gm_l0;
    uint64_t t17 = gm_l1;
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)a2));
    uint64_t t19 = gm_l4;
    gm_b0_1 t20 = gm_f_add_named_i40bl11((void *)NULL, cx, NULL, t16, t17, t18, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t21 = gm_l4;
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
inline __attribute__((aligned(4))) uint64_t gm_f_add_module_3cgsegf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_add_module_3cgsegf F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* files */
    uint64_t gm_l3; /* id */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t08olq97rscn3 gm_l6; /* file */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 752u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l10;
    uint8_t gm_l11;
    gm_tftb1odc739t5 gm_l12;
    void * t15 = gm_l0;
    uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t15) + 200) + 8));
    gm_l3 = t16;
    void * t17 = gm_l0;
    void * t18 = (void *)(((uint8_t *)t17) + 200);
    gm_b16_8 t19 = gm_l1;
    gm_trqfvmjtt4sij t20;
    gm_f_list_oo6l3qa((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t21;
    gm_f_hash_map_lljdp3h((void *)&t21, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t22;
    GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(0));
    gm_trqfvmjtt4sij t23;
    gm_f_list_f0gjjbh((void *)&t23, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tt0nlfbhb3hu6 t24;
    GM_ST(uint16_t, (uint8_t *)&t24 + 114, 0);
    GM_ST(uint32_t, (uint8_t *)&t24 + 116, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t19);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t24 + 16), t20);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t24 + 56), t21);
    GM_ST(uint8_t, ((uint8_t *)&t24 + 112), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t24 + 113), ((uint8_t)0));
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 120), t22);
    GM_ST(uint64_t, ((uint8_t *)&t24 + 136), ((uint64_t)0ULL));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t24 + 144), t23);
    gm_tftb1odc739t5 t25 = gm_f_push_vhbtmrs(t18, &t24);
    gm_l10 = t25;
    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcc2qutubarto1, 459, 5);
    gm_b16_8 t27 = gm_l2;
    gm_l4 = t27;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t28 = gm_l5;
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t30 = (t28 < t29);
            if (!t30) {
                goto gm_lb1;
            }
            {
                void * t31 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t33 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&gm_l6), (((uint8_t *)t31) + (uintptr_t)t33 * 32), 32);
                        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                        gm_l7 = t34;
                        gm_l8 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t35 = gm_l8;
                                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                uint8_t t37 = (t35 < t36);
                                if (!t37) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t38 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                    uint64_t t40 = gm_l8;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l9), (((uint8_t *)t38) + (uintptr_t)t40 * 160), 160);
                                            {
                                                {
                                                    uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)&F.l9) + 64) + 0));
                                                    uint8_t t42 = (uint8_t)(t41 == ((uint8_t)1ULL));
                                                    if (!t42) {
                                                        goto gm_lb12;
                                                    }
                                                    gm_l11 = ((uint8_t)1);
                                                    goto gm_lb11;
                                                }
gm_lb12:;
                                                {
                                                    gm_l11 = ((uint8_t)0);
                                                    goto gm_lb11;
                                                }
gm_lb13:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 471, 16);
                                            }
gm_lb11:;
                                            uint8_t t43 = gm_l11;
                                            if (t43) {
                                                void * t44 = gm_l0;
                                                void * t45 = GM_LD(void *, (((uint8_t *)t44) + 200));
                                                uint64_t t46 = GM_LD(uint64_t, ((((uint8_t *)t44) + 200) + 8));
                                                uint64_t t47 = gm_l3;
                                                if ((uintptr_t)t47 >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)t47, t46, gm_srcc2qutubarto1, 472, 17);
                                                void * t48 = (void *)((((uint8_t *)t45) + (uintptr_t)t47 * 184) + 144);
                                                gm_tftb1odc739t5 t49 = gm_f_push_1e500nc(t48, &(*(gm_tvpnktv5fo8nl *)((uint8_t *)&F.l9)));
                                                gm_l12 = t49;
                                                uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                                if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcc2qutubarto1, 472, 17);
                                            } else {
                                                void * t51 = gm_l0;
                                                uint64_t t52 = gm_l3;
                                                gm_b0_1 t53 = gm_f_collect_item_d4tfq2h((void *)NULL, cx, NULL, t51, t52, &(*(gm_tvpnktv5fo8nl *)((uint8_t *)&F.l9)));
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 470, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t54 = gm_l8;
                                uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                                uint64_t t57 = r55;
                                gm_l8 = t57;
                            }
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 469, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t58 = gm_l5;
            uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
            uint64_t t61 = r59;
            gm_l5 = t61;
        }
    }
gm_lb1:;
    uint64_t t62 = gm_l3;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_named_i40bl11(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3) {
    struct gm_fr_add_named_i40bl11 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    uint64_t gm_l3; /* decl */
    void * gm_l4; /* m */
    GM_STACK_CHECK(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 200));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 200) + 8));
    uint64_t t5 = gm_l1;
    if ((uintptr_t)t5 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t5, t4, gm_srcc2qutubarto1, 540, 11);
    void * t6 = (void *)(((uint8_t *)t3) + (uintptr_t)t5 * 184);
    gm_l4 = t6;
    void * t7 = gm_l4;
    void * t8 = (void *)(((uint8_t *)t7) + 16);
    gm_b16_8 t9 = gm_l2;
    uint64_t t10 = gm_l3;
    gm_t3877pk7lv748 t11;
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t9);
    GM_ST(uint64_t, ((uint8_t *)&t11 + 16), t10);
    gm_tftb1odc739t5 t12 = gm_f_push_88e5hkq(t8, &t11);
    gm_l5 = t12;
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcc2qutubarto1, 541, 5);
    void * t14 = gm_l4;
    void * t15 = (void *)(((uint8_t *)t14) + 56);
    gm_b16_8 t16 = gm_l2;
    uint8_t t17 = gm_f_contains_c222vll(t15, t16);
    uint8_t t18 = (uint8_t)!t17;
    if (t18) {
        void * t19 = gm_l4;
        void * t20 = (void *)(((uint8_t *)t19) + 56);
        gm_b16_8 t21 = gm_l2;
        uint64_t t22 = gm_l3;
        gm_tftb1odc739t5 t23 = gm_f_put_qoojoqv(t20, t21, t22);
        gm_l6 = t23;
        uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcc2qutubarto1, 542, 37);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_bad_rest_oc5mkat(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_canonical_path_q6lq4b6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonical_path_q6lq4b6 F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* c */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* dir */
    gm_b16_8 gm_l4; /* c */
    GM_STACK_CHECK(&F, 541u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l5;
    gm_th2lqbo77pvsc gm_l6;
    gm_b16_8 gm_l7;
    gm_th2lqbo77pvsc gm_l8;
    gm_b16_8 gm_l9;
    gm_th2lqbo77pvsc gm_l10;
    {
        {
            gm_b16_8 t6 = gm_l0;
            gm_f_canonicalize_n3e3qri((void *)&gm_l5, (void *)NULL, cx, NULL, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
            gm_l1 = t8;
            gm_b16_8 t9 = gm_l1;
            F.ret = t9;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t10 = gm_l0;
    gm_f_file_name_em7v8lv((void *)&gm_l6, (void *)NULL, cx, NULL, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
    if (t11) {
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
        gm_l7 = t12;
    } else {
        gm_b16_8 t13 = gm_l0;
        F.ret = t13;
        goto gm_fret;
    }
    gm_b16_8 t14 = gm_l7;
    gm_l2 = t14;
    gm_b16_8 t15 = gm_l0;
    gm_f_parent_lk0iero((void *)&gm_l8, (void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l8))[16] != 0);
    if (t16) {
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
        gm_l9 = t17;
    } else {
        gm_b16_8 t18 = gm_l0;
        F.ret = t18;
        goto gm_fret;
    }
    gm_b16_8 t19 = gm_l9;
    gm_l3 = t19;
    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t21 = ((t20 == ((uint64_t)0ULL)));
    if (t21) {
        gm_b16_8 t22 = gm_l0;
        F.ret = t22;
        goto gm_fret;
    }
    {
        {
            gm_b16_8 t23 = gm_l3;
            gm_f_canonicalize_n3e3qri((void *)&gm_l10, (void *)NULL, cx, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
            if (!t24) {
                goto gm_lb4;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
            gm_l4 = t25;
            gm_b16_8 t26 = gm_l4;
            gm_b16_8 t27 = gm_l2;
            gm_b16_8 t28 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t28;
            goto gm_fret;
        }
gm_lb4:;
    }
gm_lb3:;
    gm_b16_8 t29 = gm_l0;
    F.ret = t29;
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
inline __attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonicalize_n3e3qri F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 195u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    gm_tih6h4avv5nrb gm_l2;
    gm_b16_8 t5 = gm_l0;
    gm_node *pn6 = gm_find(cx, 2126215853);
    if (!pn6) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 241, 8);
    gm_tih6h4avv5nrb t7;
    ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn6->clauses[5])((void *)&t7, pn6->env, pn6->hctx, pn6, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t7;
    {
        {
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
            if (t8) {
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
        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 241, 8);
    }
gm_lb1:;
    uint8_t t9 = gm_l1;
    if (t9) {
        gm_th2lqbo77pvsc t10;
        memset(&t10, 0, sizeof t10);
        memmove(gm_out, ((uint8_t *)&t10), 24);
        goto gm_fdone;
    }
    gm_b16_8 t11 = gm_l0;
    gm_b16_8 t12 = gm_f_absolute_path_gj3a0ni((void *)NULL, cx, NULL, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_th2lqbo77pvsc t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t13 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t13), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_import_cycles_2u7oqo0(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_import_cycles_2u7oqo0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* state */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* start */
    GM_STACK_CHECK(&F, 292u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l7;
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 200) + 8));
    gm_l1 = t8;
    uint64_t t9 = gm_l1;
    gm_f_make_2ip78bj((void *)&gm_l7, (void *)NULL, cx, NULL, t9, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k5c0o81h2qv)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcc2qutubarto1, 737, 14);
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
    gm_l2 = t11;
    gm_b16_8 t12 = gm_l2;
    gm_f_fill_abmf5g5(t12, ((uint8_t)0ULL));
    gm_trqfvmjtt4sij t13;
    gm_f_list_5dhgg4i((void *)&t13, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t13;
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t14 = gm_l1;
    gm_l4 = t14;
    {
        for (;;) {
            uint64_t t15 = gm_l5;
            uint64_t t16 = gm_l4;
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t18 = gm_l5;
                        gm_l6 = t18;
                        void * t19 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t21 = gm_l6;
                        if ((uintptr_t)t21 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t21, t20, gm_srcc2qutubarto1, 741, 12);
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)t19) + (uintptr_t)t21 * 1));
                        uint8_t t23 = ((t22 == ((uint8_t)0ULL)));
                        if (t23) {
                            void * t24 = gm_l0;
                            uint64_t t25 = gm_l6;
                            gm_b16_8 t26 = gm_l2;
                            void * t27 = (void *)((uint8_t *)&F.l3);
                            gm_b0_1 t28 = gm_f_visit_fs6p4tu((void *)NULL, cx, NULL, t24, t25, t26, t27);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 740, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t29 = gm_l5;
            uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
            uint64_t t32 = r30;
            gm_l5 = t32;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_item_d4tfq2h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2) {
    struct gm_fr_collect_item_d4tfq2h F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l4; /* name */
    uint64_t gm_l5; /* prev */
    GM_STACK_CHECK(&F, 1671u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_tabb710d9mq2e gm_l7;
    gm_tf5srh4mp4b3i gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)&F.l2) + 64) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l3), ((((uint8_t *)&F.l2) + 64) + 8), 64);
            uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 8) + 8));
            uint8_t t9 = (!(t8 == ((uint64_t)1ULL)));
            if (t9) {
                void * t10 = gm_l0;
                void * t11 = GM_LD(void *, (((uint8_t *)t10) + 8));
                gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 48));
                gm_b16_8 t13;
                GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sa0l93d5jbk7k));
                GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(68));
                gm_b0_1 t14 = gm_f_report_eardf07((void *)NULL, cx, NULL, t11, t12, t13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            void * t15 = GM_LD(void *, (((uint8_t *)&F.l3) + 8));
            uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)&F.l3) + 8) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t16, gm_srcc2qutubarto1, 516, 13);
            memmove(((uint8_t *)&gm_l4), (((uint8_t *)t15) + (uintptr_t)((uint64_t)0ULL) * 32), 32);
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
            uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
            if (t19) {
                void * t20 = gm_l0;
                void * t21 = GM_LD(void *, (((uint8_t *)t20) + 8));
                gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                gm_b16_8 t23;
                GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sad9qq5tf3nqa));
                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(35));
                gm_b0_1 t24 = gm_f_report_eardf07((void *)NULL, cx, NULL, t21, t22, t23);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            uint8_t t26 = gm_f_is_reserved_type_name_tuq4qsj(t25);
            if (t26) {
                void * t27 = gm_l0;
                void * t28 = GM_LD(void *, (((uint8_t *)t27) + 8));
                gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                gm_tnpifv20irnph t31;
                GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
                gm_f_format_9i45r57((void *)&gm_l7, (void *)NULL, cx, NULL, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcc2qutubarto1, 522, 35);
                gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                gm_b0_1 t35 = gm_f_report_eardf07((void *)NULL, cx, NULL, t28, t29, t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                {
                    void * t36 = gm_l0;
                    uint64_t t37 = gm_l1;
                    gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    gm_tf5srh4mp4b3i t39 = gm_f_lookup_59juqoo(t36, t37, t38);
                    gm_l8 = t39;
                    uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                    if (!t40) {
                        goto gm_lb4;
                    }
                    uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
                    gm_l5 = t41;
                    void * t42 = gm_l0;
                    uint64_t t43 = gm_l5;
                    void * t44 = (void *)((uint8_t *)&F.l2);
                    uint8_t t45 = gm_f_same_import_dav5vf0(t42, t43, t44);
                    if (t45) {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 16));
                    gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                    gm_tnpifv20irnph t48;
                    GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
                    gm_f_format_8meo3jv((void *)&gm_l9, (void *)NULL, cx, NULL, t48);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                    if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcc2qutubarto1, 529, 36);
                    gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                    gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                    gm_t4gut2c9fj1kt t52;
                    gm_f_error_s6hn871((void *)&t52, (void *)NULL, cx, NULL, t46, t51);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l6 = t52;
                    void * t53 = (void *)((uint8_t *)&F.l6);
                    void * t54 = gm_l0;
                    void * t55 = GM_LD(void *, (((uint8_t *)t54) + 240));
                    uint64_t t56 = GM_LD(uint64_t, ((((uint8_t *)t54) + 240) + 8));
                    uint64_t t57 = gm_l5;
                    if ((uintptr_t)t57 >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)t57, t56, gm_srcc2qutubarto1, 530, 21);
                    gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t55) + (uintptr_t)t57 * 216) + 16));
                    gm_b16_8 t59;
                    GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sjq0dshsov83m));
                    GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(19));
                    gm_f_add_label_50fj7vv(t53, GM_CONV(gm_cseahvg8v7p0vs, t58), t59);
                    void * t60 = gm_l0;
                    void * t61 = GM_LD(void *, (((uint8_t *)t60) + 8));
                    gm_f_push_o4ckdr1(t61, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l6)));
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            void * t62 = gm_l0;
            void * t63 = (void *)(((uint8_t *)t62) + 240);
            uint64_t t64 = gm_l1;
            gm_t08olq97rscn3 t65 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l4));
            gm_tvpnktv5fo8nl t66 = GM_LD(gm_tvpnktv5fo8nl, ((uint8_t *)&F.l2));
            gm_t8c5h7u09n10a t67;
            GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t67 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)0ULL));
            gm_t52j636u3vgk3 t68;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t68 + 0), t65);
            GM_ST(uint64_t, ((uint8_t *)&t68 + 32), t64);
            GM_ST(gm_tvpnktv5fo8nl, ((uint8_t *)&t68 + 40), t66);
            GM_ST(gm_t8c5h7u09n10a, ((uint8_t *)&t68 + 200), t67);
            gm_tftb1odc739t5 t69 = gm_f_push_rda4o9n_2(t63, &t68);
            gm_l10 = t69;
            uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcc2qutubarto1, 534, 5);
            void * t71 = gm_l0;
            uint64_t t72 = gm_l1;
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            void * t74 = gm_l0;
            uint64_t t75 = GM_LD(uint64_t, ((((uint8_t *)t74) + 240) + 8));
            uint64_t r76; bool o77 = __builtin_sub_overflow(t75, ((uint64_t)1ULL), &r76);
            if (o77) gm_trap("integer overflow in `-`", gm_srcc2qutubarto1, 535, 36);
            uint64_t t78 = r76;
            gm_b0_1 t79 = gm_f_add_named_i40bl11((void *)NULL, cx, NULL, t71, t72, t73, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        void * t80 = gm_l0;
        void * t81 = (void *)(((uint8_t *)t80) + 280);
        uint64_t t82 = gm_l1;
        gm_tvpnktv5fo8nl t83 = GM_LD(gm_tvpnktv5fo8nl, ((uint8_t *)&F.l2));
        gm_tp8h583g7s5hd t84;
        GM_ST(uint64_t, ((uint8_t *)&t84 + 0), t82);
        GM_ST(gm_tvpnktv5fo8nl, ((uint8_t *)&t84 + 8), t83);
        gm_tftb1odc739t5 t85 = gm_f_push_u9lban8(t81, &t84);
        gm_l11 = t85;
        uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcc2qutubarto1, 509, 9);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_whens_ktf37vb(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_collect_whens_ktf37vb F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* id */
    uint64_t gm_l3; /* i */
    gm_b16_8 gm_l6; /* branch */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 513u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l10;
    gm_b16_8 gm_l11;
    uint8_t gm_l12;
    gm_tftb1odc739t5 gm_l13;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, (((uint8_t *)t19) + 200));
    uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t19) + 200) + 8));
    uint64_t t22 = gm_l1;
    if ((uintptr_t)t22 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t22, t21, gm_srcc2qutubarto1, 484, 14);
    memmove(((uint8_t *)&F.l2), ((((uint8_t *)t20) + (uintptr_t)t22 * 184) + 144), 40);
    void * t23 = gm_l0;
    void * t24 = GM_LD(void *, (((uint8_t *)t23) + 200));
    uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t23) + 200) + 8));
    uint64_t t26 = gm_l1;
    if ((uintptr_t)t26 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t26, t25, gm_srcc2qutubarto1, 485, 5);
    gm_trqfvmjtt4sij t27;
    gm_f_list_f0gjjbh((void *)&t27, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, ((((uint8_t *)t24) + (uintptr_t)t26 * 184) + 144), t27);
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t28 = gm_l3;
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
            uint8_t t30 = (t28 < t29);
            if (!t30) {
                goto gm_lb1;
            }
            {
                void * t31 = GM_LD(void *, ((uint8_t *)&F.l2));
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                uint64_t t33 = gm_l3;
                if ((uintptr_t)t33 >= (uintptr_t)(t32)) gm_trap_index((uintptr_t)t33, t32, gm_srcc2qutubarto1, 488, 17);
                memmove(((uint8_t *)&F.l4), (((uint8_t *)t31) + (uintptr_t)t33 * 160), 160);
                uint64_t t34 = gm_l3;
                uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                if (o36) gm_trap("integer overflow in `+`", gm_srcc2qutubarto1, 489, 9);
                uint64_t t37 = r35;
                gm_l3 = t37;
                {
                    {
                        uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 64) + 0));
                        uint8_t t39 = (uint8_t)(t38 == ((uint8_t)1ULL));
                        if (!t39) {
                            goto gm_lb5;
                        }
                        memmove(((uint8_t *)&F.l5), ((((uint8_t *)&F.l4) + 64) + 8), 64);
                        gm_b16_8 t40 = gm_f_empty_slice_lpi80k4();
                        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                        gm_l6 = t41;
                        void * t42 = gm_l0;
                        uint64_t t43 = gm_l1;
                        void * t44 = GM_LD(void *, ((uint8_t *)&F.l5));
                        gm_tqs5p4n2tq19v t45 = gm_f_when_cond_9a46a5h((void *)NULL, cx, NULL, t42, t43, t44);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l10 = t45;
                        {
                            {
                                uint8_t t46 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
                                if (!t46) {
                                    goto gm_lb7;
                                }
                                uint8_t t47 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
                                uint8_t t48 = ((t47 == ((uint8_t)1)));
                                if (!t48) {
                                    goto gm_lb7;
                                }
                                gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 8));
                                gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                                gm_l6 = t50;
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t51 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
                                if (!t51) {
                                    goto gm_lb8;
                                }
                                uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
                                uint8_t t53 = ((t52 == ((uint8_t)0)));
                                if (!t53) {
                                    goto gm_lb8;
                                }
                                uint8_t t54 = (uint8_t)(((((uint8_t *)&F.l5) + 24))[16] != 0);
                                if (t54) {
                                    gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
                                    gm_l11 = t55;
                                } else {
                                    gm_b16_8 t56 = gm_f_empty_slice_lpi80k4();
                                    gm_l11 = t56;
                                }
                                gm_b16_8 t57 = gm_l11;
                                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                                gm_l6 = t58;
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t59 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
                                if (t59) {
                                    goto gm_lb9;
                                }
                                goto gm_lb6;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 492, 9);
                        }
gm_lb6:;
                        gm_b16_8 t60 = gm_l6;
                        gm_l7 = t60;
                        gm_l8 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t61 = gm_l8;
                                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                uint8_t t63 = (t61 < t62);
                                if (!t63) {
                                    goto gm_lb10;
                                }
                                {
                                    void * t64 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                    uint64_t t66 = gm_l8;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l9), (((uint8_t *)t64) + (uintptr_t)t66 * 160), 160);
                                            {
                                                {
                                                    uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)&F.l9) + 64) + 0));
                                                    uint8_t t68 = (uint8_t)(t67 == ((uint8_t)1ULL));
                                                    if (!t68) {
                                                        goto gm_lb16;
                                                    }
                                                    gm_l12 = ((uint8_t)1);
                                                    goto gm_lb15;
                                                }
gm_lb16:;
                                                {
                                                    gm_l12 = ((uint8_t)0);
                                                    goto gm_lb15;
                                                }
gm_lb17:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 498, 16);
                                            }
gm_lb15:;
                                            uint8_t t69 = gm_l12;
                                            if (t69) {
                                                void * t70 = (void *)((uint8_t *)&F.l2);
                                                gm_tftb1odc739t5 t71 = gm_f_push_1e500nc(t70, &(*(gm_tvpnktv5fo8nl *)((uint8_t *)&F.l9)));
                                                gm_l13 = t71;
                                                uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                                if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcc2qutubarto1, 499, 17);
                                            } else {
                                                void * t73 = gm_l0;
                                                uint64_t t74 = gm_l1;
                                                gm_b0_1 t75 = gm_f_collect_item_d4tfq2h((void *)NULL, cx, NULL, t73, t74, &(*(gm_tvpnktv5fo8nl *)((uint8_t *)&F.l9)));
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            goto gm_lb13;
                                        }
gm_lb14:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 497, 9);
                                    }
gm_lb13:;
                                }
gm_lb12:;
                                uint64_t t76 = gm_l8;
                                uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                                uint64_t t79 = r77;
                                gm_l8 = t79;
                            }
                        }
gm_lb10:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
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
inline __attribute__((aligned(4))) void gm_f_dep_dir_viuo66n(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_dep_dir_viuo66n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* p */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t6j6smj7qoctu gm_l5; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 290u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 432));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 432) + 8));
    uint64_t t10 = gm_l1;
    if ((uintptr_t)t10 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t10, t9, gm_srcc2qutubarto1, 674, 14);
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + (uintptr_t)t10 * 56) + 16));
    gm_l3 = t11;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l4;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t17 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t15) + (uintptr_t)t17 * 32), 32);
                        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                        gm_b16_8 t19 = gm_l2;
                        uint8_t t20 = (gm_eq0iqiubfm7e(((uint8_t *)&t18), ((uint8_t *)&t19)));
                        if (t20) {
                            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
                            gm_th2lqbo77pvsc t22;
                            GM_ST(uint8_t, (uint8_t *)&t22 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t22 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t22 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t22), t21);
                            ((uint8_t *)&t22)[16] = 1;
                            memmove(gm_out, ((uint8_t *)&t22), 24);
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 674, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t23 = gm_l4;
            uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
            uint64_t t26 = r24;
            gm_l4 = t26;
        }
    }
gm_lb1:;
    gm_th2lqbo77pvsc t27;
    memset(&t27, 0, sizeof t27);
    memmove(gm_out, ((uint8_t *)&t27), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_dep_dir_viuo66n_c(void * a0, uint64_t a1, gm_b16_8 a2) { gm_th2lqbo77pvsc gm_r; gm_f_dep_dir_viuo66n(&gm_r, a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_deps_of_i4no6j9(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deps_of_i4no6j9 F;
    void * gm_l0; /* res */
    gm_b16_8 gm_l1; /* root */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t7));
    gm_l2 = t8;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t14 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t12) + (uintptr_t)t14 * 56), 56);
                        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                        gm_b16_8 t16 = gm_l1;
                        uint8_t t17 = (gm_eq0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16)));
                        if (t17) {
                            gm_trqfvmjtt4sij t18 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&F.l4) + 16));
                            gm_tqetrgo0q24b5 t19;
                            GM_ST(uint8_t, (uint8_t *)&t19 + 41, 0);
                            GM_ST(uint16_t, (uint8_t *)&t19 + 42, 0);
                            GM_ST(uint32_t, (uint8_t *)&t19 + 44, 0);
                            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t19), t18);
                            ((uint8_t *)&t19)[40] = 1;
                            memmove(gm_out, ((uint8_t *)&t19), 48);
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 92, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t20 = gm_l3;
            uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
            uint64_t t23 = r21;
            gm_l3 = t23;
        }
    }
gm_lb1:;
    gm_tqetrgo0q24b5 t24;
    memset(&t24, 0, sizeof t24);
    memmove(gm_out, ((uint8_t *)&t24), 48);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tqetrgo0q24b5 gm_f_deps_of_i4no6j9_c(void * a0, gm_b16_8 a1) { gm_tqetrgo0q24b5 gm_r; gm_f_deps_of_i4no6j9(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_dir_gm_files_0s2opd8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dir_gm_files_0s2opd8 F;
    gm_b16_8 gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_trqfvmjtt4sij t3;
    gm_f_list_epvducg((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t3;
    gm_b16_8 t4 = F.l0;
    gm_l2 = t4;
    {
        gm_node wn5 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hle6hel8vh71, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t8 = gm_l2;
        gm_b0_1 t9 = gm_f_entries_44o5n9f_sbacjg5((void *)NULL, (&wn5), NULL, t8);
        if (gm_unw.kind) {
            goto gm_ll7;
        }
    }
    goto gm_ls10;
gm_ll7:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we6; }
    goto gm_lad;
gm_ls10:;
gm_lb1:;
we6:;
    gm_trqfvmjtt4sij t11 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_ttevl7jqjigkp t12;
    GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
    ((uint8_t *)&t12)[0] = 0;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t12 + 8), t11);
    memmove(gm_out, ((uint8_t *)&t12), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_dump_fugpeqi(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_fugpeqi F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* mi */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* d */
    uint64_t gm_l10; /* t */
    gm_b16_8 gm_l11; /* target */
    GM_STACK_CHECK(&F, 1522u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to8irbp065ot2 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_b16_8 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_trqfvmjtt4sij t15;
    gm_f_list_uvfu1k1((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t15;
    void * t16 = gm_l0;
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t16) + 200));
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
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
                uint64_t t22 = gm_l3;
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t25 = gm_l3;
                gm_tt0nlfbhb3hu6 t26 = GM_LD(gm_tt0nlfbhb3hu6, (((uint8_t *)t23) + (uintptr_t)t25 * 184));
                gm_to8irbp065ot2 t27;
                GM_ST(uint64_t, ((uint8_t *)&t27 + 0), t22);
                GM_ST(gm_tt0nlfbhb3hu6, ((uint8_t *)&t27 + 8), t26);
                gm_l12 = t27;
                {
                    {
                        uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)&gm_l12));
                        gm_l4 = t28;
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l12) + 8), 184);
                        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 112));
                        if (t29) {
                            goto gm_lb3;
                        }
                        void * t30 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                        void * t32 = gm_l0;
                        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 120));
                        gm_b16_8 t34 = gm_f_show_khjih0n((void *)NULL, cx, NULL, t32, t33);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t35 = gm_l0;
                        void * t36 = gm_l0;
                        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 432));
                        uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)t36) + 432) + 8));
                        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 136));
                        if ((uintptr_t)t39 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t39, t38, gm_srcc2qutubarto1, 777, 88);
                        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)t37) + (uintptr_t)t39 * 56));
                        gm_b16_8 t41 = gm_f_show_khjih0n((void *)NULL, cx, NULL, t35, t40);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tfeab279c16rr t42;
                        GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t31);
                        GM_ST(gm_b16_8, ((uint8_t *)&t42 + 16), t34);
                        GM_ST(gm_b16_8, ((uint8_t *)&t42 + 32), t41);
                        gm_tftb1odc739t5 t43 = gm_f_write_otk7ft7(t30, &t42);
                        gm_l13 = t43;
                        uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcc2qutubarto1, 777, 9);
                        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 113));
                        if (t45) {
                            void * t46 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t47;
                            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_srv05plrkjhf8));
                            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(6));
                            gm_tftb1odc739t5 t48 = gm_f_extend_0cs85gs(t46, t47);
                            gm_l14 = t48;
                            uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                            if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcc2qutubarto1, 778, 23);
                        }
                        void * t50 = gm_l0;
                        uint64_t t51 = gm_l4;
                        gm_b16_8 t52 = gm_f_imports_of_5rkfdia((void *)NULL, cx, NULL, t50, t51);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l6 = t52;
                        gm_l7 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t53 = gm_l7;
                                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint8_t t55 = (t53 < t54);
                                if (!t55) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t56 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    uint64_t t58 = gm_l7;
                                    {
                                        {
                                            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)t56) + (uintptr_t)t58 * 8));
                                            gm_l8 = t59;
                                            void * t60 = gm_l0;
                                            void * t61 = GM_LD(void *, (((uint8_t *)t60) + 240));
                                            uint64_t t62 = GM_LD(uint64_t, ((((uint8_t *)t60) + 240) + 8));
                                            uint64_t t63 = gm_l8;
                                            if ((uintptr_t)t63 >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)t63, t62, gm_srcc2qutubarto1, 780, 19);
                                            void * t64 = (void *)(((uint8_t *)t61) + (uintptr_t)t63 * 216);
                                            gm_l9 = t64;
                                            void * t65 = gm_l9;
                                            {
                                                {
                                                    uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t65) + 200) + 0));
                                                    uint8_t t67 = (uint8_t)(t66 == ((uint8_t)1ULL));
                                                    if (!t67) {
                                                        goto gm_lb12;
                                                    }
                                                    uint64_t t68 = GM_LD(uint64_t, ((((uint8_t *)t65) + 200) + 8));
                                                    gm_l10 = t68;
                                                    void * t69 = gm_l0;
                                                    void * t70 = gm_l0;
                                                    void * t71 = GM_LD(void *, (((uint8_t *)t70) + 200));
                                                    uint64_t t72 = GM_LD(uint64_t, ((((uint8_t *)t70) + 200) + 8));
                                                    uint64_t t73 = gm_l10;
                                                    if ((uintptr_t)t73 >= (uintptr_t)(t72)) gm_trap_index((uintptr_t)t73, t72, gm_srcc2qutubarto1, 782, 38);
                                                    gm_b16_8 t74 = GM_LD(gm_b16_8, ((((uint8_t *)t71) + (uintptr_t)t73 * 184) + 120));
                                                    gm_b16_8 t75 = gm_f_show_khjih0n((void *)NULL, cx, NULL, t69, t74);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l15 = t75;
                                                    goto gm_lb11;
                                                }
gm_lb12:;
                                                {
                                                    gm_b16_8 t76;
                                                    GM_ST(void *, (uint8_t *)&t76, (void *)(gm_s79s3h98matgr));
                                                    GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(5));
                                                    gm_l15 = t76;
                                                    goto gm_lb11;
                                                }
gm_lb13:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 781, 23);
                                            }
gm_lb11:;
                                            gm_b16_8 t77 = gm_l15;
                                            gm_l11 = t77;
                                            void * t78 = (void *)((uint8_t *)&F.l1);
                                            void * t79 = gm_l9;
                                            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)t79));
                                            gm_tnpifv20irnph t81;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t81 + 0), t80);
                                            gm_tftb1odc739t5 t82 = gm_f_write_gf33g1q(t78, t81);
                                            gm_l16 = t82;
                                            uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                            if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcc2qutubarto1, 785, 13);
                                            void * t84 = gm_l9;
                                            void * t85 = (void *)(((uint8_t *)t84) + 40);
                                            gm_b16_8 t86 = gm_f_import_path_any_q8ttsvi(t85);
                                            void * t87 = (void *)((uint8_t *)&F.l1);
                                            gm_b0_1 t88 = gm_f_escape_bytes_25mo226((void *)NULL, cx, NULL, t86, t87);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            void * t89 = (void *)((uint8_t *)&F.l1);
                                            gm_b16_8 t90 = gm_l11;
                                            gm_tnpifv20irnph t91;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t91 + 0), t90);
                                            gm_tftb1odc739t5 t92 = gm_f_write_2l4ia51(t89, t91);
                                            gm_l17 = t92;
                                            uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                            if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcc2qutubarto1, 787, 13);
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 779, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t94 = gm_l7;
                                uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                                uint64_t t97 = r95;
                                gm_l7 = t97;
                            }
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 775, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t98 = gm_l3;
            uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
            uint64_t t101 = r99;
            gm_l3 = t101;
        }
    }
gm_lb1:;
    gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
    F.ret = t103;
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
inline __attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_find_package_root_fs4fuqj F;
    gm_b16_8 gm_l0; /* start */
    gm_b16_8 gm_l1; /* abs */
    gm_th2lqbo77pvsc gm_l2; /* cur */
    gm_b16_8 gm_l3; /* d */
    GM_STACK_CHECK(&F, 347u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l4;
    gm_b16_8 t5 = gm_l0;
    gm_b16_8 t6 = gm_f_absolute_path_gj3a0ni((void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t6;
    gm_b16_8 t7 = gm_l1;
    uint8_t t8 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t8) {
        gm_b16_8 t9 = gm_l1;
        gm_th2lqbo77pvsc t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t10), t9);
        ((uint8_t *)&t10)[16] = 1;
        gm_l4 = t10;
    } else {
        gm_b16_8 t11 = gm_l1;
        gm_f_parent_lk0iero((void *)&gm_l4, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    memmove(((uint8_t *)&gm_l2), ((uint8_t *)&gm_l4), 24);
    {
        for (;;) {
            uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
            if (!t12) {
                goto gm_lb1;
            }
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_l3 = t13;
            {
                gm_b16_8 t14 = gm_l3;
                gm_b16_8 t15;
                GM_ST(void *, (uint8_t *)&t15, (void *)(gm_se24c7hpp401n));
                GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(10));
                gm_b16_8 t16 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t14, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t17 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t17) {
                    gm_b16_8 t18 = gm_l3;
                    gm_th2lqbo77pvsc t19;
                    GM_ST(uint8_t, (uint8_t *)&t19 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t19 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t19 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t19), t18);
                    ((uint8_t *)&t19)[16] = 1;
                    memmove(gm_out, ((uint8_t *)&t19), 24);
                    goto gm_fdone;
                }
                gm_b16_8 t20 = gm_l3;
                gm_f_parent_lk0iero((void *)&gm_l2, (void *)NULL, cx, NULL, t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_th2lqbo77pvsc t21;
    memset(&t21, 0, sizeof t21);
    memmove(gm_out, ((uint8_t *)&t21), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_bq6tmqb(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_bq6tmqb F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_tibupt4dmcqai gm_l2; /* e */
    gm_b16_8 gm_l3; /* p */
    GM_STACK_CHECK(&F, 291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tibupt4dmcqai gm_l4;
    gm_tftb1odc739t5 gm_l5;
    {
        {
            memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l0), 32);
            uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
            if (t6) {
                memmove(((uint8_t *)&gm_l4), (((uint8_t *)&gm_l1) + 8), 24);
            } else {
                GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_load_package_modules_t4hh6gf *)((&F.h)->up))->l11), ((uint8_t)0));
                gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
                goto gm_lad;
            }
            memmove(((uint8_t *)&gm_l2), ((uint8_t *)&gm_l4), 24);
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_load_package_modules_t4hh6gf *)((&F.h)->up))->l9));
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_b16_8 t9 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t9;
            {
                {
                    gm_b16_8 t10 = gm_l3;
                    uint8_t t11 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t10);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (!t11) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                    uint8_t t13 = gm_f_skipped_dir_orfng8m(t12);
                    uint8_t t14 = (uint8_t)!t13;
                    if (!t14) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t15 = gm_l3;
                    gm_b16_8 t16;
                    GM_ST(void *, (uint8_t *)&t16, (void *)(gm_se24c7hpp401n));
                    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(10));
                    gm_b16_8 t17 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t15, t16);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t18 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t17);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t19 = (uint8_t)!t18;
                    if (!t19) {
                        goto gm_lb4;
                    }
                    void * t20 = (void *)((uint8_t *)&((struct gm_fr_load_package_modules_t4hh6gf *)((&F.h)->up))->l10);
                    gm_b16_8 t21 = gm_l3;
                    gm_tftb1odc739t5 t22 = gm_f_push_3ef746b(t20, t21);
                    gm_l5 = t22;
                    uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                    if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcc2qutubarto1, 434, 17);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 427, 9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fnpff4s(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_fnpff4s F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_toe5fmr3vvf4d gm_l2; /* err */
    gm_tibupt4dmcqai gm_l3; /* e */
    gm_b16_8 gm_l4; /* p */
    GM_STACK_CHECK(&F, 338u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tibupt4dmcqai gm_l5;
    gm_tftb1odc739t5 gm_l6;
    {
        {
            memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l0), 32);
            uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
            if (t6) {
                memmove(((uint8_t *)&gm_l5), (((uint8_t *)&gm_l1) + 8), 24);
            } else {
                gm_toe5fmr3vvf4d t7 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l1) + 8));
                gm_l2 = t7;
                gm_toe5fmr3vvf4d t8 = gm_l2;
                gm_ttevl7jqjigkp t9;
                memset(&t9, 0, sizeof t9);
                ((uint8_t *)&t9)[0] = 1;
                GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t9 + 8), t8);
                uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
                ((struct gm_fr_dir_gm_files_0s2opd8 *)((&F.h)->up))->ret = t9;
                gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t10;
                goto gm_lad;
            }
            memmove(((uint8_t *)&gm_l3), ((uint8_t *)&gm_l5), 24);
            {
                {
                    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t12 = (t11 > ((uint64_t)3ULL));
                    if (!t12) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_b16_8 t14;
                    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s0ucactj6hj0a));
                    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(3));
                    uint8_t t15 = gm_f_ends_with_biriu5l(t13, t14);
                    if (!t15) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_dir_gm_files_0s2opd8 *)((&F.h)->up))->l0));
                    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_b16_8 t18 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t16, t17);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l4 = t18;
                    gm_b16_8 t19 = gm_l4;
                    uint8_t t20 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t19);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t20) {
                        void * t21 = (void *)((uint8_t *)&((struct gm_fr_dir_gm_files_0s2opd8 *)((&F.h)->up))->l1);
                        gm_b16_8 t22 = gm_l4;
                        gm_tftb1odc739t5 t23 = gm_f_push_3ef746b(t21, t22);
                        gm_l6 = t23;
                        uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcc2qutubarto1, 282, 29);
                    }
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 278, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_m8t719h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_m8t719h F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    uint8_t gm_l3;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
            if (t6) {
                gm_l3 = ((uint8_t)1);
            } else {
                gm_b16_8 t7 = gm_l1;
                gm_b16_8 t8;
                GM_ST(void *, (uint8_t *)&t8, (void *)(gm_snenkhfev166s));
                GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(2));
                uint8_t t9 = (gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
                gm_l3 = t9;
            }
            uint8_t t10 = gm_l3;
            if (t10) {
                gm_l2 = ((uint8_t)1);
            } else {
                gm_b16_8 t11 = gm_l1;
                uint8_t t12 = 0;
                uint8_t t13 = ((uint8_t)92ULL);
                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t11) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t11))) + i * 1, ((uint8_t *)&t13))) { t12 = 1; break; }
                uint8_t t14 = (uint8_t)(t12);
                gm_l2 = t14;
            }
            uint8_t t15 = gm_l2;
            if (t15) {
                uint8_t t16 = 0;
                ((struct gm_fr_bad_rest_oc5mkat *)((&F.h)->up))->ret = ((uint8_t)1);
                gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t16;
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 666, 5);
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
__attribute__((aligned(4))) gm_t8c5h7u09n10a gm_f_import_decl_vuv14lf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, void * a3) {
    struct gm_fr_import_decl_vuv14lf F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* d */
    void * gm_l3; /* b */
    gm_tl874t2atnlh3 gm_l5; /* between */
    gm_b16_8 gm_l6; /* text */
    gm_tibupt4dmcqai gm_l7; /* path */
    uint64_t gm_l8; /* m */
    GM_STACK_CHECK(&F, 958u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l9;
    uint8_t gm_l10;
    gm_tf5srh4mp4b3i gm_l11;
    uint64_t gm_l12;
    void * t12 = gm_l3;
    gm_t9qkh0rsooc8d t13 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t12) + 32));
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
    uint8_t t15 = (uint8_t)((t14 == ((uint8_t)1ULL)));
    gm_t9qkh0rsooc8d t16;
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)1ULL));
    if (t15) {
        void * t17 = gm_l3;
        gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t17) + 48));
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s6lnp3h2ojl1f));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(76));
        gm_t4gut2c9fj1kt t20;
        gm_f_error_s6hn871((void *)&t20, (void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t20;
        void * t21 = gm_l3;
        uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)t21) + 48));
        void * t23 = gm_l2;
        uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)t23) + 16) + 8));
        void * t25 = gm_l3;
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 40));
        uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)t26) + 72) + 4));
        gm_tl874t2atnlh3 t28;
        GM_ST(uint32_t, ((uint8_t *)&t28 + 0), t22);
        GM_ST(uint32_t, ((uint8_t *)&t28 + 4), t24);
        GM_ST(uint32_t, ((uint8_t *)&t28 + 8), t27);
        gm_l5 = t28;
        void * t29 = gm_l0;
        void * t30 = GM_LD(void *, ((uint8_t *)t29));
        uint32_t t31 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
        void * t32 = gm_f_file_o15fqku(t30, t31);
        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 16));
        uint64_t t34 = GM_LD(uint64_t, ((((uint8_t *)t32) + 16) + 8));
        uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
        uint64_t t36 = (uint64_t)t35;
        uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
        uint64_t t38 = (uint64_t)t37;
        uint64_t t39 = (uintptr_t)t36;
        uint64_t t40 = (uintptr_t)t38;
        if (t39 > t40 || t40 > (uintptr_t)t34) gm_trap_slice(t39, t40, t34, gm_srcc2qutubarto1, 600, 17);
        uint64_t t41 = ((uintptr_t)t38) - (uintptr_t)t36;
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t33 + (uintptr_t)t36 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(t41));
        gm_l6 = t42;
        {
            {
                void * t43 = gm_l3;
                {
                    {
                        uint8_t t44 = (uint8_t)(GM_LD(void *, (((uint8_t *)t43) + 24)) != 0);
                        if (t44) {
                            goto gm_lb4;
                        }
                        gm_l9 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_l9 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 601, 12);
                }
gm_lb3:;
                uint8_t t45 = gm_l9;
                if (!t45) {
                    goto gm_lb2;
                }
                gm_b16_8 t46 = gm_l6;
                gm_b16_8 t47 = gm_f_trim_fdrfe6i(t46);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(gm_shge5n743ua4h));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(2));
                uint8_t t49 = (gm_eq0iqiubfm7e(((uint8_t *)&t47), ((uint8_t *)&t48)));
                if (!t49) {
                    goto gm_lb2;
                }
                void * t50 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t51;
                GM_ST(void *, (uint8_t *)&t51, (void *)(gm_spq5ja4c7ciga));
                GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(10));
                gm_tl874t2atnlh3 t52 = gm_l5;
                gm_b16_8 t53;
                GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s2urait735ofq));
                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(4));
                gm_b0_1 t54 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t50, t51, t52, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t55 = gm_l0;
        void * t56 = GM_LD(void *, (((uint8_t *)t55) + 8));
        gm_f_push_o4ckdr1(t56, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l4)));
        gm_t8c5h7u09n10a t57;
        GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t57 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)2ULL));
        F.ret = t57;
        goto gm_fret;
    }
    void * t58 = gm_l3;
    {
        {
            uint8_t t59 = (uint8_t)(GM_LD(void *, (((uint8_t *)t58) + 24)) != 0);
            if (t59) {
                goto gm_lb7;
            }
            gm_l10 = ((uint8_t)0);
            goto gm_lb6;
        }
gm_lb7:;
        {
            gm_l10 = ((uint8_t)1);
            goto gm_lb6;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 605, 8);
    }
gm_lb6:;
    uint8_t t60 = gm_l10;
    if (t60) {
        void * t61 = gm_l0;
        void * t62 = GM_LD(void *, (((uint8_t *)t61) + 8));
        void * t63 = gm_l3;
        void * t64 = GM_LD(void *, (((uint8_t *)t63) + 40));
        gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
        gm_b16_8 t66;
        GM_ST(void *, (uint8_t *)&t66, (void *)(gm_su5dv07glhc06));
        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(36));
        gm_b0_1 t67 = gm_f_report_eardf07((void *)NULL, cx, NULL, t62, t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t8c5h7u09n10a t68;
        GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t68 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)2ULL));
        F.ret = t68;
        goto gm_fret;
    }
    {
        {
            void * t69 = gm_l3;
            void * t70 = GM_LD(void *, (((uint8_t *)t69) + 40));
            uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t70) + 8) + 0));
            uint8_t t72 = (uint8_t)(t71 == ((uint8_t)38ULL));
            if (!t72) {
                goto gm_lb10;
            }
            memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t70) + 8) + 8), 24);
            void * t73 = gm_l0;
            uint64_t t74 = gm_l1;
            gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
            void * t77 = gm_l3;
            void * t78 = GM_LD(void *, (((uint8_t *)t77) + 40));
            gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t78) + 72));
            gm_tf5srh4mp4b3i t80 = gm_f_import_module_8345fv4((void *)NULL, cx, NULL, t73, t74, t76, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t80;
            uint8_t t81 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
            if (t81) {
                uint64_t t82 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
                gm_l12 = t82;
            } else {
                gm_t8c5h7u09n10a t83;
                GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t83 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)2ULL));
                F.ret = t83;
                goto gm_fret;
            }
            uint64_t t84 = gm_l12;
            gm_l8 = t84;
            uint64_t t85 = gm_l8;
            gm_t8c5h7u09n10a t86;
            GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)1ULL));
            GM_ST(uint64_t, ((uint8_t *)&t86 + 8), t85);
            F.ret = t86;
            goto gm_fret;
        }
gm_lb10:;
        gm_t8c5h7u09n10a t87;
        GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t87 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)2ULL));
        F.ret = t87;
        goto gm_fret;
    }
gm_lb9:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_import_module_8345fv4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_import_module_8345fv4 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* path */
    gm_tl874t2atnlh3 gm_l3; /* at */
    gm_b16_8 gm_l4; /* from_dir */
    uint64_t gm_l5; /* package */
    uint8_t gm_l6; /* relative */
    gm_th2lqbo77pvsc gm_l7; /* name */
    gm_b16_8 gm_l8; /* dir */
    gm_b16_8 gm_l9; /* canon */
    gm_b16_8 gm_l10; /* first */
    gm_b16_8 gm_l11; /* rest */
    gm_t6j6smj7qoctu gm_l12; /* split */
    gm_b16_8 gm_l13; /* base */
    uint64_t gm_l14; /* m */
    GM_STACK_CHECK(&F, 1906u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l15;
    gm_th2lqbo77pvsc gm_l16;
    gm_b16_8 gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_ttu09lij36f8c gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_th2lqbo77pvsc gm_l21;
    gm_b16_8 gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_b16_8 gm_l24;
    gm_tf5srh4mp4b3i gm_l25;
    gm_tf5srh4mp4b3i gm_l26;
    uint64_t gm_l27;
    gm_tftb1odc739t5 gm_l28;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, (((uint8_t *)t6) + 200));
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t6) + 200) + 8));
    uint64_t t9 = gm_l1;
    if ((uintptr_t)t9 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t9, t8, gm_srcc2qutubarto1, 616, 17);
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)t7) + (uintptr_t)t9 * 184) + 120));
    gm_l4 = t10;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, (((uint8_t *)t11) + 200));
    uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)t11) + 200) + 8));
    uint64_t t14 = gm_l1;
    if ((uintptr_t)t14 >= (uintptr_t)(t13)) gm_trap_index((uintptr_t)t14, t13, gm_srcc2qutubarto1, 617, 16);
    uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t12) + (uintptr_t)t14 * 184) + 136));
    gm_l5 = t15;
    gm_b16_8 t16 = gm_l2;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sdsu0imtl17e3));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(2));
    uint8_t t18 = gm_f_starts_with_6vvt01e(t16, t17);
    if (t18) {
        gm_l15 = ((uint8_t)1);
    } else {
        gm_b16_8 t19 = gm_l2;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_strcbt1c3upss));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
        uint8_t t21 = gm_f_starts_with_6vvt01e(t19, t20);
        gm_l15 = t21;
    }
    uint8_t t22 = gm_l15;
    gm_l6 = t22;
    gm_th2lqbo77pvsc t23;
    memset(&t23, 0, sizeof t23);
    gm_l7 = t23;
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(0));
    gm_l8 = t24;
    uint8_t t25 = gm_l6;
    if (t25) {
        gm_b16_8 t26 = gm_l4;
        gm_b16_8 t27 = gm_l2;
        gm_b16_8 t28 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t29 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t29;
        gm_b16_8 t30 = gm_l8;
        gm_f_canonicalize_n3e3qri((void *)&gm_l16, (void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l16))[16] != 0);
        if (t31) {
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
            gm_l17 = t32;
        } else {
            gm_b16_8 t33 = gm_l8;
            gm_b16_8 t34 = gm_f_absolute_path_gj3a0ni((void *)NULL, cx, NULL, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t34;
        }
        gm_b16_8 t35 = gm_l17;
        gm_l9 = t35;
        gm_b16_8 t36 = gm_l9;
        void * t37 = gm_l0;
        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 432));
        uint64_t t39 = GM_LD(uint64_t, ((((uint8_t *)t37) + 432) + 8));
        uint64_t t40 = gm_l5;
        if ((uintptr_t)t40 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t40, t39, gm_srcc2qutubarto1, 624, 35);
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)t38) + (uintptr_t)t40 * 56));
        uint8_t t42 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t36, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t43 = (uint8_t)!t42;
        if (t43) {
            void * t44 = gm_l0;
            void * t45 = GM_LD(void *, (((uint8_t *)t44) + 8));
            gm_tl874t2atnlh3 t46 = gm_l3;
            gm_b16_8 t47 = gm_l2;
            gm_tnpifv20irnph t48;
            GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
            gm_f_format_cgpq8i6((void *)&gm_l18, (void *)NULL, cx, NULL, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcc2qutubarto1, 625, 32);
            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
            gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
            gm_b0_1 t52 = gm_f_report_eardf07((void *)NULL, cx, NULL, t45, t46, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tf5srh4mp4b3i t53;
            memset(&t53, 0, sizeof t53);
            F.ret = t53;
            goto gm_fret;
        }
    } else {
        gm_b16_8 t54 = gm_l2;
        gm_l10 = t54;
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(0));
        gm_l11 = t55;
        {
            {
                gm_b16_8 t56 = gm_l2;
                gm_b16_8 t57;
                GM_ST(void *, (uint8_t *)&t57, (void *)(gm_st3ja0nqi37bv));
                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(1));
                gm_f_split_once_mqcu1ce((void *)&gm_l19, t56, t57);
                uint8_t t58 = (uint8_t)((((uint8_t *)&gm_l19))[32] != 0);
                if (!t58) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l19), 32);
                gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
                gm_l10 = t59;
                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                gm_l11 = t60;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        {
            {
                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t62 = (t61 > ((uint64_t)0ULL));
                if (!t62) {
                    goto gm_lb4;
                }
                gm_b16_8 t63 = gm_l11;
                uint8_t t64 = gm_f_bad_rest_oc5mkat(t63);
                if (!t64) {
                    goto gm_lb4;
                }
                void * t65 = gm_l0;
                void * t66 = GM_LD(void *, (((uint8_t *)t65) + 8));
                gm_tl874t2atnlh3 t67 = gm_l3;
                gm_b16_8 t68 = gm_l2;
                gm_b16_8 t69 = gm_l10;
                gm_t6j6smj7qoctu t70;
                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t68);
                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 16), t69);
                gm_f_format_ajjbrpr((void *)&gm_l20, (void *)NULL, cx, NULL, &t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcc2qutubarto1, 636, 32);
                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                gm_b0_1 t74 = gm_f_report_eardf07((void *)NULL, cx, NULL, t66, t67, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tf5srh4mp4b3i t75;
                memset(&t75, 0, sizeof t75);
                F.ret = t75;
                goto gm_fret;
            }
gm_lb4:;
        }
gm_lb3:;
        void * t76 = gm_l0;
        gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)t76) + 152));
        gm_l13 = t77;
        gm_b16_8 t78 = gm_l10;
        gm_b16_8 t79;
        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s8sv6qdepk5ba));
        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(3));
        uint8_t t80 = (!gm_eq0iqiubfm7e(((uint8_t *)&t78), ((uint8_t *)&t79)));
        if (t80) {
            void * t81 = gm_l0;
            uint64_t t82 = gm_l5;
            gm_b16_8 t83 = gm_l10;
            gm_f_dep_dir_viuo66n((void *)&gm_l21, t81, t82, t83);
            uint8_t t84 = (uint8_t)((((uint8_t *)&gm_l21))[16] != 0);
            if (t84) {
                gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                gm_l22 = t85;
            } else {
                void * t86 = gm_l0;
                void * t87 = GM_LD(void *, (((uint8_t *)t86) + 8));
                gm_tl874t2atnlh3 t88 = gm_l3;
                gm_b16_8 t89 = gm_l10;
                gm_tnpifv20irnph t90;
                GM_ST(gm_b16_8, ((uint8_t *)&t90 + 0), t89);
                gm_f_format_qpmmpss((void *)&gm_l23, (void *)NULL, cx, NULL, t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_srcc2qutubarto1, 646, 36);
                gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                gm_b0_1 t94 = gm_f_report_eardf07((void *)NULL, cx, NULL, t87, t88, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tf5srh4mp4b3i t95;
                memset(&t95, 0, sizeof t95);
                F.ret = t95;
                goto gm_fret;
            }
            gm_b16_8 t96 = gm_l22;
            gm_l13 = t96;
            uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
            uint8_t t98 = ((t97 == ((uint64_t)0ULL)));
            if (t98) {
                gm_b16_8 t99 = gm_l10;
                gm_th2lqbo77pvsc t100;
                GM_ST(uint8_t, (uint8_t *)&t100 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t100 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t100 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t100), t99);
                ((uint8_t *)&t100)[16] = 1;
                gm_l7 = t100;
            }
        }
        uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
        uint8_t t102 = ((t101 == ((uint64_t)0ULL)));
        if (t102) {
            gm_b16_8 t103 = gm_l13;
            gm_l24 = t103;
        } else {
            gm_b16_8 t104 = gm_l13;
            gm_b16_8 t105 = gm_l11;
            gm_b16_8 t106 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t104, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t106;
        }
        gm_b16_8 t107 = gm_l24;
        gm_b16_8 t108 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t108;
    }
    void * t109 = gm_l0;
    gm_b16_8 t110 = gm_l8;
    gm_tl874t2atnlh3 t111 = gm_l3;
    uint8_t t112 = gm_l6;
    if (t112) {
        uint64_t t113 = gm_l5;
        gm_tf5srh4mp4b3i t114;
        GM_ST(uint8_t, (uint8_t *)&t114 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t114 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t114 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t114), t113);
        ((uint8_t *)&t114)[8] = 1;
        gm_l25 = t114;
    } else {
        gm_tf5srh4mp4b3i t115;
        memset(&t115, 0, sizeof t115);
        gm_l25 = t115;
    }
    gm_tf5srh4mp4b3i t116 = gm_l25;
    gm_tf5srh4mp4b3i t117 = gm_f_load_dir_module_a75lmvr((void *)NULL, cx, NULL, t109, t110, t111, t116, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l26 = t117;
    uint8_t t118 = (uint8_t)((((uint8_t *)&gm_l26))[8] != 0);
    if (t118) {
        uint64_t t119 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
        gm_l27 = t119;
    } else {
        gm_tf5srh4mp4b3i t120;
        memset(&t120, 0, sizeof t120);
        F.ret = t120;
        goto gm_fret;
    }
    uint64_t t121 = gm_l27;
    gm_l14 = t121;
    void * t122 = gm_l0;
    void * t123 = (void *)(((uint8_t *)t122) + 512);
    uint64_t t124 = gm_l1;
    uint64_t t125 = gm_l14;
    gm_tl874t2atnlh3 t126 = gm_l3;
    gm_t0nqnmillkbs6 t127;
    GM_ST(uint32_t, (uint8_t *)&t127 + 28, 0);
    GM_ST(uint64_t, ((uint8_t *)&t127 + 0), t124);
    GM_ST(uint64_t, ((uint8_t *)&t127 + 8), t125);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t127 + 16), t126);
    gm_tftb1odc739t5 t128 = gm_f_push_89v6mq9(t123, &t127);
    gm_l28 = t128;
    uint8_t t129 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
    if (!t129) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcc2qutubarto1, 659, 5);
    uint64_t t130 = gm_l14;
    gm_tf5srh4mp4b3i t131;
    GM_ST(uint8_t, (uint8_t *)&t131 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t131 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t131 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t131), t130);
    ((uint8_t *)&t131)[8] = 1;
    F.ret = t131;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_import_path_any_q8ttsvi(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_import_path_any_q8ttsvi F;
    void * gm_l0; /* item */
    gm_tibupt4dmcqai gm_l2; /* path */
    GM_STACK_CHECK_SHALLOW(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t6 = gm_l0;
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 64) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)0ULL));
            if (!t8) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l1), ((((uint8_t *)t6) + 64) + 8), 64);
            {
                {
                    void * t9 = GM_LD(void *, (((uint8_t *)&F.l1) + 40));
                    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
                    uint8_t t11 = (uint8_t)(t10 == ((uint8_t)38ULL));
                    if (!t11) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t9) + 8) + 8), 24);
                    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
                    F.ret = t13;
                    goto gm_fret;
                }
gm_lb4:;
                gm_b16_8 t14;
                GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(0));
                F.ret = t14;
                goto gm_fret;
            }
gm_lb3:;
        }
gm_lb2:;
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(0));
        F.ret = t15;
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_import_path_na25mjr(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_import_path_na25mjr F;
    void * gm_l0; /* item */
    gm_tibupt4dmcqai gm_l2; /* path */
    GM_STACK_CHECK_SHALLOW(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l3;
    void * t9 = gm_l0;
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t9) + 40) + 8));
    uint8_t t11 = (t10 > ((uint64_t)0ULL));
    if (t11) {
        gm_th2lqbo77pvsc t12;
        memset(&t12, 0, sizeof t12);
        memmove(gm_out, ((uint8_t *)&t12), 24);
        goto gm_fdone;
    }
    {
        {
            void * t13 = gm_l0;
            uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t13) + 64) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)0ULL));
            if (!t15) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l1), ((((uint8_t *)t13) + 64) + 8), 64);
            {
                {
                    uint8_t t16 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l1) + 24)) != 0);
                    if (t16) {
                        goto gm_lb4;
                    }
                    gm_l3 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l3 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 562, 8);
            }
gm_lb3:;
            uint8_t t17 = gm_l3;
            if (t17) {
                gm_th2lqbo77pvsc t18;
                memset(&t18, 0, sizeof t18);
                memmove(gm_out, ((uint8_t *)&t18), 24);
                goto gm_fdone;
            }
            {
                {
                    void * t19 = GM_LD(void *, (((uint8_t *)&F.l1) + 40));
                    uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)38ULL));
                    if (!t21) {
                        goto gm_lb7;
                    }
                    memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t19) + 8) + 8), 24);
                    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
                    gm_th2lqbo77pvsc t24;
                    GM_ST(uint8_t, (uint8_t *)&t24 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t24 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t24 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t24), t23);
                    ((uint8_t *)&t24)[16] = 1;
                    memmove(gm_out, ((uint8_t *)&t24), 24);
                    goto gm_fdone;
                }
gm_lb7:;
                gm_th2lqbo77pvsc t25;
                memset(&t25, 0, sizeof t25);
                memmove(gm_out, ((uint8_t *)&t25), 24);
                goto gm_fdone;
            }
gm_lb6:;
        }
gm_lb2:;
        gm_th2lqbo77pvsc t26;
        memset(&t26, 0, sizeof t26);
        memmove(gm_out, ((uint8_t *)&t26), 24);
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
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_import_path_na25mjr_c(void * a0) { gm_th2lqbo77pvsc gm_r; gm_f_import_path_na25mjr(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_imports_of_5rkfdia(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_imports_of_5rkfdia F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t3877pk7lv748 gm_l5; /* n */
    GM_STACK_CHECK(&F, 335u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l6;
    gm_trqfvmjtt4sij t7;
    gm_f_list_5dhgg4i((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t7;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 200));
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t8) + 200) + 8));
    uint64_t t11 = gm_l1;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcc2qutubarto1, 204, 14);
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t9) + (uintptr_t)t11 * 184) + 16));
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
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t18 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t16) + (uintptr_t)t18 * 24), 24);
                        void * t19 = gm_l0;
                        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 240));
                        uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t19) + 240) + 8));
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
                        if ((uintptr_t)t22 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t22, t21, gm_srcc2qutubarto1, 205, 23);
                        void * t23 = (void *)((((uint8_t *)t20) + (uintptr_t)t22 * 216) + 40);
                        uint8_t t24 = gm_f_is_import_1c0aicu(t23);
                        if (t24) {
                            void * t25 = (void *)((uint8_t *)&F.l2);
                            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
                            gm_tftb1odc739t5 t27 = gm_f_push_ucgmlo2(t25, t26);
                            gm_l6 = t27;
                            uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                            if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcc2qutubarto1, 205, 53);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 204, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t29 = gm_l4;
            uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
            uint64_t t32 = r30;
            gm_l4 = t32;
        }
    }
gm_lb1:;
    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_f_sort_o401rjn(t33);
    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    F.ret = t34;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_import_1c0aicu(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_reserved_type_name_tuq4qsj F;
    gm_b16_8 gm_l0; /* name */
    uint32_t gm_l1; /* bits */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    uint8_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 609u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    gm_b16_8 t7 = gm_l0;
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s6groq37ram3h));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(5));
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_s0i3i86nphpsh));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(5));
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_shnig5tqtdedi));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(3));
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sm9e81djuncnu));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(3));
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sqqajgod2bb4o));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(3));
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s929lennfd3g6));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(4));
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s3cv29r83pgn4));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(5));
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s2737i1iq57nf));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(4));
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(gm_slolcr6ikvg33));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(6));
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_st2dnokm1h0jd));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
    gm_tk6rvhh54g86p t18;
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t8);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 16), t9);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 32), t10);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 48), t11);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 64), t12);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 80), t13);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 96), t14);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 112), t15);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 128), t16);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 144), t17);
    uint8_t t19 = 0;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t18) + 0, ((uint8_t *)&t7))) t19 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t18) + 16, ((uint8_t *)&t7))) t19 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t18) + 32, ((uint8_t *)&t7))) t19 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t18) + 48, ((uint8_t *)&t7))) t19 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t18) + 64, ((uint8_t *)&t7))) t19 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t18) + 80, ((uint8_t *)&t7))) t19 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t18) + 96, ((uint8_t *)&t7))) t19 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t18) + 112, ((uint8_t *)&t7))) t19 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t18) + 128, ((uint8_t *)&t7))) t19 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t18) + 144, ((uint8_t *)&t7))) t19 = 1;
    uint8_t t20 = (uint8_t)(t19);
    if (t20) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t22 = (t21 < ((uint64_t)2ULL));
    if (t22) {
        gm_l9 = ((uint8_t)1);
    } else {
        void * t23 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t24, gm_srcc2qutubarto1, 571, 24);
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t23) + (uintptr_t)((uint64_t)0ULL) * 1));
        gm_tqs5p4n2tq19v t26;
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)117ULL));
        GM_ST(uint8_t, ((uint8_t *)&t26 + 1), ((uint8_t)105ULL));
        uint8_t t27 = 0;
        if (gm_eqjmccqdb90f(((uint8_t *)&t26) + 0, ((uint8_t *)&t25))) t27 = 1;
        if (gm_eqjmccqdb90f(((uint8_t *)&t26) + 1, ((uint8_t *)&t25))) t27 = 1;
        uint8_t t28 = (uint8_t)(!t27);
        gm_l9 = t28;
    }
    uint8_t t29 = gm_l9;
    if (t29) {
        gm_l8 = ((uint8_t)1);
    } else {
        void * t30 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t31, gm_srcc2qutubarto1, 571, 53);
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t30) + (uintptr_t)((uint64_t)1ULL) * 1));
        uint8_t t33 = ((t32 == ((uint8_t)48ULL)));
        gm_l8 = t33;
    }
    uint8_t t34 = gm_l8;
    if (t34) {
        gm_l7 = ((uint8_t)1);
    } else {
        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint8_t t36 = (t35 > ((uint64_t)4ULL));
        gm_l7 = t36;
    }
    uint8_t t37 = gm_l7;
    if (t37) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_l1 = ((uint32_t)0ULL);
    void * t38 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t40 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t41 = (uintptr_t)t39;
    if (t40 > t41 || t41 > (uintptr_t)t39) gm_trap_slice(t40, t41, t39, gm_srcc2qutubarto1, 573, 14);
    uint64_t t42 = ((uintptr_t)t39) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t43;
    GM_ST(void *, (uint8_t *)&t43, (void *)(((uint8_t *)t38 + (uintptr_t)((uint64_t)1ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(t42));
    gm_l2 = t43;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t44 = gm_l3;
            uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t46 = (t44 < t45);
            if (!t46) {
                goto gm_lb1;
            }
            {
                void * t47 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t49 = gm_l3;
                {
                    {
                        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)t47) + (uintptr_t)t49 * 1));
                        gm_l4 = t50;
                        uint8_t t51 = gm_l4;
                        gm_l5 = t51;
                        uint8_t t52 = gm_l5;
                        uint8_t t53 = (((uint8_t)48ULL) <= t52);
                        if (t53) {
                            uint8_t t54 = gm_l5;
                            uint8_t t55 = (t54 <= ((uint8_t)57ULL));
                            gm_l10 = t55;
                        } else {
                            gm_l10 = ((uint8_t)0);
                        }
                        uint8_t t56 = gm_l10;
                        uint8_t t57 = (uint8_t)!t56;
                        if (t57) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        uint32_t t58 = gm_l1;
                        uint32_t r59; bool o60 = __builtin_mul_overflow(t58, ((uint32_t)10ULL), &r59);
                        if (o60) gm_trap("integer overflow in `*`", gm_srcc2qutubarto1, 575, 16);
                        uint32_t t61 = r59;
                        uint8_t t62 = gm_l4;
                        uint8_t r63; bool o64 = __builtin_sub_overflow(t62, ((uint8_t)48ULL), &r63);
                        if (o64) gm_trap("integer overflow in `-`", gm_srcc2qutubarto1, 575, 32);
                        uint8_t t65 = r63;
                        uint32_t t66 = (uint32_t)t65;
                        uint32_t r67; bool o68 = __builtin_add_overflow(t61, t66, &r67);
                        if (o68) gm_trap("integer overflow in `+`", gm_srcc2qutubarto1, 575, 16);
                        uint32_t t69 = r67;
                        gm_l1 = t69;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 573, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t70 = gm_l3;
            uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
            uint64_t t73 = r71;
            gm_l3 = t73;
        }
    }
gm_lb1:;
    uint32_t t74 = gm_l1;
    gm_l6 = t74;
    uint32_t t75 = gm_l6;
    uint8_t t76 = (((uint32_t)1ULL) <= t75);
    if (t76) {
        uint32_t t77 = gm_l6;
        uint8_t t78 = (t77 <= ((uint32_t)128ULL));
        gm_l11 = t78;
    } else {
        gm_l11 = ((uint8_t)0);
    }
    uint8_t t79 = gm_l11;
    F.ret = t79;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_load_dir_module_a75lmvr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_tf5srh4mp4b3i a3, const void * a4) {
    struct gm_fr_load_dir_module_a75lmvr F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* dir */
    gm_tl874t2atnlh3 gm_l2; /* at */
    gm_tf5srh4mp4b3i gm_l3; /* parent_package */
    gm_th2lqbo77pvsc gm_l4; /* name */
    gm_b16_8 gm_l5; /* key */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t3877pk7lv748 gm_l8; /* d */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* p */
    gm_b16_8 gm_l14; /* module_name */
    uint64_t gm_l15; /* package */
    uint64_t gm_l16; /* m */
    GM_STACK_CHECK(&F, 1714u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&gm_l4, a4, sizeof gm_l4);
    gm_th2lqbo77pvsc gm_l17;
    gm_b16_8 gm_l18;
    gm_tqetrgo0q24b5 gm_l19;
    gm_trqfvmjtt4sij gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_t1s75i08qtptd gm_l23;
    gm_t08olq97rscn3 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_b16_8 gm_l26;
    gm_th2lqbo77pvsc gm_l27;
    gm_b16_8 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_b16_8 t12 = gm_l1;
    gm_f_canonicalize_n3e3qri((void *)&gm_l17, (void *)NULL, cx, NULL, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
    if (t13) {
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
        gm_l18 = t14;
    } else {
        gm_b16_8 t15 = gm_l1;
        gm_l18 = t15;
    }
    gm_b16_8 t16 = gm_l18;
    gm_l5 = t16;
    void * t17 = gm_l0;
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t17) + 472));
    gm_l6 = t18;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t19 = gm_l7;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                void * t22 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t24 = gm_l7;
                {
                    {
                        memmove(((uint8_t *)&gm_l8), (((uint8_t *)t22) + (uintptr_t)t24 * 24), 24);
                        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
                        gm_b16_8 t26 = gm_l5;
                        uint8_t t27 = (gm_eq0iqiubfm7e(((uint8_t *)&t25), ((uint8_t *)&t26)));
                        if (t27) {
                            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 16));
                            gm_tf5srh4mp4b3i t29;
                            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t29), t28);
                            ((uint8_t *)&t29)[8] = 1;
                            F.ret = t29;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 383, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t30 = gm_l7;
            uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
            uint64_t t33 = r31;
            gm_l7 = t33;
        }
    }
gm_lb1:;
    gm_b16_8 t34 = gm_l1;
    void * t35 = gm_l0;
    gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)t35) + 32));
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
    gm_f_module_files_with_abkh6r4((void *)&gm_l19, (void *)NULL, cx, NULL, t34, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t38 = (uint8_t)((((uint8_t *)&gm_l19))[40] != 0);
    if (t38) {
        memmove(((uint8_t *)&gm_l20), ((uint8_t *)&gm_l19), 40);
    } else {
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 8));
        gm_tl874t2atnlh3 t41 = gm_l2;
        gm_b16_8 t42 = gm_l1;
        gm_tnpifv20irnph t43;
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t42);
        gm_f_format_atch2pp((void *)&gm_l21, (void *)NULL, cx, NULL, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
        if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_srcc2qutubarto1, 387, 28);
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
        gm_b0_1 t47 = gm_f_report_eardf07((void *)NULL, cx, NULL, t40, t41, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tf5srh4mp4b3i t48;
        memset(&t48, 0, sizeof t48);
        F.ret = t48;
        goto gm_fret;
    }
    memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l20), 40);
    gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
    gm_f_sort_skeree7(t49);
    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
    uint8_t t51 = ((t50 == ((uint64_t)0ULL)));
    if (t51) {
        void * t52 = gm_l0;
        void * t53 = GM_LD(void *, (((uint8_t *)t52) + 8));
        gm_tl874t2atnlh3 t54 = gm_l2;
        gm_b16_8 t55 = gm_l1;
        gm_tnpifv20irnph t56;
        GM_ST(gm_b16_8, ((uint8_t *)&t56 + 0), t55);
        gm_f_format_t2lplfr((void *)&gm_l22, (void *)NULL, cx, NULL, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
        if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srcc2qutubarto1, 392, 28);
        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
        gm_b0_1 t60 = gm_f_report_eardf07((void *)NULL, cx, NULL, t53, t54, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tf5srh4mp4b3i t61;
        memset(&t61, 0, sizeof t61);
        F.ret = t61;
        goto gm_fret;
    }
    gm_trqfvmjtt4sij t62;
    gm_f_list_j6fu443((void *)&t62, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l10 = t62;
    gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
    gm_l11 = t63;
    gm_l12 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t64 = gm_l12;
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
            uint8_t t66 = (t64 < t65);
            if (!t66) {
                goto gm_lb6;
            }
            {
                void * t67 = GM_LD(void *, ((uint8_t *)&gm_l11));
                uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint64_t t69 = gm_l12;
                {
                    {
                        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)t67) + (uintptr_t)t69 * 16));
                        gm_l13 = t70;
                        void * t71 = (void *)((uint8_t *)&F.l10);
                        void * t72 = gm_l0;
                        gm_b16_8 t73 = gm_l13;
                        gm_f_load_file_rdflf4e((void *)&gm_l23, (void *)NULL, cx, NULL, t72, t73);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t74 = (uint8_t)((((uint8_t *)&gm_l23))[32] != 0);
                        if (t74) {
                            memmove(((uint8_t *)&gm_l24), ((uint8_t *)&gm_l23), 32);
                        } else {
                            gm_tf5srh4mp4b3i t75;
                            memset(&t75, 0, sizeof t75);
                            F.ret = t75;
                            goto gm_fret;
                        }
                        gm_tftb1odc739t5 t76 = gm_f_push_5h5ofgv(t71, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l24)));
                        gm_l25 = t76;
                        uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                        if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcc2qutubarto1, 396, 28);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 396, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t78 = gm_l12;
            uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
            uint64_t t81 = r79;
            gm_l12 = t81;
        }
    }
gm_lb6:;
    uint8_t t82 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
    if (t82) {
        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
        gm_l26 = t83;
    } else {
        gm_b16_8 t84 = gm_l5;
        gm_f_file_name_em7v8lv((void *)&gm_l27, (void *)NULL, cx, NULL, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t85 = (uint8_t)((((uint8_t *)&gm_l27))[16] != 0);
        if (t85) {
            gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
            gm_l28 = t86;
        } else {
            gm_b16_8 t87;
            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sbgl5dgur8udg));
            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(6));
            gm_l28 = t87;
        }
        gm_b16_8 t88 = gm_l28;
        gm_l26 = t88;
    }
    gm_b16_8 t89 = gm_l26;
    gm_l14 = t89;
    void * t90 = gm_l0;
    gm_b16_8 t91 = gm_l5;
    gm_tf5srh4mp4b3i t92 = gm_l3;
    uint64_t t93 = gm_f_package_of_hul50on((void *)NULL, cx, NULL, t90, t91, t92);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l15 = t93;
    void * t94 = gm_l0;
    gm_b16_8 t95 = gm_l14;
    gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
    gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
    uint64_t t98 = gm_f_add_module_3cgsegf((void *)NULL, cx, NULL, t94, t95, t97);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l16 = t98;
    void * t99 = gm_l0;
    void * t100 = GM_LD(void *, (((uint8_t *)t99) + 200));
    uint64_t t101 = GM_LD(uint64_t, ((((uint8_t *)t99) + 200) + 8));
    uint64_t t102 = gm_l16;
    if ((uintptr_t)t102 >= (uintptr_t)(t101)) gm_trap_index((uintptr_t)t102, t101, gm_srcc2qutubarto1, 400, 5);
    gm_b16_8 t103 = gm_l1;
    GM_ST(gm_b16_8, ((((uint8_t *)t100) + (uintptr_t)t102 * 184) + 120), t103);
    void * t104 = gm_l0;
    void * t105 = GM_LD(void *, (((uint8_t *)t104) + 200));
    uint64_t t106 = GM_LD(uint64_t, ((((uint8_t *)t104) + 200) + 8));
    uint64_t t107 = gm_l16;
    if ((uintptr_t)t107 >= (uintptr_t)(t106)) gm_trap_index((uintptr_t)t107, t106, gm_srcc2qutubarto1, 402, 5);
    gm_b16_8 t108 = gm_l5;
    void * t109 = gm_l0;
    gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)t109) + 168));
    uint8_t t111 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t108, t110);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, ((((uint8_t *)t105) + (uintptr_t)t107 * 184) + 113), t111);
    void * t112 = gm_l0;
    void * t113 = GM_LD(void *, (((uint8_t *)t112) + 200));
    uint64_t t114 = GM_LD(uint64_t, ((((uint8_t *)t112) + 200) + 8));
    uint64_t t115 = gm_l16;
    if ((uintptr_t)t115 >= (uintptr_t)(t114)) gm_trap_index((uintptr_t)t115, t114, gm_srcc2qutubarto1, 403, 5);
    uint64_t t116 = gm_l15;
    GM_ST(uint64_t, ((((uint8_t *)t113) + (uintptr_t)t115 * 184) + 136), t116);
    void * t117 = gm_l0;
    void * t118 = (void *)(((uint8_t *)t117) + 472);
    gm_b16_8 t119 = gm_l5;
    uint64_t t120 = gm_l16;
    gm_t3877pk7lv748 t121;
    GM_ST(gm_b16_8, ((uint8_t *)&t121 + 0), t119);
    GM_ST(uint64_t, ((uint8_t *)&t121 + 16), t120);
    gm_tftb1odc739t5 t122 = gm_f_push_46vfpsn(t118, &t121);
    gm_l29 = t122;
    uint8_t t123 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
    if (!t123) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcc2qutubarto1, 404, 5);
    void * t124 = gm_l0;
    uint64_t t125 = gm_l16;
    gm_b0_1 t126 = gm_f_collect_whens_ktf37vb((void *)NULL, cx, NULL, t124, t125);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t127 = gm_l16;
    gm_tf5srh4mp4b3i t128;
    GM_ST(uint8_t, (uint8_t *)&t128 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t128 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t128 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t128), t127);
    ((uint8_t *)&t128)[8] = 1;
    F.ret = t128;
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
inline __attribute__((aligned(4))) void gm_f_load_file_rdflf4e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_load_file_rdflf4e F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* text */
    gm_tj5jn79kq9pnm gm_l3; /* e */
    gm_b16_8 gm_l4; /* text */
    GM_STACK_CHECK(&F, 707u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l5;
    gm_tgvh15qshf643 gm_l6;
    gm_b16_8 gm_l7;
    gm_tabb710d9mq2e gm_l8;
    {
        {
            void * t4 = gm_l0;
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 32));
            gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
            gm_b16_8 t7 = gm_l1;
            gm_f_overlay_text_haaaurk((void *)&gm_l5, (void *)NULL, cx, NULL, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
            if (!t8) {
                goto gm_lb2;
            }
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
            gm_l2 = t9;
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_l1;
            gm_b16_8 t12 = gm_l2;
            gm_t08olq97rscn3 t13;
            gm_f_parse_text_j7gp86k((void *)&t13, (void *)NULL, cx, NULL, t10, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t1s75i08qtptd t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 36, 0);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[32] = 1;
            memmove(gm_out, ((uint8_t *)&t14), 40);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t15 = gm_l1;
    gm_f_read_all_r9rh9qd((void *)&gm_l6, (void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t16) {
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_l7 = t17;
    } else {
        gm_tj5jn79kq9pnm t18 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&gm_l6) + 8));
        gm_l3 = t18;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 8));
        gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
        gm_b16_8 t22 = gm_l1;
        gm_tj5jn79kq9pnm t23 = gm_l3;
        gm_b16_8 t24 = gm_f_describe_read_error_uikkr59((void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t25;
        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t22);
        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 16), t24);
        gm_f_format_04pispc((void *)&gm_l8, (void *)NULL, cx, NULL, &t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcc2qutubarto1, 335, 38);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_b0_1 t29 = gm_f_report_eardf07((void *)NULL, cx, NULL, t20, t21, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t1s75i08qtptd t30;
        memset(&t30, 0, sizeof t30);
        memmove(gm_out, ((uint8_t *)&t30), 40);
        goto gm_fdone;
    }
    gm_b16_8 t31 = gm_l7;
    gm_l4 = t31;
    void * t32 = gm_l0;
    gm_b16_8 t33 = gm_l1;
    gm_b16_8 t34 = gm_l4;
    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
    gm_t08olq97rscn3 t36;
    gm_f_parse_text_j7gp86k((void *)&t36, (void *)NULL, cx, NULL, t32, t33, t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t1s75i08qtptd t37;
    GM_ST(uint8_t, (uint8_t *)&t37 + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&t37 + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&t37 + 36, 0);
    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t37), t36);
    ((uint8_t *)&t37)[32] = 1;
    memmove(gm_out, ((uint8_t *)&t37), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_load_modules_2vujekg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_load_modules_2vujekg F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* root */
    uint64_t gm_l2; /* done */
    uint64_t gm_l3; /* m */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* d */
    GM_STACK_CHECK(&F, 428u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l7;
    uint64_t gm_l8;
    void * t10 = gm_l0;
    gm_b16_8 t11 = gm_l1;
    gm_tf5srh4mp4b3i t12 = gm_f_load_root_af4ud7b((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t12;
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
    if (t13) {
        uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
        gm_l8 = t14;
    } else {
        gm_th2lqbo77pvsc t15;
        memset(&t15, 0, sizeof t15);
        memmove(gm_out, ((uint8_t *)&t15), 24);
        goto gm_fdone;
    }
    uint64_t t16 = gm_l8;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l2;
            void * t18 = gm_l0;
            uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)t18) + 200) + 8));
            uint8_t t20 = (t17 < t19);
            if (!t20) {
                goto gm_lb1;
            }
            {
                uint64_t t21 = gm_l2;
                gm_l3 = t21;
                uint64_t t22 = gm_l2;
                uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                if (o24) gm_trap("integer overflow in `+`", gm_srcc2qutubarto1, 193, 9);
                uint64_t t25 = r23;
                gm_l2 = t25;
                void * t26 = gm_l0;
                void * t27 = GM_LD(void *, (((uint8_t *)t26) + 200));
                uint64_t t28 = GM_LD(uint64_t, ((((uint8_t *)t26) + 200) + 8));
                uint64_t t29 = gm_l3;
                if ((uintptr_t)t29 >= (uintptr_t)(t28)) gm_trap_index((uintptr_t)t29, t28, gm_srcc2qutubarto1, 194, 12);
                uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t27) + (uintptr_t)t29 * 184) + 112));
                if (t30) {
                    goto gm_lb3;
                }
                void * t31 = gm_l0;
                uint64_t t32 = gm_l3;
                gm_b16_8 t33 = gm_f_imports_of_5rkfdia((void *)NULL, cx, NULL, t31, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t33;
                gm_l5 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t34 = gm_l5;
                        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint8_t t36 = (t34 < t35);
                        if (!t36) {
                            goto gm_lb4;
                        }
                        {
                            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l4));
                            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            uint64_t t39 = gm_l5;
                            {
                                {
                                    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t37) + (uintptr_t)t39 * 8));
                                    gm_l6 = t40;
                                    void * t41 = gm_l0;
                                    uint64_t t42 = gm_l6;
                                    gm_b0_1 t43 = gm_f_resolve_import_944uqa9((void *)NULL, cx, NULL, t41, t42);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb7;
                                }
gm_lb8:;
                                gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 195, 9);
                            }
gm_lb7:;
                        }
gm_lb6:;
                        uint64_t t44 = gm_l5;
                        uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                        uint64_t t47 = r45;
                        gm_l5 = t47;
                    }
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t48 = gm_l0;
    gm_b0_1 t49 = gm_f_check_import_cycles_2u7oqo0((void *)NULL, cx, NULL, t48);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t50 = gm_l0;
    gm_b16_8 t51 = gm_f_dump_fugpeqi((void *)NULL, cx, NULL, t50);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_th2lqbo77pvsc t52;
    GM_ST(uint8_t, (uint8_t *)&t52 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t52 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t52 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t52), t51);
    ((uint8_t *)&t52)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t52), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_load_package_modules_t4hh6gf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_load_package_modules_t4hh6gf F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* root */
    uint64_t gm_l2; /* package */
    gm_b16_8 gm_l3; /* package_root */
    gm_b16_8 gm_l4; /* start */
    gm_b16_8 gm_l5; /* rel */
    gm_b16_8 gm_l6; /* root_dir */
    gm_b16_8 gm_l7; /* root_key */
    gm_b16_8 gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* key */
    uint8_t gm_l14; /* loaded */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_t3877pk7lv748 gm_l17; /* d */
    GM_STACK_CHECK(&F, 1292u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l19;
    gm_b16_8 gm_l20;
    gm_th2lqbo77pvsc gm_l21;
    gm_b16_8 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_th2lqbo77pvsc gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_th2lqbo77pvsc gm_l26;
    gm_b16_8 gm_l27;
    gm_tqetrgo0q24b5 gm_l28;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, (((uint8_t *)t15) + 200));
    uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t15) + 200) + 8));
    uint64_t t18 = gm_l1;
    if ((uintptr_t)t18 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t18, t17, gm_srcc2qutubarto1, 414, 16);
    uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)t16) + (uintptr_t)t18 * 184) + 136));
    gm_l2 = t19;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 432));
    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 432) + 8));
    uint64_t t23 = gm_l2;
    if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcc2qutubarto1, 415, 21);
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t21) + (uintptr_t)t23 * 56));
    gm_l3 = t24;
    gm_b16_8 t25 = gm_l3;
    gm_l4 = t25;
    {
        {
            gm_b16_8 t26 = gm_l3;
            void * t27 = gm_l0;
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t27) + 184));
            gm_f_strip_prefix_vl3t0h4((void *)&gm_l19, (void *)NULL, cx, NULL, t26, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t29 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
            if (!t29) {
                goto gm_lb2;
            }
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
            gm_l5 = t30;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t32 = ((t31 == ((uint64_t)0ULL)));
            if (t32) {
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sl3q52ep6aa53));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(1));
                gm_l20 = t33;
            } else {
                gm_b16_8 t34 = gm_l5;
                gm_l20 = t34;
            }
            gm_b16_8 t35 = gm_l20;
            gm_l4 = t35;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t36 = gm_l0;
    void * t37 = GM_LD(void *, (((uint8_t *)t36) + 200));
    uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)t36) + 200) + 8));
    uint64_t t39 = gm_l1;
    if ((uintptr_t)t39 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t39, t38, gm_srcc2qutubarto1, 420, 17);
    gm_b16_8 t40 = GM_LD(gm_b16_8, ((((uint8_t *)t37) + (uintptr_t)t39 * 184) + 120));
    gm_l6 = t40;
    gm_b16_8 t41 = gm_l6;
    gm_f_canonicalize_n3e3qri((void *)&gm_l21, (void *)NULL, cx, NULL, t41);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l21))[16] != 0);
    if (t42) {
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
        gm_l22 = t43;
    } else {
        gm_b16_8 t44 = gm_l6;
        gm_l22 = t44;
    }
    gm_b16_8 t45 = gm_l22;
    gm_l7 = t45;
    gm_trqfvmjtt4sij t46;
    gm_f_list_epvducg((void *)&t46, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l8 = t46;
    void * t47 = (void *)((uint8_t *)&F.l8);
    gm_b16_8 t48 = gm_l4;
    gm_tftb1odc739t5 t49 = gm_f_push_3ef746b(t47, t48);
    gm_l23 = t49;
    uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcc2qutubarto1, 423, 5);
    {
        for (;;) {
            void * t52 = (void *)((uint8_t *)&F.l8);
            gm_f_pop_vr08sii((void *)&gm_l24, t52);
            uint8_t t53 = (uint8_t)((((uint8_t *)&gm_l24))[16] != 0);
            if (!t53) {
                goto gm_lb3;
            }
            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l24));
            F.l9 = t54;
            {
                gm_trqfvmjtt4sij t55;
                gm_f_list_epvducg((void *)&t55, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ll51;
                }
                F.l10 = t55;
                F.l11 = ((uint8_t)1);
                gm_b16_8 t56 = F.l9;
                gm_l12 = t56;
                {
                    gm_node wn57 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hl3gh4gojekg, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
                    gm_b16_8 t60 = gm_l12;
                    gm_b0_1 t61 = gm_f_entries_44o5n9f_siirbaa((void *)NULL, (&wn57), NULL, t60);
                    if (gm_unw.kind) {
                        goto gm_ll59;
                    }
                }
                goto gm_ls62;
gm_ll59:;
                if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we58; }
                goto gm_ll51;
gm_ls62:;
gm_lb6:;
we58:;
                uint8_t t63 = F.l11;
                uint8_t t64 = (uint8_t)!t63;
                if (t64) {
                    goto gm_lb5;
                }
                gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                gm_f_sort_skeree7(t65);
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                gm_f_reverse_cgmjfos(t66);
                void * t67 = (void *)((uint8_t *)&F.l8);
                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                gm_tftb1odc739t5 t70 = gm_f_extend_vogsd72(t67, t69);
                gm_l25 = t70;
                uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcc2qutubarto1, 441, 9);
                gm_b16_8 t72 = F.l9;
                gm_f_canonicalize_n3e3qri((void *)&gm_l26, (void *)NULL, cx, NULL, t72);
                if (gm_unw.kind) {
                    goto gm_ll51;
                }
                uint8_t t73 = (uint8_t)((((uint8_t *)&gm_l26))[16] != 0);
                if (t73) {
                    gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l26));
                    gm_l27 = t74;
                } else {
                    gm_b16_8 t75 = F.l9;
                    gm_l27 = t75;
                }
                gm_b16_8 t76 = gm_l27;
                gm_l13 = t76;
                gm_b16_8 t77 = gm_l13;
                gm_b16_8 t78 = gm_l7;
                uint8_t t79 = (gm_eq0iqiubfm7e(((uint8_t *)&t77), ((uint8_t *)&t78)));
                if (t79) {
                    goto gm_lb5;
                }
                gm_l14 = ((uint8_t)0);
                void * t80 = gm_l0;
                gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)t80) + 472));
                gm_l15 = t81;
                gm_l16 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t83 = gm_l16;
                        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        uint8_t t85 = (t83 < t84);
                        if (!t85) {
                            goto gm_lb7;
                        }
                        {
                            void * t86 = GM_LD(void *, ((uint8_t *)&gm_l15));
                            uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint64_t t88 = gm_l16;
                            {
                                {
                                    memmove(((uint8_t *)&gm_l17), (((uint8_t *)t86) + (uintptr_t)t88 * 24), 24);
                                    gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
                                    gm_b16_8 t90 = gm_l13;
                                    uint8_t t91 = (gm_eq0iqiubfm7e(((uint8_t *)&t89), ((uint8_t *)&t90)));
                                    if (t91) {
                                        gm_l14 = ((uint8_t)1);
                                    }
                                    goto gm_lb10;
                                }
gm_lb11:;
                                gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 445, 9);
                            }
gm_lb10:;
                        }
gm_lb9:;
                        uint64_t t92 = gm_l16;
                        uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                        uint64_t t95 = r93;
                        gm_l16 = t95;
                    }
                }
gm_lb7:;
                uint8_t t96 = gm_l14;
                if (t96) {
                    goto gm_lb5;
                }
                {
                    {
                        gm_b16_8 t97 = F.l9;
                        gm_f_module_files_8j8gjpm((void *)&gm_l28, (void *)NULL, cx, NULL, t97);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                        uint8_t t98 = (uint8_t)((((uint8_t *)&gm_l28))[40] != 0);
                        if (!t98) {
                            goto gm_lb13;
                        }
                        memmove(((uint8_t *)&F.l18), ((uint8_t *)&gm_l28), 40);
                        uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&F.l18) + 8));
                        uint8_t t100 = (t99 > ((uint64_t)0ULL));
                        if (t100) {
                            void * t101 = gm_l0;
                            gm_b16_8 t102 = F.l9;
                            gm_tl874t2atnlh3 t103 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                            uint64_t t104 = gm_l2;
                            gm_tf5srh4mp4b3i t105;
                            GM_ST(uint8_t, (uint8_t *)&t105 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t105 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t105 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t105), t104);
                            ((uint8_t *)&t105)[8] = 1;
                            gm_th2lqbo77pvsc t106;
                            memset(&t106, 0, sizeof t106);
                            gm_tf5srh4mp4b3i t107 = gm_f_load_dir_module_a75lmvr((void *)NULL, cx, NULL, t101, t102, t103, t105, &t106);
                            if (gm_unw.kind) {
                                goto gm_ll51;
                            }
                        }
                        goto gm_lb12;
                    }
gm_lb13:;
                }
gm_lb12:;
            }
gm_lb5:;
        }
    }
    goto gm_ls108;
gm_ll51:;
    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb3; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb5; }
    }
    goto gm_lad;
gm_ls108:;
gm_lb3:;
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_load_root_af4ud7b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_load_root_af4ud7b F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* root */
    uint64_t gm_l2; /* m */
    gm_t08olq97rscn3 gm_l3; /* file */
    gm_b16_8 gm_l4; /* dir */
    gm_b16_8 gm_l5; /* canon */
    uint64_t gm_l6; /* package */
    uint64_t gm_l8; /* m */
    GM_STACK_CHECK(&F, 1279u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    gm_tqetrgo0q24b5 gm_l12;
    gm_tf5srh4mp4b3i gm_l13;
    uint64_t gm_l14;
    gm_t1s75i08qtptd gm_l15;
    gm_t08olq97rscn3 gm_l16;
    gm_th2lqbo77pvsc gm_l17;
    gm_b16_8 gm_l18;
    gm_th2lqbo77pvsc gm_l19;
    gm_b16_8 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_th2lqbo77pvsc gm_l22;
    gm_b16_8 gm_l23;
    gm_b16_8 t9 = gm_l1;
    uint8_t t10 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t10) {
        gm_l9 = ((uint8_t)1);
    } else {
        gm_b16_8 t11 = gm_l1;
        uint8_t t12 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t13 = (uint8_t)!t12;
        if (t13) {
            gm_b16_8 t14 = gm_l1;
            void * t15 = gm_l0;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 32));
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            gm_f_module_files_with_abkh6r4((void *)&gm_l12, (void *)NULL, cx, NULL, t14, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l12))[40] != 0);
                    if (t18) {
                        goto gm_lb2;
                    }
                    gm_l11 = ((uint8_t)0);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    gm_l11 = ((uint8_t)1);
                    goto gm_lb1;
                }
gm_lb3:;
                gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 357, 47);
            }
gm_lb1:;
            uint8_t t19 = gm_l11;
            gm_l10 = t19;
        } else {
            gm_l10 = ((uint8_t)0);
        }
        uint8_t t20 = gm_l10;
        gm_l9 = t20;
    }
    uint8_t t21 = gm_l9;
    if (t21) {
        void * t22 = gm_l0;
        gm_b16_8 t23 = gm_l1;
        gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
        gm_tf5srh4mp4b3i t25;
        memset(&t25, 0, sizeof t25);
        gm_th2lqbo77pvsc t26;
        memset(&t26, 0, sizeof t26);
        gm_tf5srh4mp4b3i t27 = gm_f_load_dir_module_a75lmvr((void *)NULL, cx, NULL, t22, t23, t24, t25, &t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t27;
        uint8_t t28 = (uint8_t)((((uint8_t *)&gm_l13))[8] != 0);
        if (t28) {
            uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&gm_l13));
            gm_l14 = t29;
        } else {
            gm_tf5srh4mp4b3i t30;
            memset(&t30, 0, sizeof t30);
            F.ret = t30;
            goto gm_fret;
        }
        uint64_t t31 = gm_l14;
        gm_l2 = t31;
        {
            {
                void * t32 = gm_l0;
                void * t33 = GM_LD(void *, (((uint8_t *)t32) + 8));
                uint8_t t34 = gm_f_has_errors_if7ai0m(t33);
                if (!t34) {
                    goto gm_lb5;
                }
                void * t35 = gm_l0;
                uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t35) + 25));
                uint8_t t37 = (uint8_t)!t36;
                if (!t37) {
                    goto gm_lb5;
                }
                gm_tf5srh4mp4b3i t38;
                memset(&t38, 0, sizeof t38);
                F.ret = t38;
                goto gm_fret;
            }
gm_lb5:;
        }
gm_lb4:;
        uint64_t t39 = gm_l2;
        gm_tf5srh4mp4b3i t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t40), t39);
        ((uint8_t *)&t40)[8] = 1;
        F.ret = t40;
        goto gm_fret;
    }
    void * t41 = gm_l0;
    gm_b16_8 t42 = gm_l1;
    gm_f_load_file_rdflf4e((void *)&gm_l15, (void *)NULL, cx, NULL, t41, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l15))[32] != 0);
    if (t43) {
        memmove(((uint8_t *)&gm_l16), ((uint8_t *)&gm_l15), 32);
    } else {
        gm_tf5srh4mp4b3i t44;
        memset(&t44, 0, sizeof t44);
        F.ret = t44;
        goto gm_fret;
    }
    memmove(((uint8_t *)&gm_l3), ((uint8_t *)&gm_l16), 32);
    {
        {
            void * t45 = gm_l0;
            void * t46 = GM_LD(void *, (((uint8_t *)t45) + 8));
            uint8_t t47 = gm_f_has_errors_if7ai0m(t46);
            if (!t47) {
                goto gm_lb7;
            }
            void * t48 = gm_l0;
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)t48) + 25));
            uint8_t t50 = (uint8_t)!t49;
            if (!t50) {
                goto gm_lb7;
            }
            gm_tf5srh4mp4b3i t51;
            memset(&t51, 0, sizeof t51);
            F.ret = t51;
            goto gm_fret;
        }
gm_lb7:;
    }
gm_lb6:;
    gm_b16_8 t52 = gm_l1;
    gm_f_parent_lk0iero((void *)&gm_l17, (void *)NULL, cx, NULL, t52);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t53 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
    if (t53) {
        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
        gm_l18 = t54;
    } else {
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(1));
        gm_l18 = t55;
    }
    gm_b16_8 t56 = gm_l18;
    gm_l4 = t56;
    uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint8_t t58 = ((t57 == ((uint64_t)0ULL)));
    if (t58) {
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(1));
        gm_l4 = t59;
    }
    gm_b16_8 t60 = gm_l4;
    gm_f_canonicalize_n3e3qri((void *)&gm_l19, (void *)NULL, cx, NULL, t60);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t61 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
    if (t61) {
        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
        gm_l20 = t62;
    } else {
        gm_b16_8 t63 = gm_l4;
        gm_l20 = t63;
    }
    gm_b16_8 t64 = gm_l20;
    gm_l5 = t64;
    void * t65 = gm_l0;
    gm_b16_8 t66 = gm_l5;
    gm_tf5srh4mp4b3i t67;
    memset(&t67, 0, sizeof t67);
    uint64_t t68 = gm_f_package_of_hul50on((void *)NULL, cx, NULL, t65, t66, t67);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t68;
    gm_trqfvmjtt4sij t69;
    gm_f_list_j6fu443((void *)&t69, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l7 = t69;
    void * t70 = (void *)((uint8_t *)&F.l7);
    gm_tftb1odc739t5 t71 = gm_f_push_5h5ofgv(t70, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l3)));
    gm_l21 = t71;
    uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
    if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcc2qutubarto1, 370, 5);
    void * t73 = gm_l0;
    gm_b16_8 t74 = gm_l1;
    gm_f_file_stem_v6vrs6i((void *)&gm_l22, (void *)NULL, cx, NULL, t74);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t75 = (uint8_t)((((uint8_t *)&gm_l22))[16] != 0);
    if (t75) {
        gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
        gm_l23 = t76;
    } else {
        gm_b16_8 t77;
        GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sog4rksrmsqd2));
        GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(4));
        gm_l23 = t77;
    }
    gm_b16_8 t78 = gm_l23;
    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
    uint64_t t81 = gm_f_add_module_3cgsegf((void *)NULL, cx, NULL, t73, t78, t80);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t81;
    void * t82 = gm_l0;
    void * t83 = GM_LD(void *, (((uint8_t *)t82) + 200));
    uint64_t t84 = GM_LD(uint64_t, ((((uint8_t *)t82) + 200) + 8));
    uint64_t t85 = gm_l8;
    if ((uintptr_t)t85 >= (uintptr_t)(t84)) gm_trap_index((uintptr_t)t85, t84, gm_srcc2qutubarto1, 372, 5);
    gm_b16_8 t86 = gm_l4;
    GM_ST(gm_b16_8, ((((uint8_t *)t83) + (uintptr_t)t85 * 184) + 120), t86);
    void * t87 = gm_l0;
    void * t88 = GM_LD(void *, (((uint8_t *)t87) + 200));
    uint64_t t89 = GM_LD(uint64_t, ((((uint8_t *)t87) + 200) + 8));
    uint64_t t90 = gm_l8;
    if ((uintptr_t)t90 >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)t90, t89, gm_srcc2qutubarto1, 373, 5);
    uint64_t t91 = gm_l6;
    GM_ST(uint64_t, ((((uint8_t *)t88) + (uintptr_t)t90 * 184) + 136), t91);
    void * t92 = gm_l0;
    uint64_t t93 = gm_l8;
    gm_b0_1 t94 = gm_f_collect_whens_ktf37vb((void *)NULL, cx, NULL, t92, t93);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t95 = gm_l8;
    gm_tf5srh4mp4b3i t96;
    GM_ST(uint8_t, (uint8_t *)&t96 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t96 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t96 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t96), t95);
    ((uint8_t *)&t96)[8] = 1;
    F.ret = t96;
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
inline __attribute__((aligned(4))) void gm_f_loader_on0up0a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, uint8_t a4) {
    struct gm_fr_loader_on0up0a F;
    void * gm_l0; /* sm */
    void * gm_l1; /* diags */
    void * gm_l2; /* ids */
    gm_b16_8 gm_l3; /* std_root */
    uint8_t gm_l4; /* recover */
    void * gm_l5; /* <tmp> */
    void * gm_l6; /* <tmp> */
    void * gm_l7; /* <tmp> */
    uint8_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l11; /* <tmp> */
    gm_b16_8 gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    GM_STACK_CHECK(&F, 1381u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_th2lqbo77pvsc gm_l22;
    gm_b16_8 gm_l23;
    void * t2 = gm_l0;
    gm_l5 = t2;
    void * t3 = gm_l1;
    gm_l6 = t3;
    void * t4 = gm_l2;
    gm_l7 = t4;
    uint8_t t5 = gm_l4;
    gm_l8 = t5;
    gm_f_host_target_vsu78om((void *)&F.l9);
    gm_f_list_j41vvuc((void *)&F.l10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t6 = gm_l3;
    gm_l11 = t6;
    gm_b16_8 t7 = gm_l3;
    gm_l12 = t7;
    gm_b16_8 t8 = gm_f_working_dir_dj7eq93((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l13 = t8;
    gm_f_list_5msmod6((void *)&F.l14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_list_016fo30_2((void *)&F.l15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_list_g6e4eeu((void *)&F.l16, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_list_5dhgg4i((void *)&F.l17, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_list_vdg8k7c((void *)&F.l18, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_list_12lt7k0((void *)&F.l19, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_list_6kkl75k((void *)&F.l20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t9 = gm_l5;
    void * t10 = gm_l6;
    void * t11 = gm_l7;
    uint8_t t12 = gm_l8;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)gm_cuqbj62gqhf_3));
    gm_b16_8 t14 = gm_l11;
    gm_b16_8 t15 = gm_l12;
    gm_b16_8 t16 = gm_l13;
    gm_tf5srh4mp4b3i t17;
    memset(&t17, 0, sizeof t17);
    gm_tv37d3dpt2opu t18;
    memset(&t18, 0, sizeof t18);
    gm_trqfvmjtt4sij t19 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l10));
    gm_t0rfe1ou14h21 t20 = GM_LD(gm_t0rfe1ou14h21, ((uint8_t *)&F.l9));
    gm_trqfvmjtt4sij t21 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l14));
    gm_trqfvmjtt4sij t22 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l15));
    gm_trqfvmjtt4sij t23 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l16));
    gm_trqfvmjtt4sij t24 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l17));
    gm_trqfvmjtt4sij t25 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l18));
    gm_trqfvmjtt4sij t26 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l19));
    gm_trqfvmjtt4sij t27 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l20));
    gm_tcuh1vih4g9cr t28;
    GM_ST(uint16_t, (uint8_t *)&t28 + 26, 0);
    GM_ST(uint32_t, (uint8_t *)&t28 + 28, 0);
    GM_ST(void *, ((uint8_t *)&t28 + 0), t9);
    GM_ST(void *, ((uint8_t *)&t28 + 8), t10);
    GM_ST(void *, ((uint8_t *)&t28 + 16), t11);
    GM_ST(uint8_t, ((uint8_t *)&t28 + 24), t12);
    GM_ST(uint8_t, ((uint8_t *)&t28 + 25), ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t28 + 32), t19);
    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 72), t13);
    GM_ST(gm_t0rfe1ou14h21, ((uint8_t *)&t28 + 88), t20);
    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 152), t14);
    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 168), t15);
    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 184), t16);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t28 + 200), t21);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t28 + 240), t22);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t28 + 280), t23);
    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t28 + 320), t17);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t28 + 336), t24);
    GM_ST(gm_tv37d3dpt2opu, ((uint8_t *)&t28 + 376), t18);
    GM_ST(void *, ((uint8_t *)&t28 + 424), ((void *)0));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t28 + 432), t25);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t28 + 472), t26);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t28 + 512), t27);
    F.l21 = t28;
    gm_b16_8 t29 = gm_l3;
    gm_f_canonicalize_n3e3qri((void *)&gm_l22, (void *)NULL, cx, NULL, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l22))[16] != 0);
    if (t30) {
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
        gm_l23 = t31;
    } else {
        gm_b16_8 t32 = gm_l3;
        gm_l23 = t32;
    }
    gm_b16_8 t33 = gm_l23;
    GM_ST(gm_b16_8, (((uint8_t *)&F.l21) + 168), t33);
    memmove(gm_out, ((uint8_t *)&F.l21), 552);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_module_files_8j8gjpm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_module_files_8j8gjpm F;
    gm_b16_8 gm_l0; /* dir */
    GM_STACK_CHECK(&F, 331u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_ttevl7jqjigkp gm_l1;
    gm_tqetrgo0q24b5 gm_l2;
    gm_b16_8 t2 = gm_l0;
    uint8_t t3 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = (uint8_t)!t3;
    if (t4) {
        gm_tqetrgo0q24b5 t5;
        memset(&t5, 0, sizeof t5);
        memmove(gm_out, ((uint8_t *)&t5), 48);
        goto gm_fdone;
    }
    gm_b16_8 t6 = gm_l0;
    gm_f_dir_gm_files_0s2opd8((void *)&gm_l1, (void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (t7) {
        gm_trqfvmjtt4sij t8 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&gm_l1) + 8));
        gm_tqetrgo0q24b5 t9;
        GM_ST(uint8_t, (uint8_t *)&t9 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t9 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t9 + 44, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t9), t8);
        ((uint8_t *)&t9)[40] = 1;
        gm_l2 = t9;
    } else {
        gm_tqetrgo0q24b5 t10;
        memset(&t10, 0, sizeof t10);
        gm_l2 = t10;
    }
    memmove(gm_out, ((uint8_t *)&gm_l2), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_module_files_with_abkh6r4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_module_files_with_abkh6r4 F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* overlays */
    gm_b16_8 gm_l3; /* key */
    uint64_t gm_l5; /* n */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t6j6smj7qoctu gm_l8; /* o */
    gm_b16_8 gm_l9; /* name */
    uint8_t gm_l10; /* present */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* p */
    GM_STACK_CHECK(&F, 1003u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_trqfvmjtt4sij gm_l14;
    gm_th2lqbo77pvsc gm_l15;
    gm_b16_8 gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_th2lqbo77pvsc gm_l19;
    gm_b16_8 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    uint8_t gm_l22;
    gm_b16_8 t17 = gm_l0;
    gm_f_module_files_8j8gjpm((void *)&F.l2, (void *)NULL, cx, NULL, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t19 = ((t18 == ((uint64_t)0ULL)));
    if (t19) {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
        goto gm_fdone;
    }
    gm_b16_8 t20 = gm_l0;
    gm_b16_8 t21 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t21;
    uint8_t t22 = (uint8_t)((((uint8_t *)&F.l2))[40] != 0);
    if (t22) {
        memmove(((uint8_t *)&gm_l14), ((uint8_t *)&F.l2), 40);
    } else {
        gm_f_list_epvducg((void *)&gm_l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    memmove(((uint8_t *)&F.l4), ((uint8_t *)&gm_l14), 40);
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
    gm_l5 = t23;
    gm_b16_8 t24 = gm_l1;
    gm_l6 = t24;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t25 = gm_l7;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                void * t28 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t30 = gm_l7;
                {
                    {
                        memmove(((uint8_t *)&gm_l8), (((uint8_t *)t28) + (uintptr_t)t30 * 32), 32);
                        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
                        gm_f_file_name_em7v8lv((void *)&gm_l15, (void *)NULL, cx, NULL, t31);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t32 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                        if (t32) {
                            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                            gm_l16 = t33;
                        } else {
                            goto gm_lb3;
                        }
                        gm_b16_8 t34 = gm_l16;
                        gm_l9 = t34;
                        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
                        gm_f_parent_lk0iero((void *)&gm_l19, (void *)NULL, cx, NULL, t35);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
                        if (t36) {
                            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
                            gm_l20 = t37;
                        } else {
                            gm_b16_8 t38;
                            GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(0));
                            gm_l20 = t38;
                        }
                        gm_b16_8 t39 = gm_l20;
                        gm_b16_8 t40 = gm_l3;
                        uint8_t t41 = (!gm_eq0iqiubfm7e(((uint8_t *)&t39), ((uint8_t *)&t40)));
                        if (t41) {
                            gm_l18 = ((uint8_t)1);
                        } else {
                            gm_b16_8 t42 = gm_l9;
                            gm_b16_8 t43;
                            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s0ucactj6hj0a));
                            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(3));
                            uint8_t t44 = gm_f_ends_with_biriu5l(t42, t43);
                            uint8_t t45 = (uint8_t)!t44;
                            gm_l18 = t45;
                        }
                        uint8_t t46 = gm_l18;
                        if (t46) {
                            gm_l17 = ((uint8_t)1);
                        } else {
                            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                            uint8_t t48 = (t47 <= ((uint64_t)3ULL));
                            gm_l17 = t48;
                        }
                        uint8_t t49 = gm_l17;
                        if (t49) {
                            goto gm_lb3;
                        }
                        gm_l10 = ((uint8_t)0);
                        void * t50 = GM_LD(void *, ((uint8_t *)&F.l4));
                        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                        uint64_t t52 = gm_l5;
                        uint64_t t53 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t54 = (uintptr_t)t52;
                        if (t53 > t54 || t54 > (uintptr_t)t51) gm_trap_slice(t53, t54, t51, gm_srcc2qutubarto1, 322, 18);
                        uint64_t t55 = ((uintptr_t)t52) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t56;
                        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t50 + (uintptr_t)((uint64_t)0ULL) * 16)));
                        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55));
                        gm_l11 = t56;
                        gm_l12 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t57 = gm_l12;
                                uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                uint8_t t59 = (t57 < t58);
                                if (!t59) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t60 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                    uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                    uint64_t t62 = gm_l12;
                                    {
                                        {
                                            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)t60) + (uintptr_t)t62 * 16));
                                            gm_l13 = t63;
                                            gm_b16_8 t64 = gm_l13;
                                            gm_b16_8 t65 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t64);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
                                            uint8_t t67 = (gm_eq0iqiubfm7e(((uint8_t *)&t65), ((uint8_t *)&t66)));
                                            if (t67) {
                                                gm_l10 = ((uint8_t)1);
                                            }
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 322, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t68 = gm_l12;
                                uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                                uint64_t t71 = r69;
                                gm_l12 = t71;
                            }
                        }
gm_lb6:;
                        uint8_t t72 = gm_l10;
                        uint8_t t73 = (uint8_t)!t72;
                        if (t73) {
                            void * t74 = (void *)((uint8_t *)&F.l4);
                            gm_b16_8 t75 = gm_l0;
                            gm_b16_8 t76 = gm_l9;
                            gm_b16_8 t77 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t75, t76);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t78 = gm_f_push_3ef746b(t74, t77);
                            gm_l21 = t78;
                            uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                            if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcc2qutubarto1, 325, 26);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 318, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t80 = gm_l7;
            uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
            uint64_t t83 = r81;
            gm_l7 = t83;
        }
    }
gm_lb1:;
    {
        {
            {
                {
                    uint8_t t84 = (uint8_t)((((uint8_t *)&F.l2))[40] != 0);
                    if (t84) {
                        goto gm_lb14;
                    }
                    gm_l22 = ((uint8_t)1);
                    goto gm_lb13;
                }
gm_lb14:;
                {
                    gm_l22 = ((uint8_t)0);
                    goto gm_lb13;
                }
gm_lb15:;
                gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 327, 8);
            }
gm_lb13:;
            uint8_t t85 = gm_l22;
            if (!t85) {
                goto gm_lb12;
            }
            uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
            uint8_t t87 = ((t86 == ((uint64_t)0ULL)));
            if (!t87) {
                goto gm_lb12;
            }
            gm_tqetrgo0q24b5 t88;
            memset(&t88, 0, sizeof t88);
            memmove(gm_out, ((uint8_t *)&t88), 48);
            goto gm_fdone;
        }
gm_lb12:;
    }
gm_lb11:;
    gm_trqfvmjtt4sij t89 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l4));
    gm_tqetrgo0q24b5 t90;
    GM_ST(uint8_t, (uint8_t *)&t90 + 41, 0);
    GM_ST(uint16_t, (uint8_t *)&t90 + 42, 0);
    GM_ST(uint32_t, (uint8_t *)&t90 + 44, 0);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t90), t89);
    ((uint8_t *)&t90)[40] = 1;
    memmove(gm_out, ((uint8_t *)&t90), 48);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_overlay_text_haaaurk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_overlay_text_haaaurk F;
    gm_b16_8 gm_l0; /* overlays */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* name */
    uint8_t gm_l3; /* any */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t6j6smj7qoctu gm_l6; /* o */
    gm_b16_8 gm_l7; /* key */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t6j6smj7qoctu gm_l10; /* o */
    GM_STACK_CHECK(&F, 713u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l11;
    gm_b16_8 gm_l12;
    gm_th2lqbo77pvsc gm_l13;
    gm_b16_8 gm_l14;
    gm_b16_8 t12 = gm_l1;
    gm_f_file_name_em7v8lv((void *)&gm_l11, (void *)NULL, cx, NULL, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
    if (t13) {
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
        gm_l12 = t14;
    } else {
        gm_th2lqbo77pvsc t15;
        memset(&t15, 0, sizeof t15);
        memmove(gm_out, ((uint8_t *)&t15), 24);
        goto gm_fdone;
    }
    gm_b16_8 t16 = gm_l12;
    gm_l2 = t16;
    gm_l3 = ((uint8_t)0);
    gm_b16_8 t17 = gm_l0;
    gm_l4 = t17;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t18 = gm_l5;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t20 = (t18 < t19);
            if (!t20) {
                goto gm_lb1;
            }
            {
                void * t21 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t23 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&gm_l6), (((uint8_t *)t21) + (uintptr_t)t23 * 32), 32);
                        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                        gm_f_file_name_em7v8lv((void *)&gm_l13, (void *)NULL, cx, NULL, t24);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t25 = (uint8_t)((((uint8_t *)&gm_l13))[16] != 0);
                        if (t25) {
                            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                            gm_l14 = t26;
                        } else {
                            gm_b16_8 t27;
                            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(0));
                            gm_l14 = t27;
                        }
                        gm_b16_8 t28 = gm_l14;
                        gm_b16_8 t29 = gm_l2;
                        uint8_t t30 = (gm_eq0iqiubfm7e(((uint8_t *)&t28), ((uint8_t *)&t29)));
                        if (t30) {
                            gm_l3 = ((uint8_t)1);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 299, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t31 = gm_l5;
            uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
            uint64_t t34 = r32;
            gm_l5 = t34;
        }
    }
gm_lb1:;
    uint8_t t35 = gm_l3;
    uint8_t t36 = (uint8_t)!t35;
    if (t36) {
        gm_th2lqbo77pvsc t37;
        memset(&t37, 0, sizeof t37);
        memmove(gm_out, ((uint8_t *)&t37), 24);
        goto gm_fdone;
    }
    gm_b16_8 t38 = gm_l1;
    gm_b16_8 t39 = gm_f_canonical_path_q6lq4b6((void *)NULL, cx, NULL, t38);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t39;
    gm_b16_8 t40 = gm_l0;
    gm_l8 = t40;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t41 = gm_l9;
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t43 = (t41 < t42);
            if (!t43) {
                goto gm_lb6;
            }
            {
                void * t44 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t46 = gm_l9;
                {
                    {
                        memmove(((uint8_t *)&gm_l10), (((uint8_t *)t44) + (uintptr_t)t46 * 32), 32);
                        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
                        gm_b16_8 t48 = gm_l7;
                        uint8_t t49 = (gm_eq0iqiubfm7e(((uint8_t *)&t47), ((uint8_t *)&t48)));
                        if (t49) {
                            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 16));
                            gm_th2lqbo77pvsc t51;
                            GM_ST(uint8_t, (uint8_t *)&t51 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t51 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t51 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t51), t50);
                            ((uint8_t *)&t51)[16] = 1;
                            memmove(gm_out, ((uint8_t *)&t51), 24);
                            goto gm_fdone;
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 304, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t52 = gm_l9;
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            uint64_t t55 = r53;
            gm_l9 = t55;
        }
    }
gm_lb6:;
    gm_th2lqbo77pvsc t56;
    memset(&t56, 0, sizeof t56);
    memmove(gm_out, ((uint8_t *)&t56), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) uint64_t gm_f_package_of_hul50on(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2) {
    struct gm_fr_package_of_hul50on F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* dir */
    gm_tf5srh4mp4b3i gm_l2; /* parent_package */
    gm_b16_8 gm_l3; /* root */
    gm_th2lqbo77pvsc gm_l4; /* found */
    gm_th2lqbo77pvsc gm_l5; /* cur */
    gm_b16_8 gm_l6; /* d */
    gm_b16_8 gm_l7; /* r */
    uint64_t gm_l8; /* p */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    GM_STACK_CHECK(&F, 1117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tm101l8077enc gm_l14;
    gm_trqfvmjtt4sij gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_b16_8 t14 = gm_l1;
    gm_l3 = t14;
    gm_th2lqbo77pvsc t15;
    memset(&t15, 0, sizeof t15);
    gm_l4 = t15;
    gm_b16_8 t16 = gm_l1;
    gm_th2lqbo77pvsc t17;
    GM_ST(uint8_t, (uint8_t *)&t17 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t17 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t17 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t17), t16);
    ((uint8_t *)&t17)[16] = 1;
    gm_l5 = t17;
    {
        for (;;) {
            uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
            if (!t18) {
                goto gm_lb1;
            }
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
            gm_l6 = t19;
            {
                gm_b16_8 t20 = gm_l6;
                gm_b16_8 t21;
                GM_ST(void *, (uint8_t *)&t21, (void *)(gm_se24c7hpp401n));
                GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(10));
                gm_b16_8 t22 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t20, t21);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t23 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t23) {
                    gm_b16_8 t24 = gm_l6;
                    gm_th2lqbo77pvsc t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t25 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t25), t24);
                    ((uint8_t *)&t25)[16] = 1;
                    gm_l4 = t25;
                    goto gm_lb1;
                }
                gm_b16_8 t26 = gm_l6;
                gm_f_parent_lk0iero((void *)&gm_l5, (void *)NULL, cx, NULL, t26);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        {
            uint8_t t27 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
            if (!t27) {
                goto gm_lb5;
            }
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_l7 = t28;
            gm_b16_8 t29 = gm_l7;
            gm_l3 = t29;
            goto gm_lb4;
        }
gm_lb5:;
        {
            {
                uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                if (!t30) {
                    goto gm_lb7;
                }
                uint64_t t31 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                gm_l8 = t31;
                gm_b16_8 t32 = gm_l1;
                void * t33 = gm_l0;
                void * t34 = GM_LD(void *, (((uint8_t *)t33) + 432));
                uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)t33) + 432) + 8));
                uint64_t t36 = gm_l8;
                if ((uintptr_t)t36 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t36, t35, gm_srcc2qutubarto1, 697, 29);
                gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)t34) + (uintptr_t)t36 * 56));
                uint8_t t38 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t32, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t38) {
                    uint64_t t39 = gm_l8;
                    F.ret = t39;
                    goto gm_fret;
                }
                goto gm_lb6;
            }
gm_lb7:;
        }
gm_lb6:;
    }
gm_lb4:;
    void * t40 = gm_l0;
    gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)t40) + 432));
    gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
    gm_l9 = t42;
    gm_l10 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t43 = gm_l10;
            uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            uint8_t t45 = (t43 < t44);
            if (!t45) {
                goto gm_lb8;
            }
            {
                uint64_t t46 = gm_l10;
                void * t47 = GM_LD(void *, ((uint8_t *)&gm_l9));
                uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint64_t t49 = gm_l10;
                gm_tl7hpf5nlilqq t50 = GM_LD(gm_tl7hpf5nlilqq, (((uint8_t *)t47) + (uintptr_t)t49 * 56));
                gm_tm101l8077enc t51;
                GM_ST(uint64_t, ((uint8_t *)&t51 + 0), t46);
                GM_ST(gm_tl7hpf5nlilqq, ((uint8_t *)&t51 + 8), t50);
                gm_l14 = t51;
                {
                    {
                        uint64_t t52 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                        gm_l11 = t52;
                        memmove(((uint8_t *)&F.l12), (((uint8_t *)&gm_l14) + 8), 56);
                        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                        gm_b16_8 t54 = gm_l3;
                        uint8_t t55 = (gm_eq0iqiubfm7e(((uint8_t *)&t53), ((uint8_t *)&t54)));
                        if (t55) {
                            uint64_t t56 = gm_l11;
                            F.ret = t56;
                            goto gm_fret;
                        }
                        goto gm_lb11;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 699, 5);
                }
gm_lb11:;
            }
gm_lb10:;
            uint64_t t57 = gm_l10;
            uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
            uint64_t t60 = r58;
            gm_l10 = t60;
        }
    }
gm_lb8:;
    gm_b16_8 t61 = gm_l3;
    gm_b16_8 t62;
    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_se24c7hpp401n));
    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(10));
    gm_b16_8 t63 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t61, t62);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t64 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t63);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t64) {
        void * t65 = gm_l0;
        gm_b16_8 t66 = gm_l3;
        gm_f_read_manifest_qr219nu((void *)&gm_l15, (void *)NULL, cx, NULL, t65, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        gm_f_list_0pjrgaq((void *)&gm_l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    memmove(((uint8_t *)&F.l13), ((uint8_t *)&gm_l15), 40);
    void * t67 = gm_l0;
    void * t68 = (void *)(((uint8_t *)t67) + 432);
    gm_b16_8 t69 = gm_l3;
    gm_trqfvmjtt4sij t70 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l13));
    gm_tl7hpf5nlilqq t71;
    GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t69);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t71 + 16), t70);
    gm_tftb1odc739t5 t72 = gm_f_push_n0od5si(t68, &t71);
    gm_l16 = t72;
    uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcc2qutubarto1, 703, 5);
    void * t74 = gm_l0;
    uint64_t t75 = GM_LD(uint64_t, ((((uint8_t *)t74) + 432) + 8));
    uint64_t r76; bool o77 = __builtin_sub_overflow(t75, ((uint64_t)1ULL), &r76);
    if (o77) gm_trap("integer overflow in `-`", gm_srcc2qutubarto1, 704, 12);
    uint64_t t78 = r76;
    F.ret = t78;
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
inline __attribute__((aligned(4))) void gm_f_parse_text_j7gp86k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_parse_text_j7gp86k F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* text */
    uint32_t gm_l3; /* id */
    gm_t08olq97rscn3 gm_l5; /* tree */
    void * gm_l6; /* keep */
    GM_STACK_CHECK(&F, 535u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tr8dvlu9ik4gj gm_l7;
    gm_tftb1odc739t5 gm_l8;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    gm_b16_8 t6 = gm_l1;
    gm_b16_8 t7 = gm_l2;
    uint32_t t8 = gm_f_add_pva1tcq((void *)NULL, cx, NULL, t5, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t8;
    void * t9 = gm_l0;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t9) + 24));
    if (t10) {
        uint32_t t11 = gm_l3;
        gm_b16_8 t12 = gm_l2;
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 8));
        gm_f_lex_recovering_obo2892((void *)&gm_l7, (void *)NULL, cx, NULL, t11, t12, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        uint32_t t15 = gm_l3;
        gm_b16_8 t16 = gm_l2;
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 8));
        gm_f_lex_dc8mbmf((void *)&gm_l7, (void *)NULL, cx, NULL, t15, t16, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    memmove(((uint8_t *)&F.l4), ((uint8_t *)&gm_l7), 120);
    uint32_t t19 = gm_l3;
    void * t20 = (void *)((uint8_t *)&F.l4);
    void * t21 = gm_l0;
    void * t22 = GM_LD(void *, (((uint8_t *)t21) + 16));
    void * t23 = gm_l0;
    void * t24 = GM_LD(void *, (((uint8_t *)t23) + 8));
    gm_f_parse_file_70g7vok((void *)&gm_l5, (void *)NULL, cx, NULL, t19, t20, t22, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            void * t25 = gm_l0;
            uint8_t t26 = (uint8_t)(GM_LD(void *, (((uint8_t *)t25) + 424)) != 0);
            if (!t26) {
                goto gm_lb2;
            }
            void * t27 = GM_LD(void *, (((uint8_t *)t25) + 424));
            gm_l6 = t27;
            void * t28 = gm_l6;
            uint32_t t29 = gm_l3;
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
            gm_t08olq97rscn3 t32 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l5));
            gm_tqtt2php6e8lk t33;
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t33 + 0), t29);
            GM_ST(gm_b16_8, ((uint8_t *)&t33 + 8), t31);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t33 + 24), t32);
            gm_tftb1odc739t5 t34 = gm_f_push_9oh3uqv(t28, &t33);
            gm_l8 = t34;
            uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcc2qutubarto1, 346, 27);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&gm_l5), 32);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_read_manifest_qr219nu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_read_manifest_qr219nu F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* root */
    gm_b16_8 gm_l5; /* path */
    gm_tj5jn79kq9pnm gm_l6; /* e */
    gm_b16_8 gm_l7; /* text */
    gm_tf1bjg81u0glv gm_l8; /* e */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l13; /* p */
    GM_STACK_CHECK(&F, 1944u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqetrgo0q24b5 gm_l14;
    gm_tgvh15qshf643 gm_l15;
    gm_b16_8 gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tjgmtdrk88t7m gm_l18;
    gm_tm43kqe3e8b4a gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tabb710d9mq2e gm_l23;
    {
        {
            void * t15 = gm_l0;
            uint8_t t16 = (uint8_t)(((((uint8_t *)t15) + 376))[40] != 0);
            if (!t16) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), (((uint8_t *)t15) + 376), 40);
            {
                {
                    void * t17 = (void *)((uint8_t *)&F.l2);
                    gm_b16_8 t18 = gm_l1;
                    gm_f_deps_of_i4no6j9((void *)&gm_l14, t17, t18);
                    uint8_t t19 = (uint8_t)((((uint8_t *)&gm_l14))[40] != 0);
                    if (!t19) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l14), 40);
                    memmove(gm_out, ((uint8_t *)&F.l3), 40);
                    goto gm_fdone;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_trqfvmjtt4sij t20;
    gm_f_list_0pjrgaq((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t20;
    gm_b16_8 t21 = gm_l1;
    gm_b16_8 t22;
    GM_ST(void *, (uint8_t *)&t22, (void *)(gm_se24c7hpp401n));
    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(10));
    gm_b16_8 t23 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t21, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t23;
    gm_b16_8 t24 = gm_l5;
    gm_f_read_all_r9rh9qd((void *)&gm_l15, (void *)NULL, cx, NULL, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (t25) {
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
        gm_l16 = t26;
    } else {
        gm_tj5jn79kq9pnm t27 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&gm_l15) + 8));
        gm_l6 = t27;
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 8));
        gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
        gm_b16_8 t31 = gm_l5;
        gm_tj5jn79kq9pnm t32 = gm_l6;
        gm_b16_8 t33 = gm_f_describe_read_error_uikkr59((void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t34;
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t31);
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 16), t33);
        gm_f_format_04pispc((void *)&gm_l17, (void *)NULL, cx, NULL, &t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
        if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcc2qutubarto1, 716, 38);
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        gm_b0_1 t38 = gm_f_report_eardf07((void *)NULL, cx, NULL, t29, t30, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        memmove(gm_out, ((uint8_t *)&F.l4), 40);
        goto gm_fdone;
    }
    gm_b16_8 t39 = gm_l16;
    gm_l7 = t39;
    gm_b16_8 t40 = gm_l7;
    gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
    gm_f_parse_manifest_b61s3ln((void *)&gm_l18, (void *)NULL, cx, NULL, t41);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (t42) {
        memmove(((uint8_t *)&gm_l19), (((uint8_t *)&gm_l18) + 8), 320);
    } else {
        memmove(((uint8_t *)&gm_l8), (((uint8_t *)&gm_l18) + 8), 24);
        void * t43 = gm_l0;
        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 8));
        gm_tl874t2atnlh3 t45 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
        gm_b16_8 t46 = gm_l5;
        uint64_t t47 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_tq3m5qgvkpje8 t49;
        GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t46);
        GM_ST(uint64_t, ((uint8_t *)&t49 + 16), t47);
        GM_ST(gm_b16_8, ((uint8_t *)&t49 + 24), t48);
        gm_f_format_n6eprd4((void *)&gm_l20, (void *)NULL, cx, NULL, &t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
        if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcc2qutubarto1, 720, 38);
        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
        gm_b0_1 t53 = gm_f_report_eardf07((void *)NULL, cx, NULL, t44, t45, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        memmove(gm_out, ((uint8_t *)&F.l4), 40);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l19), 320);
    gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 48));
    gm_l10 = t54;
    gm_l11 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t55 = gm_l11;
            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            uint8_t t57 = (t55 < t56);
            if (!t57) {
                goto gm_lb5;
            }
            {
                void * t58 = GM_LD(void *, ((uint8_t *)&gm_l10));
                uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint64_t t60 = gm_l11;
                {
                    {
                        memmove(((uint8_t *)&F.l12), (((uint8_t *)t58) + (uintptr_t)t60 * 120), 120);
                        {
                            {
                                uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 24) + 0));
                                uint8_t t62 = (uint8_t)(t61 == ((uint8_t)0ULL));
                                if (!t62) {
                                    goto gm_lb11;
                                }
                                gm_b16_8 t63 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l12) + 24) + 8));
                                gm_l13 = t63;
                                void * t64 = (void *)((uint8_t *)&F.l4);
                                gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                gm_b16_8 t66 = gm_l1;
                                gm_b16_8 t67 = gm_l13;
                                gm_b16_8 t68 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t66, t67);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t6j6smj7qoctu t69;
                                GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t65);
                                GM_ST(gm_b16_8, ((uint8_t *)&t69 + 16), t68);
                                gm_tftb1odc739t5 t70 = gm_f_push_i535kr1(t64, &t69);
                                gm_l21 = t70;
                                uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcc2qutubarto1, 725, 27);
                                goto gm_lb10;
                            }
gm_lb11:;
                            {
                                uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 24) + 0));
                                uint8_t t73 = (uint8_t)(t72 == ((uint8_t)1ULL));
                                if (!t73) {
                                    goto gm_lb12;
                                }
                                void * t74 = gm_l0;
                                void * t75 = GM_LD(void *, (((uint8_t *)t74) + 8));
                                gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                                gm_b16_8 t77 = gm_l5;
                                uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&F.l12) + 16));
                                gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                gm_tq3m5qgvkpje8 t80;
                                GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t77);
                                GM_ST(uint64_t, ((uint8_t *)&t80 + 16), t78);
                                GM_ST(gm_b16_8, ((uint8_t *)&t80 + 24), t79);
                                gm_f_format_q451tst((void *)&gm_l22, (void *)NULL, cx, NULL, &t80);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t81 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                                if (!t81) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srcc2qutubarto1, 726, 55);
                                gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                                gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
                                gm_b0_1 t84 = gm_f_report_eardf07((void *)NULL, cx, NULL, t75, t76, t83);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb10;
                            }
gm_lb12:;
                            {
                                uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 24) + 0));
                                uint8_t t86 = (uint8_t)(t85 == ((uint8_t)2ULL));
                                if (!t86) {
                                    goto gm_lb13;
                                }
                                void * t87 = gm_l0;
                                void * t88 = GM_LD(void *, (((uint8_t *)t87) + 8));
                                gm_tl874t2atnlh3 t89 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                                gm_b16_8 t90 = gm_l5;
                                uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&F.l12) + 16));
                                gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                gm_tq3m5qgvkpje8 t93;
                                GM_ST(gm_b16_8, ((uint8_t *)&t93 + 0), t90);
                                GM_ST(uint64_t, ((uint8_t *)&t93 + 16), t91);
                                GM_ST(gm_b16_8, ((uint8_t *)&t93 + 24), t92);
                                gm_f_format_pl7hkg4((void *)&gm_l23, (void *)NULL, cx, NULL, &t93);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_srcc2qutubarto1, 727, 55);
                                gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                                gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
                                gm_b0_1 t97 = gm_f_report_eardf07((void *)NULL, cx, NULL, t88, t89, t96);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb10;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 724, 9);
                        }
gm_lb10:;
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 723, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t98 = gm_l11;
            uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
            uint64_t t101 = r99;
            gm_l11 = t101;
        }
    }
gm_lb5:;
    memmove(gm_out, ((uint8_t *)&F.l4), 40);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_resolve_import_944uqa9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_resolve_import_944uqa9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* id */
    gm_t8c5h7u09n10a gm_l4; /* state */
    GM_STACK_CHECK(&F, 228u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 240));
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t4) + 240) + 8));
    uint64_t t7 = gm_l1;
    if ((uintptr_t)t7 >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)t7, t6, gm_srcc2qutubarto1, 586, 8);
    gm_t8c5h7u09n10a t8 = GM_LD(gm_t8c5h7u09n10a, ((((uint8_t *)t5) + (uintptr_t)t7 * 216) + 200));
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
    uint8_t t10 = (uint8_t)(!(t9 == ((uint8_t)0ULL)));
    gm_t8c5h7u09n10a t11;
    GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t11 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
    if (t10) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, (((uint8_t *)t12) + 240));
    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 240) + 8));
    uint64_t t15 = gm_l1;
    if ((uintptr_t)t15 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t15, t14, gm_srcc2qutubarto1, 587, 10);
    memmove(((uint8_t *)&F.l2), (((uint8_t *)t13) + (uintptr_t)t15 * 216), 216);
    {
        {
            uint8_t t16 = GM_LD(uint8_t, (((((uint8_t *)&F.l2) + 40) + 64) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)0ULL));
            if (!t17) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l3), (((((uint8_t *)&F.l2) + 40) + 64) + 8), 64);
            void * t18 = gm_l0;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 32));
            void * t20 = (void *)((uint8_t *)&F.l2);
            void * t21 = (void *)((uint8_t *)&F.l3);
            gm_t8c5h7u09n10a t22 = gm_f_import_decl_vuv14lf((void *)NULL, cx, NULL, t18, t19, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t22;
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 240));
            uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t23) + 240) + 8));
            uint64_t t26 = gm_l1;
            if ((uintptr_t)t26 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t26, t25, gm_srcc2qutubarto1, 592, 5);
            gm_t8c5h7u09n10a t27 = gm_l4;
            GM_ST(gm_t8c5h7u09n10a, ((((uint8_t *)t24) + (uintptr_t)t26 * 216) + 200), t27);
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_import_dav5vf0(void * a0, uint64_t a1, void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_import_dav5vf0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* prev */
    void * gm_l2; /* item */
    void * gm_l3; /* d */
    gm_b16_8 gm_l4; /* a */
    gm_b16_8 gm_l5; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 330u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l6;
    gm_b16_8 gm_l7;
    gm_th2lqbo77pvsc gm_l8;
    gm_b16_8 gm_l9;
    uint8_t gm_l10;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 240));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 240) + 8));
    uint64_t t5 = gm_l1;
    if ((uintptr_t)t5 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t5, t4, gm_srcc2qutubarto1, 552, 11);
    void * t6 = (void *)(((uint8_t *)t3) + (uintptr_t)t5 * 216);
    gm_l3 = t6;
    void * t7 = gm_l3;
    void * t8 = (void *)(((uint8_t *)t7) + 40);
    gm_f_import_path_na25mjr((void *)&gm_l6, t8);
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
    if (t9) {
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
        gm_l7 = t10;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t11 = gm_l7;
    gm_l4 = t11;
    void * t12 = gm_l2;
    gm_f_import_path_na25mjr((void *)&gm_l8, t12);
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l8))[16] != 0);
    if (t13) {
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
        gm_l9 = t14;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t15 = gm_l9;
    gm_l5 = t15;
    void * t16 = gm_l3;
    gm_t9qkh0rsooc8d t17 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t16) + 40) + 56));
    void * t18 = gm_l2;
    gm_t9qkh0rsooc8d t19 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t18) + 56));
    uint8_t t20 = (gm_eqk27942ja0g(((uint8_t *)&t17), ((uint8_t *)&t19)));
    if (t20) {
        gm_b16_8 t21 = gm_l4;
        gm_b16_8 t22 = gm_l5;
        uint8_t t23 = (gm_eq0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22)));
        gm_l10 = t23;
    } else {
        gm_l10 = ((uint8_t)0);
    }
    uint8_t t24 = gm_l10;
    F.ret = t24;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_khjih0n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_show_khjih0n F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* p */
    gm_b16_8 gm_l2; /* rest */
    gm_b16_8 gm_l3; /* rest */
    GM_STACK_CHECK(&F, 581u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l4;
    gm_th2lqbo77pvsc gm_l5;
    gm_th2lqbo77pvsc gm_l6;
    gm_b16_8 gm_l7;
    gm_th2lqbo77pvsc gm_l8;
    gm_b16_8 gm_l9;
    {
        {
            gm_b16_8 t6 = gm_l1;
            void * t7 = gm_l0;
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 152));
            gm_f_strip_prefix_vl3t0h4((void *)&gm_l4, (void *)NULL, cx, NULL, t6, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
            if (t9) {
                memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l4), 24);
            } else {
                gm_b16_8 t10 = gm_l1;
                void * t11 = gm_l0;
                gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t11) + 168));
                gm_f_strip_prefix_vl3t0h4((void *)&gm_l5, (void *)NULL, cx, NULL, t10, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l5), 24);
            uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
            if (!t13) {
                goto gm_lb2;
            }
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
            gm_l2 = t14;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t16 = ((t15 == ((uint64_t)0ULL)));
            if (t16) {
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sjkg27kmogh66));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(6));
                gm_l7 = t17;
            } else {
                gm_b16_8 t18;
                GM_ST(void *, (uint8_t *)&t18, (void *)(gm_san68r6np95il));
                GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(5));
                gm_b16_8 t19 = gm_l2;
                gm_b16_8 t20 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t18, t19);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t20;
            }
            gm_b16_8 t21 = gm_l7;
            gm_b16_8 t22 = gm_f_slashes_0qtfkuu((void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t22;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            gm_b16_8 t23 = gm_l1;
            void * t24 = gm_l0;
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 184));
            gm_f_strip_prefix_vl3t0h4((void *)&gm_l8, (void *)NULL, cx, NULL, t23, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l8))[16] != 0);
            if (!t26) {
                goto gm_lb4;
            }
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
            gm_l3 = t27;
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t29 = ((t28 == ((uint64_t)0ULL)));
            if (t29) {
                gm_b16_8 t30;
                GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sl3q52ep6aa53));
                GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(1));
                gm_l9 = t30;
            } else {
                gm_b16_8 t31 = gm_l3;
                gm_l9 = t31;
            }
            gm_b16_8 t32 = gm_l9;
            gm_b16_8 t33 = gm_f_slashes_0qtfkuu((void *)NULL, cx, NULL, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t33;
            goto gm_fret;
        }
gm_lb4:;
    }
gm_lb3:;
    gm_b16_8 t34 = gm_l1;
    gm_b16_8 t35 = gm_f_slashes_0qtfkuu((void *)NULL, cx, NULL, t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t35;
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
extern __attribute__((aligned(4))) uint8_t gm_f_skipped_dir_orfng8m(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_visit_fs6p4tu(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_visit_fs6p4tu F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l2; /* state */
    void * gm_l3; /* stack */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t0nqnmillkbs6 gm_l6; /* e */
    uint64_t gm_l7; /* i */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* x */
    GM_STACK_CHECK(&F, 1013u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l12;
    gm_tf5srh4mp4b3i gm_l13;
    uint64_t gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    void * t12 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t14 = gm_l1;
    if ((uintptr_t)t14 >= (uintptr_t)(t13)) gm_trap_index((uintptr_t)t14, t13, gm_srcc2qutubarto1, 746, 5);
    GM_ST(uint8_t, (((uint8_t *)t12) + (uintptr_t)t14 * 1), ((uint8_t)1ULL));
    void * t15 = gm_l3;
    uint64_t t16 = gm_l1;
    gm_tftb1odc739t5 t17 = gm_f_push_ucgmlo2(t15, t16);
    gm_l12 = t17;
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcc2qutubarto1, 747, 5);
    void * t19 = gm_l0;
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t19) + 512));
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
                        memmove(((uint8_t *)&gm_l6), (((uint8_t *)t24) + (uintptr_t)t26 * 32), 32);
                        uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
                        uint64_t t28 = gm_l1;
                        uint8_t t29 = (!(t27 == t28));
                        if (t29) {
                            goto gm_lb3;
                        }
                        void * t30 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        if ((uintptr_t)t32 >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)t32, t31, gm_srcc2qutubarto1, 750, 12);
                        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)t30) + (uintptr_t)t32 * 1));
                        uint8_t t34 = ((t33 == ((uint8_t)0ULL)));
                        if (t34) {
                            void * t35 = gm_l0;
                            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            gm_b16_8 t37 = gm_l2;
                            void * t38 = gm_l3;
                            gm_b0_1 t39 = gm_f_visit_fs6p4tu((void *)NULL, cx, NULL, t35, t36, t37, t38);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        } else {
                            void * t40 = GM_LD(void *, ((uint8_t *)&gm_l2));
                            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)t42 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t42, t41, gm_srcc2qutubarto1, 752, 19);
                            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)t40) + (uintptr_t)t42 * 1));
                            uint8_t t44 = ((t43 == ((uint8_t)1ULL)));
                            if (t44) {
                                void * t45 = gm_l3;
                                gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)t45));
                                gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                                uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                gm_tf5srh4mp4b3i t49 = gm_f_index_of_s9trr09(t47, t48);
                                gm_l13 = t49;
                                uint8_t t50 = (uint8_t)((((uint8_t *)&gm_l13))[8] != 0);
                                if (t50) {
                                    uint64_t t51 = GM_LD(uint64_t, ((uint8_t *)&gm_l13));
                                    gm_l14 = t51;
                                } else {
                                    gm_l14 = ((uint64_t)0ULL);
                                }
                                uint64_t t52 = gm_l14;
                                gm_l7 = t52;
                                gm_trqfvmjtt4sij t53;
                                gm_f_list_epvducg((void *)&t53, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l8 = t53;
                                void * t54 = gm_l3;
                                void * t55 = GM_LD(void *, ((uint8_t *)t54));
                                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t54) + 8));
                                uint64_t t57 = gm_l7;
                                uint64_t t58 = (uintptr_t)t57;
                                uint64_t t59 = (uintptr_t)t56;
                                if (t58 > t59 || t59 > (uintptr_t)t56) gm_trap_slice(t58, t59, t56, gm_srcc2qutubarto1, 755, 22);
                                uint64_t t60 = ((uintptr_t)t56) - (uintptr_t)t57;
                                gm_b16_8 t61;
                                GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t55 + (uintptr_t)t57 * 8)));
                                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(t60));
                                gm_l9 = t61;
                                gm_l10 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t62 = gm_l10;
                                        uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                        uint8_t t64 = (t62 < t63);
                                        if (!t64) {
                                            goto gm_lb6;
                                        }
                                        {
                                            void * t65 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                            uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                            uint64_t t67 = gm_l10;
                                            {
                                                {
                                                    uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)t65) + (uintptr_t)t67 * 8));
                                                    gm_l11 = t68;
                                                    void * t69 = (void *)((uint8_t *)&F.l8);
                                                    void * t70 = gm_l0;
                                                    void * t71 = GM_LD(void *, (((uint8_t *)t70) + 200));
                                                    uint64_t t72 = GM_LD(uint64_t, ((((uint8_t *)t70) + 200) + 8));
                                                    uint64_t t73 = gm_l11;
                                                    if ((uintptr_t)t73 >= (uintptr_t)(t72)) gm_trap_index((uintptr_t)t73, t72, gm_srcc2qutubarto1, 755, 52);
                                                    gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)t71) + (uintptr_t)t73 * 184));
                                                    gm_tftb1odc739t5 t75 = gm_f_push_3ef746b(t69, t74);
                                                    gm_l15 = t75;
                                                    uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                                    if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcc2qutubarto1, 755, 41);
                                                    goto gm_lb9;
                                                }
gm_lb10:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 755, 13);
                                            }
gm_lb9:;
                                        }
gm_lb8:;
                                        uint64_t t77 = gm_l10;
                                        uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                                        uint64_t t80 = r78;
                                        gm_l10 = t80;
                                    }
                                }
gm_lb6:;
                                void * t81 = (void *)((uint8_t *)&F.l8);
                                void * t82 = gm_l0;
                                void * t83 = GM_LD(void *, (((uint8_t *)t82) + 200));
                                uint64_t t84 = GM_LD(uint64_t, ((((uint8_t *)t82) + 200) + 8));
                                uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)t85 >= (uintptr_t)(t84)) gm_trap_index((uintptr_t)t85, t84, gm_srcc2qutubarto1, 756, 24);
                                gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)t83) + (uintptr_t)t85 * 184));
                                gm_tftb1odc739t5 t87 = gm_f_push_3ef746b(t81, t86);
                                gm_l16 = t87;
                                uint8_t t88 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                if (!t88) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcc2qutubarto1, 756, 13);
                                void * t89 = gm_l0;
                                void * t90 = GM_LD(void *, (((uint8_t *)t89) + 8));
                                gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l6) + 16));
                                gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                                gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                                gm_b16_8 t94;
                                GM_ST(void *, (uint8_t *)&t94, (void *)(gm_sdbvaa960ee0f));
                                GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(4));
                                gm_f_join_sfknljb((void *)&gm_l17, (void *)NULL, cx, NULL, t93, t94);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcc2qutubarto1, 757, 67);
                                gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                                gm_tnpifv20irnph t97;
                                GM_ST(gm_b16_8, ((uint8_t *)&t97 + 0), t96);
                                gm_f_format_bl6khrb((void *)&gm_l18, (void *)NULL, cx, NULL, t97);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcc2qutubarto1, 757, 36);
                                gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                                gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
                                gm_b0_1 t101 = gm_f_report_eardf07((void *)NULL, cx, NULL, t90, t91, t100);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc2qutubarto1, 748, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t102 = gm_l5;
            uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
            uint64_t t105 = r103;
            gm_l5 = t105;
        }
    }
gm_lb1:;
    void * t106 = gm_l3;
    gm_tf5srh4mp4b3i t107 = gm_f_pop_j4rpvm5(t106);
    void * t108 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t110 = gm_l1;
    if ((uintptr_t)t110 >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)t110, t109, gm_srcc2qutubarto1, 761, 5);
    GM_ST(uint8_t, (((uint8_t *)t108) + (uintptr_t)t110 * 1), ((uint8_t)2ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_working_dir_dj7eq93(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_working_dir_dj7eq93 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 290u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_thf5kr0is9u9i gm_l2;
    uint64_t gm_l3;
    gm_tabb710d9mq2e gm_l4;
    void * t2 = (void *)((uint8_t *)&F.l0);
    uint64_t t3 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t4 = (uintptr_t)((uint64_t)4096ULL);
    if (t3 > t4 || t4 > (uintptr_t)((uint64_t)4096ULL)) gm_trap_slice(t3, t4, ((uint64_t)4096ULL), gm_srcc2qutubarto1, 224, 17);
    uint64_t t5 = ((uintptr_t)((uint64_t)4096ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(((uint8_t *)t2 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(t5));
    gm_node *pn7 = gm_find(cx, 2126215853);
    if (!pn7) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 224, 10);
    gm_thf5kr0is9u9i t8;
    t8 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn7->clauses[16])(pn7->env, pn7->hctx, pn7, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t9) {
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        gm_l3 = t10;
    } else {
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(1));
        F.ret = t11;
        goto gm_fret;
    }
    uint64_t t12 = gm_l3;
    gm_l1 = t12;
    void * t13 = (void *)((uint8_t *)&F.l0);
    uint64_t t14 = gm_l1;
    uint64_t t15 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t16 = (uintptr_t)t14;
    if (t15 > t16 || t16 > (uintptr_t)((uint64_t)4096ULL)) gm_trap_slice(t15, t16, ((uint64_t)4096ULL), gm_srcc2qutubarto1, 225, 17);
    uint64_t t17 = ((uintptr_t)t14) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t18;
    GM_ST(void *, (uint8_t *)&t18, (void *)(((uint8_t *)t13 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(t17));
    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
    gm_f_dupe_g9iqpvn((void *)&gm_l4, (void *)NULL, cx, NULL, t19, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kn7qjfrkcl7)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcc2qutubarto1, 225, 12);
    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
    F.ret = t22;
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
