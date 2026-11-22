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
typedef struct gm_cs3cuou245tr5f { _Bool f0; _Bool f1; _Bool f2; } gm_cs3cuou245tr5f;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
_Static_assert(sizeof(gm_cs3cuou245tr5f) == 3 && _Alignof(gm_cs3cuou245tr5f) == 1 && offsetof(gm_cs3cuou245tr5f, f0) == 0 && offsetof(gm_cs3cuou245tr5f, f1) == 1 && offsetof(gm_cs3cuou245tr5f, f2) == 2, "layout of `Restr`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tl874t2atnlh3 v1; } f1; } gm_t0u0j6voh3loe;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v1; } f1; } gm_tt7ek1a8bopcl;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0u0j6voh3loe) == 16 && _Alignof(gm_t0u0j6voh3loe) == 4 && offsetof(gm_t0u0j6voh3loe, f0) == 0 && offsetof(gm_t0u0j6voh3loe, f1) == 4, "layout of `Abort!Span`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v1; } f1; } gm_t4jn8fl3lmk5k;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf2orqcrphmek v1; } f1; } gm_t8u9q1tmoidhk;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; void * v1; } f1; } gm_tapdr2ag17fh7;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_th2lqbo77pvsc v1; } f1; } gm_thk954m1ol1ig;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v1; } f1; } gm_toc4mrkn2e7o9;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tpv68m88f9lcr;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tt7ek1a8bopcl) == 24 && _Alignof(gm_tt7ek1a8bopcl) == 8 && offsetof(gm_tt7ek1a8bopcl, f0) == 0 && offsetof(gm_tt7ek1a8bopcl, f1) == 8, "layout of `Abort![]Attribute`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
typedef struct { gm_tlivgedjs5ps3 f0; gm_b16_8 f1; gm_tapdr2ag17fh7 f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t0kv05e068jrn;
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4jn8fl3lmk5k) == 40 && _Alignof(gm_t4jn8fl3lmk5k) == 8 && offsetof(gm_t4jn8fl3lmk5k, f0) == 0 && offsetof(gm_t4jn8fl3lmk5k, f1) == 8, "layout of `Abort!Ident`");
_Static_assert(sizeof(gm_t8u9q1tmoidhk) == 64 && _Alignof(gm_t8u9q1tmoidhk) == 8 && offsetof(gm_t8u9q1tmoidhk, f0) == 0 && offsetof(gm_t8u9q1tmoidhk, f1) == 8, "layout of `Abort!Param`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_t3fcbuehh3bs2 f1; gm_t3fcbuehh3bs2 f2; gm_t3fcbuehh3bs2 f3; gm_t3fcbuehh3bs2 f4; } gm_tak1k6bk5qcen;
_Static_assert(sizeof(gm_tapdr2ag17fh7) == 48 && _Alignof(gm_tapdr2ag17fh7) == 8 && offsetof(gm_tapdr2ag17fh7, f0) == 0 && offsetof(gm_tapdr2ag17fh7, f1) == 8, "layout of `WhenElse`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tlivgedjs5ps3 v1; } f1; } gm_tdmdfjrg7d59n;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_thk954m1ol1ig) == 32 && _Alignof(gm_thk954m1ol1ig) == 8 && offsetof(gm_thk954m1ol1ig, f0) == 0 && offsetof(gm_thk954m1ol1ig, f1) == 8, "layout of `Abort!CallArgs`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tapdr2ag17fh7 v1; } f1; } gm_tihhqadak7gf3;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_toc4mrkn2e7o9) == 48 && _Alignof(gm_toc4mrkn2e7o9) == 8 && offsetof(gm_toc4mrkn2e7o9, f0) == 0 && offsetof(gm_toc4mrkn2e7o9, f1) == 8, "layout of `Abort!Block`");
_Static_assert(sizeof(gm_tpv68m88f9lcr) == 56 && _Alignof(gm_tpv68m88f9lcr) == 8 && offsetof(gm_tpv68m88f9lcr, f0) == 0 && offsetof(gm_tpv68m88f9lcr, f1) == 32 && offsetof(gm_tpv68m88f9lcr, f2) == 40, "layout of `Constraint`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_t0kv05e068jrn) == 152 && _Alignof(gm_t0kv05e068jrn) == 8 && offsetof(gm_t0kv05e068jrn, f0) == 0 && offsetof(gm_t0kv05e068jrn, f1) == 72 && offsetof(gm_t0kv05e068jrn, f2) == 88 && offsetof(gm_t0kv05e068jrn, f3) == 136, "layout of `FnLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_tak1k6bk5qcen) == 160 && _Alignof(gm_tak1k6bk5qcen) == 8 && offsetof(gm_tak1k6bk5qcen, f0) == 0 && offsetof(gm_tak1k6bk5qcen, f1) == 32 && offsetof(gm_tak1k6bk5qcen, f2) == 64 && offsetof(gm_tak1k6bk5qcen, f3) == 96 && offsetof(gm_tak1k6bk5qcen, f4) == 128, "layout of `(TokenKind, TokenKind, TokenKind, TokenKind, TokenKind)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tdmdfjrg7d59n) == 80 && _Alignof(gm_tdmdfjrg7d59n) == 8 && offsetof(gm_tdmdfjrg7d59n, f0) == 0 && offsetof(gm_tdmdfjrg7d59n, f1) == 8, "layout of `Abort!FnSig`");
_Static_assert(sizeof(gm_tihhqadak7gf3) == 56 && _Alignof(gm_tihhqadak7gf3) == 8 && offsetof(gm_tihhqadak7gf3, f0) == 0 && offsetof(gm_tihhqadak7gf3, f1) == 8, "layout of `Abort!FnBody`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v1; } f1; } gm_to1cssm8dfiv5;
_Static_assert(sizeof(gm_to1cssm8dfiv5) == 96 && _Alignof(gm_to1cssm8dfiv5) == 8 && offsetof(gm_to1cssm8dfiv5, f0) == 0 && offsetof(gm_to1cssm8dfiv5, f1) == 8, "layout of `Abort!Expr`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_at_54906rm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_kw_p0r3sr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_newline_94n732e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_at_word_18dnh5f {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_box_j4urmed {
    gm_frame h;
    gm_tc3oht4ksth9d l0; /* e */
    void * ret;
};
struct gm_fr_eat_const_0ntbpcs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_eat_qmock9g {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_3jt8vgk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_65q32rn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_atlcs4j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ito0pap {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fail_e5kqpus {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_sig_to_type_3r7l7ih {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* params */
    gm_tf2orqcrphmek l5; /* param */
    gm_b0_1 ret;
};
struct gm_fr_kind_k239kpg {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_leave_rb20u32 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_18sm81j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_3t77g3a {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5kg5cjm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_mho6jfm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lo_fp82gt4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_mk_expr_53qj7nd {
    gm_frame h;
    gm_t2gtbh39ptekd l1; /* k */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_next_id_a7upaf4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_nth_c0r2nvs {
    gm_frame h;
    gm_t3fcbuehh3bs2 ret;
};
struct gm_fr_nth_is_3fgqmcb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_parse_attr_args_16rponu {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* args */
    gm_tt7ek1a8bopcl ret;
};
struct gm_fr_parse_attributed_00u9rjp {
    gm_frame h;
    gm_tc3oht4ksth9d l4; /* inner */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_attrs_imvfdlq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* attrs */
    gm_tm4pc0emhrv1g l3; /* t */
    gm_tt7ek1a8bopcl ret;
};
struct gm_fr_parse_cont_type_ljmco4q {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* t */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_fn_body_09mp5ir {
    gm_frame h;
    gm_tihhqadak7gf3 ret;
};
struct gm_fr_parse_fn_expr_c9357a4 {
    gm_frame h;
    gm_tlivgedjs5ps3 l2; /* sig */
    gm_tf2orqcrphmek l7; /* param */
    gm_trqfvmjtt4sij l8; /* saved */
    gm_tihhqadak7gf3 l9; /* body */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_fn_sig_fecr4sl {
    gm_frame h;
    gm_tdmdfjrg7d59n ret;
};
struct gm_fr_parse_fn_type_only_0ifghqh {
    gm_frame h;
    gm_tlivgedjs5ps3 l2; /* sig */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_fn_type_sig_jrv5v82 {
    gm_frame h;
    gm_tlivgedjs5ps3 l2; /* sig */
    gm_tdmdfjrg7d59n ret;
};
struct gm_fr_parse_needs_opt_8lp1u45 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* effects */
    gm_thk954m1ol1ig ret;
};
struct gm_fr_parse_param_group_ac8kpvk {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* names */
    gm_t8u9q1tmoidhk ret;
};
struct gm_fr_parse_params_3g43t5f {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* params */
    gm_thk954m1ol1ig ret;
};
struct gm_fr_parse_postfix_ao4kstr {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* base */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_prefix_type_1rn6mn8 {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_type_2m93dtq {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* lhs */
    gm_tc3oht4ksth9d l2; /* ok */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_type_name_d0ioo05 {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* e */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_type_operand_7guq29a {
    gm_frame h;
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_type_prefix_kfrfcdd {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* inner */
    gm_tc3oht4ksth9d l5; /* inner */
    gm_tc3oht4ksth9d l7; /* inner */
    gm_tc3oht4ksth9d l9; /* inner */
    gm_tc3oht4ksth9d l10; /* elem */
    gm_t4gut2c9fj1kt l12; /* d */
    gm_tc3oht4ksth9d l13; /* elem */
    gm_to1cssm8dfiv5 ret;
};
struct gm_fr_parse_where_opt_a5p2ja6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_tt7ek1a8bopcl ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_9cfi95o {
    gm_frame h;
    gm_tkacitepg5fk0 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_aqjeqnr {
    gm_frame h;
    gm_tpv68m88f9lcr l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_s5rtia7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vbfukt4 {
    gm_frame h;
    gm_tf2orqcrphmek l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5u3f7s0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hvlnmhb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_jjvqg83 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ljd0ph4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_djvi2hf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_set_restr_p85qqmv {
    gm_frame h;
    gm_toh0ootodhjue ret;
};
struct gm_fr_skip_newlines_0ap7sq2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_span_bm4ll8d {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_from_1sqap0o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_tok_ai6sc12 {
    gm_frame h;
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b);
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b);
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b);
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b);
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) uint8_t gm_f_at_newline_94n732e(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_at_word_18dnh5f(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_bump_q2i40re(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_const_0ntbpcs(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_65q32rn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_enter_5bf793b(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_ijgfrs1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t0u0j6voh3loe gm_f_expect_kw_7kbmp0i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_expected_5famfb9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fail_diag_u0q4kp3(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_sig_to_type_3r7l7ih(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0);
inline __attribute__((aligned(4))) void gm_f_list_18sm81j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_3t77g3a(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5kg5cjm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_mho6jfm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0);
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_48hbqcm(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_cnt7p5u(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0);
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) uint8_t gm_f_nth_is_name_oqhoobd(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_attr_args_16rponu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_attributed_00u9rjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_attrs_imvfdlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_block_tpg3f1i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_call_args_32e205h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_cont_type_ljmco4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_effect_lit_cqei081(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_expr_bkb2n6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_expr_in_45seo3h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_toh0ootodhjue a1);
__attribute__((aligned(4))) void gm_f_parse_fn_body_09mp5ir(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_fn_expr_c9357a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_fn_sig_fecr4sl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_fn_type_only_0ifghqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_fn_type_sig_jrv5v82(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_ident_n2c6b75(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_intrinsic_4ou54h3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_name_figjvbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_needs_opt_8lp1u45(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_param_group_ac8kpvk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_params_3g43t5f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_paren_sq1guoi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
inline __attribute__((aligned(4))) void gm_f_parse_postfix_ao4kstr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_parse_postfix_ops_2p1csn3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_parse_prefix_type_1rn6mn8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_primary_1vvr4ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_struct_lit_86pp62c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_2m93dtq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_in_bvi7uav(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_toh0ootodhjue a1);
__attribute__((aligned(4))) void gm_f_parse_type_name_d0ioo05(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_type_operand_7guq29a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_type_prefix_kfrfcdd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_parse_union_lit_39ap2o5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_parse_where_opt_a5p2ja6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9cfi95o(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_aqjeqnr(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s5rtia7(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vbfukt4(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5u3f7s0(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hvlnmhb(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_jjvqg83(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ljd0ph4(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1);
inline __attribute__((aligned(4))) void gm_f_skip_newlines_0ap7sq2(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0);
extern uint8_t gm_kakc8s3pa9o[40] __attribute__((aligned(8)));
extern uint8_t gm_kdpnk3k16op[40] __attribute__((aligned(8)));
extern uint8_t gm_kh832np8pgs[40] __attribute__((aligned(8)));
extern uint8_t gm_khouror17c2[40] __attribute__((aligned(8)));
extern const uint8_t gm_s1pvh8pfrs3ir[7];
extern const uint8_t gm_s35hbb2rhhdvv[33];
extern const uint8_t gm_s8dujv4dnroq0[24];
extern const uint8_t gm_s9rm2mbmo3nku[7];
extern const uint8_t gm_s9tr38mqso4n7[93];
extern const uint8_t gm_sa8mtcad5rfmf[26];
extern const uint8_t gm_sandsuergllg3[30];
extern const uint8_t gm_sc71n5mgucblh[50];
extern const uint8_t gm_se7kda46rm021[107];
extern const uint8_t gm_sh7ob55fi0i62[26];
extern const uint8_t gm_shv4avtqvfr4p[6];
extern const uint8_t gm_skcto6psrjh96[28];
extern const uint8_t gm_slgf61fmeeof0[28];
extern const uint8_t gm_smglbbtmdmip2[6];
extern const uint8_t gm_smu8iti9fc903[85];
extern const uint8_t gm_spferj7kepn49[115];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcp8o48ld4n4jq[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuiab49hlgv7g[];
extern const uint8_t gm_sri0h4dm5d3ul[17];
extern const uint8_t gm_st2dnokm1h0jd[5];
extern const uint8_t gm_st803nqm7iok0[17];
extern const uint8_t gm_stugmrojdno85[6];
extern const uint8_t gm_suatl193icom4[41];
extern const uint8_t gm_sud6d85d454o1[42];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqbdg26q4aml(const uint8_t *a, const uint8_t *b) {
    if (!gm_eqbihgc69jv3(a + 0, b + 0)) return false;
    if (!gm_eqk7n1cdvnss(a + 16, b + 16)) return false;
    return true;
}
inline bool gm_eqbihgc69jv3(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqk7n1cdvnss(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqpa2btif7eh(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqrv7v3j3o54(const uint8_t *a, const uint8_t *b) {
    return GM_LD(void *, a) == GM_LD(void *, b);
}
inline bool gm_eqt7ftn1841u(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)2) return gm_eq2ceic3f8rp(a + 8, b + 8);
    if (ta == (uint8_t)3) return gm_eqrv7v3j3o54(a + 8, b + 8);
    if (ta == (uint8_t)4) return gm_eqpa2btif7eh(a + 8, b + 8);
    if (ta == (uint8_t)5) return gm_eqbdg26q4aml(a + 8, b + 8);
    if (ta == (uint8_t)6) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)7) return gm_eq0iqiubfm7e(a + 8, b + 8);
    if (ta == (uint8_t)8) return gm_eqv3tj1pu43g(a + 8, b + 8);
    return true;
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 80);
    gm_b16_8 t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcgfglnv8p059r, 96, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_54906rm(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_54906rm F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t3fcbuehh3bs2 t3;
    gm_f_kind_k239kpg((void *)&t3, t2);
    gm_t9qkh0rsooc8d t4 = gm_l1;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    uint8_t t6 = (uint8_t)(t5 == ((uint8_t)8ULL));
    if (t6) {
        gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t3) + 8));
        t6 = (uint8_t)gm_eqv3tj1pu43g(((uint8_t *)&t7), ((uint8_t *)&t4));
    }
    uint8_t t8 = (uint8_t)(t6);
    gm_t3fcbuehh3bs2 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t9 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t9 + 8), t4);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_kw_p0r3sr9(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_kw_p0r3sr9 F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t3fcbuehh3bs2 t3;
    gm_f_kind_k239kpg((void *)&t3, t2);
    gm_t9qkh0rsooc8d t4 = gm_l1;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    uint8_t t6 = (uint8_t)(t5 == ((uint8_t)2ULL));
    if (t6) {
        gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t3) + 8));
        t6 = (uint8_t)gm_eq2ceic3f8rp(((uint8_t *)&t7), ((uint8_t *)&t4));
    }
    uint8_t t8 = (uint8_t)(t6);
    gm_t3fcbuehh3bs2 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t9 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t9 + 8), t4);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_newline_94n732e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_newline_94n732e F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t3fcbuehh3bs2 t3;
    gm_f_kind_k239kpg((void *)&t3, t2);
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    uint8_t t5 = (uint8_t)((t4 == ((uint8_t)9ULL)));
    gm_t3fcbuehh3bs2 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)9ULL));
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_at_word_18dnh5f(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_at_word_18dnh5f F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* word */
    gm_b16_8 gm_l2; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 135u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    gm_t3fcbuehh3bs2 gm_l4;
    void * t5 = gm_l0;
    gm_f_nth_c0r2nvs((void *)&gm_l4, t5, ((uint64_t)0ULL));
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
            gm_l2 = t8;
            gm_b16_8 t9 = gm_l2;
            gm_b16_8 t10 = gm_l1;
            uint8_t t11 = (gm_eq0iqiubfm7e(((uint8_t *)&t9), ((uint8_t *)&t10)));
            gm_l3 = t11;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcuiab49hlgv7g, 170, 58);
    }
gm_lb1:;
    uint8_t t12 = gm_l3;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_box_j4urmed(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_box_j4urmed F;
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tq7s98ihonkju gm_l1;
    gm_tq7s98ihonkju t2 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l0)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdpnk3k16op)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t2;
    uint8_t t3 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t3) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_srcuiab49hlgv7g, 452, 52);
    void * t4 = GM_LD(void *, (((uint8_t *)&gm_l1) + 8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_eat_const_0ntbpcs(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_const_0ntbpcs F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stugmrojdno85));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    uint8_t t4 = gm_f_at_word_18dnh5f(t2, t3);
    uint8_t t5 = (uint8_t)!t4;
    if (t5) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t6 = gm_l0;
    gm_tm4pc0emhrv1g t7;
    gm_f_bump_q2i40re((void *)&t7, t6);
    F.ret = ((uint8_t)1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_eat_qmock9g(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eat_qmock9g F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 78u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = gm_l1;
    uint8_t t4 = gm_f_at_54906rm(t2, t3);
    uint8_t t5 = (uint8_t)!t4;
    if (t5) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t6 = gm_l0;
    gm_tm4pc0emhrv1g t7;
    gm_f_bump_q2i40re((void *)&t7, t6);
    F.ret = ((uint8_t)1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_65q32rn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_65q32rn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_atlcs4j F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 72)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fail_e5kqpus(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_fail_e5kqpus F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
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
    gm_b0_1 t6 = gm_f_fail_diag_u0q4kp3(t2, &t5);
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
inline __attribute__((aligned(4))) void gm_f_kind_k239kpg(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_k239kpg F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_f_tok_ai6sc12(t2);
    memmove(gm_out, ((uint8_t *)t3), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_leave_rb20u32(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_leave_rb20u32 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 132));
    uint32_t r5; bool o6 = __builtin_sub_overflow(t4, ((uint32_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 434, 5);
    uint32_t t7 = r5;
    GM_ST(uint32_t, (((uint8_t *)t2) + 132), t7);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_18sm81j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_18sm81j F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_atlcs4j();
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
inline __attribute__((aligned(4))) void gm_f_list_3t77g3a(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_3t77g3a F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ito0pap();
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
inline __attribute__((aligned(4))) void gm_f_list_5kg5cjm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5kg5cjm F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_3jt8vgk();
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
inline __attribute__((aligned(4))) void gm_f_list_mho6jfm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_mho6jfm F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_65q32rn();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_g3tddrb();
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
inline __attribute__((aligned(4))) uint32_t gm_f_lo_fp82gt4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lo_fp82gt4 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tl874t2atnlh3 gm_l1;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t2));
    gm_l1 = t3;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_mk_expr_53qj7nd(void *gm_out, void * a0, const void * a1, gm_cseahvg8v7p0vs a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mk_expr_53qj7nd F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l2; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = GM_CONV(gm_tl874t2atnlh3, a2);
    void * t2 = gm_l0;
    uint32_t t3 = gm_f_next_id_a7upaf4(t2);
    gm_tl874t2atnlh3 t4 = gm_l2;
    gm_t2gtbh39ptekd t5 = GM_LD(gm_t2gtbh39ptekd, ((uint8_t *)&F.l1));
    gm_tc3oht4ksth9d t6;
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 84, 0);
    GM_ST(uint32_t, ((uint8_t *)&t6 + 0), t3);
    GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t6 + 8), t5);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 72), t4);
    memmove(gm_out, ((uint8_t *)&t6), 88);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_next_id_a7upaf4(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_id_a7upaf4 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* id */
    void * gm_l2; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 56));
    uint32_t t4 = GM_LD(uint32_t, ((uint8_t *)t3));
    gm_l1 = t4;
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, (((uint8_t *)t5) + 56));
    void * t7 = (void *)((uint8_t *)t6);
    gm_l2 = t7;
    void * t8 = gm_l2;
    void * t9 = gm_l2;
    uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)t9));
    uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 443, 5);
    uint32_t t13 = r11;
    GM_ST(uint32_t, ((uint8_t *)t8), t13);
    uint32_t t14 = gm_l1;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_nth_c0r2nvs(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_c0r2nvs F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 72));
    uint64_t t7 = gm_l1;
    uint64_t r8; bool o9 = __builtin_add_overflow(t6, t7, &r8);
    if (o9) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 126, 70);
    uint64_t t10 = r8;
    void * t11 = gm_l0;
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t11) + 8) + 8));
    uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
    if (o14) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 126, 85);
    uint64_t t15 = r13;
    uint64_t t16 = (t10 < t15 ? t10 : t15);
    if ((uintptr_t)t16 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t16, t4, gm_srcuiab49hlgv7g, 126, 62);
    memmove(gm_out, (((uint8_t *)t3) + (uintptr_t)t16 * 48), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_nth_is_3fgqmcb(void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nth_is_3fgqmcb F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* n */
    gm_t9qkh0rsooc8d gm_l2; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    gm_t3fcbuehh3bs2 t4;
    gm_f_nth_c0r2nvs((void *)&t4, t2, t3);
    gm_t9qkh0rsooc8d t5 = gm_l2;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    uint8_t t7 = (uint8_t)(t6 == ((uint8_t)8ULL));
    if (t7) {
        gm_t9qkh0rsooc8d t8 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t4) + 8));
        t7 = (uint8_t)gm_eqv3tj1pu43g(((uint8_t *)&t8), ((uint8_t *)&t5));
    }
    uint8_t t9 = (uint8_t)(t7);
    gm_t3fcbuehh3bs2 t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)8ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 8), t5);
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_parse_postfix_ao4kstr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_parse_postfix_ao4kstr F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* allow_bang */
    GM_STACK_CHECK(&F, 307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_to1cssm8dfiv5 gm_l3;
    void * t2 = gm_l0;
    gm_f_parse_primary_1vvr4ji((void *)&gm_l3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t3) {
    } else {
        gm_to1cssm8dfiv5 t4;
        memset(&t4, 0, sizeof t4);
        ((uint8_t *)&t4)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t4 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t4), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l3) + 8), 88);
    void * t5 = gm_l0;
    uint8_t t6 = gm_l1;
    gm_tc3oht4ksth9d t7 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
    gm_f_parse_postfix_ops_2p1csn3((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t5, &t7, t6);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_9cfi95o(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_9cfi95o F;
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
        gm_b16_8 t9 = gm_f_empty_slice_atlcs4j();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_jjvqg83(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 72), ((uint8_t *)a1), 72);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 72)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_aqjeqnr(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_aqjeqnr F;
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
        gm_b16_8 t9 = gm_f_empty_slice_3jt8vgk();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_hvlnmhb(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d943vgp F;
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
        gm_b16_8 t9 = gm_f_empty_slice_g3tddrb();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_61fb705(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s5rtia7(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_s5rtia7 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_65q32rn();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ljd0ph4(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vbfukt4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vbfukt4 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_ito0pap();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5u3f7s0(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5u3f7s0(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5u3f7s0 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_61fb705 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hvlnmhb(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hvlnmhb F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_jjvqg83(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_jjvqg83 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)72ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ljd0ph4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ljd0ph4 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_djvi2hf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_report_djvi2hf F;
    void * gm_l0; /* p */
    gm_tl874t2atnlh3 gm_l1; /* at */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 136));
    uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srcuiab49hlgv7g, 253, 5);
    uint64_t t7 = r5;
    GM_ST(uint64_t, (((uint8_t *)t2) + 136), t7);
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 64));
    gm_tl874t2atnlh3 t10 = gm_l1;
    gm_b16_8 t11 = gm_l2;
    gm_b0_1 t12 = gm_f_report_eardf07((void *)NULL, cx, NULL, t9, t10, t11);
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
inline __attribute__((aligned(4))) gm_cs3cuou245tr5f gm_f_set_restr_p85qqmv(void * a0, gm_cs3cuou245tr5f a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_restr_p85qqmv F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* r */
    gm_toh0ootodhjue gm_l2; /* old */
    GM_STACK_CHECK_SHALLOW(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_toh0ootodhjue, a1);
    void * t2 = gm_l0;
    gm_toh0ootodhjue t3 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)t2) + 128));
    gm_l2 = t3;
    void * t4 = gm_l0;
    gm_toh0ootodhjue t5 = gm_l1;
    GM_ST(gm_toh0ootodhjue, (((uint8_t *)t4) + 128), t5);
    gm_toh0ootodhjue t6 = gm_l2;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cs3cuou245tr5f, F.ret);
gm_fprop:
    return GM_CONV(gm_cs3cuou245tr5f, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_skip_newlines_0ap7sq2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_skip_newlines_0ap7sq2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t5 = gm_l0;
            uint8_t t6 = gm_f_at_newline_94n732e(t5);
            if (!t6) {
                goto gm_lb1;
            }
            {
                void * t7 = gm_l0;
                gm_tm4pc0emhrv1g t8;
                gm_f_bump_q2i40re((void *)&t8, t7);
            }
gm_lb3:;
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
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_bm4ll8d(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_bm4ll8d F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_f_tok_ai6sc12(t2);
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t3) + 32));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_from_1sqap0o(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_from_1sqap0o F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)t2));
    uint32_t t4 = gm_l1;
    void * t5 = gm_l0;
    uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t5) + 80));
    uint32_t t7 = gm_l1;
    uint32_t t8 = (t6 > t7 ? t6 : t7);
    gm_tl874t2atnlh3 t9;
    GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t3);
    GM_ST(uint32_t, ((uint8_t *)&t9 + 4), t4);
    GM_ST(uint32_t, ((uint8_t *)&t9 + 8), t8);
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) void * gm_f_tok_ai6sc12(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tok_ai6sc12 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 72));
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 8) + 8));
    uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
    if (o10) gm_trap("integer overflow in `-`", gm_srcuiab49hlgv7g, 122, 73);
    uint64_t t11 = r9;
    uint64_t t12 = (t6 < t11 ? t6 : t11);
    if ((uintptr_t)t12 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t12, t4, gm_srcuiab49hlgv7g, 122, 56);
    void * t13 = (void *)(((uint8_t *)t3) + (uintptr_t)t12 * 48);
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_eat_const_0ntbpcs(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_sig_to_type_3r7l7ih(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_sig_to_type_3r7l7ih F;
    void * gm_l0; /* p */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t08olq97rscn3 gm_l8; /* n */
    GM_STACK_CHECK(&F, 651u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l9;
    uint8_t gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_trqfvmjtt4sij t20;
    gm_f_list_3t77g3a((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t20;
    void * t21 = gm_l1;
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)t21));
    gm_l3 = t22;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t23 = gm_l4;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t25 = (t23 < t24);
            if (!t25) {
                goto gm_lb1;
            }
            {
                void * t26 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t28 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t26) + (uintptr_t)t28 * 56), 56);
                        {
                            {
                                {
                                    {
                                        uint8_t t29 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l5) + 24)) != 0);
                                        if (t29) {
                                            goto gm_lb9;
                                        }
                                        gm_l9 = ((uint8_t)1);
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        gm_l9 = ((uint8_t)0);
                                        goto gm_lb8;
                                    }
gm_lb10:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 238, 12);
                                }
gm_lb8:;
                                uint8_t t30 = gm_l9;
                                if (!t30) {
                                    goto gm_lb7;
                                }
                                {
                                    {
                                        uint8_t t31 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l5) + 32)) != 0);
                                        if (t31) {
                                            goto gm_lb12;
                                        }
                                        gm_l10 = ((uint8_t)1);
                                        goto gm_lb11;
                                    }
gm_lb12:;
                                    {
                                        gm_l10 = ((uint8_t)0);
                                        goto gm_lb11;
                                    }
gm_lb13:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 238, 33);
                                }
gm_lb11:;
                                uint8_t t32 = gm_l10;
                                if (!t32) {
                                    goto gm_lb7;
                                }
                                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                uint8_t t34 = (t33 > ((uint64_t)0ULL));
                                if (!t34) {
                                    goto gm_lb7;
                                }
                                gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                                gm_l6 = t35;
                                gm_l7 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t36 = gm_l7;
                                        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                        uint8_t t38 = (t36 < t37);
                                        if (!t38) {
                                            goto gm_lb14;
                                        }
                                        {
                                            void * t39 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                            uint64_t t41 = gm_l7;
                                            {
                                                {
                                                    memmove(((uint8_t *)&gm_l8), (((uint8_t *)t39) + (uintptr_t)t41 * 32), 32);
                                                    void * t42 = (void *)((uint8_t *)&F.l2);
                                                    gm_b16_8 t43 = gm_f_empty_slice_65q32rn();
                                                    void * t44 = gm_l0;
                                                    gm_t08olq97rscn3 t45 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l8));
                                                    gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l8) + 16));
                                                    gm_t2gtbh39ptekd t47;
                                                    GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t47 + 40, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t47 + 48, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&t47 + 56, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)6ULL));
                                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t47 + 8), t45);
                                                    gm_tc3oht4ksth9d t48;
                                                    gm_f_mk_expr_53qj7nd((void *)&t48, t44, &t47, GM_CONV(gm_cseahvg8v7p0vs, t46));
                                                    void * t49 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &t48);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l8) + 16));
                                                    gm_tf2orqcrphmek t51;
                                                    GM_ST(uint16_t, (uint8_t *)&t51 + 18, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t51 + 20, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t51 + 52, 0);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t51 + 0), t43);
                                                    GM_ST(uint8_t, ((uint8_t *)&t51 + 16), ((uint8_t)0));
                                                    GM_ST(uint8_t, ((uint8_t *)&t51 + 17), ((uint8_t)0));
                                                    GM_ST(void *, ((uint8_t *)&t51 + 24), t49);
                                                    GM_ST(void *, ((uint8_t *)&t51 + 32), ((void *)0));
                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t51 + 40), t50);
                                                    gm_tftb1odc739t5 t52 = gm_f_push_vbfukt4(t42, &t51);
                                                    gm_l11 = t52;
                                                    uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                                    if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcp8o48ld4n4jq, 240, 17);
                                                    goto gm_lb17;
                                                }
gm_lb18:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 239, 13);
                                            }
gm_lb17:;
                                        }
gm_lb16:;
                                        uint64_t t54 = gm_l7;
                                        uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                                        uint64_t t57 = r55;
                                        gm_l7 = t57;
                                    }
                                }
gm_lb14:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            void * t58 = (void *)((uint8_t *)&F.l2);
                            gm_tftb1odc739t5 t59 = gm_f_push_vbfukt4(t58, &(*(gm_tf2orqcrphmek *)((uint8_t *)&F.l5)));
                            gm_l12 = t59;
                            uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                            if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcp8o48ld4n4jq, 250, 13);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 237, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t61 = gm_l4;
            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
            uint64_t t64 = r62;
            gm_l4 = t64;
        }
    }
gm_lb1:;
    void * t65 = gm_l1;
    gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    GM_ST(gm_b16_8, ((uint8_t *)t65), t66);
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
inline __attribute__((aligned(4))) void gm_f_parse_attr_args_16rponu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_attr_args_16rponu F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    GM_STACK_CHECK(&F, 479u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t0u0j6voh3loe gm_l3;
    gm_to1cssm8dfiv5 gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_t0u0j6voh3loe gm_l6;
    {
        void * t7 = gm_l0;
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(0));
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)61ULL));
        gm_t0u0j6voh3loe t10 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t7, t9, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t10;
        uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t11) {
        } else {
            gm_tt7ek1a8bopcl t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 16, 0);
            ((uint8_t *)&t12)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t12 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t12), 24);
            goto gm_fdone;
        }
        gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 4));
        void * t14 = gm_l0;
        gm_toh0ootodhjue t15;
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t15 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t15 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t16 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t14, GM_CONV(gm_cs3cuou245tr5f, t15)));
        gm_l1 = t16;
        gm_trqfvmjtt4sij t17;
        gm_f_list_vp1djv1((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld6_1;
        }
        F.l2 = t17;
        {
            for (;;) {
                void * t18 = gm_l0;
                gm_t9qkh0rsooc8d t19;
                GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)62ULL));
                uint8_t t20 = gm_f_at_54906rm(t18, t19);
                uint8_t t21 = (uint8_t)!t20;
                if (!t21) {
                    goto gm_lb2;
                }
                {
                    void * t22 = (void *)((uint8_t *)&F.l2);
                    void * t23 = gm_l0;
                    gm_f_parse_expr_bkb2n6e((void *)&gm_l4, (void *)NULL, cx, NULL, t23);
                    if (gm_unw.kind) {
                        goto gm_ld6_1;
                    }
                    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                    if (t24) {
                    } else {
                        gm_tt7ek1a8bopcl t25;
                        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t25 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t25 + 16, 0);
                        ((uint8_t *)&t25)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t25 + 8), GM_ZERO(gm_b0_1));
                        memmove(&F.ret, ((uint8_t *)&t25), 24);
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                        goto gm_ld6_1;
                    }
                    gm_tftb1odc739t5 t26 = gm_f_push_d943vgp(t22, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&gm_l4) + 8)));
                    gm_l5 = t26;
                    uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                    if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcp8o48ld4n4jq, 164, 9);
                    void * t28 = gm_l0;
                    gm_t9qkh0rsooc8d t29;
                    GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)57ULL));
                    uint8_t t30 = gm_f_eat_qmock9g(t28, t29);
                    uint8_t t31 = (uint8_t)!t30;
                    if (t31) {
                        goto gm_lb2;
                    }
                }
gm_lb4:;
            }
        }
gm_lb2:;
        void * t32 = gm_l0;
        gm_b16_8 t33;
        GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s35hbb2rhhdvv));
        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(32));
        gm_t9qkh0rsooc8d t34;
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)62ULL));
        gm_t0u0j6voh3loe t35 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t32, t34, t33);
        if (gm_unw.kind) {
            goto gm_ld6_1;
        }
        gm_l6 = t35;
        uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (t36) {
        } else {
            gm_tt7ek1a8bopcl t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 16, 0);
            ((uint8_t *)&t37)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t37 + 8), GM_ZERO(gm_b0_1));
            memmove(&F.ret, ((uint8_t *)&t37), 24);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld6_1;
        }
        gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l6) + 4));
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        gm_tt7ek1a8bopcl t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        ((uint8_t *)&t40)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t40 + 8), t39);
        memmove(&F.ret, ((uint8_t *)&t40), 24);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
        goto gm_ld6_1;
    }
    {
        void * t41 = gm_l0;
        gm_toh0ootodhjue t42 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t41) + 128), t42);
    }
    goto gm_ls43;
gm_ld6_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t44 = gm_l0;
        gm_toh0ootodhjue t45 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t44) + 128), t45);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls43:;
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
inline __attribute__((aligned(4))) void gm_f_parse_attributed_00u9rjp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_parse_attributed_00u9rjp F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* mode */
    uint32_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* attrs */
    GM_STACK_CHECK(&F, 716u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tt7ek1a8bopcl gm_l5;
    gm_to1cssm8dfiv5 gm_l6;
    void * t2 = gm_l0;
    uint32_t t3 = gm_f_lo_fp82gt4(t2);
    gm_l2 = t3;
    void * t4 = gm_l0;
    gm_f_parse_attrs_imvfdlq((void *)&gm_l5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (t5) {
    } else {
        gm_to1cssm8dfiv5 t6;
        memset(&t6, 0, sizeof t6);
        ((uint8_t *)&t6)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t6), 96);
        goto gm_fdone;
    }
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_l3 = t7;
    void * t8 = gm_l0;
    gm_t9qkh0rsooc8d t9 = gm_l1;
    gm_f_parse_type_operand_7guq29a((void *)&gm_l6, (void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t10) {
    } else {
        gm_to1cssm8dfiv5 t11;
        memset(&t11, 0, sizeof t11);
        ((uint8_t *)&t11)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t11), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l6) + 8), 88);
    void * t12 = gm_l0;
    gm_b16_8 t13 = gm_l3;
    void * t14 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l4)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tpe1hv3fekn05 t15;
    GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t13);
    GM_ST(void *, ((uint8_t *)&t15 + 16), t14);
    void * t16 = gm_l0;
    uint32_t t17 = gm_l2;
    gm_t2gtbh39ptekd t18;
    memset(&t18, 0, sizeof t18);
    GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)35ULL));
    GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t18 + 8), t15);
    gm_tl874t2atnlh3 t19 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t16, t17));
    gm_tc3oht4ksth9d t20;
    gm_f_mk_expr_53qj7nd((void *)&t20, t12, &t18, GM_CONV(gm_cseahvg8v7p0vs, t19));
    gm_to1cssm8dfiv5 t21;
    GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
    ((uint8_t *)&t21)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t21 + 8), t20);
    memmove(gm_out, ((uint8_t *)&t21), 96);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_parse_attrs_imvfdlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_attrs_imvfdlq F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l2; /* sym */
    gm_th2lqbo77pvsc gm_l4; /* args */
    GM_STACK_CHECK(&F, 526u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t3fcbuehh3bs2 gm_l5;
    gm_tt7ek1a8bopcl gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_trqfvmjtt4sij t5;
    gm_f_list_18sm81j((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t5;
    {
        for (;;) {
            void * t6 = gm_l0;
            gm_f_kind_k239kpg((void *)&gm_l5, t6);
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)7ULL));
            if (!t8) {
                goto gm_lb1;
            }
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
            gm_l2 = t9;
            {
                void * t10 = gm_l0;
                gm_f_bump_q2i40re((void *)&F.l3, t10);
                gm_th2lqbo77pvsc t11;
                memset(&t11, 0, sizeof t11);
                gm_l4 = t11;
                void * t12 = gm_l0;
                gm_t9qkh0rsooc8d t13;
                GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)61ULL));
                uint8_t t14 = gm_f_at_54906rm(t12, t13);
                if (t14) {
                    void * t15 = gm_l0;
                    gm_f_parse_attr_args_16rponu((void *)&gm_l6, (void *)NULL, cx, NULL, t15);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                    if (t16) {
                    } else {
                        gm_tt7ek1a8bopcl t17;
                        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t17 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t17 + 16, 0);
                        ((uint8_t *)&t17)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 8), GM_ZERO(gm_b0_1));
                        memmove(gm_out, ((uint8_t *)&t17), 24);
                        goto gm_fdone;
                    }
                    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                    gm_th2lqbo77pvsc t19;
                    GM_ST(uint8_t, (uint8_t *)&t19 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t19 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t19 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t19), t18);
                    ((uint8_t *)&t19)[16] = 1;
                    gm_l4 = t19;
                }
                void * t20 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t21 = gm_l2;
                gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 32));
                gm_t08olq97rscn3 t23;
                GM_ST(uint32_t, (uint8_t *)&t23 + 28, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t21);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t23 + 16), t22);
                void * t24 = gm_l0;
                uint32_t t25 = GM_LD(uint32_t, ((((uint8_t *)&F.l3) + 32) + 4));
                gm_th2lqbo77pvsc t26 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l4));
                gm_tl874t2atnlh3 t27 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t24, t25));
                gm_tkacitepg5fk0 t28;
                GM_ST(uint32_t, (uint8_t *)&t28 + 68, 0);
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t28 + 0), t23);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t28 + 32), t26);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t28 + 56), t27);
                gm_tftb1odc739t5 t29 = gm_f_push_9cfi95o(t20, &t28);
                gm_l7 = t29;
                uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcp8o48ld4n4jq, 152, 9);
                void * t31 = gm_l0;
                gm_f_skip_newlines_0ap7sq2(t31);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_tt7ek1a8bopcl t33;
    GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
    ((uint8_t *)&t33)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t33 + 8), t32);
    memmove(gm_out, ((uint8_t *)&t33), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_parse_cont_type_ljmco4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_cont_type_ljmco4q F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK(&F, 794u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_to1cssm8dfiv5 gm_l3;
    gm_t0u0j6voh3loe gm_l4;
    void * t2 = gm_l0;
    uint32_t t3 = gm_f_lo_fp82gt4(t2);
    gm_l1 = t3;
    void * t4 = gm_l0;
    gm_tm4pc0emhrv1g t5;
    gm_f_bump_q2i40re((void *)&t5, t4);
    void * t6 = gm_l0;
    gm_tm4pc0emhrv1g t7;
    gm_f_bump_q2i40re((void *)&t7, t6);
    void * t8 = gm_l0;
    gm_toh0ootodhjue t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t9 + 1), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t9 + 2), ((uint8_t)0));
    gm_f_parse_type_in_bvi7uav((void *)&gm_l3, (void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t10) {
    } else {
        gm_to1cssm8dfiv5 t11;
        memset(&t11, 0, sizeof t11);
        ((uint8_t *)&t11)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t11), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l3) + 8), 88);
    void * t12 = gm_l0;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_st803nqm7iok0));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(16));
    gm_t9qkh0rsooc8d t14;
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)62ULL));
    gm_t0u0j6voh3loe t15 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t12, t14, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t15;
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (t16) {
    } else {
        gm_to1cssm8dfiv5 t17;
        memset(&t17, 0, sizeof t17);
        ((uint8_t *)&t17)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t17 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t17), 96);
        goto gm_fdone;
    }
    gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l4) + 4));
    void * t19 = gm_l0;
    void * t20 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t21 = gm_l0;
    uint32_t t22 = gm_l1;
    gm_t2gtbh39ptekd t23;
    memset(&t23, 0, sizeof t23);
    GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)31ULL));
    GM_ST(void *, ((uint8_t *)&t23 + 8), t20);
    gm_tl874t2atnlh3 t24 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t21, t22));
    gm_tc3oht4ksth9d t25;
    gm_f_mk_expr_53qj7nd((void *)&t25, t19, &t23, GM_CONV(gm_cseahvg8v7p0vs, t24));
    gm_to1cssm8dfiv5 t26;
    GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
    ((uint8_t *)&t26)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t26 + 8), t25);
    memmove(gm_out, ((uint8_t *)&t26), 96);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_parse_fn_body_09mp5ir(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_fn_body_09mp5ir F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 860u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toc4mrkn2e7o9 gm_l1;
    gm_toc4mrkn2e7o9 gm_l2;
    gm_to1cssm8dfiv5 gm_l3;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)59ULL));
    uint8_t t4 = gm_f_eat_qmock9g(t2, t3);
    uint8_t t5 = (uint8_t)!t4;
    if (t5) {
        void * t6 = gm_l0;
        gm_f_parse_block_tpg3f1i((void *)&gm_l1, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
        if (t7) {
        } else {
            gm_tihhqadak7gf3 t8;
            memset(&t8, 0, sizeof t8);
            ((uint8_t *)&t8)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t8 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t8), 56);
            goto gm_fdone;
        }
        gm_t79rbdv4rafe5 t9 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&gm_l1) + 8));
        gm_tapdr2ag17fh7 t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t10 + 8), t9);
        gm_tihhqadak7gf3 t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
        ((uint8_t *)&t11)[0] = 0;
        GM_ST(gm_tapdr2ag17fh7, ((uint8_t *)&t11 + 8), t10);
        memmove(gm_out, ((uint8_t *)&t11), 56);
        goto gm_fdone;
    }
    void * t12 = gm_l0;
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)65ULL));
    uint8_t t14 = gm_f_at_54906rm(t12, t13);
    if (t14) {
        void * t15 = gm_l0;
        void * t16 = gm_l0;
        gm_tl874t2atnlh3 t17 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t16));
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_smu8iti9fc903));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(84));
        gm_b0_1 t19 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t15, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t20 = gm_l0;
        gm_f_parse_block_tpg3f1i((void *)&gm_l2, (void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (t21) {
        } else {
            gm_tihhqadak7gf3 t22;
            memset(&t22, 0, sizeof t22);
            ((uint8_t *)&t22)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t22 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t22), 56);
            goto gm_fdone;
        }
        gm_t79rbdv4rafe5 t23 = GM_LD(gm_t79rbdv4rafe5, (((uint8_t *)&gm_l2) + 8));
        gm_tapdr2ag17fh7 t24;
        GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
        GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t24 + 8), t23);
        gm_tihhqadak7gf3 t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
        ((uint8_t *)&t25)[0] = 0;
        GM_ST(gm_tapdr2ag17fh7, ((uint8_t *)&t25 + 8), t24);
        memmove(gm_out, ((uint8_t *)&t25), 56);
        goto gm_fdone;
    }
    void * t26 = gm_l0;
    gm_f_parse_expr_bkb2n6e((void *)&gm_l3, (void *)NULL, cx, NULL, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t27) {
    } else {
        gm_tihhqadak7gf3 t28;
        memset(&t28, 0, sizeof t28);
        ((uint8_t *)&t28)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t28 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t28), 56);
        goto gm_fdone;
    }
    void * t29 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&gm_l3) + 8)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tapdr2ag17fh7 t30;
    memset(&t30, 0, sizeof t30);
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)1ULL));
    GM_ST(void *, ((uint8_t *)&t30 + 8), t29);
    gm_tihhqadak7gf3 t31;
    GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
    ((uint8_t *)&t31)[0] = 0;
    GM_ST(gm_tapdr2ag17fh7, ((uint8_t *)&t31 + 8), t30);
    memmove(gm_out, ((uint8_t *)&t31), 56);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_parse_fn_expr_c9357a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_fn_expr_c9357a4 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    gm_b16_8 gm_l3; /* where_ */
    uint8_t gm_l4; /* is_lit */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tl874t2atnlh3 gm_l10; /* whole */
    GM_STACK_CHECK(&F, 1829u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tdmdfjrg7d59n gm_l11;
    gm_tt7ek1a8bopcl gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    gm_tq7s98ihonkju gm_l15;
    gm_tq7s98ihonkju gm_l16;
    void * t7 = gm_l0;
    uint32_t t8 = gm_f_lo_fp82gt4(t7);
    gm_l1 = t8;
    void * t9 = gm_l0;
    gm_f_parse_fn_sig_fecr4sl((void *)&gm_l11, (void *)NULL, cx, NULL, t9, ((uint8_t)0));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (t10) {
    } else {
        gm_to1cssm8dfiv5 t11;
        memset(&t11, 0, sizeof t11);
        ((uint8_t *)&t11)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t11 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t11), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l11) + 8), 72);
    void * t12 = gm_l0;
    gm_f_parse_where_opt_a5p2ja6((void *)&gm_l12, (void *)NULL, cx, NULL, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (t13) {
    } else {
        gm_to1cssm8dfiv5 t14;
        memset(&t14, 0, sizeof t14);
        ((uint8_t *)&t14)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t14 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t14), 96);
        goto gm_fdone;
    }
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
    gm_l3 = t15;
    void * t16 = gm_l0;
    gm_t9qkh0rsooc8d t17;
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)59ULL));
    uint8_t t18 = gm_f_at_54906rm(t16, t17);
    if (t18) {
        gm_l13 = ((uint8_t)1);
    } else {
        void * t19 = gm_l0;
        gm_t9qkh0rsooc8d t20;
        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)65ULL));
        uint8_t t21 = gm_f_at_54906rm(t19, t20);
        if (t21) {
            void * t22 = gm_l0;
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t22) + 128));
            uint8_t t24 = (uint8_t)!t23;
            gm_l14 = t24;
        } else {
            gm_l14 = ((uint8_t)0);
        }
        uint8_t t25 = gm_l14;
        gm_l13 = t25;
    }
    uint8_t t26 = gm_l13;
    gm_l4 = t26;
    uint8_t t27 = gm_l4;
    uint8_t t28 = (uint8_t)!t27;
    if (t28) {
        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t30 = (t29 > ((uint64_t)0ULL));
        if (t30) {
            void * t31 = gm_l0;
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_suatl193icom4));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(40));
            gm_b0_1 t33 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t34;
            memset(&t34, 0, sizeof t34);
            ((uint8_t *)&t34)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t34 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t34), 96);
            goto gm_fdone;
        }
        void * t35 = gm_l0;
        void * t36 = (void *)((uint8_t *)&F.l2);
        gm_b0_1 t37 = gm_f_fn_sig_to_type_3r7l7ih((void *)NULL, cx, NULL, t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t38 = gm_l0;
        gm_tq7s98ihonkju t39 = gm_f_new_48hbqcm((void *)NULL, cx, NULL, &(*(gm_tlivgedjs5ps3 *)((uint8_t *)&F.l2)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_khouror17c2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t39;
        uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
        if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcp8o48ld4n4jq, 210, 34);
        void * t41 = GM_LD(void *, (((uint8_t *)&gm_l15) + 8));
        void * t42 = gm_l0;
        uint32_t t43 = gm_l1;
        gm_t2gtbh39ptekd t44;
        memset(&t44, 0, sizeof t44);
        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)30ULL));
        GM_ST(void *, ((uint8_t *)&t44 + 8), t41);
        gm_tl874t2atnlh3 t45 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t42, t43));
        gm_tc3oht4ksth9d t46;
        gm_f_mk_expr_53qj7nd((void *)&t46, t38, &t44, GM_CONV(gm_cseahvg8v7p0vs, t45));
        gm_to1cssm8dfiv5 t47;
        GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
        ((uint8_t *)&t47)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t47 + 8), t46);
        memmove(gm_out, ((uint8_t *)&t47), 96);
        goto gm_fdone;
    }
    gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_l5 = t48;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t49 = gm_l6;
            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t51 = (t49 < t50);
            if (!t51) {
                goto gm_lb1;
            }
            {
                void * t52 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t54 = gm_l6;
                {
                    {
                        memmove(((uint8_t *)&F.l7), (((uint8_t *)t52) + (uintptr_t)t54 * 56), 56);
                        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                        uint8_t t56 = ((t55 == ((uint64_t)0ULL)));
                        if (t56) {
                            void * t57 = gm_l0;
                            gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l7) + 40));
                            gm_b16_8 t59;
                            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sud6d85d454o1));
                            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(41));
                            gm_b0_1 t60 = gm_f_report_djvi2hf((void *)NULL, cx, NULL, t57, t58, t59);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 212, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t61 = gm_l6;
            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
            uint64_t t64 = r62;
            gm_l6 = t64;
        }
    }
gm_lb1:;
    void * t65 = gm_l0;
    memmove(((uint8_t *)&F.l8), (((uint8_t *)t65) + 88), 40);
    void * t66 = gm_l0;
    gm_trqfvmjtt4sij t67;
    gm_f_list_epvducg((void *)&t67, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t66) + 88), t67);
    void * t68 = gm_l0;
    gm_f_parse_fn_body_09mp5ir((void *)&F.l9, (void *)NULL, cx, NULL, t68);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t69 = gm_l0;
    memmove((((uint8_t *)t69) + 88), ((uint8_t *)&F.l8), 40);
    void * t70 = gm_l0;
    uint32_t t71 = gm_l1;
    gm_tl874t2atnlh3 t72 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t70, t71));
    gm_l10 = t72;
    void * t73 = gm_l0;
    gm_b16_8 t74 = gm_l3;
    uint8_t t75 = (uint8_t)!((((uint8_t *)&F.l9))[0] != 0);
    gm_tlivgedjs5ps3 t76 = GM_LD(gm_tlivgedjs5ps3, ((uint8_t *)&F.l2));
    if (t75) {
    } else {
        gm_to1cssm8dfiv5 t77;
        memset(&t77, 0, sizeof t77);
        ((uint8_t *)&t77)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t77 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t77), 96);
        goto gm_fdone;
    }
    gm_tl874t2atnlh3 t78 = gm_l10;
    gm_tapdr2ag17fh7 t79 = GM_LD(gm_tapdr2ag17fh7, (((uint8_t *)&F.l9) + 8));
    gm_t0kv05e068jrn t80;
    GM_ST(uint32_t, (uint8_t *)&t80 + 148, 0);
    GM_ST(gm_tlivgedjs5ps3, ((uint8_t *)&t80 + 0), t76);
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 72), t74);
    GM_ST(gm_tapdr2ag17fh7, ((uint8_t *)&t80 + 88), t79);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t80 + 136), t78);
    gm_tq7s98ihonkju t81 = gm_f_new_cnt7p5u((void *)NULL, cx, NULL, &t80, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kakc8s3pa9o)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l16 = t81;
    uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcp8o48ld4n4jq, 221, 26);
    void * t83 = GM_LD(void *, (((uint8_t *)&gm_l16) + 8));
    gm_tl874t2atnlh3 t84 = gm_l10;
    gm_t2gtbh39ptekd t85;
    memset(&t85, 0, sizeof t85);
    GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)36ULL));
    GM_ST(void *, ((uint8_t *)&t85 + 8), t83);
    gm_tc3oht4ksth9d t86;
    gm_f_mk_expr_53qj7nd((void *)&t86, t73, &t85, GM_CONV(gm_cseahvg8v7p0vs, t84));
    gm_to1cssm8dfiv5 t87;
    GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
    ((uint8_t *)&t87)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t87 + 8), t86);
    memmove(gm_out, ((uint8_t *)&t87), 96);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_parse_fn_sig_fecr4sl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_parse_fn_sig_fecr4sl F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* is_foreign */
    uint32_t gm_l2; /* start */
    gm_th2lqbo77pvsc gm_l3; /* params */
    void * gm_l4; /* ret */
    gm_th2lqbo77pvsc gm_l5; /* effects */
    GM_STACK_CHECK(&F, 1052u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t0u0j6voh3loe gm_l6;
    gm_t0u0j6voh3loe gm_l7;
    gm_thk954m1ol1ig gm_l8;
    gm_to1cssm8dfiv5 gm_l9;
    gm_thk954m1ol1ig gm_l10;
    void * t2 = gm_l0;
    uint32_t t3 = gm_f_lo_fp82gt4(t2);
    gm_l2 = t3;
    void * t4 = gm_l0;
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(0));
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)11ULL));
    gm_t0u0j6voh3loe t7 = gm_f_expect_kw_7kbmp0i((void *)NULL, cx, NULL, t4, t6, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t8) {
    } else {
        gm_tdmdfjrg7d59n t9;
        memset(&t9, 0, sizeof t9);
        ((uint8_t *)&t9)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t9 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t9), 80);
        goto gm_fdone;
    }
    gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l6) + 4));
    void * t11 = gm_l0;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_slgf61fmeeof0));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(27));
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)61ULL));
    gm_t0u0j6voh3loe t14 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t11, t13, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t14;
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (t15) {
    } else {
        gm_tdmdfjrg7d59n t16;
        memset(&t16, 0, sizeof t16);
        ((uint8_t *)&t16)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t16 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t16), 80);
        goto gm_fdone;
    }
    gm_tl874t2atnlh3 t17 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l7) + 4));
    void * t18 = gm_l0;
    gm_f_parse_params_3g43t5f((void *)&gm_l8, (void *)NULL, cx, NULL, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (t19) {
    } else {
        gm_tdmdfjrg7d59n t20;
        memset(&t20, 0, sizeof t20);
        ((uint8_t *)&t20)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t20 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t20), 80);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l8) + 8), 24);
    gm_l4 = ((void *)0);
    void * t21 = gm_l0;
    gm_t9qkh0rsooc8d t22;
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)60ULL));
    uint8_t t23 = gm_f_eat_qmock9g(t21, t22);
    if (t23) {
        void * t24 = gm_l0;
        gm_f_parse_type_2m93dtq((void *)&gm_l9, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (t25) {
        } else {
            gm_tdmdfjrg7d59n t26;
            memset(&t26, 0, sizeof t26);
            ((uint8_t *)&t26)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t26 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t26), 80);
            goto gm_fdone;
        }
        void * t27 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&gm_l9) + 8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t27;
    }
    void * t28 = gm_l0;
    uint8_t t29 = gm_l1;
    gm_f_parse_needs_opt_8lp1u45((void *)&gm_l10, (void *)NULL, cx, NULL, t28, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (t30) {
    } else {
        gm_tdmdfjrg7d59n t31;
        memset(&t31, 0, sizeof t31);
        ((uint8_t *)&t31)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t31 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t31), 80);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&gm_l5), (((uint8_t *)&gm_l10) + 8), 24);
    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 16));
    void * t34 = gm_l4;
    void * t35 = gm_l0;
    uint32_t t36 = gm_l2;
    gm_th2lqbo77pvsc t37 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l5));
    gm_tl874t2atnlh3 t38 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t35, t36));
    gm_tlivgedjs5ps3 t39;
    GM_ST(uint8_t, (uint8_t *)&t39 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t39 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t39 + 20, 0);
    GM_ST(uint32_t, (uint8_t *)&t39 + 68, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t32);
    GM_ST(uint8_t, ((uint8_t *)&t39 + 16), t33);
    GM_ST(void *, ((uint8_t *)&t39 + 24), t34);
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t39 + 32), t37);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t39 + 56), t38);
    gm_tdmdfjrg7d59n t40;
    GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
    ((uint8_t *)&t40)[0] = 0;
    GM_ST(gm_tlivgedjs5ps3, ((uint8_t *)&t40 + 8), t39);
    memmove(gm_out, ((uint8_t *)&t40), 80);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_parse_fn_type_only_0ifghqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_fn_type_only_0ifghqh F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    GM_STACK_CHECK(&F, 530u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tdmdfjrg7d59n gm_l3;
    gm_tq7s98ihonkju gm_l4;
    void * t2 = gm_l0;
    uint32_t t3 = gm_f_lo_fp82gt4(t2);
    gm_l1 = t3;
    void * t4 = gm_l0;
    gm_f_parse_fn_type_sig_jrv5v82((void *)&gm_l3, (void *)NULL, cx, NULL, t4, ((uint8_t)0));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t5) {
    } else {
        gm_to1cssm8dfiv5 t6;
        memset(&t6, 0, sizeof t6);
        ((uint8_t *)&t6)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t6 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t6), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l3) + 8), 72);
    void * t7 = gm_l0;
    gm_tq7s98ihonkju t8 = gm_f_new_48hbqcm((void *)NULL, cx, NULL, &(*(gm_tlivgedjs5ps3 *)((uint8_t *)&F.l2)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kh832np8pgs)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcp8o48ld4n4jq, 198, 30);
    void * t10 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
    void * t11 = gm_l0;
    uint32_t t12 = gm_l1;
    gm_t2gtbh39ptekd t13;
    memset(&t13, 0, sizeof t13);
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)30ULL));
    GM_ST(void *, ((uint8_t *)&t13 + 8), t10);
    gm_tl874t2atnlh3 t14 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t11, t12));
    gm_tc3oht4ksth9d t15;
    gm_f_mk_expr_53qj7nd((void *)&t15, t7, &t13, GM_CONV(gm_cseahvg8v7p0vs, t14));
    gm_to1cssm8dfiv5 t16;
    GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
    ((uint8_t *)&t16)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t16 + 8), t15);
    memmove(gm_out, ((uint8_t *)&t16), 96);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_parse_fn_type_sig_jrv5v82(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_parse_fn_type_sig_jrv5v82 F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* is_foreign */
    GM_STACK_CHECK(&F, 347u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tdmdfjrg7d59n gm_l3;
    void * t2 = gm_l0;
    uint8_t t3 = gm_l1;
    gm_f_parse_fn_sig_fecr4sl((void *)&gm_l3, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (t4) {
    } else {
        gm_tdmdfjrg7d59n t5;
        memset(&t5, 0, sizeof t5);
        ((uint8_t *)&t5)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t5 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t5), 80);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l3) + 8), 72);
    void * t6 = gm_l0;
    void * t7 = (void *)((uint8_t *)&F.l2);
    gm_b0_1 t8 = gm_f_fn_sig_to_type_3r7l7ih((void *)NULL, cx, NULL, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tlivgedjs5ps3 t9 = GM_LD(gm_tlivgedjs5ps3, ((uint8_t *)&F.l2));
    gm_tdmdfjrg7d59n t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
    ((uint8_t *)&t10)[0] = 0;
    GM_ST(gm_tlivgedjs5ps3, ((uint8_t *)&t10 + 8), t9);
    memmove(gm_out, ((uint8_t *)&t10), 80);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_parse_needs_opt_8lp1u45(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_parse_needs_opt_8lp1u45 F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* is_foreign */
    gm_tl874t2atnlh3 gm_l2; /* at */
    GM_STACK_CHECK(&F, 791u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l4;
    gm_t3fcbuehh3bs2 gm_l5;
    gm_to1cssm8dfiv5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    void * t8 = gm_l0;
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_shv4avtqvfr4p));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(5));
    uint8_t t10 = gm_f_at_word_18dnh5f(t8, t9);
    uint8_t t11 = (uint8_t)!t10;
    if (t11) {
        gm_th2lqbo77pvsc t12;
        memset(&t12, 0, sizeof t12);
        gm_thk954m1ol1ig t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        ((uint8_t *)&t13)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t13 + 8), t12);
        memmove(gm_out, ((uint8_t *)&t13), 32);
        goto gm_fdone;
    }
    void * t14 = gm_l0;
    gm_tm4pc0emhrv1g t15;
    gm_f_bump_q2i40re((void *)&t15, t14);
    void * t16 = gm_l0;
    gm_t9qkh0rsooc8d t17;
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)18ULL));
    uint8_t t18 = gm_f_at_kw_p0r3sr9(t16, t17);
    if (t18) {
        void * t19 = gm_l0;
        gm_tl874t2atnlh3 t20 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t19));
        gm_l2 = t20;
        void * t21 = gm_l0;
        gm_tm4pc0emhrv1g t22;
        gm_f_bump_q2i40re((void *)&t22, t21);
        uint8_t t23 = gm_l1;
        uint8_t t24 = (uint8_t)!t23;
        if (t24) {
            void * t25 = gm_l0;
            gm_tl874t2atnlh3 t26 = gm_l2;
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_se7kda46rm021));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(106));
            gm_b0_1 t28 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t25, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thk954m1ol1ig t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t29 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t29), 32);
            goto gm_fdone;
        }
        void * t30 = gm_l0;
        gm_f_kind_k239kpg((void *)&gm_l5, t30);
        {
            {
                uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                uint8_t t32 = (uint8_t)(t31 == ((uint8_t)0ULL));
                if (!t32) {
                    goto gm_lb2;
                }
                gm_l4 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l4 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 329, 12);
        }
gm_lb1:;
        uint8_t t33 = gm_l4;
        if (t33) {
            void * t34 = gm_l0;
            void * t35 = gm_l0;
            gm_tl874t2atnlh3 t36 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t35));
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s9tr38mqso4n7));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(92));
            gm_b0_1 t38 = gm_f_fail_e5kqpus((void *)NULL, cx, NULL, t34, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thk954m1ol1ig t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t39 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t39 + 24, 0);
            ((uint8_t *)&t39)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t39 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t39), 32);
            goto gm_fdone;
        }
        gm_b16_8 t40 = gm_f_empty_slice_g3tddrb();
        gm_th2lqbo77pvsc t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t41 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t41 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t41), t40);
        ((uint8_t *)&t41)[16] = 1;
        gm_thk954m1ol1ig t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
        ((uint8_t *)&t42)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t42 + 8), t41);
        memmove(gm_out, ((uint8_t *)&t42), 32);
        goto gm_fdone;
    }
    gm_trqfvmjtt4sij t43;
    gm_f_list_vp1djv1((void *)&t43, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t43;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb4;
            }
            {
                void * t44 = (void *)((uint8_t *)&F.l3);
                void * t45 = gm_l0;
                gm_toh0ootodhjue t46;
                GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)1));
                GM_ST(uint8_t, ((uint8_t *)&t46 + 1), ((uint8_t)0));
                GM_ST(uint8_t, ((uint8_t *)&t46 + 2), ((uint8_t)0));
                gm_f_parse_type_in_bvi7uav((void *)&gm_l6, (void *)NULL, cx, NULL, t45, t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                if (t47) {
                } else {
                    gm_thk954m1ol1ig t48;
                    GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t48 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t48 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t48 + 24, 0);
                    ((uint8_t *)&t48)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t48 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t48), 32);
                    goto gm_fdone;
                }
                gm_tftb1odc739t5 t49 = gm_f_push_d943vgp(t44, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&gm_l6) + 8)));
                gm_l7 = t49;
                uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcp8o48ld4n4jq, 336, 9);
                void * t51 = gm_l0;
                gm_t9qkh0rsooc8d t52;
                GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)0ULL));
                uint8_t t53 = gm_f_eat_qmock9g(t51, t52);
                uint8_t t54 = (uint8_t)!t53;
                if (t54) {
                    goto gm_lb4;
                }
                void * t55 = gm_l0;
                gm_f_skip_newlines_0ap7sq2(t55);
            }
gm_lb6:;
        }
    }
gm_lb4:;
    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_th2lqbo77pvsc t57;
    GM_ST(uint8_t, (uint8_t *)&t57 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t57 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t57 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t57), t56);
    ((uint8_t *)&t57)[16] = 1;
    gm_thk954m1ol1ig t58;
    GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
    ((uint8_t *)&t58)[0] = 0;
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t58 + 8), t57);
    memmove(gm_out, ((uint8_t *)&t58), 32);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_parse_param_group_ac8kpvk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_param_group_ac8kpvk F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* start */
    uint64_t gm_l2; /* k */
    uint64_t gm_l3; /* names_found */
    uint8_t gm_l4; /* named */
    void * gm_l5; /* ty */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint8_t gm_l10; /* comptime */
    uint8_t gm_l11; /* rest */
    void * gm_l12; /* ty */
    void * gm_l13; /* default */
    GM_STACK_CHECK(&F, 1873u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l14;
    uint8_t gm_l15;
    gm_to1cssm8dfiv5 gm_l16;
    gm_t4jn8fl3lmk5k gm_l17;
    gm_tftb1odc739t5 gm_l18;
    uint8_t gm_l19;
    gm_to1cssm8dfiv5 gm_l20;
    gm_to1cssm8dfiv5 gm_l21;
    void * t12 = gm_l0;
    uint32_t t13 = gm_f_lo_fp82gt4(t12);
    gm_l1 = t13;
    gm_l2 = ((uint64_t)0ULL);
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            void * t14 = gm_l0;
            uint64_t t15 = gm_l2;
            uint8_t t16 = gm_f_nth_is_name_oqhoobd(t14, t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                uint64_t t17 = gm_l3;
                uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
                if (o19) gm_trap("integer overflow in `+`", gm_srcp8o48ld4n4jq, 288, 9);
                uint64_t t20 = r18;
                gm_l3 = t20;
                uint64_t t21 = gm_l2;
                uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                if (o23) gm_trap("integer overflow in `+`", gm_srcp8o48ld4n4jq, 289, 9);
                uint64_t t24 = r22;
                gm_l2 = t24;
                void * t25 = gm_l0;
                uint64_t t26 = gm_l2;
                gm_t9qkh0rsooc8d t27;
                GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)57ULL));
                uint8_t t28 = gm_f_nth_is_3fgqmcb(t25, t26, t27);
                if (t28) {
                    void * t29 = gm_l0;
                    uint64_t t30 = gm_l2;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    if (o32) gm_trap("integer overflow in `+`", gm_srcp8o48ld4n4jq, 290, 55);
                    uint64_t t33 = r31;
                    uint8_t t34 = gm_f_nth_is_name_oqhoobd(t29, t33);
                    gm_l14 = t34;
                } else {
                    gm_l14 = ((uint8_t)0);
                }
                uint8_t t35 = gm_l14;
                uint8_t t36 = (uint8_t)!t35;
                if (t36) {
                    goto gm_lb1;
                }
                uint64_t t37 = gm_l2;
                uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                if (o39) gm_trap("integer overflow in `+`", gm_srcp8o48ld4n4jq, 291, 9);
                uint64_t t40 = r38;
                gm_l2 = t40;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t41 = gm_l3;
    uint8_t t42 = (t41 > ((uint64_t)0ULL));
    if (t42) {
        void * t43 = gm_l0;
        uint64_t t44 = gm_l2;
        gm_t3fcbuehh3bs2 t45;
        gm_f_nth_c0r2nvs((void *)&t45, t43, t44);
        gm_t9qkh0rsooc8d t46;
        GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)56ULL));
        gm_t9qkh0rsooc8d t47;
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)54ULL));
        gm_t9qkh0rsooc8d t48;
        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)57ULL));
        gm_t9qkh0rsooc8d t49;
        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)62ULL));
        gm_t9qkh0rsooc8d t50;
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)22ULL));
        gm_t3fcbuehh3bs2 t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t51 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t51 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t51 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t51 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t51 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t51 + 8), t46);
        gm_t3fcbuehh3bs2 t52;
        GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t52 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t52 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t52 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t52 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t52 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t52 + 8), t47);
        gm_t3fcbuehh3bs2 t53;
        GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t53 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t53 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t53 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t53 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t53 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t53 + 8), t48);
        gm_t3fcbuehh3bs2 t54;
        GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t54 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t54 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t54 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t54 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t54 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t54 + 8), t49);
        gm_t3fcbuehh3bs2 t55;
        GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t55 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t55 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t55 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t55 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t55 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t55 + 8), t50);
        gm_tak1k6bk5qcen t56;
        GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t56 + 0), t51);
        GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t56 + 32), t52);
        GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t56 + 64), t53);
        GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t56 + 96), t54);
        GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t56 + 128), t55);
        uint8_t t57 = 0;
        if (gm_eqt7ftn1841u(((uint8_t *)&t56) + 0, ((uint8_t *)&t45))) t57 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t56) + 32, ((uint8_t *)&t45))) t57 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t56) + 64, ((uint8_t *)&t45))) t57 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t56) + 96, ((uint8_t *)&t45))) t57 = 1;
        if (gm_eqt7ftn1841u(((uint8_t *)&t56) + 128, ((uint8_t *)&t45))) t57 = 1;
        uint8_t t58 = (uint8_t)(t57);
        gm_l15 = t58;
    } else {
        gm_l15 = ((uint8_t)0);
    }
    uint8_t t59 = gm_l15;
    gm_l4 = t59;
    uint8_t t60 = gm_l4;
    uint8_t t61 = (uint8_t)!t60;
    if (t61) {
        void * t62 = gm_l0;
        gm_f_parse_type_2m93dtq((void *)&gm_l16, (void *)NULL, cx, NULL, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (t63) {
        } else {
            gm_t8u9q1tmoidhk t64;
            memset(&t64, 0, sizeof t64);
            ((uint8_t *)&t64)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t64 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t64), 64);
            goto gm_fdone;
        }
        void * t65 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&gm_l16) + 8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t65;
        gm_b16_8 t66 = gm_f_empty_slice_65q32rn();
        void * t67 = gm_l5;
        void * t68 = gm_l0;
        uint32_t t69 = gm_l1;
        gm_tl874t2atnlh3 t70 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t68, t69));
        gm_tf2orqcrphmek t71;
        GM_ST(uint16_t, (uint8_t *)&t71 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t71 + 20, 0);
        GM_ST(uint32_t, (uint8_t *)&t71 + 52, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t66);
        GM_ST(uint8_t, ((uint8_t *)&t71 + 16), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t71 + 17), ((uint8_t)0));
        GM_ST(void *, ((uint8_t *)&t71 + 24), t67);
        GM_ST(void *, ((uint8_t *)&t71 + 32), ((void *)0));
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t71 + 40), t70);
        gm_t8u9q1tmoidhk t72;
        GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
        ((uint8_t *)&t72)[0] = 0;
        GM_ST(gm_tf2orqcrphmek, ((uint8_t *)&t72 + 8), t71);
        memmove(gm_out, ((uint8_t *)&t72), 64);
        goto gm_fdone;
    }
    gm_trqfvmjtt4sij t73;
    gm_f_list_mho6jfm((void *)&t73, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t73;
    gm_l8 = ((uint64_t)0ULL);
    uint64_t t74 = gm_l3;
    gm_l7 = t74;
    {
        for (;;) {
            uint64_t t75 = gm_l8;
            uint64_t t76 = gm_l7;
            uint8_t t77 = (t75 < t76);
            if (!t77) {
                goto gm_lb4;
            }
            {
                {
                    {
                        uint64_t t78 = gm_l8;
                        gm_l9 = t78;
                        uint64_t t79 = gm_l9;
                        uint8_t t80 = (t79 > ((uint64_t)0ULL));
                        if (t80) {
                            void * t81 = gm_l0;
                            gm_tm4pc0emhrv1g t82;
                            gm_f_bump_q2i40re((void *)&t82, t81);
                        }
                        void * t83 = (void *)((uint8_t *)&F.l6);
                        void * t84 = gm_l0;
                        gm_b16_8 t85;
                        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sri0h4dm5d3ul));
                        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(16));
                        gm_f_parse_name_figjvbe((void *)&gm_l17, (void *)NULL, cx, NULL, t84, t85);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                        if (t86) {
                        } else {
                            gm_t8u9q1tmoidhk t87;
                            memset(&t87, 0, sizeof t87);
                            ((uint8_t *)&t87)[0] = 1;
                            GM_ST(gm_b0_1, ((uint8_t *)&t87 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t87), 64);
                            goto gm_fdone;
                        }
                        gm_tftb1odc739t5 t88 = gm_f_push_s5rtia7(t83, &(*(gm_t08olq97rscn3 *)(((uint8_t *)&gm_l17) + 8)));
                        gm_l18 = t88;
                        uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                        if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcp8o48ld4n4jq, 301, 9);
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 299, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t90 = gm_l8;
            uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
            uint64_t t93 = r91;
            gm_l8 = t93;
        }
    }
gm_lb4:;
    gm_l10 = ((uint8_t)0);
    gm_l11 = ((uint8_t)0);
    gm_l12 = ((void *)0);
    void * t94 = gm_l0;
    gm_t9qkh0rsooc8d t95;
    GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)56ULL));
    uint8_t t96 = gm_f_at_54906rm(t94, t95);
    if (t96) {
        gm_l19 = ((uint8_t)1);
    } else {
        void * t97 = gm_l0;
        gm_t9qkh0rsooc8d t98;
        GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)54ULL));
        uint8_t t99 = gm_f_at_54906rm(t97, t98);
        gm_l19 = t99;
    }
    uint8_t t100 = gm_l19;
    if (t100) {
        void * t101 = gm_l0;
        gm_t9qkh0rsooc8d t102;
        GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)54ULL));
        uint8_t t103 = gm_f_at_54906rm(t101, t102);
        gm_l10 = t103;
        void * t104 = gm_l0;
        gm_tm4pc0emhrv1g t105;
        gm_f_bump_q2i40re((void *)&t105, t104);
        {
            {
                uint8_t t106 = gm_l10;
                uint8_t t107 = (uint8_t)!t106;
                if (!t107) {
                    goto gm_lb10;
                }
                void * t108 = gm_l0;
                gm_t9qkh0rsooc8d t109;
                GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)50ULL));
                uint8_t t110 = gm_f_eat_qmock9g(t108, t109);
                if (!t110) {
                    goto gm_lb10;
                }
                gm_l11 = ((uint8_t)1);
                goto gm_lb9;
            }
gm_lb10:;
        }
gm_lb9:;
        void * t111 = gm_l0;
        gm_f_parse_type_2m93dtq((void *)&gm_l20, (void *)NULL, cx, NULL, t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
        if (t112) {
        } else {
            gm_t8u9q1tmoidhk t113;
            memset(&t113, 0, sizeof t113);
            ((uint8_t *)&t113)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t113 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t113), 64);
            goto gm_fdone;
        }
        void * t114 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&gm_l20) + 8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t114;
    }
    gm_l13 = ((void *)0);
    void * t115 = gm_l0;
    gm_t9qkh0rsooc8d t116;
    GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)22ULL));
    uint8_t t117 = gm_f_eat_qmock9g(t115, t116);
    if (t117) {
        void * t118 = gm_l0;
        gm_f_parse_expr_bkb2n6e((void *)&gm_l21, (void *)NULL, cx, NULL, t118);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
        if (t119) {
        } else {
            gm_t8u9q1tmoidhk t120;
            memset(&t120, 0, sizeof t120);
            ((uint8_t *)&t120)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t120 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t120), 64);
            goto gm_fdone;
        }
        void * t121 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&gm_l21) + 8)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t121;
    }
    gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    uint8_t t123 = gm_l10;
    uint8_t t124 = gm_l11;
    void * t125 = gm_l12;
    void * t126 = gm_l13;
    void * t127 = gm_l0;
    uint32_t t128 = gm_l1;
    gm_tl874t2atnlh3 t129 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t127, t128));
    gm_tf2orqcrphmek t130;
    GM_ST(uint16_t, (uint8_t *)&t130 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t130 + 20, 0);
    GM_ST(uint32_t, (uint8_t *)&t130 + 52, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t130 + 0), t122);
    GM_ST(uint8_t, ((uint8_t *)&t130 + 16), t123);
    GM_ST(uint8_t, ((uint8_t *)&t130 + 17), t124);
    GM_ST(void *, ((uint8_t *)&t130 + 24), t125);
    GM_ST(void *, ((uint8_t *)&t130 + 32), t126);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t130 + 40), t129);
    gm_t8u9q1tmoidhk t131;
    GM_ST(uint8_t, (uint8_t *)&t131 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t131 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t131 + 4, 0);
    ((uint8_t *)&t131)[0] = 0;
    GM_ST(gm_tf2orqcrphmek, ((uint8_t *)&t131 + 8), t130);
    memmove(gm_out, ((uint8_t *)&t131), 64);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_parse_params_3g43t5f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_params_3g43t5f F;
    void * gm_l0; /* p */
    gm_toh0ootodhjue gm_l1; /* old */
    uint8_t gm_l3; /* variadic */
    GM_STACK_CHECK(&F, 423u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t8u9q1tmoidhk gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_t0u0j6voh3loe gm_l6;
    {
        void * t7 = gm_l0;
        gm_toh0ootodhjue t8;
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t8 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t8 + 2), ((uint8_t)0));
        gm_toh0ootodhjue t9 = GM_CONV(gm_toh0ootodhjue, gm_f_set_restr_p85qqmv(t7, GM_CONV(gm_cs3cuou245tr5f, t8)));
        gm_l1 = t9;
        gm_trqfvmjtt4sij t10;
        gm_f_list_3t77g3a((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_ld6_1;
        }
        F.l2 = t10;
        gm_l3 = ((uint8_t)0);
        {
            for (;;) {
                void * t11 = gm_l0;
                gm_t9qkh0rsooc8d t12;
                GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)62ULL));
                uint8_t t13 = gm_f_at_54906rm(t11, t12);
                uint8_t t14 = (uint8_t)!t13;
                if (!t14) {
                    goto gm_lb2;
                }
                {
                    void * t15 = gm_l0;
                    gm_t9qkh0rsooc8d t16;
                    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)52ULL));
                    uint8_t t17 = gm_f_eat_qmock9g(t15, t16);
                    if (t17) {
                        gm_l3 = ((uint8_t)1);
                        void * t18 = gm_l0;
                        gm_t9qkh0rsooc8d t19;
                        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)57ULL));
                        uint8_t t20 = gm_f_eat_qmock9g(t18, t19);
                        goto gm_lb2;
                    }
                    void * t21 = (void *)((uint8_t *)&F.l2);
                    void * t22 = gm_l0;
                    gm_f_parse_param_group_ac8kpvk((void *)&gm_l4, (void *)NULL, cx, NULL, t22);
                    if (gm_unw.kind) {
                        goto gm_ld6_1;
                    }
                    uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                    if (t23) {
                    } else {
                        gm_thk954m1ol1ig t24;
                        GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t24 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t24 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t24 + 24, 0);
                        ((uint8_t *)&t24)[0] = 1;
                        GM_ST(gm_b0_1, ((uint8_t *)&t24 + 8), GM_ZERO(gm_b0_1));
                        memmove(&F.ret, ((uint8_t *)&t24), 32);
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                        goto gm_ld6_1;
                    }
                    gm_tftb1odc739t5 t25 = gm_f_push_vbfukt4(t21, &(*(gm_tf2orqcrphmek *)(((uint8_t *)&gm_l4) + 8)));
                    gm_l5 = t25;
                    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                    if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcp8o48ld4n4jq, 274, 9);
                    void * t27 = gm_l0;
                    gm_t9qkh0rsooc8d t28;
                    GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)57ULL));
                    uint8_t t29 = gm_f_eat_qmock9g(t27, t28);
                    uint8_t t30 = (uint8_t)!t29;
                    if (t30) {
                        goto gm_lb2;
                    }
                }
gm_lb4:;
            }
        }
gm_lb2:;
        void * t31 = gm_l0;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_skcto6psrjh96));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(27));
        gm_t9qkh0rsooc8d t33;
        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)62ULL));
        gm_t0u0j6voh3loe t34 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t31, t33, t32);
        if (gm_unw.kind) {
            goto gm_ld6_1;
        }
        gm_l6 = t34;
        uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (t35) {
        } else {
            gm_thk954m1ol1ig t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t36 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t36 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t36 + 24, 0);
            ((uint8_t *)&t36)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t36 + 8), GM_ZERO(gm_b0_1));
            memmove(&F.ret, ((uint8_t *)&t36), 32);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld6_1;
        }
        gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l6) + 4));
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        uint8_t t39 = gm_l3;
        gm_th2lqbo77pvsc t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t38);
        GM_ST(uint8_t, ((uint8_t *)&t40 + 16), t39);
        gm_thk954m1ol1ig t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
        ((uint8_t *)&t41)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t41 + 8), t40);
        memmove(&F.ret, ((uint8_t *)&t41), 32);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
        goto gm_ld6_1;
    }
    {
        void * t42 = gm_l0;
        gm_toh0ootodhjue t43 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t42) + 128), t43);
    }
    goto gm_ls44;
gm_ld6_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t45 = gm_l0;
        gm_toh0ootodhjue t46 = gm_l1;
        GM_ST(gm_toh0ootodhjue, (((uint8_t *)t45) + 128), t46);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls44:;
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
__attribute__((aligned(4))) void gm_f_parse_prefix_type_1rn6mn8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_prefix_type_1rn6mn8 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 364u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t3fcbuehh3bs2 gm_l2;
    void * t19 = gm_l0;
    gm_f_kind_k239kpg((void *)&gm_l2, t19);
    {
        {
            {
                {
                    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)8ULL));
                    if (!t21) {
                        goto gm_lb4;
                    }
                    uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)45ULL));
                    if (!t23) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t25 = (uint8_t)(t24 == ((uint8_t)8ULL));
                    if (!t25) {
                        goto gm_lb5;
                    }
                    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)47ULL));
                    if (!t27) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)8ULL));
                    if (!t29) {
                        goto gm_lb6;
                    }
                    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)63ULL));
                    if (!t31) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t32 = gm_l0;
            gm_t9qkh0rsooc8d t33;
            GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)1ULL));
            gm_f_parse_type_prefix_kfrfcdd((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)2ULL));
            if (!t35) {
                goto gm_lb7;
            }
            uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)11ULL));
            if (!t37) {
                goto gm_lb7;
            }
            void * t38 = gm_l0;
            gm_f_parse_fn_type_only_0ifghqh((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb7:;
        {
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)8ULL));
            if (!t40) {
                goto gm_lb8;
            }
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)61ULL));
            if (!t42) {
                goto gm_lb8;
            }
            void * t43 = gm_l0;
            gm_t9qkh0rsooc8d t44;
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)1ULL));
            gm_f_parse_paren_sq1guoi((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb8:;
        {
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)2ULL));
            if (!t46) {
                goto gm_lb9;
            }
            uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)24ULL));
            if (!t48) {
                goto gm_lb9;
            }
            void * t49 = gm_l0;
            gm_f_parse_struct_lit_86pp62c((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb9:;
        {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)2ULL));
            if (!t51) {
                goto gm_lb10;
            }
            uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)27ULL));
            if (!t53) {
                goto gm_lb10;
            }
            void * t54 = gm_l0;
            gm_f_parse_union_lit_39ap2o5((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb10:;
        {
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)2ULL));
            if (!t56) {
                goto gm_lb11;
            }
            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)&gm_l2) + 8) + 0));
            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)6ULL));
            if (!t58) {
                goto gm_lb11;
            }
            void * t59 = gm_l0;
            gm_f_parse_effect_lit_cqei081((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb11:;
        {
            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t61 = (uint8_t)(t60 == ((uint8_t)7ULL));
            if (!t61) {
                goto gm_lb12;
            }
            void * t62 = gm_l0;
            gm_t9qkh0rsooc8d t63;
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)1ULL));
            gm_f_parse_attributed_00u9rjp((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb12:;
        {
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)6ULL));
            if (!t65) {
                goto gm_lb13;
            }
            void * t66 = gm_l0;
            gm_f_parse_intrinsic_4ou54h3((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb13:;
        {
            uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t68 = (uint8_t)(t67 == ((uint8_t)0ULL));
            if (!t68) {
                goto gm_lb14;
            }
            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
            gm_l1 = t69;
            {
                {
                    gm_b16_8 t70 = gm_l1;
                    gm_b16_8 t71;
                    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_st2dnokm1h0jd));
                    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(4));
                    uint8_t t72 = (gm_eq0iqiubfm7e(((uint8_t *)&t70), ((uint8_t *)&t71)));
                    if (!t72) {
                        goto gm_lb16;
                    }
                    void * t73 = gm_l0;
                    gm_t9qkh0rsooc8d t74;
                    GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)61ULL));
                    uint8_t t75 = gm_f_nth_is_3fgqmcb(t73, ((uint64_t)1ULL), t74);
                    if (!t75) {
                        goto gm_lb16;
                    }
                    void * t76 = gm_l0;
                    gm_f_parse_cont_type_ljmco4q((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t76);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb16:;
            }
gm_lb15:;
            void * t77 = gm_l0;
            gm_f_parse_type_name_d0ioo05((void *)&(*(gm_to1cssm8dfiv5 *)gm_out), (void *)NULL, cx, NULL, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb14:;
        {
            void * t78 = gm_l0;
            gm_b16_8 t79;
            GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s9rm2mbmo3nku));
            GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(6));
            gm_b0_1 t80 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_to1cssm8dfiv5 t81;
            memset(&t81, 0, sizeof t81);
            ((uint8_t *)&t81)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t81 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t81), 96);
            goto gm_fdone;
        }
gm_lb17:;
        gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 29, 5);
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
inline __attribute__((aligned(4))) void gm_f_parse_type_2m93dtq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_type_2m93dtq F;
    void * gm_l0; /* p */
    GM_STACK_CHECK(&F, 1052u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d gm_l3;
    gm_to1cssm8dfiv5 gm_l4;
    gm_to1cssm8dfiv5 gm_l5;
    {
        void * t4 = gm_l0;
        gm_t9qkh0rsooc8d t5 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
        uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (t6) {
        } else {
            gm_to1cssm8dfiv5 t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t7), 96);
            goto gm_fdone;
        }
        void * t8 = gm_l0;
        gm_f_parse_prefix_type_1rn6mn8((void *)&gm_l4, (void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_ld3_1;
        }
        uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (t9) {
        } else {
            gm_to1cssm8dfiv5 t10;
            memset(&t10, 0, sizeof t10);
            ((uint8_t *)&t10)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t10 + 8), GM_ZERO(gm_b0_1));
            memmove(&F.ret, ((uint8_t *)&t10), 96);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld3_1;
        }
        memmove(((uint8_t *)&F.l1), (((uint8_t *)&gm_l4) + 8), 88);
        void * t11 = gm_l0;
        gm_t9qkh0rsooc8d t12;
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)46ULL));
        uint8_t t13 = gm_f_eat_qmock9g(t11, t12);
        uint8_t t14 = (uint8_t)!t13;
        if (t14) {
            gm_tc3oht4ksth9d t15 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l1));
            gm_to1cssm8dfiv5 t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
            ((uint8_t *)&t16)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t16 + 8), t15);
            memmove(&F.ret, ((uint8_t *)&t16), 96);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld3_1;
        }
        void * t17 = gm_l0;
        gm_f_parse_type_2m93dtq((void *)&gm_l5, (void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_ld3_1;
        }
        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
        if (t18) {
        } else {
            gm_to1cssm8dfiv5 t19;
            memset(&t19, 0, sizeof t19);
            ((uint8_t *)&t19)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t19 + 8), GM_ZERO(gm_b0_1));
            memmove(&F.ret, ((uint8_t *)&t19), 96);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld3_1;
        }
        memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l5) + 8), 88);
        void * t20 = gm_l0;
        void * t21 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_ld3_1;
        }
        void * t22 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_ld3_1;
        }
        gm_t3mf58gv9kf3n t23;
        GM_ST(void *, ((uint8_t *)&t23 + 0), t21);
        GM_ST(void *, ((uint8_t *)&t23 + 8), t22);
        gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 72));
        gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
        gm_t2gtbh39ptekd t26;
        memset(&t26, 0, sizeof t26);
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)29ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t26 + 8), t23);
        gm_tl874t2atnlh3 t27 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t24), GM_CONV(gm_cseahvg8v7p0vs, t25)));
        gm_tc3oht4ksth9d t28;
        gm_f_mk_expr_53qj7nd((void *)&t28, t20, &t26, GM_CONV(gm_cseahvg8v7p0vs, t27));
        gm_to1cssm8dfiv5 t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
        ((uint8_t *)&t29)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t29 + 8), t28);
        memmove(&F.ret, ((uint8_t *)&t29), 96);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
        goto gm_ld3_1;
    }
    {
        void * t30 = gm_l0;
        gm_f_leave_rb20u32(t30);
    }
    goto gm_ls31;
gm_ld3_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t32 = gm_l0;
        gm_f_leave_rb20u32(t32);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls31:;
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
inline __attribute__((aligned(4))) void gm_f_parse_type_name_d0ioo05(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_type_name_d0ioo05 F;
    void * gm_l0; /* p */
    gm_t08olq97rscn3 gm_l1; /* id */
    uint32_t gm_l3; /* start */
    gm_t08olq97rscn3 gm_l4; /* name */
    gm_th2lqbo77pvsc gm_l5; /* args */
    GM_STACK_CHECK(&F, 1312u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4jn8fl3lmk5k gm_l6;
    uint8_t gm_l7;
    gm_t3fcbuehh3bs2 gm_l8;
    gm_t4jn8fl3lmk5k gm_l9;
    gm_thk954m1ol1ig gm_l10;
    void * t10 = gm_l0;
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s9rm2mbmo3nku));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(6));
    gm_f_parse_ident_n2c6b75((void *)&gm_l6, (void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t12) {
    } else {
        gm_to1cssm8dfiv5 t13;
        memset(&t13, 0, sizeof t13);
        ((uint8_t *)&t13)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t13 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t13), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&gm_l1), (((uint8_t *)&gm_l6) + 8), 32);
    void * t14 = gm_l0;
    gm_t08olq97rscn3 t15 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l1));
    gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l1) + 16));
    gm_t2gtbh39ptekd t17;
    GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t17 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t17 + 48, 0);
    GM_ST(uint64_t, (uint8_t *)&t17 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)6ULL));
    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t17 + 8), t15);
    gm_f_mk_expr_53qj7nd((void *)&F.l2, t14, &t17, GM_CONV(gm_cseahvg8v7p0vs, t16));
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                uint32_t t18 = GM_LD(uint32_t, ((((uint8_t *)&F.l2) + 72) + 4));
                gm_l3 = t18;
                {
                    {
                        void * t19 = gm_l0;
                        gm_t9qkh0rsooc8d t20;
                        GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)58ULL));
                        uint8_t t21 = gm_f_at_54906rm(t19, t20);
                        if (!t21) {
                            goto gm_lb5;
                        }
                        void * t22 = gm_l0;
                        gm_f_nth_c0r2nvs((void *)&gm_l8, t22, ((uint64_t)1ULL));
                        {
                            {
                                uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                                uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
                                if (!t24) {
                                    goto gm_lb7;
                                }
                                gm_l7 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l7 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 52, 27);
                        }
gm_lb6:;
                        uint8_t t25 = gm_l7;
                        if (!t25) {
                            goto gm_lb5;
                        }
                        void * t26 = gm_l0;
                        gm_tm4pc0emhrv1g t27;
                        gm_f_bump_q2i40re((void *)&t27, t26);
                        void * t28 = gm_l0;
                        gm_b16_8 t29;
                        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s1pvh8pfrs3ir));
                        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(6));
                        gm_f_parse_ident_n2c6b75((void *)&gm_l9, (void *)NULL, cx, NULL, t28, t29);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (t30) {
                        } else {
                            gm_to1cssm8dfiv5 t31;
                            memset(&t31, 0, sizeof t31);
                            ((uint8_t *)&t31)[0] = 1;
                            GM_ST(gm_b0_1, ((uint8_t *)&t31 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t31), 96);
                            goto gm_fdone;
                        }
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)&gm_l9) + 8), 32);
                        void * t32 = gm_l0;
                        void * t33 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t08olq97rscn3 t34 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l4));
                        gm_tioie2ju3q4ul t35;
                        GM_ST(void *, ((uint8_t *)&t35 + 0), t33);
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t35 + 8), t34);
                        void * t36 = gm_l0;
                        uint32_t t37 = gm_l3;
                        gm_t2gtbh39ptekd t38;
                        GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t38 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&t38 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)22ULL));
                        GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t38 + 8), t35);
                        gm_tl874t2atnlh3 t39 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t36, t37));
                        gm_f_mk_expr_53qj7nd((void *)&F.l2, t32, &t38, GM_CONV(gm_cseahvg8v7p0vs, t39));
                        goto gm_lb4;
                    }
gm_lb5:;
                    void * t40 = gm_l0;
                    gm_t9qkh0rsooc8d t41;
                    GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)61ULL));
                    uint8_t t42 = gm_f_at_54906rm(t40, t41);
                    if (t42) {
                        void * t43 = gm_l0;
                        gm_f_parse_call_args_32e205h((void *)&gm_l10, (void *)NULL, cx, NULL, t43);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (t44) {
                        } else {
                            gm_to1cssm8dfiv5 t45;
                            memset(&t45, 0, sizeof t45);
                            ((uint8_t *)&t45)[0] = 1;
                            GM_ST(gm_b0_1, ((uint8_t *)&t45 + 8), GM_ZERO(gm_b0_1));
                            memmove(gm_out, ((uint8_t *)&t45), 96);
                            goto gm_fdone;
                        }
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)&gm_l10) + 8), 24);
                        void * t46 = gm_l0;
                        void * t47 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l2)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 16));
                        gm_tlljg61a2ctgq t50;
                        GM_ST(uint8_t, (uint8_t *)&t50 + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&t50 + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&t50 + 28, 0);
                        GM_ST(void *, ((uint8_t *)&t50 + 0), t47);
                        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 8), t48);
                        GM_ST(uint8_t, ((uint8_t *)&t50 + 24), t49);
                        void * t51 = gm_l0;
                        uint32_t t52 = gm_l3;
                        gm_t2gtbh39ptekd t53;
                        GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t53 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t53 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&t53 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)19ULL));
                        GM_ST(gm_tlljg61a2ctgq, ((uint8_t *)&t53 + 8), t50);
                        gm_tl874t2atnlh3 t54 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t51, t52));
                        gm_f_mk_expr_53qj7nd((void *)&F.l2, t46, &t53, GM_CONV(gm_cseahvg8v7p0vs, t54));
                    } else {
                        goto gm_lb1;
                    }
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tc3oht4ksth9d t55 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l2));
    gm_to1cssm8dfiv5 t56;
    GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
    ((uint8_t *)&t56)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t56 + 8), t55);
    memmove(gm_out, ((uint8_t *)&t56), 96);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_parse_type_operand_7guq29a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_parse_type_operand_7guq29a F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* mode */
    GM_STACK_CHECK(&F, 344u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t9qkh0rsooc8d gm_l2;
    {
        void * t4 = gm_l0;
        gm_t9qkh0rsooc8d t5 = gm_f_enter_5bf793b((void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t5;
        uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (t6) {
        } else {
            gm_to1cssm8dfiv5 t7;
            memset(&t7, 0, sizeof t7);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t7 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t7), 96);
            goto gm_fdone;
        }
        gm_t9qkh0rsooc8d t8 = gm_l1;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        uint8_t t10 = (uint8_t)((t9 == ((uint8_t)1ULL)));
        gm_t9qkh0rsooc8d t11;
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)1ULL));
        if (t10) {
            void * t12 = gm_l0;
            gm_to1cssm8dfiv5 t13;
            gm_f_parse_prefix_type_1rn6mn8((void *)&t13, (void *)NULL, cx, NULL, t12);
            if (gm_unw.kind) {
                goto gm_ld3_1;
            }
            memmove(&F.ret, ((uint8_t *)&t13), 96);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld3_1;
        }
        void * t14 = gm_l0;
        gm_to1cssm8dfiv5 t15;
        gm_f_parse_postfix_ao4kstr((void *)&t15, (void *)NULL, cx, NULL, t14, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_ld3_1;
        }
        memmove(&F.ret, ((uint8_t *)&t15), 96);
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
        goto gm_ld3_1;
    }
    {
        void * t16 = gm_l0;
        gm_f_leave_rb20u32(t16);
    }
    goto gm_ls17;
gm_ld3_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t18 = gm_l0;
        gm_f_leave_rb20u32(t18);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls17:;
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
__attribute__((aligned(4))) void gm_f_parse_type_prefix_kfrfcdd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_parse_type_prefix_kfrfcdd F;
    void * gm_l0; /* p */
    gm_t9qkh0rsooc8d gm_l1; /* mode */
    uint32_t gm_l2; /* start */
    uint8_t gm_l4; /* is_const */
    uint8_t gm_l6; /* is_const */
    uint8_t gm_l8; /* is_const */
    void * gm_l11; /* length */
    GM_STACK_CHECK(&F, 4319u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_to1cssm8dfiv5 gm_l14;
    gm_to1cssm8dfiv5 gm_l15;
    gm_to1cssm8dfiv5 gm_l16;
    gm_to1cssm8dfiv5 gm_l17;
    gm_to1cssm8dfiv5 gm_l18;
    gm_to1cssm8dfiv5 gm_l19;
    gm_t0u0j6voh3loe gm_l20;
    gm_to1cssm8dfiv5 gm_l21;
    void * t6 = gm_l0;
    uint32_t t7 = gm_f_lo_fp82gt4(t6);
    gm_l2 = t7;
    void * t8 = gm_l0;
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)45ULL));
    uint8_t t10 = gm_f_eat_qmock9g(t8, t9);
    if (t10) {
        void * t11 = gm_l0;
        gm_t9qkh0rsooc8d t12 = gm_l1;
        gm_f_parse_type_operand_7guq29a((void *)&gm_l14, (void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (t13) {
        } else {
            gm_to1cssm8dfiv5 t14;
            memset(&t14, 0, sizeof t14);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t14 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t14), 96);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l14) + 8), 88);
        void * t15 = gm_l0;
        void * t16 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t17 = gm_l0;
        uint32_t t18 = gm_l2;
        gm_t2gtbh39ptekd t19;
        memset(&t19, 0, sizeof t19);
        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)24ULL));
        GM_ST(void *, ((uint8_t *)&t19 + 8), t16);
        gm_tl874t2atnlh3 t20 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t17, t18));
        gm_tc3oht4ksth9d t21;
        gm_f_mk_expr_53qj7nd((void *)&t21, t15, &t19, GM_CONV(gm_cseahvg8v7p0vs, t20));
        gm_to1cssm8dfiv5 t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
        ((uint8_t *)&t22)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t22 + 8), t21);
        memmove(gm_out, ((uint8_t *)&t22), 96);
        goto gm_fdone;
    }
    void * t23 = gm_l0;
    gm_t9qkh0rsooc8d t24;
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)47ULL));
    uint8_t t25 = gm_f_eat_qmock9g(t23, t24);
    if (t25) {
        void * t26 = gm_l0;
        uint8_t t27 = gm_f_eat_const_0ntbpcs(t26);
        gm_l4 = t27;
        void * t28 = gm_l0;
        gm_t9qkh0rsooc8d t29 = gm_l1;
        gm_f_parse_type_operand_7guq29a((void *)&gm_l15, (void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
        if (t30) {
        } else {
            gm_to1cssm8dfiv5 t31;
            memset(&t31, 0, sizeof t31);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t31 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t31), 96);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l15) + 8), 88);
        void * t32 = gm_l0;
        uint8_t t33 = gm_l4;
        void * t34 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgtub0v94d5u5 t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), t33);
        GM_ST(void *, ((uint8_t *)&t35 + 8), t34);
        void * t36 = gm_l0;
        uint32_t t37 = gm_l2;
        gm_t2gtbh39ptekd t38;
        memset(&t38, 0, sizeof t38);
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)25ULL));
        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t38 + 8), t35);
        gm_tl874t2atnlh3 t39 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t36, t37));
        gm_tc3oht4ksth9d t40;
        gm_f_mk_expr_53qj7nd((void *)&t40, t32, &t38, GM_CONV(gm_cseahvg8v7p0vs, t39));
        gm_to1cssm8dfiv5 t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
        ((uint8_t *)&t41)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t41 + 8), t40);
        memmove(gm_out, ((uint8_t *)&t41), 96);
        goto gm_fdone;
    }
    void * t42 = gm_l0;
    gm_t9qkh0rsooc8d t43;
    GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)63ULL));
    uint8_t t44 = gm_f_eat_qmock9g(t42, t43);
    uint8_t t45 = (uint8_t)!t44;
    if (t45) {
        void * t46 = gm_l0;
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_s9rm2mbmo3nku));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(6));
        gm_b0_1 t48 = gm_f_expected_5famfb9((void *)NULL, cx, NULL, t46, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_to1cssm8dfiv5 t49;
        memset(&t49, 0, sizeof t49);
        ((uint8_t *)&t49)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t49 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t49), 96);
        goto gm_fdone;
    }
    void * t50 = gm_l0;
    gm_t9qkh0rsooc8d t51;
    GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)64ULL));
    uint8_t t52 = gm_f_eat_qmock9g(t50, t51);
    if (t52) {
        void * t53 = gm_l0;
        uint8_t t54 = gm_f_eat_const_0ntbpcs(t53);
        gm_l6 = t54;
        void * t55 = gm_l0;
        gm_t9qkh0rsooc8d t56 = gm_l1;
        gm_f_parse_type_operand_7guq29a((void *)&gm_l16, (void *)NULL, cx, NULL, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (t57) {
        } else {
            gm_to1cssm8dfiv5 t58;
            memset(&t58, 0, sizeof t58);
            ((uint8_t *)&t58)[0] = 1;
            GM_ST(gm_b0_1, ((uint8_t *)&t58 + 8), GM_ZERO(gm_b0_1));
            memmove(gm_out, ((uint8_t *)&t58), 96);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l7), (((uint8_t *)&gm_l16) + 8), 88);
        void * t59 = gm_l0;
        uint8_t t60 = gm_l6;
        void * t61 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tgtub0v94d5u5 t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t62 + 0), t60);
        GM_ST(void *, ((uint8_t *)&t62 + 8), t61);
        void * t63 = gm_l0;
        uint32_t t64 = gm_l2;
        gm_t2gtbh39ptekd t65;
        memset(&t65, 0, sizeof t65);
        GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)26ULL));
        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t65 + 8), t62);
        gm_tl874t2atnlh3 t66 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t63, t64));
        gm_tc3oht4ksth9d t67;
        gm_f_mk_expr_53qj7nd((void *)&t67, t59, &t65, GM_CONV(gm_cseahvg8v7p0vs, t66));
        gm_to1cssm8dfiv5 t68;
        GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
        ((uint8_t *)&t68)[0] = 0;
        GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t68 + 8), t67);
        memmove(gm_out, ((uint8_t *)&t68), 96);
        goto gm_fdone;
    }
    {
        {
            void * t69 = gm_l0;
            gm_t9qkh0rsooc8d t70;
            GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)47ULL));
            uint8_t t71 = gm_f_at_54906rm(t69, t70);
            if (!t71) {
                goto gm_lb2;
            }
            void * t72 = gm_l0;
            gm_t9qkh0rsooc8d t73;
            GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)64ULL));
            uint8_t t74 = gm_f_nth_is_3fgqmcb(t72, ((uint64_t)1ULL), t73);
            if (!t74) {
                goto gm_lb2;
            }
            void * t75 = gm_l0;
            gm_tm4pc0emhrv1g t76;
            gm_f_bump_q2i40re((void *)&t76, t75);
            void * t77 = gm_l0;
            gm_tm4pc0emhrv1g t78;
            gm_f_bump_q2i40re((void *)&t78, t77);
            void * t79 = gm_l0;
            uint8_t t80 = gm_f_eat_const_0ntbpcs(t79);
            gm_l8 = t80;
            void * t81 = gm_l0;
            gm_t9qkh0rsooc8d t82 = gm_l1;
            gm_f_parse_type_operand_7guq29a((void *)&gm_l17, (void *)NULL, cx, NULL, t81, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (t83) {
            } else {
                gm_to1cssm8dfiv5 t84;
                memset(&t84, 0, sizeof t84);
                ((uint8_t *)&t84)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t84 + 8), GM_ZERO(gm_b0_1));
                memmove(gm_out, ((uint8_t *)&t84), 96);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&F.l9), (((uint8_t *)&gm_l17) + 8), 88);
            void * t85 = gm_l0;
            uint8_t t86 = gm_l8;
            void * t87 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l9)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tgtub0v94d5u5 t88;
            GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t88 + 0), t86);
            GM_ST(void *, ((uint8_t *)&t88 + 8), t87);
            void * t89 = gm_l0;
            uint32_t t90 = gm_l2;
            gm_t2gtbh39ptekd t91;
            memset(&t91, 0, sizeof t91);
            GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)27ULL));
            GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t91 + 8), t88);
            gm_tl874t2atnlh3 t92 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t89, t90));
            gm_tc3oht4ksth9d t93;
            gm_f_mk_expr_53qj7nd((void *)&t93, t85, &t91, GM_CONV(gm_cseahvg8v7p0vs, t92));
            gm_to1cssm8dfiv5 t94;
            GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
            ((uint8_t *)&t94)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t94 + 8), t93);
            memmove(gm_out, ((uint8_t *)&t94), 96);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t95 = gm_l0;
            gm_t3fcbuehh3bs2 t96;
            gm_f_kind_k239kpg((void *)&t96, t95);
            uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&t96) + 0));
            uint8_t t98 = (uint8_t)((t97 == ((uint8_t)1ULL)));
            if (!t98) {
                goto gm_lb4;
            }
            void * t99 = gm_l0;
            gm_t3fcbuehh3bs2 t100;
            GM_ST(uint8_t, (uint8_t *)&t100 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t100 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t100 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t100 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t100 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t100 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)1ULL));
            gm_t9qkh0rsooc8d t101;
            GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)64ULL));
            uint8_t t102 = gm_f_nth_is_3fgqmcb(t99, ((uint64_t)1ULL), t101);
            if (!t102) {
                goto gm_lb4;
            }
            void * t103 = gm_l0;
            gm_tm4pc0emhrv1g t104;
            gm_f_bump_q2i40re((void *)&t104, t103);
            void * t105 = gm_l0;
            gm_tm4pc0emhrv1g t106;
            gm_f_bump_q2i40re((void *)&t106, t105);
            void * t107 = gm_l0;
            gm_t9qkh0rsooc8d t108 = gm_l1;
            gm_f_parse_type_operand_7guq29a((void *)&gm_l18, (void *)NULL, cx, NULL, t107, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (t109) {
            } else {
                gm_to1cssm8dfiv5 t110;
                memset(&t110, 0, sizeof t110);
                ((uint8_t *)&t110)[0] = 1;
                GM_ST(gm_b0_1, ((uint8_t *)&t110 + 8), GM_ZERO(gm_b0_1));
                memmove(gm_out, ((uint8_t *)&t110), 96);
                goto gm_fdone;
            }
            memmove(((uint8_t *)&F.l10), (((uint8_t *)&gm_l18) + 8), 88);
            void * t111 = gm_l0;
            void * t112 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l10)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t3mf58gv9kf3n t113;
            GM_ST(void *, ((uint8_t *)&t113 + 0), ((void *)0));
            GM_ST(void *, ((uint8_t *)&t113 + 8), t112);
            void * t114 = gm_l0;
            uint32_t t115 = gm_l2;
            gm_t2gtbh39ptekd t116;
            memset(&t116, 0, sizeof t116);
            GM_ST(uint8_t, ((uint8_t *)&t116 + 0), ((uint8_t)28ULL));
            GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t116 + 8), t113);
            gm_tl874t2atnlh3 t117 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t114, t115));
            gm_tc3oht4ksth9d t118;
            gm_f_mk_expr_53qj7nd((void *)&t118, t111, &t116, GM_CONV(gm_cseahvg8v7p0vs, t117));
            gm_to1cssm8dfiv5 t119;
            GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
            ((uint8_t *)&t119)[0] = 0;
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t119 + 8), t118);
            memmove(gm_out, ((uint8_t *)&t119), 96);
            goto gm_fdone;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t120 = gm_l0;
    gm_toh0ootodhjue t121;
    GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t121 + 1), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t121 + 2), ((uint8_t)0));
    gm_f_parse_expr_in_45seo3h((void *)&gm_l19, (void *)NULL, cx, NULL, t120, t121);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
    if (t122) {
    } else {
        gm_to1cssm8dfiv5 t123;
        memset(&t123, 0, sizeof t123);
        ((uint8_t *)&t123)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t123 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t123), 96);
        goto gm_fdone;
    }
    void * t124 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&gm_l19) + 8)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t124;
    void * t125 = gm_l0;
    gm_t9qkh0rsooc8d t126;
    GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)57ULL));
    uint8_t t127 = gm_f_at_54906rm(t125, t126);
    if (t127) {
        void * t128 = gm_l0;
        gm_tl874t2atnlh3 t129 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_bm4ll8d(t128));
        gm_b16_8 t130;
        GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sc71n5mgucblh));
        GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(49));
        gm_t4gut2c9fj1kt t131;
        gm_f_error_s6hn871((void *)&t131, (void *)NULL, cx, NULL, t129, t130);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l12 = t131;
        void * t132 = (void *)((uint8_t *)&F.l12);
        gm_b16_8 t133;
        GM_ST(void *, (uint8_t *)&t133, (void *)(gm_spferj7kepn49));
        GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(114));
        gm_f_add_note_46v502s(t132, t133);
        void * t134 = gm_l0;
        gm_b0_1 t135 = gm_f_fail_diag_u0q4kp3(t134, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l12)));
        gm_to1cssm8dfiv5 t136;
        memset(&t136, 0, sizeof t136);
        ((uint8_t *)&t136)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t136 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t136), 96);
        goto gm_fdone;
    }
    void * t137 = gm_l0;
    gm_b16_8 t138;
    GM_ST(void *, (uint8_t *)&t138, (void *)(gm_sh7ob55fi0i62));
    GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(25));
    gm_t9qkh0rsooc8d t139;
    GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)64ULL));
    gm_t0u0j6voh3loe t140 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t137, t139, t138);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l20 = t140;
    uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
    if (t141) {
    } else {
        gm_to1cssm8dfiv5 t142;
        memset(&t142, 0, sizeof t142);
        ((uint8_t *)&t142)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t142 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t142), 96);
        goto gm_fdone;
    }
    gm_tl874t2atnlh3 t143 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l20) + 4));
    void * t144 = gm_l0;
    gm_t9qkh0rsooc8d t145 = gm_l1;
    gm_f_parse_type_operand_7guq29a((void *)&gm_l21, (void *)NULL, cx, NULL, t144, t145);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t146 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
    if (t146) {
    } else {
        gm_to1cssm8dfiv5 t147;
        memset(&t147, 0, sizeof t147);
        ((uint8_t *)&t147)[0] = 1;
        GM_ST(gm_b0_1, ((uint8_t *)&t147 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t147), 96);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l13), (((uint8_t *)&gm_l21) + 8), 88);
    void * t148 = gm_l0;
    void * t149 = gm_l11;
    void * t150 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l13)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t3mf58gv9kf3n t151;
    GM_ST(void *, ((uint8_t *)&t151 + 0), t149);
    GM_ST(void *, ((uint8_t *)&t151 + 8), t150);
    void * t152 = gm_l0;
    uint32_t t153 = gm_l2;
    gm_t2gtbh39ptekd t154;
    memset(&t154, 0, sizeof t154);
    GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)28ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t154 + 8), t151);
    gm_tl874t2atnlh3 t155 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t152, t153));
    gm_tc3oht4ksth9d t156;
    gm_f_mk_expr_53qj7nd((void *)&t156, t148, &t154, GM_CONV(gm_cseahvg8v7p0vs, t155));
    gm_to1cssm8dfiv5 t157;
    GM_ST(uint8_t, (uint8_t *)&t157 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t157 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t157 + 4, 0);
    ((uint8_t *)&t157)[0] = 0;
    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t157 + 8), t156);
    memmove(gm_out, ((uint8_t *)&t157), 96);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_parse_where_opt_a5p2ja6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_parse_where_opt_a5p2ja6 F;
    void * gm_l0; /* p */
    uint32_t gm_l2; /* start */
    gm_t08olq97rscn3 gm_l3; /* name */
    void * gm_l4; /* t */
    GM_STACK_CHECK(&F, 885u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4jn8fl3lmk5k gm_l5;
    gm_t0u0j6voh3loe gm_l6;
    gm_to1cssm8dfiv5 gm_l7;
    gm_t0u0j6voh3loe gm_l8;
    gm_tftb1odc739t5 gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    gm_t3fcbuehh3bs2 gm_l13;
    gm_trqfvmjtt4sij t8;
    gm_f_list_5kg5cjm((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t8;
    void * t9 = gm_l0;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_smglbbtmdmip2));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(5));
    uint8_t t11 = gm_f_at_word_18dnh5f(t9, t10);
    uint8_t t12 = (uint8_t)!t11;
    if (t12) {
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
        gm_tt7ek1a8bopcl t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
        ((uint8_t *)&t14)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t14 + 8), t13);
        memmove(gm_out, ((uint8_t *)&t14), 24);
        goto gm_fdone;
    }
    void * t15 = gm_l0;
    gm_tm4pc0emhrv1g t16;
    gm_f_bump_q2i40re((void *)&t16, t15);
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t17 = gm_l0;
                uint32_t t18 = gm_f_lo_fp82gt4(t17);
                gm_l2 = t18;
                void * t19 = gm_l0;
                gm_b16_8 t20;
                GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sandsuergllg3));
                GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(29));
                gm_f_parse_ident_n2c6b75((void *)&gm_l5, (void *)NULL, cx, NULL, t19, t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                if (t21) {
                } else {
                    gm_tt7ek1a8bopcl t22;
                    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t22 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t22 + 16, 0);
                    ((uint8_t *)&t22)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t22 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t22), 24);
                    goto gm_fdone;
                }
                memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l5) + 8), 32);
                void * t23 = gm_l0;
                gm_b16_8 t24;
                GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sa8mtcad5rfmf));
                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(25));
                gm_t9qkh0rsooc8d t25;
                GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)61ULL));
                gm_t0u0j6voh3loe t26 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t23, t25, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t26;
                uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                if (t27) {
                } else {
                    gm_tt7ek1a8bopcl t28;
                    GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t28 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t28 + 16, 0);
                    ((uint8_t *)&t28)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t28 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t28), 24);
                    goto gm_fdone;
                }
                gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l6) + 4));
                void * t30 = gm_l0;
                gm_toh0ootodhjue t31;
                GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)0));
                GM_ST(uint8_t, ((uint8_t *)&t31 + 1), ((uint8_t)0));
                GM_ST(uint8_t, ((uint8_t *)&t31 + 2), ((uint8_t)0));
                gm_f_parse_type_in_bvi7uav((void *)&gm_l7, (void *)NULL, cx, NULL, t30, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                if (t32) {
                } else {
                    gm_tt7ek1a8bopcl t33;
                    GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t33 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
                    ((uint8_t *)&t33)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t33 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t33), 24);
                    goto gm_fdone;
                }
                void * t34 = gm_f_box_j4urmed((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)(((uint8_t *)&gm_l7) + 8)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t34;
                void * t35 = gm_l0;
                gm_b16_8 t36;
                GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s8dujv4dnroq0));
                GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(23));
                gm_t9qkh0rsooc8d t37;
                GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)62ULL));
                gm_t0u0j6voh3loe t38 = gm_f_expect_ijgfrs1((void *)NULL, cx, NULL, t35, t37, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t38;
                uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                if (t39) {
                } else {
                    gm_tt7ek1a8bopcl t40;
                    GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t40 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t40 + 16, 0);
                    ((uint8_t *)&t40)[0] = 1;
                    GM_ST(gm_b0_1, ((uint8_t *)&t40 + 8), GM_ZERO(gm_b0_1));
                    memmove(gm_out, ((uint8_t *)&t40), 24);
                    goto gm_fdone;
                }
                gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l8) + 4));
                void * t42 = (void *)((uint8_t *)&F.l1);
                void * t43 = gm_l4;
                void * t44 = gm_l0;
                uint32_t t45 = gm_l2;
                gm_t08olq97rscn3 t46 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l3));
                gm_tl874t2atnlh3 t47 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_from_1sqap0o(t44, t45));
                gm_tpv68m88f9lcr t48;
                GM_ST(uint32_t, (uint8_t *)&t48 + 52, 0);
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t48 + 0), t46);
                GM_ST(void *, ((uint8_t *)&t48 + 32), t43);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t48 + 40), t47);
                gm_tftb1odc739t5 t49 = gm_f_push_aqjeqnr(t42, &t48);
                gm_l9 = t49;
                uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcp8o48ld4n4jq, 354, 9);
                void * t51 = gm_l0;
                gm_t9qkh0rsooc8d t52;
                GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)57ULL));
                uint8_t t53 = gm_f_eat_qmock9g(t51, t52);
                if (t53) {
                    void * t54 = gm_l0;
                    gm_f_kind_k239kpg((void *)&gm_l13, t54);
                    {
                        {
                            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)0ULL));
                            if (!t56) {
                                goto gm_lb5;
                            }
                            gm_l12 = ((uint8_t)1);
                            goto gm_lb4;
                        }
gm_lb5:;
                        {
                            gm_l12 = ((uint8_t)0);
                            goto gm_lb4;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp8o48ld4n4jq, 355, 35);
                    }
gm_lb4:;
                    uint8_t t57 = gm_l12;
                    gm_l11 = t57;
                } else {
                    gm_l11 = ((uint8_t)0);
                }
                uint8_t t58 = gm_l11;
                if (t58) {
                    void * t59 = gm_l0;
                    gm_t9qkh0rsooc8d t60;
                    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)61ULL));
                    uint8_t t61 = gm_f_nth_is_3fgqmcb(t59, ((uint64_t)1ULL), t60);
                    gm_l10 = t61;
                } else {
                    gm_l10 = ((uint8_t)0);
                }
                uint8_t t62 = gm_l10;
                uint8_t t63 = (uint8_t)!t62;
                if (t63) {
                    goto gm_lb1;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_tt7ek1a8bopcl t65;
    GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
    ((uint8_t *)&t65)[0] = 0;
    GM_ST(gm_b16_8, ((uint8_t *)&t65 + 8), t64);
    memmove(gm_out, ((uint8_t *)&t65), 24);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
