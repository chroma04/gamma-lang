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
typedef struct gm_cskjv6t053tkje { uint32_t f0; uint64_t f1; } gm_cskjv6t053tkje;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; } gm_tb1msgo1fu1n3;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; uint32_t f4; uint32_t f5; uint32_t f6; uint32_t f7; } gm_tu4r32bi6mhcq;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_cskjv6t053tkje) == 16 && _Alignof(gm_cskjv6t053tkje) == 8 && offsetof(gm_cskjv6t053tkje, f0) == 0 && offsetof(gm_cskjv6t053tkje, f1) == 8, "layout of `Char`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_tl874t2atnlh3 f1; } gm_t2eb59dvglnle;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_t3jim1gvsdr87;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; } gm_t5r6k0fdcfsb3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; } gm_t6ofeqo2h242p;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; } gm_t724v60om7sh5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb1msgo1fu1n3) == 5 && _Alignof(gm_tb1msgo1fu1n3) == 1 && offsetof(gm_tb1msgo1fu1n3, f0) == 0 && offsetof(gm_tb1msgo1fu1n3, f1) == 1 && offsetof(gm_tb1msgo1fu1n3, f2) == 2 && offsetof(gm_tb1msgo1fu1n3, f3) == 3 && offsetof(gm_tb1msgo1fu1n3, f4) == 4, "layout of `(u8, u8, u8, u8, u8)`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; uint64_t f1; } gm_te0mgnjdphs16;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; gm_t9qkh0rsooc8d f3; gm_t9qkh0rsooc8d f4; gm_t9qkh0rsooc8d f5; gm_t9qkh0rsooc8d f6; gm_t9qkh0rsooc8d f7; gm_t9qkh0rsooc8d f8; gm_t9qkh0rsooc8d f9; } gm_tfh78knfg7urq;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; } gm_tk22flr9mh3ql;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tu4r32bi6mhcq) == 32 && _Alignof(gm_tu4r32bi6mhcq) == 4 && offsetof(gm_tu4r32bi6mhcq, f0) == 0 && offsetof(gm_tu4r32bi6mhcq, f1) == 4 && offsetof(gm_tu4r32bi6mhcq, f2) == 8 && offsetof(gm_tu4r32bi6mhcq, f3) == 12 && offsetof(gm_tu4r32bi6mhcq, f4) == 16 && offsetof(gm_tu4r32bi6mhcq, f5) == 20 && offsetof(gm_tu4r32bi6mhcq, f6) == 24 && offsetof(gm_tu4r32bi6mhcq, f7) == 28, "layout of `(u32, u32, u32, u32, u32, u32, u32, u32)`");
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
_Static_assert(sizeof(gm_t2eb59dvglnle) == 16 && _Alignof(gm_t2eb59dvglnle) == 4 && offsetof(gm_t2eb59dvglnle, f0) == 0 && offsetof(gm_t2eb59dvglnle, f1) == 4, "layout of `Bracket`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t9qkh0rsooc8d v1; void * v2; void * v3; gm_tibupt4dmcqai v4; gm_b16_8 v5; gm_b16_8 v6; gm_t9qkh0rsooc8d v7; } f1; } gm_t3fcbuehh3bs2;
_Static_assert(sizeof(gm_t3jim1gvsdr87) == 40 && _Alignof(gm_t3jim1gvsdr87) == 8 && offsetof(gm_t3jim1gvsdr87, f0) == 0 && offsetof(gm_t3jim1gvsdr87, f1) == 16 && offsetof(gm_t3jim1gvsdr87, f2) == 32, "layout of `DocComment`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5r6k0fdcfsb3) == 4 && _Alignof(gm_t5r6k0fdcfsb3) == 1 && offsetof(gm_t5r6k0fdcfsb3, f0) == 0 && offsetof(gm_t5r6k0fdcfsb3, f1) == 2, "layout of `(?u8, ?u8)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t6ofeqo2h242p) == 7 && _Alignof(gm_t6ofeqo2h242p) == 1 && offsetof(gm_t6ofeqo2h242p, f0) == 0 && offsetof(gm_t6ofeqo2h242p, f1) == 1 && offsetof(gm_t6ofeqo2h242p, f2) == 2 && offsetof(gm_t6ofeqo2h242p, f3) == 3 && offsetof(gm_t6ofeqo2h242p, f4) == 4 && offsetof(gm_t6ofeqo2h242p, f5) == 5 && offsetof(gm_t6ofeqo2h242p, f6) == 6, "layout of `(Command, Command, Command, Command, Command, Command, Command)`");
_Static_assert(sizeof(gm_t724v60om7sh5) == 5 && _Alignof(gm_t724v60om7sh5) == 1 && offsetof(gm_t724v60om7sh5, f0) == 0 && offsetof(gm_t724v60om7sh5, f1) == 1 && offsetof(gm_t724v60om7sh5, f2) == 2 && offsetof(gm_t724v60om7sh5, f3) == 3 && offsetof(gm_t724v60om7sh5, f4) == 4, "layout of `(Keyword, Keyword, Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_te0mgnjdphs16) == 16 && _Alignof(gm_te0mgnjdphs16) == 8 && offsetof(gm_te0mgnjdphs16, f0) == 0 && offsetof(gm_te0mgnjdphs16, f1) == 8, "layout of `Chunk`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tfh78knfg7urq) == 10 && _Alignof(gm_tfh78knfg7urq) == 1 && offsetof(gm_tfh78knfg7urq, f0) == 0 && offsetof(gm_tfh78knfg7urq, f1) == 1 && offsetof(gm_tfh78knfg7urq, f2) == 2 && offsetof(gm_tfh78knfg7urq, f3) == 3 && offsetof(gm_tfh78knfg7urq, f4) == 4 && offsetof(gm_tfh78knfg7urq, f5) == 5 && offsetof(gm_tfh78knfg7urq, f6) == 6 && offsetof(gm_tfh78knfg7urq, f7) == 7 && offsetof(gm_tfh78knfg7urq, f8) == 8 && offsetof(gm_tfh78knfg7urq, f9) == 9, "layout of `(Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct, Punct)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tk22flr9mh3ql) == 3 && _Alignof(gm_tk22flr9mh3ql) == 1 && offsetof(gm_tk22flr9mh3ql, f0) == 0 && offsetof(gm_tk22flr9mh3ql, f1) == 1 && offsetof(gm_tk22flr9mh3ql, f2) == 2, "layout of `(Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint64_t f2; void * f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_tc0nbpdhofi37 f7; gm_trqfvmjtt4sij f8; gm_toceffb697p0u f9; gm_trqfvmjtt4sij f10; uint8_t f11; uint8_t pe[7]; } gm_trd6bg8is1v4n;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t2eb59dvglnle f0; uint8_t f1; uint8_t pe[3]; } gm_tu1ap1uj3psaf;
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
_Static_assert(sizeof(gm_t3fcbuehh3bs2) == 32 && _Alignof(gm_t3fcbuehh3bs2) == 8 && offsetof(gm_t3fcbuehh3bs2, f0) == 0 && offsetof(gm_t3fcbuehh3bs2, f1) == 8, "layout of `TokenKind`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { gm_t3fcbuehh3bs2 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tm4pc0emhrv1g;
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_trd6bg8is1v4n) == 288 && _Alignof(gm_trd6bg8is1v4n) == 8 && offsetof(gm_trd6bg8is1v4n, f0) == 0 && offsetof(gm_trd6bg8is1v4n, f1) == 8 && offsetof(gm_trd6bg8is1v4n, f2) == 24 && offsetof(gm_trd6bg8is1v4n, f3) == 32 && offsetof(gm_trd6bg8is1v4n, f4) == 40 && offsetof(gm_trd6bg8is1v4n, f5) == 80 && offsetof(gm_trd6bg8is1v4n, f6) == 120 && offsetof(gm_trd6bg8is1v4n, f7) == 160 && offsetof(gm_trd6bg8is1v4n, f8) == 184 && offsetof(gm_trd6bg8is1v4n, f9) == 224 && offsetof(gm_trd6bg8is1v4n, f10) == 240 && offsetof(gm_trd6bg8is1v4n, f11) == 280, "layout of `Lexer`");
_Static_assert(sizeof(gm_tu1ap1uj3psaf) == 20 && _Alignof(gm_tu1ap1uj3psaf) == 4 && offsetof(gm_tu1ap1uj3psaf, f0) == 0 && offsetof(gm_tu1ap1uj3psaf, f1) == 16, "layout of `?Bracket`");
_Static_assert(sizeof(gm_tm4pc0emhrv1g) == 48 && _Alignof(gm_tm4pc0emhrv1g) == 8 && offsetof(gm_tm4pc0emhrv1g, f0) == 0 && offsetof(gm_tm4pc0emhrv1g, f1) == 32, "layout of `Token`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_backtick_run_l95fb09 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_block_comment_fvce7qt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_byte_b4v8vul {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_c_equality_u566nc4 {
    gm_frame h;
    gm_t4gut2c9fj1kt l10; /* d */
    gm_b0_1 ret;
};
struct gm_fr_clear_4qp22g2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_close_all_brackets_bql4j88 {
    gm_frame h;
    gm_t4gut2c9fj1kt l6; /* d */
    gm_b0_1 ret;
};
struct gm_fr_close_bracket_tbjprpe {
    gm_frame h;
    gm_t4gut2c9fj1kt l11; /* d */
    gm_b0_1 ret;
};
struct gm_fr_closer_fus0off {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_closer_slot_f4sb6v0 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_closing_na3a1ur {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_current_char_ctktsl2 {
    gm_frame h;
    gm_tb31k5l794t7p ret;
};
struct gm_fr_declaration_starts_at_mp730g1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_emit_ekvv703 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_emit_semicolon_7da5b93 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3pd55t2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hc7thq6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qbupbr6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_p5r0d56 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_flavors_qsu4oq0 {
    gm_frame h;
    gm_t6ofeqo2h242p ret;
};
struct gm_fr_flush_newline_9023ml8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_7a5f58b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d7osfot {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_daod35i {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dsta2ql {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fuvjr35 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_g637ghi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_i801868 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ooibcq2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pscs6r9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t32jdqr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t4ivgd1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_inline_comment_end_8i5h1eq {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_inline_raw_string_end_6lenf33 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_is_alnum_n81ervr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_alpha_12215ga {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ascii_space_t2ltvst {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_digit_2hoggnj {
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
struct gm_fr_is_lower_4ebokoh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_punct_fdeminj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unicode_space_b2m9rku {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_upper_iekrftp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_lex_dc8mbmf {
    gm_frame h;
    gm_trd6bg8is1v4n l3; /* lx */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_lex_recovering_obo2892 {
    gm_frame h;
    gm_t2792tsg5eg72 l3; /* first */
    gm_trd6bg8is1v4n l4; /* lx */
    gm_tr8dvlu9ik4gj l5; /* out */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_trd6bg8is1v4n l9; /* again */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_lex_token_h8g44s4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_lex_word_9li16te {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_line_binds_jm0pnul {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_line_comment_s71u8hq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_2iogcnb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5o6u5a9 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_lpokg1m {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lone_cr_ktb0jvm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_new_lexer_dm12g12 {
    gm_frame h;
    gm_trd6bg8is1v4n ret;
};
struct gm_fr_note_newline_cchbr7g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_one_or_two_lava5tu {
    gm_frame h;
    gm_te0mgnjdphs16 ret;
};
struct gm_fr_open_bracket_3kcj4k8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_opener_8hqhoc3 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_peek_72bostn {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_peek_at_74p8lkq {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_pop_1q0s3gu {
    gm_frame h;
    gm_tu1ap1uj3psaf ret;
};
struct gm_fr_pop_j4rpvm5 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_newline_ke0tojq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_push_o6f0em6 {
    gm_frame h;
    gm_t3jim1gvsdr87 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pnf14h1 {
    gm_frame h;
    gm_tm4pc0emhrv1g l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vs3f95a {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_13c41om {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_bnsp8p9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ci9rduq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_reserve_i65s8oj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_run_v26is74 {
    gm_frame h;
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_scan_ident_h1b2n4u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_scan_punct_ecrh51c {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_second_word_end_dpnj7g8 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_skip_trivia_014s7ai {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_slot_4oe07nr {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_span_64igq5o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_span_tg7cev6 {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_starts_continuation_n3333i7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_suppresses_newline_after_o08ord2 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_truncate_h4ov51t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unexpected_char_r3t8d4c {
    gm_frame h;
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq2ceic3f8rp(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqgj9fu5tboe(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqq6nrh55uon(const uint8_t *a, const uint8_t *b);
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint64_t gm_f_backtick_run_l95fb09(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_block_comment_fvce7qt(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_c_equality_u566nc4(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_clear_4qp22g2(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_close_all_brackets_bql4j88(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_close_bracket_tbjprpe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_closer_fus0off(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) uint64_t gm_f_closer_slot_f4sb6v0(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_closing_na3a1ur(const void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_closing_na3a1ur_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_declaration_starts_at_mp730g1(void * a0, uint64_t a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_declaration_starts_at_mp730g1_c(void * a0, uint64_t a1, gm_t3fcbuehh3bs2 a2);
__attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_decode_jrk6b03(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_ekvv703(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_semicolon_7da5b93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3pd55t2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hc7thq6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qbupbr6(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t6ofeqo2h242p gm_f_flavors_qsu4oq0(uint8_t a0);
__attribute__((aligned(4))) void gm_f_flush_newline_9023ml8(void * a0, const void * a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_flush_newline_9023ml8_c(void * a0, gm_t3fcbuehh3bs2 a1, uint64_t a2);
inline __attribute__((aligned(4))) void gm_f_format_7a5f58b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_d7osfot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_daod35i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dq6oc41(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fuvjr35(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_g637ghi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_i801868(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_pscs6r9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_t32jdqr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_t4ivgd1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_inline_comment_end_8i5h1eq(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_inline_raw_string_end_6lenf33(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alnum_n81ervr(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_alpha_12215ga(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ascii_space_t2ltvst(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_digit_2hoggnj(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ident_start_rjefpfm(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_lower_4ebokoh(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj(const void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) uint8_t gm_f_is_unicode_space_b2m9rku(uint32_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_iekrftp(uint8_t a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_keyword_from_1ofmh1u(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_char_4qk4vjp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_number_9j9sdfi(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_raw_string_uf6bd4g(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_lex_recovering_obo2892(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_string_bcdbdjm(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_token_h8g44s4(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_word_9li16te(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_line_binds_jm0pnul(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_line_comment_s71u8hq(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_2iogcnb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5o6u5a9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_lpokg1m(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_note_newline_cchbr7g(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_te0mgnjdphs16 gm_f_one_or_two_lava5tu(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) void gm_f_open_bracket_3kcj4k8(void * a0, gm_t9qkh0rsooc8d a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_opener_8hqhoc3(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_1q0s3gu(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_punct_text_ov4r40u(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_newline_ke0tojq(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o6f0em6(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pnf14h1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vs3f95a(void * a0, gm_t2eb59dvglnle a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_13c41om(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bnsp8p9(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ci9rduq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_i65s8oj(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_scan_ident_h1b2n4u(void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_scan_punct_ecrh51c(void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_second_word_end_dpnj7g8(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_skip_trivia_014s7ai(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint64_t gm_f_slot_4oe07nr(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) uint8_t gm_f_starts_continuation_n3333i7(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_continuation_n3333i7_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_suppresses_newline_after_o08ord2(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_suppresses_newline_after_o08ord2_c(gm_t3fcbuehh3bs2 a0);
inline __attribute__((aligned(4))) void gm_f_truncate_h4ov51t(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unexpected_char_r3t8d4c(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_c644mb9o5bq[16] __attribute__((aligned(8)));
extern const uint8_t gm_s0irgfjsc3pat[76];
extern const uint8_t gm_s1c4sfff8ac39[7];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s5t1b3eks758h[27];
extern const uint8_t gm_s8cr606ter6p9[39];
extern const uint8_t gm_s9r64t1166q1f[5];
extern const uint8_t gm_sb2k9e71jsraf[7];
extern const uint8_t gm_sb54ui2amic32[2];
extern const uint8_t gm_scvhpklutvpjg[4];
extern const uint8_t gm_sd25v201ev3d4[30];
extern const uint8_t gm_setftr5leknrn[23];
extern const uint8_t gm_sf50oc24o4dhk[11];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sj8ptihcm0srb[51];
extern const uint8_t gm_smrmke0a8tkf0[120];
extern const uint8_t gm_sn1puqmdafub0[14];
extern const uint8_t gm_sp02l2e4viomq[26];
extern const uint8_t gm_sphr6fu31dtrl[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr4vl7ov3g7t6[55];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src41vqrhgekac9[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcvmmksi0829pa[];
extern const uint8_t gm_srfagufvviad7[6];
extern const uint8_t gm_srh29phg8b30r[4];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_stnlf4pvbut1c[3];
extern const uint8_t gm_svbhcvlahd4n2[3];
extern const gm_tmpl gm_tpl1bno78mc58dl;
extern const gm_tmpl gm_tpla4jdv6pnguh4;
extern const gm_tmpl gm_tplav4294nbj9c7;
extern const gm_tmpl gm_tplfqr816g9de0k;
extern const gm_tmpl gm_tplgan4klju39em;
extern const gm_tmpl gm_tplhphg08jcb4qr;
extern const gm_tmpl gm_tplk6g6bjh5rkat;
extern const gm_tmpl gm_tpllcp8fo2sqc1r;
extern const gm_tmpl gm_tplsrr2cfplv9ff;
extern const gm_tmpl gm_tplv8tsq846c0lj;
extern const gm_tmpl gm_tplvvic2bq8a97l;
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
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqgj9fu5tboe(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqq6nrh55uon(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqgj9fu5tboe(a, b);
}
inline bool gm_eqv3tj1pu43g(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_byte_b4v8vul F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
    uint64_t t5 = gm_l1;
    uint8_t t6 = (t5 < t4);
    if (t6) {
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t5 * 1));
        gm_tqs5p4n2tq19v t8;
        GM_ST(uint8_t, ((uint8_t *)&t8), t7);
        ((uint8_t *)&t8)[1] = 1;
        gm_l2 = t8;
    } else {
        gm_tqs5p4n2tq19v t9;
        memset(&t9, 0, sizeof t9);
        gm_l2 = t9;
    }
    gm_tqs5p4n2tq19v t10 = gm_l2;
    F.ret = t10;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_clear_4qp22g2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_4qp22g2 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
    if (t6 > t7 || t7 > (uintptr_t)t5) gm_trap_slice(t6, t7, t5, gm_srcrgc9bkquf6nr, 140, 15);
    uint64_t t8 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint64_t gm_f_closer_slot_f4sb6v0(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_closer_slot_f4sb6v0 F;
    gm_t9qkh0rsooc8d gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 37u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t gm_l1;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)62ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_l1 = ((uint64_t)0ULL);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)64ULL));
            if (!t9) {
                goto gm_lb3;
            }
            gm_l1 = ((uint64_t)1ULL);
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_l1 = ((uint64_t)2ULL);
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 67, 41);
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
inline __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_current_char_ctktsl2 F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 8));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 24));
    gm_tb31k5l794t7p t6 = GM_CONV(gm_tb31k5l794t7p, gm_f_decode_jrk6b03(t3, t5));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cskjv6t053tkje, F.ret);
gm_fprop:
    return GM_CONV(gm_cskjv6t053tkje, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_diagnostics_n3kkhf4 F;
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_prbqo9j((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t2792tsg5eg72 t3;
    GM_ST(uint32_t, (uint8_t *)&t3 + 60, 0);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t3 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t3 + 40), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t3 + 48), ((uint64_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 56), ((uint32_t)0ULL));
    memmove(gm_out, ((uint8_t *)&t3), 64);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_semicolon_7da5b93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_emit_semicolon_7da5b93 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    gm_t3fcbuehh3bs2 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t4 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t4 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t4 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)9ULL));
    gm_f_flush_newline_9023ml8(t2, &t4, t3);
    void * t5 = gm_l0;
    void * t6 = gm_l0;
    uint64_t t7 = gm_l1;
    uint64_t t8 = gm_l1;
    uint64_t r9; bool o10 = __builtin_add_overflow(t8, ((uint64_t)1ULL), &r9);
    if (o10) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 656, 33);
    uint64_t t11 = r9;
    gm_tl874t2atnlh3 t12 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t6, t7, t11));
    gm_f_push_newline_ke0tojq(t5, GM_CONV(gm_cseahvg8v7p0vs, t12));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3pd55t2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3pd55t2 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 16)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hc7thq6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hc7thq6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qbupbr6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qbupbr6 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3) {
    struct gm_fr_error_p5r0d56 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    gm_b16_8 gm_l3; /* message */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 32));
    void * t4 = gm_l0;
    uint64_t t5 = gm_l1;
    uint64_t t6 = gm_l2;
    gm_tl874t2atnlh3 t7 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t4, t5, t6));
    gm_b16_8 t8 = gm_l3;
    gm_b0_1 t9 = gm_f_report_eardf07((void *)NULL, cx, NULL, t3, t7, t8);
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
inline __attribute__((aligned(4))) void gm_f_format_7a5f58b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7a5f58b F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s8cr606ter6p9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(38));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllcp8fo2sqc1r), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_d7osfot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_d7osfot F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr4vl7ov3g7t6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(54));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1bno78mc58dl), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_daod35i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_daod35i F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_setftr5leknrn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplgan4klju39em), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dsta2ql F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb2k9e71jsraf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfqr816g9de0k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_fuvjr35(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fuvjr35 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp02l2e4viomq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplav4294nbj9c7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_g637ghi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_g637ghi F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sj8ptihcm0srb));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(50));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplv8tsq846c0lj), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_i801868(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_i801868 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd25v201ev3d4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsrr2cfplv9ff), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ooibcq2 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf50oc24o4dhk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk6g6bjh5rkat), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_pscs6r9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_pscs6r9 F;
    gm_tommf4j9upomc gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tommf4j9upomc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smrmke0a8tkf0));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(119));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvvic2bq8a97l), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_t32jdqr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_t32jdqr F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn1puqmdafub0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpla4jdv6pnguh4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t4ivgd1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_t4ivgd1 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0irgfjsc3pat));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(75));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhphg08jcb4qr), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_ascii_space_t2ltvst(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ascii_space_t2ltvst F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = gm_l0;
    gm_tb1msgo1fu1n3 t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)32ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 1), ((uint8_t)9ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 2), ((uint8_t)10ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 3), ((uint8_t)12ULL));
    GM_ST(uint8_t, ((uint8_t *)&t3 + 4), ((uint8_t)13ULL));
    uint8_t t4 = 0;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 0, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 1, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 2, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 3, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t3) + 4, ((uint8_t *)&t2))) t4 = 1;
    uint8_t t5 = (uint8_t)(t4);
    F.ret = t5;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_punct_fdeminj(const void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_punct_fdeminj F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* p */
    gm_t9qkh0rsooc8d gm_l2; /* q */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    uint8_t gm_l3;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)8ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l2 = t7;
            gm_t9qkh0rsooc8d t8 = gm_l2;
            gm_t9qkh0rsooc8d t9 = gm_l1;
            uint8_t t10 = (gm_eqv3tj1pu43g(((uint8_t *)&t8), ((uint8_t *)&t9)));
            gm_l3 = t10;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src41vqrhgekac9, 308, 51);
    }
gm_lb1:;
    uint8_t t11 = gm_l3;
    F.ret = t11;
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
inline __attribute__((aligned(4))) void gm_f_list_2iogcnb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_2iogcnb F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_3pd55t2();
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
inline __attribute__((aligned(4))) void gm_f_list_5o6u5a9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5o6u5a9 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_hc7thq6();
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
inline __attribute__((aligned(4))) void gm_f_list_lpokg1m(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_lpokg1m F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qbupbr6();
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
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_prbqo9j F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_1th396a();
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lone_cr_ktb0jvm F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 24));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
    uint64_t r7; bool o8 = __builtin_add_overflow(t6, ((uint64_t)1ULL), &r7);
    if (o8) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 198, 22);
    uint64_t t9 = r7;
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)gm_c644mb9o5bq));
    gm_b0_1 t11 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t2, t4, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t12 = gm_l0;
    void * t13 = gm_l0;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 24));
    uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 199, 5);
    uint64_t t17 = r15;
    GM_ST(uint64_t, (((uint8_t *)t12) + 24), t17);
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
inline __attribute__((aligned(4))) void gm_f_note_newline_cchbr7g(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_note_newline_cchbr7g F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 95u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l3;
    void * t5 = gm_l0;
    {
        {
            uint8_t t6 = (uint8_t)(((((uint8_t *)t5) + 224))[12] != 0);
            if (t6) {
                goto gm_lb2;
            }
            gm_l3 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 203, 8);
    }
gm_lb1:;
    uint8_t t7 = gm_l3;
    if (t7) {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        uint64_t t10 = gm_l1;
        uint64_t t11 = gm_l2;
        gm_tl874t2atnlh3 t12 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t9, t10, t11));
        gm_toceffb697p0u t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t13), t12);
        ((uint8_t *)&t13)[12] = 1;
        GM_ST(gm_toceffb697p0u, (((uint8_t *)t8) + 224), t13);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_te0mgnjdphs16 gm_f_one_or_two_lava5tu(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_one_or_two_lava5tu F;
    uint8_t gm_l0; /* two */
    gm_t9qkh0rsooc8d gm_l1; /* long */
    gm_t9qkh0rsooc8d gm_l2; /* short */
    GM_STACK_CHECK_SHALLOW(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_te0mgnjdphs16 gm_l3;
    uint8_t t2 = gm_l0;
    if (t2) {
        gm_t9qkh0rsooc8d t3 = gm_l1;
        gm_te0mgnjdphs16 t4;
        GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t4 + 0), t3);
        GM_ST(uint64_t, ((uint8_t *)&t4 + 8), ((uint64_t)2ULL));
        gm_l3 = t4;
    } else {
        gm_t9qkh0rsooc8d t5 = gm_l2;
        gm_te0mgnjdphs16 t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t6 + 0), t5);
        GM_ST(uint64_t, ((uint8_t *)&t6 + 8), ((uint64_t)1ULL));
        gm_l3 = t6;
    }
    gm_te0mgnjdphs16 t7 = gm_l3;
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_72bostn F;
    void * gm_l0; /* lx */
    GM_STACK_CHECK_SHALLOW(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
    uint8_t t7 = (t6 < t4);
    if (t7) {
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
        gm_tqs5p4n2tq19v t9;
        GM_ST(uint8_t, ((uint8_t *)&t9), t8);
        ((uint8_t *)&t9)[1] = 1;
        gm_l1 = t9;
    } else {
        gm_tqs5p4n2tq19v t10;
        memset(&t10, 0, sizeof t10);
        gm_l1 = t10;
    }
    gm_tqs5p4n2tq19v t11 = gm_l1;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_peek_at_74p8lkq F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 8) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
    uint64_t t7 = gm_l1;
    uint64_t r8; bool o9 = __builtin_add_overflow(t6, t7, &r8);
    if (o9) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 153, 64);
    uint64_t t10 = r8;
    uint8_t t11 = (t10 < t4);
    if (t11) {
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t10 * 1));
        gm_tqs5p4n2tq19v t13;
        GM_ST(uint8_t, ((uint8_t *)&t13), t12);
        ((uint8_t *)&t13)[1] = 1;
        gm_l2 = t13;
    } else {
        gm_tqs5p4n2tq19v t14;
        memset(&t14, 0, sizeof t14);
        gm_l2 = t14;
    }
    gm_tqs5p4n2tq19v t15 = gm_l2;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_1q0s3gu(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_1q0s3gu F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t2eb59dvglnle gm_l2; /* x */
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
        gm_tu1ap1uj3psaf t6;
        memset(&t6, 0, sizeof t6);
        memmove(gm_out, ((uint8_t *)&t6), 20);
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
    gm_t2eb59dvglnle t14 = GM_LD(gm_t2eb59dvglnle, (((uint8_t *)t8) + (uintptr_t)t13 * 16));
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
    gm_t2eb59dvglnle t27 = gm_l2;
    gm_tu1ap1uj3psaf t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 18, 0);
    GM_ST(gm_t2eb59dvglnle, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t28), 20);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o6f0em6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o6f0em6 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_hc7thq6();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_13c41om(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pnf14h1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pnf14h1 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_qbupbr6();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_bnsp8p9(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vs3f95a(void * a0, gm_t2eb59dvglnle a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vs3f95a F;
    void * gm_l0; /* l */
    gm_t2eb59dvglnle gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_3pd55t2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ci9rduq(t8, ((uint64_t)1ULL), t10);
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
    gm_t2eb59dvglnle t18 = gm_l1;
    GM_ST(gm_t2eb59dvglnle, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_13c41om(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_13c41om F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bnsp8p9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_bnsp8p9 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ci9rduq(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ci9rduq F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_i65s8oj(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reserve_i65s8oj F;
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
    gm_b16_8 t15 = gm_f_empty_slice_qbupbr6();
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
    gm_tftb1odc739t5 t17 = gm_f_regrow_bnsp8p9(t13, t14, t16);
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_64igq5o F;
    uint32_t gm_l0; /* file */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t t2 = gm_l0;
    uint64_t t3 = gm_l1;
    uint32_t t4 = (uint32_t)t3;
    uint64_t t5 = gm_l2;
    uint32_t t6 = (uint32_t)t5;
    gm_tl874t2atnlh3 t7;
    GM_ST(uint32_t, ((uint8_t *)&t7 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t7 + 4), t4);
    GM_ST(uint32_t, ((uint8_t *)&t7 + 8), t6);
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
}
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_tg7cev6 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)t2));
    uint64_t t4 = gm_l1;
    uint64_t t5 = gm_l2;
    gm_tl874t2atnlh3 t6 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_64igq5o(t3, t4, t5));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
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
inline __attribute__((aligned(4))) void gm_f_truncate_h4ov51t(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_h4ov51t F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 16)));
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
inline __attribute__((aligned(4))) uint64_t gm_f_backtick_run_l95fb09(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_backtick_run_l95fb09 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t5 = gm_l1;
    gm_l2 = t5;
    {
        for (;;) {
            uint64_t t6 = gm_l2;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t8 = (t6 < t7);
            if (!t8) {
                goto gm_lb1;
            }
            void * t9 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t11 = gm_l2;
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t9) + (uintptr_t)t11 * 1));
            uint8_t t13 = ((t12 == ((uint8_t)96ULL)));
            if (!t13) {
                goto gm_lb1;
            }
            {
                uint64_t t14 = gm_l2;
                uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
                if (o16) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 734, 39);
                uint64_t t17 = r15;
                gm_l2 = t17;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t18 = gm_l2;
    uint64_t t19 = gm_l1;
    uint64_t r20; bool o21 = __builtin_sub_overflow(t18, t19, &r20);
    if (o21) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 735, 12);
    uint64_t t22 = r20;
    F.ret = t22;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_block_comment_fvce7qt(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_block_comment_fvce7qt F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* i */
    int32_t gm_l3; /* depth */
    uint8_t gm_l4; /* b */
    gm_tqs5p4n2tq19v gm_l5; /* next */
    GM_STACK_CHECK(&F, 431u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l6;
    uint8_t gm_l7;
    void * t11 = gm_l0;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 24));
    gm_l1 = t12;
    uint64_t t13 = gm_l1;
    uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)2ULL), &r14);
    if (o15) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 235, 10);
    uint64_t t16 = r14;
    gm_l2 = t16;
    gm_l3 = ((int32_t)1LL);
    {
        for (;;) {
            int32_t t17 = gm_l3;
            uint8_t t18 = (t17 > ((int32_t)0LL));
            if (!t18) {
                goto gm_lb1;
            }
            {
                void * t19 = gm_l0;
                uint64_t t20 = gm_l2;
                gm_tqs5p4n2tq19v t21 = gm_f_byte_b4v8vul(t19, t20);
                gm_l6 = t21;
                uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l6))[1] != 0);
                if (t22) {
                    uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)&gm_l6));
                    gm_l7 = t23;
                } else {
                    void * t24 = gm_l0;
                    uint64_t t25 = gm_l1;
                    uint64_t t26 = gm_l1;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)2ULL), &r27);
                    if (o28) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 239, 26);
                    uint64_t t29 = r27;
                    gm_b16_8 t30;
                    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s5t1b3eks758h));
                    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(26));
                    gm_b0_1 t31 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t24, t25, t29, t30);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
                uint8_t t32 = gm_l7;
                gm_l4 = t32;
                void * t33 = gm_l0;
                uint64_t t34 = gm_l2;
                uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                if (o36) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 242, 25);
                uint64_t t37 = r35;
                gm_tqs5p4n2tq19v t38 = gm_f_byte_b4v8vul(t33, t37);
                gm_l5 = t38;
                {
                    {
                        uint8_t t39 = gm_l4;
                        uint8_t t40 = ((t39 == ((uint8_t)47ULL)));
                        if (!t40) {
                            goto gm_lb5;
                        }
                        gm_tqs5p4n2tq19v t41 = gm_l5;
                        gm_tqs5p4n2tq19v t42;
                        GM_ST(uint8_t, ((uint8_t *)&t42), ((uint8_t)42ULL));
                        ((uint8_t *)&t42)[1] = 1;
                        uint8_t t43 = (gm_eqb44cb3cf1n(((uint8_t *)&t41), ((uint8_t *)&t42)));
                        if (!t43) {
                            goto gm_lb5;
                        }
                        int32_t t44 = gm_l3;
                        int32_t r45; bool o46 = __builtin_add_overflow(t44, ((int32_t)1LL), &r45);
                        if (o46) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 244, 13);
                        int32_t t47 = r45;
                        gm_l3 = t47;
                        uint64_t t48 = gm_l2;
                        uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)2ULL), &r49);
                        if (o50) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 245, 13);
                        uint64_t t51 = r49;
                        gm_l2 = t51;
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        {
                            uint8_t t52 = gm_l4;
                            uint8_t t53 = ((t52 == ((uint8_t)42ULL)));
                            if (!t53) {
                                goto gm_lb7;
                            }
                            gm_tqs5p4n2tq19v t54 = gm_l5;
                            gm_tqs5p4n2tq19v t55;
                            GM_ST(uint8_t, ((uint8_t *)&t55), ((uint8_t)47ULL));
                            ((uint8_t *)&t55)[1] = 1;
                            uint8_t t56 = (gm_eqb44cb3cf1n(((uint8_t *)&t54), ((uint8_t *)&t55)));
                            if (!t56) {
                                goto gm_lb7;
                            }
                            int32_t t57 = gm_l3;
                            int32_t r58; bool o59 = __builtin_sub_overflow(t57, ((int32_t)1LL), &r58);
                            if (o59) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 247, 13);
                            int32_t t60 = r58;
                            gm_l3 = t60;
                            uint64_t t61 = gm_l2;
                            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)2ULL), &r62);
                            if (o63) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 248, 13);
                            uint64_t t64 = r62;
                            gm_l2 = t64;
                            goto gm_lb6;
                        }
gm_lb7:;
                        uint8_t t65 = gm_l4;
                        uint8_t t66 = ((t65 == ((uint8_t)10ULL)));
                        if (t66) {
                            void * t67 = gm_l0;
                            uint64_t t68 = gm_l2;
                            uint64_t t69 = gm_l2;
                            uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                            if (o71) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 250, 32);
                            uint64_t t72 = r70;
                            gm_f_note_newline_cchbr7g(t67, t68, t72);
                            uint64_t t73 = gm_l2;
                            uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
                            if (o75) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 251, 13);
                            uint64_t t76 = r74;
                            gm_l2 = t76;
                        } else {
                            {
                                {
                                    uint8_t t77 = gm_l4;
                                    uint8_t t78 = ((t77 == ((uint8_t)13ULL)));
                                    if (!t78) {
                                        goto gm_lb9;
                                    }
                                    gm_tqs5p4n2tq19v t79 = gm_l5;
                                    gm_tqs5p4n2tq19v t80;
                                    GM_ST(uint8_t, ((uint8_t *)&t80), ((uint8_t)10ULL));
                                    ((uint8_t *)&t80)[1] = 1;
                                    uint8_t t81 = (gm_eqb44cb3cf1n(((uint8_t *)&t79), ((uint8_t *)&t80)));
                                    if (!t81) {
                                        goto gm_lb9;
                                    }
                                    void * t82 = gm_l0;
                                    uint64_t t83 = gm_l2;
                                    uint64_t t84 = gm_l2;
                                    uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)2ULL), &r85);
                                    if (o86) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 253, 32);
                                    uint64_t t87 = r85;
                                    gm_f_note_newline_cchbr7g(t82, t83, t87);
                                    uint64_t t88 = gm_l2;
                                    uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)2ULL), &r89);
                                    if (o90) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 254, 13);
                                    uint64_t t91 = r89;
                                    gm_l2 = t91;
                                    goto gm_lb8;
                                }
gm_lb9:;
                                uint8_t t92 = gm_l4;
                                uint8_t t93 = ((t92 == ((uint8_t)13ULL)));
                                if (t93) {
                                    void * t94 = gm_l0;
                                    uint64_t t95 = gm_l2;
                                    uint64_t t96 = gm_l2;
                                    uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                                    if (o98) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 256, 25);
                                    uint64_t t99 = r97;
                                    gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)gm_c644mb9o5bq));
                                    gm_b0_1 t101 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t94, t95, t99, t100);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint64_t t102 = gm_l2;
                                    uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                                    if (o104) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 257, 13);
                                    uint64_t t105 = r103;
                                    gm_l2 = t105;
                                } else {
                                    uint64_t t106 = gm_l2;
                                    uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                                    if (o108) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 259, 13);
                                    uint64_t t109 = r107;
                                    gm_l2 = t109;
                                }
                            }
gm_lb8:;
                        }
                    }
gm_lb6:;
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t110 = gm_l0;
    uint64_t t111 = gm_l2;
    GM_ST(uint64_t, (((uint8_t *)t110) + 24), t111);
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
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_byte_b4v8vul(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_c_equality_u566nc4(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_c_equality_u566nc4 F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* b */
    uint64_t gm_l2; /* lo */
    uint8_t gm_l3; /* eq */
    gm_b16_8 gm_l4; /* op */
    gm_b16_8 gm_l5; /* written */
    gm_t3fcbuehh3bs2 gm_l6; /* kind */
    gm_b16_8 gm_l7; /* before */
    gm_b16_8 gm_l8; /* after */
    uint8_t gm_l9; /* c */
    GM_STACK_CHECK(&F, 1250u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l11;
    gm_b16_8 gm_l12;
    gm_t3fcbuehh3bs2 gm_l13;
    gm_b16_8 gm_l14;
    gm_tqs5p4n2tq19v gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 24));
    gm_l2 = t7;
    void * t8 = gm_l0;
    void * t9 = gm_l0;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 24));
    uint64_t r11; bool o12 = __builtin_add_overflow(t10, ((uint64_t)2ULL), &r11);
    if (o12) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 329, 5);
    uint64_t t13 = r11;
    GM_ST(uint64_t, (((uint8_t *)t8) + 24), t13);
    uint8_t t14 = gm_l1;
    uint8_t t15 = ((t14 == ((uint8_t)61ULL)));
    gm_l3 = t15;
    uint8_t t16 = gm_l3;
    if (t16) {
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sgpdmvc9s0d2p));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(2));
        gm_l11 = t17;
    } else {
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s1c4sfff8ac39));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(6));
        gm_l11 = t18;
    }
    gm_b16_8 t19 = gm_l11;
    gm_l4 = t19;
    uint8_t t20 = gm_l3;
    if (t20) {
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sphr6fu31dtrl));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(2));
        gm_l12 = t21;
    } else {
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_stnlf4pvbut1c));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(2));
        gm_l12 = t22;
    }
    gm_b16_8 t23 = gm_l12;
    gm_l5 = t23;
    uint8_t t24 = gm_l3;
    if (t24) {
        gm_t9qkh0rsooc8d t25;
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)16ULL));
        gm_t3fcbuehh3bs2 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t26 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t26 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t26 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t26 + 8), t25);
        gm_l13 = t26;
    } else {
        gm_t9qkh0rsooc8d t27;
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)67ULL));
        gm_t3fcbuehh3bs2 t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t28 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t28 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t28 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t28 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t28 + 8), t27);
        gm_l13 = t28;
    }
    memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l13), 32);
    {
        {
            uint64_t t29 = gm_l2;
            uint8_t t30 = (t29 > ((uint64_t)0ULL));
            if (!t30) {
                goto gm_lb2;
            }
            void * t31 = gm_l0;
            void * t32 = GM_LD(void *, (((uint8_t *)t31) + 8));
            uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t31) + 8) + 8));
            uint64_t t34 = gm_l2;
            uint64_t r35; bool o36 = __builtin_sub_overflow(t34, ((uint64_t)1ULL), &r35);
            if (o36) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 335, 55);
            uint64_t t37 = r35;
            if ((uintptr_t)t37 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t37, t33, gm_srcvmmksi0829pa, 335, 48);
            uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t32) + (uintptr_t)t37 * 1));
            uint8_t t39 = gm_f_is_ascii_space_t2ltvst(t38);
            uint8_t t40 = (uint8_t)!t39;
            if (!t40) {
                goto gm_lb2;
            }
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s3p0rbs9hc3lg));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(1));
            gm_l14 = t41;
            goto gm_lb1;
        }
gm_lb2:;
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(0));
        gm_l14 = t42;
    }
gm_lb1:;
    gm_b16_8 t43 = gm_l14;
    gm_l7 = t43;
    gm_b16_8 t44;
    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(0));
    gm_l8 = t44;
    {
        {
            void * t45 = gm_l0;
            gm_tqs5p4n2tq19v t46 = gm_f_peek_72bostn(t45);
            gm_l15 = t46;
            uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l15))[1] != 0);
            if (!t47) {
                goto gm_lb4;
            }
            uint8_t t48 = GM_LD(uint8_t, ((uint8_t *)&gm_l15));
            gm_l9 = t48;
            uint8_t t49 = gm_l9;
            uint8_t t50 = gm_f_is_ascii_space_t2ltvst(t49);
            uint8_t t51 = (uint8_t)!t50;
            if (t51) {
                gm_b16_8 t52;
                GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s3p0rbs9hc3lg));
                GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(1));
                gm_l8 = t52;
            }
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t53 = gm_l0;
    uint64_t t54 = gm_l2;
    void * t55 = gm_l0;
    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 24));
    gm_tl874t2atnlh3 t57 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t53, t54, t56));
    gm_b16_8 t58 = gm_l5;
    gm_b16_8 t59 = gm_l4;
    gm_t6j6smj7qoctu t60;
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t58);
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 16), t59);
    gm_f_format_g637ghi((void *)&gm_l16, (void *)NULL, cx, NULL, &t60);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcvmmksi0829pa, 340, 42);
    gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
    gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
    gm_t4gut2c9fj1kt t64;
    gm_f_error_s6hn871((void *)&t64, (void *)NULL, cx, NULL, t57, t63);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l10 = t64;
    void * t65 = (void *)((uint8_t *)&F.l10);
    gm_b16_8 t66 = gm_l4;
    gm_tnpifv20irnph t67;
    GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t66);
    gm_f_format_ooibcq2((void *)&gm_l17, (void *)NULL, cx, NULL, t67);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcvmmksi0829pa, 341, 15);
    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
    void * t71 = gm_l0;
    uint64_t t72 = gm_l2;
    void * t73 = gm_l0;
    uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)t73) + 24));
    gm_tl874t2atnlh3 t75 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t71, t72, t74));
    gm_b16_8 t76 = gm_l7;
    gm_b16_8 t77 = gm_l4;
    gm_b16_8 t78 = gm_l8;
    gm_tfeab279c16rr t79;
    GM_ST(gm_b16_8, ((uint8_t *)&t79 + 0), t76);
    GM_ST(gm_b16_8, ((uint8_t *)&t79 + 16), t77);
    GM_ST(gm_b16_8, ((uint8_t *)&t79 + 32), t78);
    gm_f_format_dsta2ql((void *)&gm_l18, (void *)NULL, cx, NULL, &t79);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcvmmksi0829pa, 341, 67);
    gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
    gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
    gm_b0_1 t83 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t65, t70, t75, t82);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t84 = gm_l0;
    void * t85 = GM_LD(void *, (((uint8_t *)t84) + 32));
    gm_f_push_o4ckdr1(t85, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l10)));
    void * t86 = gm_l0;
    uint64_t t87 = gm_l2;
    void * t88 = gm_l0;
    uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)t88) + 24));
    gm_b0_1 t90 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t86, &(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l6)), t87, t89);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_close_all_brackets_bql4j88(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_close_all_brackets_bql4j88 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_tl874t2atnlh3 gm_l2; /* here */
    gm_t2eb59dvglnle gm_l3; /* b */
    void * gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* open */
    GM_STACK_CHECK(&F, 817u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tu1ap1uj3psaf gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tftb1odc739t5 gm_l10;
    void * t5 = gm_l0;
    gm_toceffb697p0u t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(gm_toceffb697p0u, (((uint8_t *)t5) + 224), t6);
    void * t7 = gm_l0;
    uint64_t t8 = gm_l1;
    uint64_t t9 = gm_l1;
    gm_tl874t2atnlh3 t10 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t7, t8, t9));
    gm_l2 = t10;
    {
        for (;;) {
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 120);
            gm_f_pop_1q0s3gu((void *)&gm_l7, t12);
            uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
            if (!t13) {
                goto gm_lb1;
            }
            gm_t2eb59dvglnle t14 = GM_LD(gm_t2eb59dvglnle, ((uint8_t *)&gm_l7));
            gm_l3 = t14;
            {
                void * t15 = gm_l0;
                gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l3));
                uint64_t t17 = gm_f_slot_4oe07nr(t16);
                if ((uintptr_t)t17 >= (uintptr_t)(((uint64_t)3ULL))) gm_trap_index((uintptr_t)t17, ((uint64_t)3ULL), gm_srcvmmksi0829pa, 614, 9);
                void * t18 = (void *)((((uint8_t *)t15) + 160) + (uintptr_t)t17 * 8);
                gm_l4 = t18;
                void * t19 = gm_l4;
                void * t20 = gm_l4;
                uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)t20));
                uint64_t r22; bool o23 = __builtin_sub_overflow(t21, ((uint64_t)1ULL), &r22);
                if (o23) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 614, 9);
                uint64_t t24 = r22;
                GM_ST(uint64_t, ((uint8_t *)t19), t24);
                gm_t9qkh0rsooc8d t25 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l3));
                gm_t9qkh0rsooc8d t26 = gm_f_opener_8hqhoc3(t25);
                gm_b16_8 t27 = gm_f_punct_text_ov4r40u(t26);
                gm_l5 = t27;
                gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l3) + 4));
                gm_b16_8 t29 = gm_l5;
                gm_tnpifv20irnph t30;
                GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t29);
                gm_f_format_t32jdqr((void *)&gm_l8, (void *)NULL, cx, NULL, t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcvmmksi0829pa, 616, 33);
                gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                gm_t4gut2c9fj1kt t34;
                gm_f_error_s6hn871((void *)&t34, (void *)NULL, cx, NULL, t28, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l6 = t34;
                void * t35 = (void *)((uint8_t *)&F.l6);
                gm_tl874t2atnlh3 t36 = gm_l2;
                gm_b16_8 t37 = gm_l5;
                gm_tnpifv20irnph t38;
                GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t37);
                gm_f_format_d7osfot((void *)&gm_l9, (void *)NULL, cx, NULL, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcvmmksi0829pa, 617, 27);
                gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                gm_f_add_label_50fj7vv(t35, GM_CONV(gm_cseahvg8v7p0vs, t36), t41);
                void * t42 = gm_l0;
                void * t43 = GM_LD(void *, (((uint8_t *)t42) + 32));
                gm_f_push_o4ckdr1(t43, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l6)));
                void * t44 = gm_l0;
                void * t45 = (void *)(((uint8_t *)t44) + 40);
                gm_t9qkh0rsooc8d t46 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l3));
                gm_t9qkh0rsooc8d t47 = gm_f_closer_fus0off(t46);
                gm_tl874t2atnlh3 t48 = gm_l2;
                gm_t3fcbuehh3bs2 t49;
                GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t49 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t49 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t49 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t49 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t49 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t49 + 8), t47);
                gm_tm4pc0emhrv1g t50;
                GM_ST(uint32_t, (uint8_t *)&t50 + 44, 0);
                GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t50 + 0), t49);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t50 + 32), t48);
                gm_tftb1odc739t5 t51 = gm_f_push_pnf14h1(t45, &t50);
                gm_l10 = t51;
                uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcvmmksi0829pa, 619, 9);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t53 = gm_l0;
    void * t54 = (void *)(((uint8_t *)t53) + 184);
    gm_f_clear_4qp22g2(t54);
    void * t55 = gm_l0;
    gm_tl874t2atnlh3 t56 = gm_l2;
    gm_f_push_newline_ke0tojq(t55, GM_CONV(gm_cseahvg8v7p0vs, t56));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_close_bracket_tbjprpe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_close_bracket_tbjprpe F;
    void * gm_l0; /* lx */
    gm_t9qkh0rsooc8d gm_l1; /* c */
    gm_tl874t2atnlh3 gm_l2; /* at */
    gm_b16_8 gm_l3; /* items */
    uint64_t gm_l4; /* i */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    gm_t2eb59dvglnle gm_l8; /* skipped */
    void * gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* open */
    void * gm_l12; /* <tmp> */
    uint64_t gm_l13; /* depth */
    uint64_t gm_l14; /* n */
    GM_STACK_CHECK(&F, 1054u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    uint8_t gm_l17;
    void * t13 = gm_l0;
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 120));
    gm_l3 = t14;
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
    if (o17) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 634, 10);
    uint64_t t18 = r16;
    gm_l4 = t18;
    {
        for (;;) {
            uint64_t t19 = gm_l4;
            uint8_t t20 = (t19 > ((uint64_t)0ULL));
            if (!t20) {
                goto gm_lb1;
            }
            void * t21 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t23 = gm_l4;
            if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcvmmksi0829pa, 635, 28);
            gm_t9qkh0rsooc8d t24 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t21) + (uintptr_t)t23 * 16));
            gm_t9qkh0rsooc8d t25 = gm_f_closer_fus0off(t24);
            gm_t9qkh0rsooc8d t26 = gm_l1;
            uint8_t t27 = (!gm_eqv3tj1pu43g(((uint8_t *)&t25), ((uint8_t *)&t26)));
            if (!t27) {
                goto gm_lb1;
            }
            {
                uint64_t t28 = gm_l4;
                uint64_t r29; bool o30 = __builtin_sub_overflow(t28, ((uint64_t)1ULL), &r29);
                if (o30) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 635, 54);
                uint64_t t31 = r29;
                gm_l4 = t31;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t32 = gm_l4;
    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
    if (o34) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 636, 14);
    uint64_t t35 = r33;
    gm_l6 = t35;
    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    gm_l5 = t36;
    {
        for (;;) {
            uint64_t t37 = gm_l6;
            uint64_t t38 = gm_l5;
            uint8_t t39 = (t37 < t38);
            if (!t39) {
                goto gm_lb4;
            }
            {
                {
                    {
                        uint64_t t40 = gm_l6;
                        gm_l7 = t40;
                        void * t41 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t43 = gm_l7;
                        gm_t2eb59dvglnle t44 = GM_LD(gm_t2eb59dvglnle, (((uint8_t *)t41) + (uintptr_t)t43 * 16));
                        gm_l8 = t44;
                        void * t45 = gm_l0;
                        gm_t9qkh0rsooc8d t46 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l8));
                        uint64_t t47 = gm_f_slot_4oe07nr(t46);
                        if ((uintptr_t)t47 >= (uintptr_t)(((uint64_t)3ULL))) gm_trap_index((uintptr_t)t47, ((uint64_t)3ULL), gm_srcvmmksi0829pa, 638, 9);
                        void * t48 = (void *)((((uint8_t *)t45) + 160) + (uintptr_t)t47 * 8);
                        gm_l9 = t48;
                        void * t49 = gm_l9;
                        void * t50 = gm_l9;
                        uint64_t t51 = GM_LD(uint64_t, ((uint8_t *)t50));
                        uint64_t r52; bool o53 = __builtin_sub_overflow(t51, ((uint64_t)1ULL), &r52);
                        if (o53) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 638, 9);
                        uint64_t t54 = r52;
                        GM_ST(uint64_t, ((uint8_t *)t49), t54);
                        gm_t9qkh0rsooc8d t55 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l8));
                        gm_t9qkh0rsooc8d t56 = gm_f_opener_8hqhoc3(t55);
                        gm_b16_8 t57 = gm_f_punct_text_ov4r40u(t56);
                        gm_l10 = t57;
                        gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l8) + 4));
                        gm_b16_8 t59 = gm_l10;
                        gm_tnpifv20irnph t60;
                        GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t59);
                        gm_f_format_t32jdqr((void *)&gm_l15, (void *)NULL, cx, NULL, t60);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                        if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcvmmksi0829pa, 640, 39);
                        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
                        gm_t4gut2c9fj1kt t64;
                        gm_f_error_s6hn871((void *)&t64, (void *)NULL, cx, NULL, t58, t63);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l11 = t64;
                        void * t65 = (void *)((uint8_t *)&F.l11);
                        gm_tl874t2atnlh3 t66 = gm_l2;
                        gm_t9qkh0rsooc8d t67 = gm_l1;
                        gm_b16_8 t68 = gm_f_punct_text_ov4r40u(t67);
                        gm_b16_8 t69 = gm_l10;
                        gm_t6j6smj7qoctu t70;
                        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t68);
                        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 16), t69);
                        gm_f_format_7a5f58b((void *)&gm_l16, (void *)NULL, cx, NULL, &t70);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                        if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcvmmksi0829pa, 641, 25);
                        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                        gm_f_add_label_50fj7vv(t65, GM_CONV(gm_cseahvg8v7p0vs, t66), t73);
                        void * t74 = gm_l0;
                        void * t75 = GM_LD(void *, (((uint8_t *)t74) + 32));
                        gm_f_push_o4ckdr1(t75, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l11)));
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 636, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t76 = gm_l6;
            uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
            uint64_t t79 = r77;
            gm_l6 = t79;
        }
    }
gm_lb4:;
    void * t80 = gm_l0;
    gm_t9qkh0rsooc8d t81 = gm_l1;
    uint64_t t82 = gm_f_closer_slot_f4sb6v0(t81);
    if ((uintptr_t)t82 >= (uintptr_t)(((uint64_t)3ULL))) gm_trap_index((uintptr_t)t82, ((uint64_t)3ULL), gm_srcvmmksi0829pa, 644, 5);
    void * t83 = (void *)((((uint8_t *)t80) + 160) + (uintptr_t)t82 * 8);
    gm_l12 = t83;
    void * t84 = gm_l12;
    void * t85 = gm_l12;
    uint64_t t86 = GM_LD(uint64_t, ((uint8_t *)t85));
    uint64_t r87; bool o88 = __builtin_sub_overflow(t86, ((uint64_t)1ULL), &r87);
    if (o88) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 644, 5);
    uint64_t t89 = r87;
    GM_ST(uint64_t, ((uint8_t *)t84), t89);
    void * t90 = gm_l0;
    void * t91 = (void *)(((uint8_t *)t90) + 120);
    uint64_t t92 = gm_l4;
    gm_f_truncate_h4ov51t(t91, t92);
    uint64_t t93 = gm_l4;
    gm_l13 = t93;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb9;
            }
            {
                void * t94 = gm_l0;
                uint64_t t95 = GM_LD(uint64_t, ((((uint8_t *)t94) + 184) + 8));
                gm_l14 = t95;
                uint64_t t96 = gm_l14;
                uint8_t t97 = ((t96 == ((uint64_t)0ULL)));
                if (t97) {
                    gm_l17 = ((uint8_t)1);
                } else {
                    void * t98 = gm_l0;
                    void * t99 = GM_LD(void *, (((uint8_t *)t98) + 184));
                    uint64_t t100 = GM_LD(uint64_t, ((((uint8_t *)t98) + 184) + 8));
                    uint64_t t101 = gm_l14;
                    uint64_t r102; bool o103 = __builtin_sub_overflow(t101, ((uint64_t)1ULL), &r102);
                    if (o103) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 649, 47);
                    uint64_t t104 = r102;
                    if ((uintptr_t)t104 >= (uintptr_t)(t100)) gm_trap_index((uintptr_t)t104, t100, gm_srcvmmksi0829pa, 649, 22);
                    uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)t99) + (uintptr_t)t104 * 8));
                    uint64_t t106 = gm_l13;
                    uint8_t t107 = (t105 <= t106);
                    gm_l17 = t107;
                }
                uint8_t t108 = gm_l17;
                if (t108) {
                    goto gm_lb9;
                }
                void * t109 = gm_l0;
                void * t110 = (void *)(((uint8_t *)t109) + 184);
                gm_tf5srh4mp4b3i t111 = gm_f_pop_j4rpvm5(t110);
            }
gm_lb11:;
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_closer_fus0off(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_closer_fus0off F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d gm_l1;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)0ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)62ULL));
            gm_l1 = t11;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)1ULL));
            if (!t13) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)64ULL));
            gm_l1 = t14;
            goto gm_lb1;
        }
gm_lb3:;
        {
            {
                {
                    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t16 = (uint8_t)(t15 == ((uint8_t)2ULL));
                    if (!t16) {
                        goto gm_lb6;
                    }
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)3ULL));
                    if (!t18) {
                        goto gm_lb7;
                    }
                    goto gm_lb5;
                }
gm_lb7:;
                goto gm_lb4;
            }
gm_lb5:;
            gm_t9qkh0rsooc8d t19;
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)66ULL));
            gm_l1 = t19;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 53, 42);
    }
gm_lb1:;
    gm_t9qkh0rsooc8d t20 = gm_l1;
    F.ret = t20;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint64_t gm_f_closer_slot_f4sb6v0(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_closing_na3a1ur(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_closing_na3a1ur F;
    gm_t3fcbuehh3bs2 gm_l0; /* k */
    gm_t9qkh0rsooc8d gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_taari1s0m51a7 gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)8ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l1 = t7;
            gm_t9qkh0rsooc8d t8 = gm_l1;
            gm_t9qkh0rsooc8d t9;
            GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)62ULL));
            gm_t9qkh0rsooc8d t10;
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)64ULL));
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)66ULL));
            gm_tk22flr9mh3ql t12;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 0), t9);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 1), t10);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 2), t11);
            uint8_t t13 = 0;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t12) + 0, ((uint8_t *)&t8))) t13 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t12) + 1, ((uint8_t *)&t8))) t13 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t12) + 2, ((uint8_t *)&t8))) t13 = 1;
            uint8_t t14 = (uint8_t)(t13);
            if (!t14) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t15 = gm_l1;
            gm_taari1s0m51a7 t16;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[1] = 1;
            gm_l2 = t16;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_taari1s0m51a7 t17;
            memset(&t17, 0, sizeof t17);
            gm_l2 = t17;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 74, 42);
    }
gm_lb1:;
    gm_taari1s0m51a7 t18 = gm_l2;
    F.ret = t18;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_closing_na3a1ur_c(gm_t3fcbuehh3bs2 a0) { return gm_f_closing_na3a1ur(&a0); }
extern __attribute__((aligned(4))) gm_cskjv6t053tkje gm_f_current_char_ctktsl2(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_declaration_starts_at_mp730g1(void * a0, uint64_t a1, const void * a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_declaration_starts_at_mp730g1 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_t3fcbuehh3bs2 gm_l2; /* kind */
    uint64_t gm_l3; /* i */
    gm_b16_8 gm_l4; /* word */
    gm_b16_8 gm_l5; /* after */
    gm_t9qkh0rsooc8d gm_l6; /* k */
    GM_STACK_CHECK(&F, 706u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    {
        {
            uint64_t t15 = gm_l1;
            uint8_t t16 = (!(t15 == ((uint64_t)0ULL)));
            if (!t16) {
                goto gm_lb2;
            }
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 8));
            uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)t17) + 8) + 8));
            uint64_t t20 = gm_l1;
            uint64_t r21; bool o22 = __builtin_sub_overflow(t20, ((uint64_t)1ULL), &r21);
            if (o22) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 592, 31);
            uint64_t t23 = r21;
            if ((uintptr_t)t23 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t23, t19, gm_srcvmmksi0829pa, 592, 24);
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t18) + (uintptr_t)t23 * 1));
            uint8_t t25 = (!(t24 == ((uint8_t)10ULL)));
            if (!t25) {
                goto gm_lb2;
            }
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    uint64_t t26 = gm_l1;
    gm_l3 = t26;
    {
        for (;;) {
            uint64_t t27 = gm_l3;
            void * t28 = gm_l0;
            uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)t28) + 8) + 8));
            uint8_t t30 = (t27 < t29);
            if (!t30) {
                goto gm_lb3;
            }
            void * t31 = gm_l0;
            void * t32 = GM_LD(void *, (((uint8_t *)t31) + 8));
            uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t31) + 8) + 8));
            uint64_t t34 = gm_l3;
            if ((uintptr_t)t34 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t34, t33, gm_srcvmmksi0829pa, 594, 51);
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)t32) + (uintptr_t)t34 * 1));
            uint8_t t36 = gm_f_is_ident_cont_317fpdb(t35);
            if (!t36) {
                goto gm_lb3;
            }
            {
                uint64_t t37 = gm_l3;
                uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                if (o39) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 594, 64);
                uint64_t t40 = r38;
                gm_l3 = t40;
            }
gm_lb5:;
        }
    }
gm_lb3:;
    void * t41 = gm_l0;
    void * t42 = GM_LD(void *, (((uint8_t *)t41) + 8));
    uint64_t t43 = GM_LD(uint64_t, ((((uint8_t *)t41) + 8) + 8));
    uint64_t t44 = gm_l1;
    uint64_t t45 = gm_l3;
    uint64_t t46 = (uintptr_t)t44;
    uint64_t t47 = (uintptr_t)t45;
    if (t46 > t47 || t47 > (uintptr_t)t43) gm_trap_slice(t46, t47, t43, gm_srcvmmksi0829pa, 595, 13);
    uint64_t t48 = ((uintptr_t)t45) - (uintptr_t)t44;
    gm_b16_8 t49;
    GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t42 + (uintptr_t)t44 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(t48));
    gm_l4 = t49;
    {
        for (;;) {
            uint64_t t50 = gm_l3;
            void * t51 = gm_l0;
            uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)t51) + 8) + 8));
            uint8_t t53 = (t50 < t52);
            if (!t53) {
                goto gm_lb6;
            }
            void * t54 = gm_l0;
            void * t55 = GM_LD(void *, (((uint8_t *)t54) + 8));
            uint64_t t56 = GM_LD(uint64_t, ((((uint8_t *)t54) + 8) + 8));
            uint64_t t57 = gm_l3;
            if ((uintptr_t)t57 >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)t57, t56, gm_srcvmmksi0829pa, 596, 30);
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)t57 * 1));
            gm_tqs5p4n2tq19v t59;
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)32ULL));
            GM_ST(uint8_t, ((uint8_t *)&t59 + 1), ((uint8_t)9ULL));
            uint8_t t60 = 0;
            if (gm_eqjmccqdb90f(((uint8_t *)&t59) + 0, ((uint8_t *)&t58))) t60 = 1;
            if (gm_eqjmccqdb90f(((uint8_t *)&t59) + 1, ((uint8_t *)&t58))) t60 = 1;
            uint8_t t61 = (uint8_t)(t60);
            if (!t61) {
                goto gm_lb6;
            }
            {
                uint64_t t62 = gm_l3;
                uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                if (o64) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 596, 57);
                uint64_t t65 = r63;
                gm_l3 = t65;
            }
gm_lb8:;
        }
    }
gm_lb6:;
    void * t66 = gm_l0;
    void * t67 = GM_LD(void *, (((uint8_t *)t66) + 8));
    uint64_t t68 = GM_LD(uint64_t, ((((uint8_t *)t66) + 8) + 8));
    uint64_t t69 = gm_l3;
    uint64_t t70 = (uintptr_t)t69;
    uint64_t t71 = (uintptr_t)t68;
    if (t70 > t71 || t71 > (uintptr_t)t68) gm_trap_slice(t70, t71, t68, gm_srcvmmksi0829pa, 597, 14);
    uint64_t t72 = ((uintptr_t)t68) - (uintptr_t)t69;
    gm_b16_8 t73;
    GM_ST(void *, (uint8_t *)&t73, (void *)(((uint8_t *)t67 + (uintptr_t)t69 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(t72));
    gm_l5 = t73;
    {
        {
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            uint8_t t75 = (uint8_t)(t74 == ((uint8_t)7ULL));
            if (!t75) {
                goto gm_lb10;
            }
            gm_l7 = ((uint8_t)1);
            goto gm_lb9;
        }
gm_lb10:;
        {
            uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            uint8_t t77 = (uint8_t)(t76 == ((uint8_t)2ULL));
            if (!t77) {
                goto gm_lb11;
            }
            gm_t9qkh0rsooc8d t78 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a2) + 8));
            gm_l6 = t78;
            gm_t9qkh0rsooc8d t79 = gm_l6;
            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&t79) + 0));
            uint8_t t81 = (uint8_t)((t80 == ((uint8_t)21ULL)));
            gm_t9qkh0rsooc8d t82;
            GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)21ULL));
            gm_l7 = t81;
            goto gm_lb9;
        }
gm_lb11:;
        {
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            uint8_t t84 = (uint8_t)(t83 == ((uint8_t)0ULL));
            if (!t84) {
                goto gm_lb12;
            }
            gm_b16_8 t85 = gm_l5;
            gm_b16_8 t86;
            GM_ST(void *, (uint8_t *)&t86, (void *)(gm_ss67f0gmfen1m));
            GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(2));
            uint8_t t87 = gm_f_starts_with_6vvt01e(t85, t86);
            if (t87) {
                gm_l9 = ((uint8_t)1);
            } else {
                gm_b16_8 t88 = gm_l4;
                gm_b16_8 t89;
                GM_ST(void *, (uint8_t *)&t89, (void *)(gm_s9r64t1166q1f));
                GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(4));
                gm_b16_8 t90;
                GM_ST(void *, (uint8_t *)&t90, (void *)(gm_srfagufvviad7));
                GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(5));
                gm_t6j6smj7qoctu t91;
                GM_ST(gm_b16_8, ((uint8_t *)&t91 + 0), t89);
                GM_ST(gm_b16_8, ((uint8_t *)&t91 + 16), t90);
                uint8_t t92 = 0;
                if (gm_eq0iqiubfm7e(((uint8_t *)&t91) + 0, ((uint8_t *)&t88))) t92 = 1;
                if (gm_eq0iqiubfm7e(((uint8_t *)&t91) + 16, ((uint8_t *)&t88))) t92 = 1;
                uint8_t t93 = (uint8_t)(t92);
                if (t93) {
                    gm_b16_8 t94 = gm_l5;
                    gm_b16_8 t95;
                    GM_ST(void *, (uint8_t *)&t95, (void *)(gm_sb54ui2amic32));
                    GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(1));
                    uint8_t t96 = gm_f_starts_with_6vvt01e(t94, t95);
                    gm_l10 = t96;
                } else {
                    gm_l10 = ((uint8_t)0);
                }
                uint8_t t97 = gm_l10;
                gm_l9 = t97;
            }
            uint8_t t98 = gm_l9;
            if (t98) {
                gm_l8 = ((uint8_t)1);
            } else {
                gm_b16_8 t99 = gm_l4;
                gm_b16_8 t100;
                GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sic5vsema0r36));
                GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(7));
                uint8_t t101 = (gm_eq0iqiubfm7e(((uint8_t *)&t99), ((uint8_t *)&t100)));
                if (t101) {
                    uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t103 = (t102 > ((uint64_t)0ULL));
                    gm_l12 = t103;
                } else {
                    gm_l12 = ((uint8_t)0);
                }
                uint8_t t104 = gm_l12;
                if (t104) {
                    void * t105 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t106)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t106, gm_srcvmmksi0829pa, 603, 62);
                    uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)t105) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t108 = ((t107 == ((uint8_t)40ULL)));
                    if (t108) {
                        gm_l13 = ((uint8_t)1);
                    } else {
                        void * t109 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t110)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t110, gm_srcvmmksi0829pa, 603, 103);
                        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)t109) + (uintptr_t)((uint64_t)0ULL) * 1));
                        uint8_t t112 = gm_f_is_ident_start_rjefpfm(t111);
                        gm_l13 = t112;
                    }
                    uint8_t t113 = gm_l13;
                    gm_l11 = t113;
                } else {
                    gm_l11 = ((uint8_t)0);
                }
                uint8_t t114 = gm_l11;
                gm_l8 = t114;
            }
            uint8_t t115 = gm_l8;
            gm_l7 = t115;
            goto gm_lb9;
        }
gm_lb12:;
        {
            gm_l7 = ((uint8_t)0);
            goto gm_lb9;
        }
gm_lb13:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 598, 12);
    }
gm_lb9:;
    uint8_t t116 = gm_l7;
    F.ret = t116;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_declaration_starts_at_mp730g1_c(void * a0, uint64_t a1, gm_t3fcbuehh3bs2 a2) { return gm_f_declaration_starts_at_mp730g1(a0, a1, &a2); }
__attribute__((aligned(4))) gm_b0_1 gm_f_emit_ekvv703(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint64_t a3) {
    struct gm_fr_emit_ekvv703 F;
    void * gm_l0; /* lx */
    gm_t3fcbuehh3bs2 gm_l1; /* kind */
    uint64_t gm_l2; /* lo */
    uint64_t gm_l3; /* hi */
    gm_tl874t2atnlh3 gm_l4; /* here */
    gm_t9qkh0rsooc8d gm_l5; /* c */
    uint64_t gm_l6; /* depth */
    uint64_t gm_l7; /* n */
    uint8_t gm_l8; /* is_match_body */
    gm_t9qkh0rsooc8d gm_l9; /* p */
    GM_STACK_CHECK(&F, 754u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_taari1s0m51a7 gm_l10;
    gm_tabb710d9mq2e gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    gm_t9qkh0rsooc8d gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    void * t16 = gm_l0;
    uint64_t t17 = gm_l2;
    uint64_t t18 = gm_l3;
    gm_tl874t2atnlh3 t19 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t16, t17, t18));
    gm_l4 = t19;
    {
        {
            gm_taari1s0m51a7 t20 = gm_f_closing_na3a1ur(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)));
            gm_l10 = t20;
            uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
            if (!t21) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t22 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l10));
            gm_l5 = t22;
            void * t23 = gm_l0;
            gm_t9qkh0rsooc8d t24 = gm_l5;
            uint64_t t25 = gm_f_closer_slot_f4sb6v0(t24);
            if ((uintptr_t)t25 >= (uintptr_t)(((uint64_t)3ULL))) gm_trap_index((uintptr_t)t25, ((uint64_t)3ULL), gm_srcvmmksi0829pa, 563, 12);
            uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t23) + 160) + (uintptr_t)t25 * 8));
            uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
            if (t27) {
                void * t28 = gm_l0;
                uint64_t t29 = gm_l2;
                uint64_t t30 = gm_l3;
                gm_t9qkh0rsooc8d t31 = gm_l5;
                gm_b16_8 t32 = gm_f_punct_text_ov4r40u(t31);
                gm_tnpifv20irnph t33;
                GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
                gm_f_format_daod35i((void *)&gm_l11, (void *)NULL, cx, NULL, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcvmmksi0829pa, 564, 30);
                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                gm_b0_1 t37 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t28, t29, t30, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t38 = gm_l0;
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t38) + 280));
            if (!t39) {
                goto gm_lb4;
            }
            void * t40 = gm_l0;
            uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t40) + 120) + 8));
            uint8_t t42 = (t41 > ((uint64_t)0ULL));
            if (!t42) {
                goto gm_lb4;
            }
            void * t43 = gm_l0;
            uint64_t t44 = gm_l2;
            uint8_t t45 = gm_f_declaration_starts_at_mp730g1(t43, t44, &(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)));
            if (!t45) {
                goto gm_lb4;
            }
            void * t46 = gm_l0;
            uint64_t t47 = gm_l2;
            gm_b0_1 t48 = gm_f_close_all_brackets_bql4j88((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t49 = gm_l0;
    {
        {
            uint8_t t50 = (uint8_t)(((((uint8_t *)t49) + 224))[12] != 0);
            if (t50) {
                goto gm_lb6;
            }
            gm_l12 = ((uint8_t)0);
            goto gm_lb5;
        }
gm_lb6:;
        {
            gm_l12 = ((uint8_t)1);
            goto gm_lb5;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 571, 8);
    }
gm_lb5:;
    uint8_t t51 = gm_l12;
    if (t51) {
        void * t52 = gm_l0;
        uint64_t t53 = gm_l3;
        gm_f_flush_newline_9023ml8(t52, &(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)), t53);
    }
    {
        {
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)8ULL));
            if (!t55) {
                goto gm_lb9;
            }
            uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)&gm_l1) + 8) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)61ULL));
            if (!t57) {
                goto gm_lb9;
            }
            void * t58 = gm_l0;
            gm_tl874t2atnlh3 t59 = gm_l4;
            gm_t9qkh0rsooc8d t60;
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)0ULL));
            gm_f_open_bracket_3kcj4k8(t58, t60, GM_CONV(gm_cseahvg8v7p0vs, t59));
            goto gm_lb8;
        }
gm_lb9:;
        {
            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t62 = (uint8_t)(t61 == ((uint8_t)8ULL));
            if (!t62) {
                goto gm_lb10;
            }
            uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)&gm_l1) + 8) + 0));
            uint8_t t64 = (uint8_t)(t63 == ((uint8_t)63ULL));
            if (!t64) {
                goto gm_lb10;
            }
            void * t65 = gm_l0;
            gm_tl874t2atnlh3 t66 = gm_l4;
            gm_t9qkh0rsooc8d t67;
            GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)1ULL));
            gm_f_open_bracket_3kcj4k8(t65, t67, GM_CONV(gm_cseahvg8v7p0vs, t66));
            goto gm_lb8;
        }
gm_lb10:;
        {
            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t69 = (uint8_t)(t68 == ((uint8_t)8ULL));
            if (!t69) {
                goto gm_lb11;
            }
            uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)&gm_l1) + 8) + 0));
            uint8_t t71 = (uint8_t)(t70 == ((uint8_t)65ULL));
            if (!t71) {
                goto gm_lb11;
            }
            void * t72 = gm_l0;
            uint64_t t73 = GM_LD(uint64_t, ((((uint8_t *)t72) + 120) + 8));
            gm_l6 = t73;
            void * t74 = gm_l0;
            uint64_t t75 = GM_LD(uint64_t, ((((uint8_t *)t74) + 184) + 8));
            gm_l7 = t75;
            uint64_t t76 = gm_l7;
            uint8_t t77 = (t76 > ((uint64_t)0ULL));
            if (t77) {
                void * t78 = gm_l0;
                void * t79 = GM_LD(void *, (((uint8_t *)t78) + 184));
                uint64_t t80 = GM_LD(uint64_t, ((((uint8_t *)t78) + 184) + 8));
                uint64_t t81 = gm_l7;
                uint64_t r82; bool o83 = __builtin_sub_overflow(t81, ((uint64_t)1ULL), &r82);
                if (o83) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 578, 65);
                uint64_t t84 = r82;
                if ((uintptr_t)t84 >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)t84, t80, gm_srcvmmksi0829pa, 578, 40);
                uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)t79) + (uintptr_t)t84 * 8));
                uint64_t t86 = gm_l6;
                uint8_t t87 = ((t85 == t86));
                gm_l13 = t87;
            } else {
                gm_l13 = ((uint8_t)0);
            }
            uint8_t t88 = gm_l13;
            gm_l8 = t88;
            uint8_t t89 = gm_l8;
            if (t89) {
                void * t90 = gm_l0;
                void * t91 = (void *)(((uint8_t *)t90) + 184);
                gm_tf5srh4mp4b3i t92 = gm_f_pop_j4rpvm5(t91);
            }
            void * t93 = gm_l0;
            uint8_t t94 = gm_l8;
            if (t94) {
                gm_t9qkh0rsooc8d t95;
                GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)3ULL));
                gm_l14 = t95;
            } else {
                gm_t9qkh0rsooc8d t96;
                GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)2ULL));
                gm_l14 = t96;
            }
            gm_t9qkh0rsooc8d t97 = gm_l14;
            gm_tl874t2atnlh3 t98 = gm_l4;
            gm_f_open_bracket_3kcj4k8(t93, t97, GM_CONV(gm_cseahvg8v7p0vs, t98));
            goto gm_lb8;
        }
gm_lb11:;
        {
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t100 = (uint8_t)(t99 == ((uint8_t)8ULL));
            if (!t100) {
                goto gm_lb12;
            }
            gm_t9qkh0rsooc8d t101 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 8));
            gm_l9 = t101;
            gm_t9qkh0rsooc8d t102 = gm_l9;
            gm_t9qkh0rsooc8d t103;
            GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)62ULL));
            gm_t9qkh0rsooc8d t104;
            GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)64ULL));
            gm_t9qkh0rsooc8d t105;
            GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)66ULL));
            gm_tk22flr9mh3ql t106;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t106 + 0), t103);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t106 + 1), t104);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t106 + 2), t105);
            uint8_t t107 = 0;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t106) + 0, ((uint8_t *)&t102))) t107 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t106) + 1, ((uint8_t *)&t102))) t107 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t106) + 2, ((uint8_t *)&t102))) t107 = 1;
            uint8_t t108 = (uint8_t)(t107);
            if (!t108) {
                goto gm_lb12;
            }
            void * t109 = gm_l0;
            gm_t9qkh0rsooc8d t110 = gm_l9;
            gm_tl874t2atnlh3 t111 = gm_l4;
            gm_b0_1 t112 = gm_f_close_bracket_tbjprpe((void *)NULL, cx, NULL, t109, t110, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb8;
        }
gm_lb12:;
        {
            uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t114 = (uint8_t)(t113 == ((uint8_t)2ULL));
            if (!t114) {
                goto gm_lb13;
            }
            uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)&gm_l1) + 8) + 0));
            uint8_t t116 = (uint8_t)(t115 == ((uint8_t)17ULL));
            if (!t116) {
                goto gm_lb13;
            }
            void * t117 = gm_l0;
            void * t118 = (void *)(((uint8_t *)t117) + 184);
            void * t119 = gm_l0;
            uint64_t t120 = GM_LD(uint64_t, ((((uint8_t *)t119) + 120) + 8));
            gm_tftb1odc739t5 t121 = gm_f_push_ucgmlo2(t118, t120);
            gm_l15 = t121;
            uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
            if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcvmmksi0829pa, 583, 29);
            goto gm_lb8;
        }
gm_lb13:;
        {
            goto gm_lb8;
        }
gm_lb14:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 572, 5);
    }
gm_lb8:;
    void * t123 = gm_l0;
    void * t124 = (void *)(((uint8_t *)t123) + 40);
    gm_tl874t2atnlh3 t125 = gm_l4;
    gm_t3fcbuehh3bs2 t126 = GM_LD(gm_t3fcbuehh3bs2, ((uint8_t *)&gm_l1));
    gm_tm4pc0emhrv1g t127;
    GM_ST(uint32_t, (uint8_t *)&t127 + 44, 0);
    GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t127 + 0), t126);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t127 + 32), t125);
    gm_tftb1odc739t5 t128 = gm_f_push_pnf14h1(t124, &t127);
    gm_l16 = t128;
    uint8_t t129 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (!t129) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcvmmksi0829pa, 586, 5);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_emit_semicolon_7da5b93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_error_p5r0d56(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_t6ofeqo2h242p gm_f_flavors_qsu4oq0(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_flavors_qsu4oq0 F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t6ofeqo2h242p gm_l1;
    {
        {
            uint8_t t6 = gm_l0;
            uint8_t t7 = ((t6 == ((uint8_t)43ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t8;
            GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)0ULL));
            gm_t9qkh0rsooc8d t9;
            GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)23ULL));
            gm_t9qkh0rsooc8d t10;
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)10ULL));
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)14ULL));
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)18ULL));
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)33ULL));
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)37ULL));
            gm_t6ofeqo2h242p t15;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 0), t8);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t9);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 2), t10);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 3), t11);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 4), t12);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 5), t13);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 6), t14);
            gm_l1 = t15;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t16 = gm_l0;
            uint8_t t17 = ((t16 == ((uint8_t)45ULL)));
            if (!t17) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t18;
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)1ULL));
            gm_t9qkh0rsooc8d t19;
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)24ULL));
            gm_t9qkh0rsooc8d t20;
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)11ULL));
            gm_t9qkh0rsooc8d t21;
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)15ULL));
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)19ULL));
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)34ULL));
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)38ULL));
            gm_t6ofeqo2h242p t25;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 0), t18);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 1), t19);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 2), t20);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 3), t21);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 4), t22);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 5), t23);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 6), t24);
            gm_l1 = t25;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)2ULL));
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)25ULL));
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)12ULL));
            gm_t9qkh0rsooc8d t29;
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)16ULL));
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)20ULL));
            gm_t9qkh0rsooc8d t31;
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)35ULL));
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)39ULL));
            gm_t6ofeqo2h242p t33;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 0), t26);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 1), t27);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 2), t28);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 3), t29);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 4), t30);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 5), t31);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 6), t32);
            gm_l1 = t33;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 542, 36);
    }
gm_lb1:;
    gm_t6ofeqo2h242p t34 = gm_l1;
    F.ret = t34;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_flush_newline_9023ml8(void * a0, const void * a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_flush_newline_9023ml8 F;
    void * gm_l0; /* lx */
    gm_t3fcbuehh3bs2 gm_l1; /* next */
    uint64_t gm_l2; /* next_hi */
    gm_tl874t2atnlh3 gm_l3; /* at */
    uint64_t gm_l4; /* depth */
    gm_taari1s0m51a7 gm_l5; /* innermost */
    uint64_t gm_l6; /* n */
    uint8_t gm_l7; /* dot_name */
    GM_STACK_CHECK(&F, 498u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_tl874t2atnlh3 gm_l8;
    gm_taari1s0m51a7 gm_l9;
    uint8_t gm_l10;
    gm_tftb1odc739t5 gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    void * t2 = gm_l0;
    uint8_t t3 = (uint8_t)(((((uint8_t *)t2) + 224))[12] != 0);
    if (t3) {
        gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 224));
        gm_l8 = t4;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_tl874t2atnlh3 t5 = gm_l8;
    gm_l3 = t5;
    void * t6 = gm_l0;
    gm_toceffb697p0u t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(gm_toceffb697p0u, (((uint8_t *)t6) + 224), t7);
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 120) + 8));
    gm_l4 = t9;
    uint64_t t10 = gm_l4;
    uint8_t t11 = (t10 > ((uint64_t)0ULL));
    if (t11) {
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 120));
        uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 120) + 8));
        uint64_t t15 = gm_l4;
        uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 673, 64);
        uint64_t t18 = r16;
        if ((uintptr_t)t18 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t18, t14, gm_srcvmmksi0829pa, 673, 46);
        gm_t9qkh0rsooc8d t19 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t13) + (uintptr_t)t18 * 16));
        gm_taari1s0m51a7 t20;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t20), t19);
        ((uint8_t *)&t20)[1] = 1;
        gm_l9 = t20;
    } else {
        gm_taari1s0m51a7 t21;
        memset(&t21, 0, sizeof t21);
        gm_l9 = t21;
    }
    gm_taari1s0m51a7 t22 = gm_l9;
    gm_l5 = t22;
    gm_taari1s0m51a7 t23 = gm_l5;
    gm_t9qkh0rsooc8d t24;
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)0ULL));
    gm_taari1s0m51a7 t25;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25), t24);
    ((uint8_t *)&t25)[1] = 1;
    uint8_t t26 = (gm_eqq6nrh55uon(((uint8_t *)&t23), ((uint8_t *)&t25)));
    if (t26) {
        gm_l10 = ((uint8_t)1);
    } else {
        gm_taari1s0m51a7 t27 = gm_l5;
        gm_t9qkh0rsooc8d t28;
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)1ULL));
        gm_taari1s0m51a7 t29;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29), t28);
        ((uint8_t *)&t29)[1] = 1;
        uint8_t t30 = (gm_eqq6nrh55uon(((uint8_t *)&t27), ((uint8_t *)&t29)));
        gm_l10 = t30;
    }
    uint8_t t31 = gm_l10;
    if (t31) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t32 = gm_l0;
    uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t32) + 40) + 8));
    gm_l6 = t33;
    uint64_t t34 = gm_l6;
    uint8_t t35 = ((t34 == ((uint64_t)0ULL)));
    if (t35) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t36 = gm_l0;
    void * t37 = GM_LD(void *, (((uint8_t *)t36) + 40));
    uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)t36) + 40) + 8));
    uint64_t t39 = gm_l6;
    uint64_t r40; bool o41 = __builtin_sub_overflow(t39, ((uint64_t)1ULL), &r40);
    if (o41) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 677, 49);
    uint64_t t42 = r40;
    if ((uintptr_t)t42 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t42, t38, gm_srcvmmksi0829pa, 677, 33);
    uint8_t t43 = gm_f_suppresses_newline_after_o08ord2(&(*(gm_t3fcbuehh3bs2 *)(((uint8_t *)t37) + (uintptr_t)t42 * 48)));
    if (t43) {
        void * t44 = gm_l0;
        void * t45 = (void *)(((uint8_t *)t44) + 240);
        uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        gm_tftb1odc739t5 t47 = gm_f_push_im9d4ro(t45, t46);
        gm_l11 = t47;
        uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcvmmksi0829pa, 678, 9);
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_taari1s0m51a7 t49 = gm_l5;
    gm_t9qkh0rsooc8d t50;
    GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)3ULL));
    gm_taari1s0m51a7 t51;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t51), t50);
    ((uint8_t *)&t51)[1] = 1;
    uint8_t t52 = (!gm_eqq6nrh55uon(((uint8_t *)&t49), ((uint8_t *)&t51)));
    if (t52) {
        gm_t9qkh0rsooc8d t53;
        GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)58ULL));
        uint8_t t54 = gm_f_is_punct_fdeminj(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)), t53);
        if (t54) {
            uint64_t t55 = gm_l2;
            void * t56 = gm_l0;
            uint64_t t57 = GM_LD(uint64_t, ((((uint8_t *)t56) + 8) + 8));
            uint8_t t58 = (t55 < t57);
            gm_l14 = t58;
        } else {
            gm_l14 = ((uint8_t)0);
        }
        uint8_t t59 = gm_l14;
        if (t59) {
            void * t60 = gm_l0;
            void * t61 = GM_LD(void *, (((uint8_t *)t60) + 8));
            uint64_t t62 = GM_LD(uint64_t, ((((uint8_t *)t60) + 8) + 8));
            uint64_t t63 = gm_l2;
            if ((uintptr_t)t63 >= (uintptr_t)(t62)) gm_trap_index((uintptr_t)t63, t62, gm_srcvmmksi0829pa, 683, 92);
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)t61) + (uintptr_t)t63 * 1));
            uint8_t t65 = gm_f_is_ident_start_rjefpfm(t64);
            gm_l13 = t65;
        } else {
            gm_l13 = ((uint8_t)0);
        }
        uint8_t t66 = gm_l13;
        if (t66) {
            void * t67 = gm_l0;
            void * t68 = GM_LD(void *, (((uint8_t *)t67) + 8));
            uint64_t t69 = GM_LD(uint64_t, ((((uint8_t *)t67) + 8) + 8));
            uint64_t t70 = gm_l2;
            uint64_t t71 = (uintptr_t)t70;
            uint64_t t72 = (uintptr_t)t69;
            if (t71 > t72 || t72 > (uintptr_t)t69) gm_trap_slice(t71, t72, t69, gm_srcvmmksi0829pa, 684, 32);
            uint64_t t73 = ((uintptr_t)t69) - (uintptr_t)t70;
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(((uint8_t *)t68 + (uintptr_t)t70 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(t73));
            uint8_t t75 = gm_f_line_binds_jm0pnul(t74);
            uint8_t t76 = (uint8_t)!t75;
            gm_l12 = t76;
        } else {
            gm_l12 = ((uint8_t)0);
        }
        uint8_t t77 = gm_l12;
        gm_l7 = t77;
        uint8_t t78 = gm_l7;
        if (t78) {
            gm_l15 = ((uint8_t)1);
        } else {
            uint8_t t79 = gm_f_starts_continuation_n3333i7(&(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l1)));
            gm_l15 = t79;
        }
        uint8_t t80 = gm_l15;
        if (t80) {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
    }
    void * t81 = gm_l0;
    gm_tl874t2atnlh3 t82 = gm_l3;
    gm_f_push_newline_ke0tojq(t81, GM_CONV(gm_cseahvg8v7p0vs, t82));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_flush_newline_9023ml8_c(void * a0, gm_t3fcbuehh3bs2 a1, uint64_t a2) { gm_f_flush_newline_9023ml8(a0, &a1, a2); }
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_inline_comment_end_8i5h1eq(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_inline_comment_end_8i5h1eq F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* i */
    int32_t gm_l3; /* depth */
    uint8_t gm_l4; /* b */
    gm_tqs5p4n2tq19v gm_l5; /* next */
    GM_STACK_CHECK_SHALLOW(&F, 313u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l6;
    gm_tqs5p4n2tq19v gm_l7;
    uint8_t gm_l8;
    gm_tqs5p4n2tq19v gm_l9;
    uint64_t t9 = gm_l1;
    uint64_t r10; bool o11 = __builtin_add_overflow(t9, ((uint64_t)2ULL), &r10);
    if (o11) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 711, 10);
    uint64_t t12 = r10;
    gm_l2 = t12;
    gm_l3 = ((int32_t)1LL);
    {
        for (;;) {
            int32_t t13 = gm_l3;
            uint8_t t14 = (t13 > ((int32_t)0LL));
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t17 = gm_l2;
                uint8_t t18 = (t17 < t16);
                if (t18) {
                    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t15) + (uintptr_t)t17 * 1));
                    gm_tqs5p4n2tq19v t20;
                    GM_ST(uint8_t, ((uint8_t *)&t20), t19);
                    ((uint8_t *)&t20)[1] = 1;
                    gm_l6 = t20;
                } else {
                    gm_tqs5p4n2tq19v t21;
                    memset(&t21, 0, sizeof t21);
                    gm_l6 = t21;
                }
                gm_tqs5p4n2tq19v t22 = gm_l6;
                gm_l7 = t22;
                uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l7))[1] != 0);
                if (t23) {
                    uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                    gm_l8 = t24;
                } else {
                    gm_tf5srh4mp4b3i t25;
                    memset(&t25, 0, sizeof t25);
                    F.ret = t25;
                    goto gm_fret;
                }
                uint8_t t26 = gm_l8;
                gm_l4 = t26;
                void * t27 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t29 = gm_l2;
                uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 715, 23);
                uint64_t t32 = r30;
                uint8_t t33 = (t32 < t28);
                if (t33) {
                    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)t27) + (uintptr_t)t32 * 1));
                    gm_tqs5p4n2tq19v t35;
                    GM_ST(uint8_t, ((uint8_t *)&t35), t34);
                    ((uint8_t *)&t35)[1] = 1;
                    gm_l9 = t35;
                } else {
                    gm_tqs5p4n2tq19v t36;
                    memset(&t36, 0, sizeof t36);
                    gm_l9 = t36;
                }
                gm_tqs5p4n2tq19v t37 = gm_l9;
                gm_l5 = t37;
                {
                    {
                        uint8_t t38 = gm_l4;
                        uint8_t t39 = ((t38 == ((uint8_t)47ULL)));
                        if (!t39) {
                            goto gm_lb5;
                        }
                        gm_tqs5p4n2tq19v t40 = gm_l5;
                        gm_tqs5p4n2tq19v t41;
                        GM_ST(uint8_t, ((uint8_t *)&t41), ((uint8_t)42ULL));
                        ((uint8_t *)&t41)[1] = 1;
                        uint8_t t42 = (gm_eqb44cb3cf1n(((uint8_t *)&t40), ((uint8_t *)&t41)));
                        if (!t42) {
                            goto gm_lb5;
                        }
                        int32_t t43 = gm_l3;
                        int32_t r44; bool o45 = __builtin_add_overflow(t43, ((int32_t)1LL), &r44);
                        if (o45) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 717, 13);
                        int32_t t46 = r44;
                        gm_l3 = t46;
                        uint64_t t47 = gm_l2;
                        uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)2ULL), &r48);
                        if (o49) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 718, 13);
                        uint64_t t50 = r48;
                        gm_l2 = t50;
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        {
                            uint8_t t51 = gm_l4;
                            uint8_t t52 = ((t51 == ((uint8_t)42ULL)));
                            if (!t52) {
                                goto gm_lb7;
                            }
                            gm_tqs5p4n2tq19v t53 = gm_l5;
                            gm_tqs5p4n2tq19v t54;
                            GM_ST(uint8_t, ((uint8_t *)&t54), ((uint8_t)47ULL));
                            ((uint8_t *)&t54)[1] = 1;
                            uint8_t t55 = (gm_eqb44cb3cf1n(((uint8_t *)&t53), ((uint8_t *)&t54)));
                            if (!t55) {
                                goto gm_lb7;
                            }
                            int32_t t56 = gm_l3;
                            int32_t r57; bool o58 = __builtin_sub_overflow(t56, ((int32_t)1LL), &r57);
                            if (o58) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 720, 13);
                            int32_t t59 = r57;
                            gm_l3 = t59;
                            uint64_t t60 = gm_l2;
                            uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)2ULL), &r61);
                            if (o62) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 721, 13);
                            uint64_t t63 = r61;
                            gm_l2 = t63;
                            goto gm_lb6;
                        }
gm_lb7:;
                        uint8_t t64 = gm_l4;
                        gm_tqs5p4n2tq19v t65;
                        GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)10ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t65 + 1), ((uint8_t)13ULL));
                        uint8_t t66 = 0;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t65) + 0, ((uint8_t *)&t64))) t66 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t65) + 1, ((uint8_t *)&t64))) t66 = 1;
                        uint8_t t67 = (uint8_t)(t66);
                        if (t67) {
                            gm_tf5srh4mp4b3i t68;
                            memset(&t68, 0, sizeof t68);
                            F.ret = t68;
                            goto gm_fret;
                        } else {
                            uint64_t t69 = gm_l2;
                            uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                            if (o71) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 725, 13);
                            uint64_t t72 = r70;
                            gm_l2 = t72;
                        }
                    }
gm_lb6:;
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t73 = gm_l2;
    gm_tf5srh4mp4b3i t74;
    GM_ST(uint8_t, (uint8_t *)&t74 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t74 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t74 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t74), t73);
    ((uint8_t *)&t74)[8] = 1;
    F.ret = t74;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_inline_raw_string_end_6lenf33(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_inline_raw_string_end_6lenf33 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* delim */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* b */
    uint64_t gm_l5; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 329u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l6;
    gm_tqs5p4n2tq19v gm_l7;
    uint8_t gm_l8;
    gm_b16_8 t5 = gm_l0;
    uint64_t t6 = gm_l1;
    uint64_t t7 = gm_f_backtick_run_l95fb09(t5, t6);
    gm_l2 = t7;
    uint64_t t8 = gm_l1;
    uint64_t t9 = gm_l2;
    uint64_t r10; bool o11 = __builtin_add_overflow(t8, t9, &r10);
    if (o11) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 742, 10);
    uint64_t t12 = r10;
    gm_l3 = t12;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t15 = gm_l3;
                uint8_t t16 = (t15 < t14);
                if (t16) {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t13) + (uintptr_t)t15 * 1));
                    gm_tqs5p4n2tq19v t18;
                    GM_ST(uint8_t, ((uint8_t *)&t18), t17);
                    ((uint8_t *)&t18)[1] = 1;
                    gm_l6 = t18;
                } else {
                    gm_tqs5p4n2tq19v t19;
                    memset(&t19, 0, sizeof t19);
                    gm_l6 = t19;
                }
                gm_tqs5p4n2tq19v t20 = gm_l6;
                gm_l7 = t20;
                uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l7))[1] != 0);
                if (t21) {
                    uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                    gm_l8 = t22;
                } else {
                    gm_tf5srh4mp4b3i t23;
                    memset(&t23, 0, sizeof t23);
                    F.ret = t23;
                    goto gm_fret;
                }
                uint8_t t24 = gm_l8;
                gm_l4 = t24;
                uint8_t t25 = gm_l4;
                uint8_t t26 = ((t25 == ((uint8_t)96ULL)));
                if (t26) {
                    gm_b16_8 t27 = gm_l0;
                    uint64_t t28 = gm_l3;
                    uint64_t t29 = gm_f_backtick_run_l95fb09(t27, t28);
                    gm_l5 = t29;
                    uint64_t t30 = gm_l3;
                    uint64_t t31 = gm_l5;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 747, 13);
                    uint64_t t34 = r32;
                    gm_l3 = t34;
                    uint64_t t35 = gm_l5;
                    uint64_t t36 = gm_l2;
                    uint8_t t37 = ((t35 == t36));
                    if (t37) {
                        uint64_t t38 = gm_l3;
                        gm_tf5srh4mp4b3i t39;
                        GM_ST(uint8_t, (uint8_t *)&t39 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t39 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t39 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t39), t38);
                        ((uint8_t *)&t39)[8] = 1;
                        F.ret = t39;
                        goto gm_fret;
                    }
                } else {
                    uint8_t t40 = gm_l4;
                    gm_tqs5p4n2tq19v t41;
                    GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)10ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t41 + 1), ((uint8_t)13ULL));
                    uint8_t t42 = 0;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t41) + 0, ((uint8_t *)&t40))) t42 = 1;
                    if (gm_eqjmccqdb90f(((uint8_t *)&t41) + 1, ((uint8_t *)&t40))) t42 = 1;
                    uint8_t t43 = (uint8_t)(t42);
                    if (t43) {
                        gm_tf5srh4mp4b3i t44;
                        memset(&t44, 0, sizeof t44);
                        F.ret = t44;
                        goto gm_fret;
                    } else {
                        uint64_t t45 = gm_l3;
                        uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                        if (o47) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 752, 13);
                        uint64_t t48 = r46;
                        gm_l3 = t48;
                    }
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_ascii_space_t2ltvst(uint8_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unicode_space_b2m9rku(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unicode_space_b2m9rku F;
    uint32_t gm_l0; /* c */
    uint32_t gm_l1; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint32_t t2 = gm_l0;
    gm_tu4r32bi6mhcq t3;
    GM_ST(uint32_t, ((uint8_t *)&t3 + 0), ((uint32_t)133ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 4), ((uint32_t)160ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), ((uint32_t)5760ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 12), ((uint32_t)8232ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 16), ((uint32_t)8233ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 20), ((uint32_t)8239ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 24), ((uint32_t)8287ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 28), ((uint32_t)12288ULL));
    uint8_t t4 = 0;
    if (gm_eq467d5cm1d5(((uint8_t *)&t3) + 0, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eq467d5cm1d5(((uint8_t *)&t3) + 4, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eq467d5cm1d5(((uint8_t *)&t3) + 8, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eq467d5cm1d5(((uint8_t *)&t3) + 12, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eq467d5cm1d5(((uint8_t *)&t3) + 16, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eq467d5cm1d5(((uint8_t *)&t3) + 20, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eq467d5cm1d5(((uint8_t *)&t3) + 24, ((uint8_t *)&t2))) t4 = 1;
    if (gm_eq467d5cm1d5(((uint8_t *)&t3) + 28, ((uint8_t *)&t2))) t4 = 1;
    uint8_t t5 = (uint8_t)(t4);
    if (t5) {
        gm_l2 = ((uint8_t)1);
    } else {
        uint32_t t6 = gm_l0;
        gm_l1 = t6;
        uint32_t t7 = gm_l1;
        uint8_t t8 = (((uint32_t)8192ULL) <= t7);
        if (t8) {
            uint32_t t9 = gm_l1;
            uint8_t t10 = (t9 <= ((uint32_t)8202ULL));
            gm_l3 = t10;
        } else {
            gm_l3 = ((uint8_t)0);
        }
        uint8_t t11 = gm_l3;
        gm_l2 = t11;
    }
    uint8_t t12 = gm_l2;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_lex_recovering_obo2892(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_lex_recovering_obo2892 F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 855u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t2792tsg5eg72 t7;
    gm_f_diagnostics_n3kkhf4((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t7;
    uint32_t t8 = gm_l0;
    gm_b16_8 t9 = gm_l1;
    void * t10 = (void *)((uint8_t *)&F.l3);
    gm_trd6bg8is1v4n t11;
    gm_f_new_lexer_dm12g12((void *)&t11, (void *)NULL, cx, NULL, t8, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t11;
    void * t12 = (void *)((uint8_t *)&F.l4);
    gm_f_run_v26is74((void *)&F.l5, (void *)NULL, cx, NULL, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t13 = (void *)((uint8_t *)&F.l3);
    uint8_t t14 = gm_f_has_errors_if7ai0m(t13);
    uint8_t t15 = (uint8_t)!t14;
    if (t15) {
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_l6 = t16;
        gm_l7 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t17 = gm_l7;
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint8_t t19 = (t17 < t18);
                if (!t19) {
                    goto gm_lb1;
                }
                {
                    void * t20 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint64_t t22 = gm_l7;
                    {
                        {
                            memmove(((uint8_t *)&F.l8), (((uint8_t *)t20) + (uintptr_t)t22 * 184), 184);
                            void * t23 = gm_l2;
                            gm_f_push_o4ckdr1(t23, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 30, 9);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t24 = gm_l7;
                uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                uint64_t t27 = r25;
                gm_l7 = t27;
            }
        }
gm_lb1:;
        memmove(gm_out, ((uint8_t *)&F.l5), 120);
        goto gm_fdone;
    }
    uint32_t t28 = gm_l0;
    gm_b16_8 t29 = gm_l1;
    void * t30 = gm_l2;
    gm_trd6bg8is1v4n t31;
    gm_f_new_lexer_dm12g12((void *)&t31, (void *)NULL, cx, NULL, t28, t29, t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l9 = t31;
    GM_ST(uint8_t, (((uint8_t *)&F.l9) + 280), ((uint8_t)1));
    void * t32 = (void *)((uint8_t *)&F.l9);
    gm_f_run_v26is74((void *)&(*(gm_tr8dvlu9ik4gj *)gm_out), (void *)NULL, cx, NULL, t32);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_token_h8g44s4(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_lex_token_h8g44s4 F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* b */
    uint64_t gm_l2; /* lo */
    uint8_t gm_l3; /* next */
    gm_b16_8 gm_l4; /* name */
    gm_t3fcbuehh3bs2 gm_l5; /* kind */
    gm_t9qkh0rsooc8d gm_l6; /* p */
    GM_STACK_CHECK(&F, 622u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l7;
    uint8_t gm_l8;
    gm_t3fcbuehh3bs2 gm_l9;
    gm_t9qkh0rsooc8d gm_l10;
    gm_taari1s0m51a7 gm_l11;
    void * t24 = gm_l0;
    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 24));
    gm_l2 = t25;
    {
        {
            {
                {
                    uint8_t t26 = gm_l1;
                    uint8_t t27 = (t26 >= ((uint8_t)97ULL));
                    if (!t27) {
                        goto gm_lb4;
                    }
                    uint8_t t28 = (t26 <= ((uint8_t)122ULL));
                    if (!t28) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t29 = gm_l1;
                    uint8_t t30 = (t29 >= ((uint8_t)65ULL));
                    if (!t30) {
                        goto gm_lb5;
                    }
                    uint8_t t31 = (t29 <= ((uint8_t)90ULL));
                    if (!t31) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t32 = gm_l1;
                    uint8_t t33 = ((t32 == ((uint8_t)95ULL)));
                    if (!t33) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t34 = gm_l0;
            gm_b0_1 t35 = gm_f_lex_word_9li16te((void *)NULL, cx, NULL, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t36 = gm_l1;
            uint8_t t37 = (t36 >= ((uint8_t)48ULL));
            if (!t37) {
                goto gm_lb7;
            }
            uint8_t t38 = (t36 <= ((uint8_t)57ULL));
            if (!t38) {
                goto gm_lb7;
            }
            void * t39 = gm_l0;
            gm_b0_1 t40 = gm_f_lex_number_9j9sdfi((void *)NULL, cx, NULL, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t41 = gm_l1;
            uint8_t t42 = ((t41 == ((uint8_t)34ULL)));
            if (!t42) {
                goto gm_lb8;
            }
            void * t43 = gm_l0;
            uint64_t t44 = gm_l2;
            gm_t9qkh0rsooc8d t45;
            GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)0ULL));
            gm_b0_1 t46 = gm_f_lex_string_bcdbdjm((void *)NULL, cx, NULL, t43, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t47 = gm_l1;
            uint8_t t48 = ((t47 == ((uint8_t)96ULL)));
            if (!t48) {
                goto gm_lb9;
            }
            void * t49 = gm_l0;
            gm_b0_1 t50 = gm_f_lex_raw_string_uf6bd4g((void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t51 = gm_l1;
            uint8_t t52 = ((t51 == ((uint8_t)39ULL)));
            if (!t52) {
                goto gm_lb10;
            }
            void * t53 = gm_l0;
            gm_b0_1 t54 = gm_f_lex_char_4qk4vjp((void *)NULL, cx, NULL, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t55 = gm_l1;
            uint8_t t56 = ((t55 == ((uint8_t)59ULL)));
            if (!t56) {
                goto gm_lb11;
            }
            void * t57 = gm_l0;
            void * t58 = gm_l0;
            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)t58) + 24));
            uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
            if (o61) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 299, 13);
            uint64_t t62 = r60;
            GM_ST(uint64_t, (((uint8_t *)t57) + 24), t62);
            void * t63 = gm_l0;
            uint64_t t64 = gm_l2;
            gm_b0_1 t65 = gm_f_emit_semicolon_7da5b93((void *)NULL, cx, NULL, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb11:;
        {
            {
                {
                    uint8_t t66 = gm_l1;
                    uint8_t t67 = ((t66 == ((uint8_t)64ULL)));
                    if (!t67) {
                        goto gm_lb14;
                    }
                    goto gm_lb13;
                }
gm_lb14:;
                {
                    uint8_t t68 = gm_l1;
                    uint8_t t69 = ((t68 == ((uint8_t)35ULL)));
                    if (!t69) {
                        goto gm_lb15;
                    }
                    goto gm_lb13;
                }
gm_lb15:;
                goto gm_lb12;
            }
gm_lb13:;
            void * t70 = gm_l0;
            void * t71 = gm_l0;
            uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)t71) + 24));
            uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
            if (o74) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 303, 13);
            uint64_t t75 = r73;
            GM_ST(uint64_t, (((uint8_t *)t70) + 24), t75);
            void * t76 = gm_l0;
            gm_tqs5p4n2tq19v t77 = gm_f_peek_72bostn(t76);
            gm_l7 = t77;
            uint8_t t78 = (uint8_t)((((uint8_t *)&gm_l7))[1] != 0);
            if (t78) {
                uint8_t t79 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                gm_l8 = t79;
            } else {
                gm_l8 = ((uint8_t)0ULL);
            }
            uint8_t t80 = gm_l8;
            gm_l3 = t80;
            uint8_t t81 = gm_l3;
            uint8_t t82 = gm_f_is_ident_start_rjefpfm(t81);
            if (t82) {
                void * t83 = gm_l0;
                gm_b16_8 t84 = gm_f_scan_ident_h1b2n4u(t83);
                gm_l4 = t84;
                uint8_t t85 = gm_l1;
                uint8_t t86 = ((t85 == ((uint8_t)64ULL)));
                if (t86) {
                    gm_b16_8 t87 = gm_l4;
                    gm_t3fcbuehh3bs2 t88;
                    GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t88 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)6ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t88 + 8), t87);
                    gm_l9 = t88;
                } else {
                    gm_b16_8 t89 = gm_l4;
                    gm_t3fcbuehh3bs2 t90;
                    GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t90 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)7ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t90 + 8), t89);
                    gm_l9 = t90;
                }
                memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l9), 32);
                void * t91 = gm_l0;
                uint64_t t92 = gm_l2;
                void * t93 = gm_l0;
                uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)t93) + 24));
                gm_b0_1 t95 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t91, &(*(gm_t3fcbuehh3bs2 *)((uint8_t *)&gm_l5)), t92, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            } else {
                void * t96 = gm_l0;
                uint8_t t97 = gm_l1;
                uint8_t t98 = ((t97 == ((uint8_t)64ULL)));
                if (t98) {
                    gm_t9qkh0rsooc8d t99;
                    GM_ST(uint8_t, ((uint8_t *)&t99 + 0), ((uint8_t)48ULL));
                    gm_l10 = t99;
                } else {
                    gm_t9qkh0rsooc8d t100;
                    GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)49ULL));
                    gm_l10 = t100;
                }
                gm_t9qkh0rsooc8d t101 = gm_l10;
                uint64_t t102 = gm_l2;
                void * t103 = gm_l0;
                uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)t103) + 24));
                gm_t3fcbuehh3bs2 t105;
                GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t105 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t105 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t105 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t105 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t105 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)8ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t105 + 8), t101);
                gm_b0_1 t106 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t96, &t105, t102, t104);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb12:;
        {
            {
                {
                    uint8_t t107 = gm_l1;
                    uint8_t t108 = ((t107 == ((uint8_t)61ULL)));
                    if (!t108) {
                        goto gm_lb18;
                    }
                    goto gm_lb17;
                }
gm_lb18:;
                {
                    uint8_t t109 = gm_l1;
                    uint8_t t110 = ((t109 == ((uint8_t)33ULL)));
                    if (!t110) {
                        goto gm_lb19;
                    }
                    goto gm_lb17;
                }
gm_lb19:;
                goto gm_lb16;
            }
gm_lb17:;
            void * t111 = gm_l0;
            gm_tqs5p4n2tq19v t112 = gm_f_peek_at_74p8lkq(t111, ((uint64_t)1ULL));
            gm_tqs5p4n2tq19v t113;
            GM_ST(uint8_t, ((uint8_t *)&t113), ((uint8_t)61ULL));
            ((uint8_t *)&t113)[1] = 1;
            uint8_t t114 = (gm_eqb44cb3cf1n(((uint8_t *)&t112), ((uint8_t *)&t113)));
            if (!t114) {
                goto gm_lb16;
            }
            void * t115 = gm_l0;
            uint8_t t116 = gm_l1;
            gm_b0_1 t117 = gm_f_c_equality_u566nc4((void *)NULL, cx, NULL, t115, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb16:;
        {
            {
                {
                    void * t118 = gm_l0;
                    gm_taari1s0m51a7 t119 = gm_f_scan_punct_ecrh51c(t118);
                    gm_l11 = t119;
                    uint8_t t120 = (uint8_t)((((uint8_t *)&gm_l11))[1] != 0);
                    if (!t120) {
                        goto gm_lb22;
                    }
                    gm_t9qkh0rsooc8d t121 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l11));
                    gm_l6 = t121;
                    void * t122 = gm_l0;
                    gm_t9qkh0rsooc8d t123 = gm_l6;
                    uint64_t t124 = gm_l2;
                    void * t125 = gm_l0;
                    uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)t125) + 24));
                    gm_t3fcbuehh3bs2 t127;
                    GM_ST(uint8_t, (uint8_t *)&t127 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t127 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t127 + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&t127 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t127 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t127 + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&t127 + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&t127 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t127 + 0), ((uint8_t)8ULL));
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t127 + 8), t123);
                    gm_b0_1 t128 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t122, &t127, t124, t126);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb21;
                }
gm_lb22:;
                void * t129 = gm_l0;
                gm_b0_1 t130 = gm_f_unexpected_char_r3t8d4c((void *)NULL, cx, NULL, t129);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb21:;
            goto gm_lb1;
        }
gm_lb20:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 292, 5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_lex_word_9li16te(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_lex_word_9li16te F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_b16_8 gm_l2; /* text */
    gm_t9qkh0rsooc8d gm_l3; /* kw */
    gm_taari1s0m51a7 gm_l4; /* two_word */
    uint64_t gm_l5; /* end */
    uint64_t gm_l6; /* e */
    uint64_t gm_l7; /* e */
    gm_t9qkh0rsooc8d gm_l8; /* p */
    GM_STACK_CHECK(&F, 677u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 gm_l9;
    gm_t9qkh0rsooc8d gm_l10;
    gm_tf5srh4mp4b3i gm_l11;
    gm_tf5srh4mp4b3i gm_l12;
    void * t10 = gm_l0;
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t10) + 24));
    gm_l1 = t11;
    void * t12 = gm_l0;
    gm_b16_8 t13 = gm_f_scan_ident_h1b2n4u(t12);
    gm_l2 = t13;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t15 = ((t14 == ((uint64_t)1ULL)));
    if (t15) {
        {
            {
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t17, gm_srcvmmksi0829pa, 365, 12);
                uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t16) + (uintptr_t)((uint64_t)0ULL) * 1));
                uint8_t t19 = ((t18 == ((uint8_t)99ULL)));
                if (!t19) {
                    goto gm_lb2;
                }
                void * t20 = gm_l0;
                gm_tqs5p4n2tq19v t21 = gm_f_peek_72bostn(t20);
                gm_tqs5p4n2tq19v t22;
                GM_ST(uint8_t, ((uint8_t *)&t22), ((uint8_t)34ULL));
                ((uint8_t *)&t22)[1] = 1;
                uint8_t t23 = (gm_eqb44cb3cf1n(((uint8_t *)&t21), ((uint8_t *)&t22)));
                if (!t23) {
                    goto gm_lb2;
                }
                void * t24 = gm_l0;
                uint64_t t25 = gm_l1;
                gm_t9qkh0rsooc8d t26;
                GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)2ULL));
                gm_b0_1 t27 = gm_f_lex_string_bcdbdjm((void *)NULL, cx, NULL, t24, t25, t26);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t28 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t29, gm_srcvmmksi0829pa, 369, 12);
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)((uint64_t)0ULL) * 1));
        uint8_t t31 = ((t30 == ((uint8_t)95ULL)));
        if (t31) {
            void * t32 = gm_l0;
            uint64_t t33 = gm_l1;
            void * t34 = gm_l0;
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t34) + 24));
            gm_t3fcbuehh3bs2 t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t36 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t36 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t36 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)1ULL));
            gm_b0_1 t37 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t32, &t36, t33, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
    }
    gm_b16_8 t38 = gm_l2;
    gm_taari1s0m51a7 t39 = gm_f_keyword_from_1ofmh1u(t38);
    gm_l9 = t39;
    uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l9))[1] != 0);
    if (t40) {
        gm_t9qkh0rsooc8d t41 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l9));
        gm_l10 = t41;
    } else {
        void * t42 = gm_l0;
        gm_b16_8 t43 = gm_l2;
        uint64_t t44 = gm_l1;
        void * t45 = gm_l0;
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 24));
        gm_t3fcbuehh3bs2 t47;
        GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t47 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t47 + 8), t43);
        gm_b0_1 t48 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t42, &t47, t44, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_t9qkh0rsooc8d t49 = gm_l10;
    gm_l3 = t49;
    gm_taari1s0m51a7 t50;
    memset(&t50, 0, sizeof t50);
    gm_l4 = t50;
    gm_l5 = ((uint64_t)0ULL);
    gm_t9qkh0rsooc8d t51 = gm_l3;
    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&t51) + 0));
    uint8_t t53 = (uint8_t)((t52 == ((uint8_t)16ULL)));
    gm_t9qkh0rsooc8d t54;
    GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)16ULL));
    if (t53) {
        {
            {
                void * t55 = gm_l0;
                void * t56 = gm_l0;
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 24));
                gm_b16_8 t58;
                GM_ST(void *, (uint8_t *)&t58, (void *)(gm_scvhpklutvpjg));
                GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(3));
                gm_tf5srh4mp4b3i t59 = gm_f_second_word_end_dpnj7g8(t55, t57, t58);
                gm_l11 = t59;
                uint8_t t60 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
                if (!t60) {
                    goto gm_lb4;
                }
                uint64_t t61 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
                gm_l6 = t61;
                gm_t9qkh0rsooc8d t62;
                GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)67ULL));
                gm_taari1s0m51a7 t63;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t63), t62);
                ((uint8_t *)&t63)[1] = 1;
                gm_l4 = t63;
                uint64_t t64 = gm_l6;
                gm_l5 = t64;
                goto gm_lb3;
            }
gm_lb4:;
        }
gm_lb3:;
    } else {
        gm_t9qkh0rsooc8d t65 = gm_l3;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t65) + 0));
        uint8_t t67 = (uint8_t)((t66 == ((uint8_t)18ULL)));
        gm_t9qkh0rsooc8d t68;
        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)18ULL));
        if (t67) {
            {
                {
                    void * t69 = gm_l0;
                    void * t70 = gm_l0;
                    uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)t70) + 24));
                    gm_b16_8 t72;
                    GM_ST(void *, (uint8_t *)&t72, (void *)(gm_svbhcvlahd4n2));
                    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(2));
                    gm_tf5srh4mp4b3i t73 = gm_f_second_word_end_dpnj7g8(t69, t71, t72);
                    gm_l12 = t73;
                    uint8_t t74 = (uint8_t)((((uint8_t *)&gm_l12))[8] != 0);
                    if (!t74) {
                        goto gm_lb6;
                    }
                    uint64_t t75 = GM_LD(uint64_t, ((uint8_t *)&gm_l12));
                    gm_l7 = t75;
                    gm_t9qkh0rsooc8d t76;
                    GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)68ULL));
                    gm_taari1s0m51a7 t77;
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t77), t76);
                    ((uint8_t *)&t77)[1] = 1;
                    gm_l4 = t77;
                    uint64_t t78 = gm_l7;
                    gm_l5 = t78;
                    goto gm_lb5;
                }
gm_lb6:;
            }
gm_lb5:;
        }
    }
    {
        {
            uint8_t t79 = (uint8_t)((((uint8_t *)&gm_l4))[1] != 0);
            if (!t79) {
                goto gm_lb8;
            }
            gm_t9qkh0rsooc8d t80 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l4));
            gm_l8 = t80;
            void * t81 = gm_l0;
            uint64_t t82 = gm_l5;
            GM_ST(uint64_t, (((uint8_t *)t81) + 24), t82);
            void * t83 = gm_l0;
            gm_t9qkh0rsooc8d t84 = gm_l8;
            uint64_t t85 = gm_l1;
            uint64_t t86 = gm_l5;
            gm_t3fcbuehh3bs2 t87;
            GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t87 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t87 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t87 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t87 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)8ULL));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t87 + 8), t84);
            gm_b0_1 t88 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t83, &t87, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb7;
        }
gm_lb8:;
        void * t89 = gm_l0;
        gm_t9qkh0rsooc8d t90 = gm_l3;
        uint64_t t91 = gm_l1;
        void * t92 = gm_l0;
        uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)t92) + 24));
        gm_t3fcbuehh3bs2 t94;
        GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t94 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t94 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t94 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t94 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t94 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t94 + 8), t90);
        gm_b0_1 t95 = gm_f_emit_ekvv703((void *)NULL, cx, NULL, t89, &t94, t91, t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
gm_lb7:;
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
__attribute__((aligned(4))) uint8_t gm_f_line_binds_jm0pnul(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_line_binds_jm0pnul F;
    gm_b16_8 gm_l0; /* rest */
    int32_t gm_l1; /* depth */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 584u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t gm_l4;
    gm_tqs5p4n2tq19v gm_l5;
    gm_tqs5p4n2tq19v gm_l6;
    gm_tf5srh4mp4b3i gm_l7;
    uint64_t gm_l8;
    gm_tf5srh4mp4b3i gm_l9;
    uint64_t gm_l10;
    uint8_t gm_l11;
    gm_tqs5p4n2tq19v gm_l12;
    gm_l1 = ((int32_t)0LL);
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t29 = gm_l2;
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t31 = (t29 < t30);
            if (!t31) {
                goto gm_lb1;
            }
            {
                void * t32 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t34 = gm_l2;
                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)t32) + (uintptr_t)t34 * 1));
                gm_l3 = t35;
                {
                    {
                        uint8_t t36 = gm_l3;
                        uint8_t t37 = ((t36 == ((uint8_t)10ULL)));
                        if (!t37) {
                            goto gm_lb5;
                        }
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
gm_lb5:;
                    {
                        {
                            {
                                uint8_t t38 = gm_l3;
                                uint8_t t39 = ((t38 == ((uint8_t)40ULL)));
                                if (!t39) {
                                    goto gm_lb8;
                                }
                                goto gm_lb7;
                            }
gm_lb8:;
                            {
                                uint8_t t40 = gm_l3;
                                uint8_t t41 = ((t40 == ((uint8_t)91ULL)));
                                if (!t41) {
                                    goto gm_lb9;
                                }
                                goto gm_lb7;
                            }
gm_lb9:;
                            {
                                uint8_t t42 = gm_l3;
                                uint8_t t43 = ((t42 == ((uint8_t)123ULL)));
                                if (!t43) {
                                    goto gm_lb10;
                                }
                                goto gm_lb7;
                            }
gm_lb10:;
                            goto gm_lb6;
                        }
gm_lb7:;
                        int32_t t44 = gm_l1;
                        int32_t r45; bool o46 = __builtin_add_overflow(t44, ((int32_t)1LL), &r45);
                        if (o46) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 767, 32);
                        int32_t t47 = r45;
                        gm_l1 = t47;
                        goto gm_lb4;
                    }
gm_lb6:;
                    {
                        {
                            {
                                uint8_t t48 = gm_l3;
                                uint8_t t49 = ((t48 == ((uint8_t)41ULL)));
                                if (!t49) {
                                    goto gm_lb13;
                                }
                                goto gm_lb12;
                            }
gm_lb13:;
                            {
                                uint8_t t50 = gm_l3;
                                uint8_t t51 = ((t50 == ((uint8_t)93ULL)));
                                if (!t51) {
                                    goto gm_lb14;
                                }
                                goto gm_lb12;
                            }
gm_lb14:;
                            {
                                uint8_t t52 = gm_l3;
                                uint8_t t53 = ((t52 == ((uint8_t)125ULL)));
                                if (!t53) {
                                    goto gm_lb15;
                                }
                                goto gm_lb12;
                            }
gm_lb15:;
                            goto gm_lb11;
                        }
gm_lb12:;
                        int32_t t54 = gm_l1;
                        int32_t r55; bool o56 = __builtin_sub_overflow(t54, ((int32_t)1LL), &r55);
                        if (o56) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 769, 17);
                        int32_t t57 = r55;
                        gm_l1 = t57;
                        int32_t t58 = gm_l1;
                        uint8_t t59 = (t58 < ((int32_t)0LL));
                        if (t59) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb11:;
                    {
                        {
                            {
                                uint8_t t60 = gm_l3;
                                uint8_t t61 = ((t60 == ((uint8_t)34ULL)));
                                if (!t61) {
                                    goto gm_lb18;
                                }
                                goto gm_lb17;
                            }
gm_lb18:;
                            {
                                uint8_t t62 = gm_l3;
                                uint8_t t63 = ((t62 == ((uint8_t)39ULL)));
                                if (!t63) {
                                    goto gm_lb19;
                                }
                                goto gm_lb17;
                            }
gm_lb19:;
                            goto gm_lb16;
                        }
gm_lb17:;
                        uint64_t t64 = gm_l2;
                        uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                        if (o66) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 773, 17);
                        uint64_t t67 = r65;
                        gm_l2 = t67;
                        {
                            for (;;) {
                                uint64_t t68 = gm_l2;
                                uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint8_t t70 = (t68 < t69);
                                if (!t70) {
                                    goto gm_lb20;
                                }
                                void * t71 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t73 = gm_l2;
                                uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)t71) + (uintptr_t)t73 * 1));
                                uint8_t t75 = gm_l3;
                                uint8_t t76 = (!(t74 == t75));
                                if (!t76) {
                                    goto gm_lb20;
                                }
                                void * t77 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t79 = gm_l2;
                                uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)t77) + (uintptr_t)t79 * 1));
                                uint8_t t81 = (!(t80 == ((uint8_t)10ULL)));
                                if (!t81) {
                                    goto gm_lb20;
                                }
                                {
                                    uint64_t t82 = gm_l2;
                                    void * t83 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                    uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                    uint64_t t85 = gm_l2;
                                    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)t83) + (uintptr_t)t85 * 1));
                                    uint8_t t87 = ((t86 == ((uint8_t)92ULL)));
                                    if (t87) {
                                        gm_l4 = ((uint64_t)2ULL);
                                    } else {
                                        gm_l4 = ((uint64_t)1ULL);
                                    }
                                    uint64_t t88 = gm_l4;
                                    uint64_t r89; bool o90 = __builtin_add_overflow(t82, t88, &r89);
                                    if (o90) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 775, 21);
                                    uint64_t t91 = r89;
                                    gm_l2 = t91;
                                }
gm_lb22:;
                            }
                        }
gm_lb20:;
                        goto gm_lb4;
                    }
gm_lb16:;
                    {
                        uint8_t t92 = gm_l3;
                        uint8_t t93 = ((t92 == ((uint8_t)47ULL)));
                        if (!t93) {
                            goto gm_lb23;
                        }
                        void * t94 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t96 = gm_l2;
                        uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                        if (o98) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 778, 29);
                        uint64_t t99 = r97;
                        uint8_t t100 = (t99 < t95);
                        if (t100) {
                            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)t94) + (uintptr_t)t99 * 1));
                            gm_tqs5p4n2tq19v t102;
                            GM_ST(uint8_t, ((uint8_t *)&t102), t101);
                            ((uint8_t *)&t102)[1] = 1;
                            gm_l5 = t102;
                        } else {
                            gm_tqs5p4n2tq19v t103;
                            memset(&t103, 0, sizeof t103);
                            gm_l5 = t103;
                        }
                        gm_tqs5p4n2tq19v t104 = gm_l5;
                        gm_tqs5p4n2tq19v t105;
                        GM_ST(uint8_t, ((uint8_t *)&t105), ((uint8_t)47ULL));
                        ((uint8_t *)&t105)[1] = 1;
                        uint8_t t106 = (gm_eqb44cb3cf1n(((uint8_t *)&t104), ((uint8_t *)&t105)));
                        if (!t106) {
                            goto gm_lb23;
                        }
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
gm_lb23:;
                    {
                        uint8_t t107 = gm_l3;
                        uint8_t t108 = ((t107 == ((uint8_t)47ULL)));
                        if (!t108) {
                            goto gm_lb24;
                        }
                        void * t109 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t111 = gm_l2;
                        uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                        if (o113) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 779, 29);
                        uint64_t t114 = r112;
                        uint8_t t115 = (t114 < t110);
                        if (t115) {
                            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)t109) + (uintptr_t)t114 * 1));
                            gm_tqs5p4n2tq19v t117;
                            GM_ST(uint8_t, ((uint8_t *)&t117), t116);
                            ((uint8_t *)&t117)[1] = 1;
                            gm_l6 = t117;
                        } else {
                            gm_tqs5p4n2tq19v t118;
                            memset(&t118, 0, sizeof t118);
                            gm_l6 = t118;
                        }
                        gm_tqs5p4n2tq19v t119 = gm_l6;
                        gm_tqs5p4n2tq19v t120;
                        GM_ST(uint8_t, ((uint8_t *)&t120), ((uint8_t)42ULL));
                        ((uint8_t *)&t120)[1] = 1;
                        uint8_t t121 = (gm_eqb44cb3cf1n(((uint8_t *)&t119), ((uint8_t *)&t120)));
                        if (!t121) {
                            goto gm_lb24;
                        }
                        gm_b16_8 t122 = gm_l0;
                        uint64_t t123 = gm_l2;
                        gm_tf5srh4mp4b3i t124 = gm_f_inline_comment_end_8i5h1eq(t122, t123);
                        gm_l7 = t124;
                        uint8_t t125 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
                        if (t125) {
                            uint64_t t126 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                            gm_l8 = t126;
                        } else {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        uint64_t t127 = gm_l8;
                        gm_l2 = t127;
                        goto gm_lb3;
                    }
gm_lb24:;
                    {
                        uint8_t t128 = gm_l3;
                        uint8_t t129 = ((t128 == ((uint8_t)96ULL)));
                        if (!t129) {
                            goto gm_lb25;
                        }
                        gm_b16_8 t130 = gm_l0;
                        uint64_t t131 = gm_l2;
                        gm_tf5srh4mp4b3i t132 = gm_f_inline_raw_string_end_6lenf33(t130, t131);
                        gm_l9 = t132;
                        uint8_t t133 = (uint8_t)((((uint8_t *)&gm_l9))[8] != 0);
                        if (t133) {
                            uint64_t t134 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                            gm_l10 = t134;
                        } else {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        uint64_t t135 = gm_l10;
                        gm_l2 = t135;
                        goto gm_lb3;
                    }
gm_lb25:;
                    {
                        uint8_t t136 = gm_l3;
                        uint8_t t137 = ((t136 == ((uint8_t)58ULL)));
                        if (!t137) {
                            goto gm_lb26;
                        }
                        int32_t t138 = gm_l1;
                        uint8_t t139 = ((t138 == ((int32_t)0LL)));
                        if (t139) {
                            void * t140 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t142 = gm_l2;
                            uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                            if (o144) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 787, 44);
                            uint64_t t145 = r143;
                            uint8_t t146 = (t145 < t141);
                            if (t146) {
                                uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)t140) + (uintptr_t)t145 * 1));
                                gm_tqs5p4n2tq19v t148;
                                GM_ST(uint8_t, ((uint8_t *)&t148), t147);
                                ((uint8_t *)&t148)[1] = 1;
                                gm_l12 = t148;
                            } else {
                                gm_tqs5p4n2tq19v t149;
                                memset(&t149, 0, sizeof t149);
                                gm_l12 = t149;
                            }
                            gm_tqs5p4n2tq19v t150 = gm_l12;
                            gm_tqs5p4n2tq19v t151;
                            GM_ST(uint8_t, ((uint8_t *)&t151), ((uint8_t)61ULL));
                            ((uint8_t *)&t151)[1] = 1;
                            uint8_t t152 = (gm_eqb44cb3cf1n(((uint8_t *)&t150), ((uint8_t *)&t151)));
                            gm_l11 = t152;
                        } else {
                            gm_l11 = ((uint8_t)0);
                        }
                        uint8_t t153 = gm_l11;
                        if (!t153) {
                            goto gm_lb26;
                        }
                        F.ret = ((uint8_t)1);
                        goto gm_fret;
                    }
gm_lb26:;
                    {
                        goto gm_lb4;
                    }
gm_lb27:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 765, 9);
                }
gm_lb4:;
                uint64_t t154 = gm_l2;
                uint64_t r155; bool o156 = __builtin_add_overflow(t154, ((uint64_t)1ULL), &r155);
                if (o156) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 790, 9);
                uint64_t t157 = r155;
                gm_l2 = t157;
            }
gm_lb3:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_line_comment_s71u8hq(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_line_comment_s71u8hq F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    uint8_t gm_l2; /* is_doc */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* b */
    gm_b16_8 gm_l5; /* text */
    uint64_t gm_l6; /* next */
    GM_STACK_CHECK(&F, 674u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l7;
    gm_tqs5p4n2tq19v gm_l8;
    gm_tftb1odc739t5 gm_l9;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 24));
    gm_l1 = t9;
    void * t10 = gm_l0;
    gm_tqs5p4n2tq19v t11 = gm_f_peek_at_74p8lkq(t10, ((uint64_t)2ULL));
    gm_tqs5p4n2tq19v t12;
    GM_ST(uint8_t, ((uint8_t *)&t12), ((uint8_t)47ULL));
    ((uint8_t *)&t12)[1] = 1;
    uint8_t t13 = (gm_eqb44cb3cf1n(((uint8_t *)&t11), ((uint8_t *)&t12)));
    if (t13) {
        void * t14 = gm_l0;
        gm_tqs5p4n2tq19v t15 = gm_f_peek_at_74p8lkq(t14, ((uint64_t)3ULL));
        gm_tqs5p4n2tq19v t16;
        GM_ST(uint8_t, ((uint8_t *)&t16), ((uint8_t)47ULL));
        ((uint8_t *)&t16)[1] = 1;
        uint8_t t17 = (!gm_eqb44cb3cf1n(((uint8_t *)&t15), ((uint8_t *)&t16)));
        gm_l7 = t17;
    } else {
        gm_l7 = ((uint8_t)0);
    }
    uint8_t t18 = gm_l7;
    gm_l2 = t18;
    uint64_t t19 = gm_l1;
    uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)2ULL), &r20);
    if (o21) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 209, 10);
    uint64_t t22 = r20;
    gm_l3 = t22;
    {
        for (;;) {
            void * t23 = gm_l0;
            uint64_t t24 = gm_l3;
            gm_tqs5p4n2tq19v t25 = gm_f_byte_b4v8vul(t23, t24);
            gm_l8 = t25;
            uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l8))[1] != 0);
            if (!t26) {
                goto gm_lb1;
            }
            uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)&gm_l8));
            gm_l4 = t27;
            {
                uint8_t t28 = gm_l4;
                uint8_t t29 = ((t28 == ((uint8_t)10ULL)));
                if (t29) {
                    goto gm_lb1;
                }
                uint8_t t30 = gm_l4;
                uint8_t t31 = ((t30 == ((uint8_t)13ULL)));
                if (t31) {
                    void * t32 = gm_l0;
                    uint64_t t33 = gm_l3;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    if (o35) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 213, 24);
                    uint64_t t36 = r34;
                    gm_tqs5p4n2tq19v t37 = gm_f_byte_b4v8vul(t32, t36);
                    gm_tqs5p4n2tq19v t38;
                    GM_ST(uint8_t, ((uint8_t *)&t38), ((uint8_t)10ULL));
                    ((uint8_t *)&t38)[1] = 1;
                    uint8_t t39 = (gm_eqb44cb3cf1n(((uint8_t *)&t37), ((uint8_t *)&t38)));
                    if (t39) {
                        goto gm_lb1;
                    }
                    void * t40 = gm_l0;
                    uint64_t t41 = gm_l3;
                    uint64_t t42 = gm_l3;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    if (o44) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 214, 25);
                    uint64_t t45 = r43;
                    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)gm_c644mb9o5bq));
                    gm_b0_1 t47 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t40, t41, t45, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint64_t t48 = gm_l3;
                uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                if (o50) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 216, 9);
                uint64_t t51 = r49;
                gm_l3 = t51;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint8_t t52 = gm_l2;
    if (t52) {
        void * t53 = gm_l0;
        void * t54 = GM_LD(void *, (((uint8_t *)t53) + 8));
        uint64_t t55 = GM_LD(uint64_t, ((((uint8_t *)t53) + 8) + 8));
        uint64_t t56 = gm_l1;
        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)3ULL), &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 219, 24);
        uint64_t t59 = r57;
        uint64_t t60 = gm_l3;
        uint64_t t61 = (uintptr_t)t59;
        uint64_t t62 = (uintptr_t)t60;
        if (t61 > t62 || t62 > (uintptr_t)t55) gm_trap_slice(t61, t62, t55, gm_srcvmmksi0829pa, 219, 17);
        uint64_t t63 = ((uintptr_t)t60) - (uintptr_t)t59;
        gm_b16_8 t64;
        GM_ST(void *, (uint8_t *)&t64, (void *)(((uint8_t *)t54 + (uintptr_t)t59 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(t63));
        gm_l5 = t64;
        gm_b16_8 t65 = gm_l5;
        gm_b16_8 t66;
        GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s3p0rbs9hc3lg));
        GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(1));
        uint8_t t67 = gm_f_starts_with_6vvt01e(t65, t66);
        if (t67) {
            void * t68 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint64_t t70 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t71 = (uintptr_t)t69;
            if (t70 > t71 || t71 > (uintptr_t)t69) gm_trap_slice(t70, t71, t69, gm_srcvmmksi0829pa, 220, 43);
            uint64_t t72 = ((uintptr_t)t69) - (uintptr_t)((uint64_t)1ULL);
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(((uint8_t *)t68 + (uintptr_t)((uint64_t)1ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(t72));
            gm_l5 = t73;
        }
        uint64_t t74 = gm_l3;
        gm_l6 = t74;
        void * t75 = gm_l0;
        uint64_t t76 = gm_l6;
        gm_tqs5p4n2tq19v t77 = gm_f_byte_b4v8vul(t75, t76);
        gm_tqs5p4n2tq19v t78;
        GM_ST(uint8_t, ((uint8_t *)&t78), ((uint8_t)13ULL));
        ((uint8_t *)&t78)[1] = 1;
        uint8_t t79 = (gm_eqb44cb3cf1n(((uint8_t *)&t77), ((uint8_t *)&t78)));
        if (t79) {
            uint64_t t80 = gm_l6;
            uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
            if (o82) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 222, 36);
            uint64_t t83 = r81;
            gm_l6 = t83;
        }
        void * t84 = gm_l0;
        uint64_t t85 = gm_l6;
        gm_tqs5p4n2tq19v t86 = gm_f_byte_b4v8vul(t84, t85);
        gm_tqs5p4n2tq19v t87;
        GM_ST(uint8_t, ((uint8_t *)&t87), ((uint8_t)10ULL));
        ((uint8_t *)&t87)[1] = 1;
        uint8_t t88 = (gm_eqb44cb3cf1n(((uint8_t *)&t86), ((uint8_t *)&t87)));
        if (t88) {
            uint64_t t89 = gm_l6;
            uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
            if (o91) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 224, 13);
            uint64_t t92 = r90;
            gm_l6 = t92;
            {
                for (;;) {
                    void * t93 = gm_l0;
                    uint64_t t94 = gm_l6;
                    gm_tqs5p4n2tq19v t95 = gm_f_byte_b4v8vul(t93, t94);
                    gm_tqs5p4n2tq19v t96;
                    GM_ST(uint8_t, ((uint8_t *)&t96), ((uint8_t)32ULL));
                    ((uint8_t *)&t96)[1] = 1;
                    gm_tqs5p4n2tq19v t97;
                    GM_ST(uint8_t, ((uint8_t *)&t97), ((uint8_t)9ULL));
                    ((uint8_t *)&t97)[1] = 1;
                    gm_t5r6k0fdcfsb3 t98;
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t98 + 0), t96);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t98 + 2), t97);
                    uint8_t t99 = 0;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t98) + 0, ((uint8_t *)&t95))) t99 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t98) + 2, ((uint8_t *)&t95))) t99 = 1;
                    uint8_t t100 = (uint8_t)(t99);
                    if (!t100) {
                        goto gm_lb4;
                    }
                    {
                        uint64_t t101 = gm_l6;
                        uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                        if (o103) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 225, 50);
                        uint64_t t104 = r102;
                        gm_l6 = t104;
                    }
gm_lb6:;
                }
            }
gm_lb4:;
        }
        void * t105 = gm_l0;
        void * t106 = (void *)(((uint8_t *)t105) + 80);
        void * t107 = gm_l0;
        uint64_t t108 = gm_l1;
        uint64_t t109 = gm_l3;
        gm_tl874t2atnlh3 t110 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t107, t108, t109));
        gm_b16_8 t111 = gm_l5;
        uint64_t t112 = gm_l6;
        uint32_t t113 = (uint32_t)t112;
        gm_t3jim1gvsdr87 t114;
        GM_ST(uint32_t, (uint8_t *)&t114 + 12, 0);
        GM_ST(uint32_t, (uint8_t *)&t114 + 36, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t114 + 0), t110);
        GM_ST(gm_b16_8, ((uint8_t *)&t114 + 16), t111);
        GM_ST(uint32_t, ((uint8_t *)&t114 + 32), t113);
        gm_tftb1odc739t5 t115 = gm_f_push_o6f0em6(t106, &t114);
        gm_l9 = t115;
        uint8_t t116 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t116) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcvmmksi0829pa, 227, 9);
    }
    void * t117 = gm_l0;
    uint64_t t118 = gm_l3;
    GM_ST(uint64_t, (((uint8_t *)t117) + 24), t118);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_lone_cr_ktb0jvm(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_new_lexer_dm12g12 F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    GM_STACK_CHECK(&F, 584u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    void * t4 = gm_l2;
    gm_trqfvmjtt4sij t5;
    gm_f_list_lpokg1m((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t6;
    gm_f_list_5o6u5a9((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t7;
    gm_f_list_2iogcnb((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tc0nbpdhofi37 t8;
    for (size_t i9 = 0; i9 < 3; i9++) GM_ST(uint64_t, (uint8_t *)&t8 + i9 * 8, ((uint64_t)0ULL));
    gm_trqfvmjtt4sij t10;
    gm_f_list_5dhgg4i((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_toceffb697p0u t11;
    memset(&t11, 0, sizeof t11);
    gm_trqfvmjtt4sij t12;
    gm_f_list_go2qg0b((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trd6bg8is1v4n t13;
    GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t13 + 281, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 282, 0);
    GM_ST(uint32_t, (uint8_t *)&t13 + 284, 0);
    GM_ST(uint32_t, ((uint8_t *)&t13 + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 8), t3);
    GM_ST(uint64_t, ((uint8_t *)&t13 + 24), ((uint64_t)0ULL));
    GM_ST(void *, ((uint8_t *)&t13 + 32), t4);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t13 + 40), t5);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t13 + 80), t6);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t13 + 120), t7);
    GM_ST(gm_tc0nbpdhofi37, ((uint8_t *)&t13 + 160), t8);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t13 + 184), t10);
    GM_ST(gm_toceffb697p0u, ((uint8_t *)&t13 + 224), t11);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t13 + 240), t12);
    GM_ST(uint8_t, ((uint8_t *)&t13 + 280), ((uint8_t)0));
    memmove(gm_out, ((uint8_t *)&t13), 288);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_note_newline_cchbr7g(void * a0, uint64_t a1, uint64_t a2);
extern __attribute__((aligned(4))) gm_te0mgnjdphs16 gm_f_one_or_two_lava5tu(uint8_t a0, gm_t9qkh0rsooc8d a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) void gm_f_open_bracket_3kcj4k8(void * a0, gm_t9qkh0rsooc8d a1, gm_cseahvg8v7p0vs a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_open_bracket_3kcj4k8 F;
    void * gm_l0; /* lx */
    gm_t9qkh0rsooc8d gm_l1; /* kind */
    gm_tl874t2atnlh3 gm_l2; /* at */
    void * gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_tl874t2atnlh3, a2);
    gm_tftb1odc739t5 gm_l4;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = gm_l1;
    uint64_t t4 = gm_f_slot_4oe07nr(t3);
    if ((uintptr_t)t4 >= (uintptr_t)(((uint64_t)3ULL))) gm_trap_index((uintptr_t)t4, ((uint64_t)3ULL), gm_srcvmmksi0829pa, 626, 5);
    void * t5 = (void *)((((uint8_t *)t2) + 160) + (uintptr_t)t4 * 8);
    gm_l3 = t5;
    void * t6 = gm_l3;
    void * t7 = gm_l3;
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)t7));
    uint64_t r9; bool o10 = __builtin_add_overflow(t8, ((uint64_t)1ULL), &r9);
    if (o10) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 626, 5);
    uint64_t t11 = r9;
    GM_ST(uint64_t, ((uint8_t *)t6), t11);
    void * t12 = gm_l0;
    void * t13 = (void *)(((uint8_t *)t12) + 120);
    gm_t9qkh0rsooc8d t14 = gm_l1;
    gm_tl874t2atnlh3 t15 = gm_l2;
    gm_t2eb59dvglnle t16;
    GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t16 + 0), t14);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t16 + 4), t15);
    gm_tftb1odc739t5 t17 = gm_f_push_vs3f95a(t13, t16);
    gm_l4 = t17;
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcvmmksi0829pa, 627, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_opener_8hqhoc3(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_opener_8hqhoc3 F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 16u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d gm_l1;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)0ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)61ULL));
            gm_l1 = t11;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)1ULL));
            if (!t13) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)63ULL));
            gm_l1 = t14;
            goto gm_lb1;
        }
gm_lb3:;
        {
            {
                {
                    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t16 = (uint8_t)(t15 == ((uint8_t)2ULL));
                    if (!t16) {
                        goto gm_lb6;
                    }
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)3ULL));
                    if (!t18) {
                        goto gm_lb7;
                    }
                    goto gm_lb5;
                }
gm_lb7:;
                goto gm_lb4;
            }
gm_lb5:;
            gm_t9qkh0rsooc8d t19;
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)65ULL));
            gm_l1 = t19;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 47, 42);
    }
gm_lb1:;
    gm_t9qkh0rsooc8d t20 = gm_l1;
    F.ret = t20;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_72bostn(void * a0);
extern __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_peek_at_74p8lkq(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_push_newline_ke0tojq(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_newline_ke0tojq F;
    void * gm_l0; /* lx */
    gm_tl874t2atnlh3 gm_l1; /* at */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 272u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_tftb1odc739t5 gm_l3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 40) + 8));
    gm_l2 = t5;
    {
        {
            uint64_t t6 = gm_l2;
            uint8_t t7 = (t6 > ((uint64_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            void * t8 = gm_l0;
            void * t9 = GM_LD(void *, (((uint8_t *)t8) + 40));
            uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t8) + 40) + 8));
            uint64_t t11 = gm_l2;
            uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
            if (o13) gm_trap("integer overflow in `-`", gm_srcvmmksi0829pa, 662, 34);
            uint64_t t14 = r12;
            if ((uintptr_t)t14 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t14, t10, gm_srcvmmksi0829pa, 662, 18);
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t9) + (uintptr_t)t14 * 48) + 0));
            uint8_t t16 = (uint8_t)(!(t15 == ((uint8_t)9ULL)));
            if (!t16) {
                goto gm_lb2;
            }
            void * t17 = gm_l0;
            void * t18 = (void *)(((uint8_t *)t17) + 40);
            gm_tl874t2atnlh3 t19 = gm_l1;
            gm_t3fcbuehh3bs2 t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t20 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t20 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t20 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)9ULL));
            gm_tm4pc0emhrv1g t21;
            GM_ST(uint32_t, (uint8_t *)&t21 + 44, 0);
            GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t21 + 0), t20);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t21 + 32), t19);
            gm_t3fcbuehh3bs2 t22 = GM_LD(gm_t3fcbuehh3bs2, (((uint8_t *)t9) + (uintptr_t)t14 * 48));
            gm_t3fcbuehh3bs2 t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t23 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t23 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t23 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)9ULL));
            gm_tftb1odc739t5 t24 = gm_f_push_pnf14h1(t18, &t21);
            gm_l3 = t24;
            uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcvmmksi0829pa, 663, 9);
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
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_run_v26is74 F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* b */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t2eb59dvglnle gm_l5; /* b */
    GM_STACK_CHECK(&F, 873u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l6;
    gm_tqs5p4n2tq19v gm_l7;
    uint8_t gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tftb1odc739t5 gm_l10;
    void * t10 = gm_l0;
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 8));
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_srh29phg8b30r));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(3));
    uint8_t t13 = gm_f_starts_with_6vvt01e(t11, t12);
    if (t13) {
        void * t14 = gm_l0;
        GM_ST(uint64_t, (((uint8_t *)t14) + 24), ((uint64_t)3ULL));
    }
    void * t15 = gm_l0;
    void * t16 = (void *)(((uint8_t *)t15) + 40);
    void * t17 = gm_l0;
    uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t17) + 8) + 8));
    if (((uint64_t)4ULL) == 0) gm_trap("division by zero", gm_srcvmmksi0829pa, 128, 23);
    uint64_t t19 = (uint64_t)(t18 / ((uint64_t)4ULL));
    uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)16ULL), &r20);
    if (o21) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 128, 23);
    uint64_t t22 = r20;
    gm_tftb1odc739t5 t23 = gm_f_reserve_i65s8oj(t16, t22);
    gm_l6 = t23;
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcvmmksi0829pa, 128, 5);
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t25 = gm_l0;
                gm_b0_1 t26 = gm_f_skip_trivia_014s7ai((void *)NULL, cx, NULL, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t27 = gm_l0;
                gm_tqs5p4n2tq19v t28 = gm_f_peek_72bostn(t27);
                gm_l7 = t28;
                uint8_t t29 = (uint8_t)((((uint8_t *)&gm_l7))[1] != 0);
                if (t29) {
                    uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                    gm_l8 = t30;
                } else {
                    goto gm_lb1;
                }
                uint8_t t31 = gm_l8;
                gm_l1 = t31;
                void * t32 = gm_l0;
                uint8_t t33 = gm_l1;
                gm_b0_1 t34 = gm_f_lex_token_h8g44s4((void *)NULL, cx, NULL, t32, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t35 = gm_l0;
    uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)t35) + 8) + 8));
    gm_l2 = t36;
    void * t37 = gm_l0;
    uint64_t t38 = gm_l2;
    gm_t3fcbuehh3bs2 t39;
    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t39 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t39 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)10ULL));
    gm_f_flush_newline_9023ml8(t37, &t39, t38);
    void * t40 = gm_l0;
    gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)t40) + 120));
    gm_l3 = t41;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t42 = gm_l4;
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t44 = (t42 < t43);
            if (!t44) {
                goto gm_lb4;
            }
            {
                void * t45 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t47 = gm_l4;
                {
                    {
                        gm_t2eb59dvglnle t48 = GM_LD(gm_t2eb59dvglnle, (((uint8_t *)t45) + (uintptr_t)t47 * 16));
                        gm_l5 = t48;
                        void * t49 = gm_l0;
                        void * t50 = GM_LD(void *, (((uint8_t *)t49) + 32));
                        gm_tl874t2atnlh3 t51 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 4));
                        gm_t9qkh0rsooc8d t52 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l5));
                        gm_t9qkh0rsooc8d t53 = gm_f_opener_8hqhoc3(t52);
                        gm_b16_8 t54 = gm_f_punct_text_ov4r40u(t53);
                        gm_tnpifv20irnph t55;
                        GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
                        gm_f_format_t32jdqr((void *)&gm_l9, (void *)NULL, cx, NULL, t55);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcvmmksi0829pa, 137, 33);
                        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                        gm_b0_1 t59 = gm_f_report_eardf07((void *)NULL, cx, NULL, t50, t51, t58);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 136, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t60 = gm_l4;
            uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
            uint64_t t63 = r61;
            gm_l4 = t63;
        }
    }
gm_lb4:;
    void * t64 = gm_l0;
    void * t65 = (void *)(((uint8_t *)t64) + 40);
    void * t66 = gm_l0;
    uint64_t t67 = gm_l2;
    uint64_t t68 = gm_l2;
    gm_t3fcbuehh3bs2 t69;
    GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t69 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t69 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t69 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)10ULL));
    gm_tl874t2atnlh3 t70 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_tg7cev6(t66, t67, t68));
    gm_tm4pc0emhrv1g t71;
    GM_ST(uint32_t, (uint8_t *)&t71 + 44, 0);
    GM_ST(gm_t3fcbuehh3bs2, ((uint8_t *)&t71 + 0), t69);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t71 + 32), t70);
    gm_tftb1odc739t5 t72 = gm_f_push_pnf14h1(t65, &t71);
    gm_l10 = t72;
    uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcvmmksi0829pa, 139, 5);
    void * t74 = gm_l0;
    void * t75 = gm_l0;
    void * t76 = gm_l0;
    gm_trqfvmjtt4sij t77 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t74) + 40));
    gm_trqfvmjtt4sij t78 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t75) + 80));
    gm_trqfvmjtt4sij t79 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t76) + 240));
    gm_tr8dvlu9ik4gj t80;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t80 + 0), t77);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t80 + 40), t78);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t80 + 80), t79);
    memmove(gm_out, ((uint8_t *)&t80), 120);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_scan_ident_h1b2n4u(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scan_ident_h1b2n4u F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* start */
    uint8_t gm_l2; /* c */
    GM_STACK_CHECK(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l3;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 24));
    gm_l1 = t6;
    {
        for (;;) {
            void * t7 = gm_l0;
            gm_tqs5p4n2tq19v t8 = gm_f_peek_72bostn(t7);
            gm_l3 = t8;
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l3))[1] != 0);
            if (!t9) {
                goto gm_lb1;
            }
            uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
            gm_l2 = t10;
            {
                uint8_t t11 = gm_l2;
                uint8_t t12 = gm_f_is_ident_cont_317fpdb(t11);
                uint8_t t13 = (uint8_t)!t12;
                if (t13) {
                    goto gm_lb1;
                }
                void * t14 = gm_l0;
                void * t15 = gm_l0;
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 24));
                uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
                if (o18) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 355, 9);
                uint64_t t19 = r17;
                GM_ST(uint64_t, (((uint8_t *)t14) + 24), t19);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 8));
    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 8) + 8));
    uint64_t t23 = gm_l1;
    void * t24 = gm_l0;
    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 24));
    uint64_t t26 = (uintptr_t)t23;
    uint64_t t27 = (uintptr_t)t25;
    if (t26 > t27 || t27 > (uintptr_t)t22) gm_trap_slice(t26, t27, t22, gm_srcvmmksi0829pa, 357, 12);
    uint64_t t28 = ((uintptr_t)t25) - (uintptr_t)t23;
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t21 + (uintptr_t)t23 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(t28));
    F.ret = t29;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_scan_punct_ecrh51c(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scan_punct_ecrh51c F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* first */
    uint8_t gm_l2; /* a1 */
    uint8_t gm_l3; /* a2 */
    uint8_t gm_l4; /* a3 */
    gm_t9qkh0rsooc8d gm_l5; /* p */
    uint64_t gm_l6; /* len */
    gm_t6ofeqo2h242p gm_l7; /* fl */
    GM_STACK_CHECK_SHALLOW(&F, 1582u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l8;
    uint8_t gm_l9;
    gm_tqs5p4n2tq19v gm_l10;
    uint8_t gm_l11;
    gm_tqs5p4n2tq19v gm_l12;
    uint8_t gm_l13;
    gm_te0mgnjdphs16 gm_l14;
    gm_te0mgnjdphs16 gm_l15;
    gm_te0mgnjdphs16 gm_l16;
    gm_te0mgnjdphs16 gm_l17;
    gm_te0mgnjdphs16 gm_l18;
    gm_te0mgnjdphs16 gm_l19;
    gm_te0mgnjdphs16 gm_l20;
    gm_te0mgnjdphs16 gm_l21;
    gm_te0mgnjdphs16 gm_l22;
    gm_te0mgnjdphs16 gm_l23;
    gm_te0mgnjdphs16 gm_l24;
    gm_te0mgnjdphs16 gm_l25;
    gm_te0mgnjdphs16 gm_l26;
    gm_te0mgnjdphs16 gm_l27;
    gm_te0mgnjdphs16 gm_l28;
    gm_te0mgnjdphs16 gm_l29;
    gm_te0mgnjdphs16 gm_l30;
    gm_te0mgnjdphs16 gm_l31;
    gm_te0mgnjdphs16 gm_l32;
    gm_te0mgnjdphs16 gm_l33;
    gm_te0mgnjdphs16 gm_l34;
    gm_te0mgnjdphs16 gm_l35;
    gm_te0mgnjdphs16 gm_l36;
    gm_te0mgnjdphs16 gm_l37;
    gm_te0mgnjdphs16 gm_l38;
    gm_te0mgnjdphs16 gm_l39;
    gm_te0mgnjdphs16 gm_l40;
    gm_te0mgnjdphs16 gm_l41;
    gm_te0mgnjdphs16 gm_l42;
    gm_te0mgnjdphs16 gm_l43;
    {
        void * t47 = gm_l0;
        void * t48 = GM_LD(void *, (((uint8_t *)t47) + 8));
        uint64_t t49 = GM_LD(uint64_t, ((((uint8_t *)t47) + 8) + 8));
        void * t50 = gm_l0;
        uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 24));
        if ((uintptr_t)t51 >= (uintptr_t)(t49)) gm_trap_index((uintptr_t)t51, t49, gm_srcvmmksi0829pa, 429, 14);
        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)t48) + (uintptr_t)t51 * 1));
        gm_l1 = t52;
        void * t53 = gm_l0;
        gm_tqs5p4n2tq19v t54 = gm_f_peek_at_74p8lkq(t53, ((uint64_t)1ULL));
        gm_l8 = t54;
        uint8_t t55 = (uint8_t)((((uint8_t *)&gm_l8))[1] != 0);
        if (t55) {
            uint8_t t56 = GM_LD(uint8_t, ((uint8_t *)&gm_l8));
            gm_l9 = t56;
        } else {
            gm_l9 = ((uint8_t)0ULL);
        }
        uint8_t t57 = gm_l9;
        gm_l2 = t57;
        void * t58 = gm_l0;
        gm_tqs5p4n2tq19v t59 = gm_f_peek_at_74p8lkq(t58, ((uint64_t)2ULL));
        gm_l10 = t59;
        uint8_t t60 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
        if (t60) {
            uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
            gm_l11 = t61;
        } else {
            gm_l11 = ((uint8_t)0ULL);
        }
        uint8_t t62 = gm_l11;
        gm_l3 = t62;
        void * t63 = gm_l0;
        gm_tqs5p4n2tq19v t64 = gm_f_peek_at_74p8lkq(t63, ((uint64_t)3ULL));
        gm_l12 = t64;
        uint8_t t65 = (uint8_t)((((uint8_t *)&gm_l12))[1] != 0);
        if (t65) {
            uint8_t t66 = GM_LD(uint8_t, ((uint8_t *)&gm_l12));
            gm_l13 = t66;
        } else {
            gm_l13 = ((uint8_t)0ULL);
        }
        uint8_t t67 = gm_l13;
        gm_l4 = t67;
        gm_t9qkh0rsooc8d t68;
        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)0ULL));
        gm_l5 = t68;
        gm_l6 = ((uint64_t)1ULL);
        {
            {
                {
                    {
                        {
                            uint8_t t69 = gm_l1;
                            uint8_t t70 = ((t69 == ((uint8_t)43ULL)));
                            if (!t70) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t71 = gm_l1;
                            uint8_t t72 = ((t71 == ((uint8_t)45ULL)));
                            if (!t72) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t73 = gm_l1;
                            uint8_t t74 = ((t73 == ((uint8_t)42ULL)));
                            if (!t74) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    uint8_t t75 = gm_l1;
                    gm_t6ofeqo2h242p t76 = gm_f_flavors_qsu4oq0(t75);
                    gm_l7 = t76;
                    {
                        {
                            uint8_t t77 = gm_l1;
                            uint8_t t78 = ((t77 == ((uint8_t)45ULL)));
                            if (!t78) {
                                goto gm_lb8;
                            }
                            uint8_t t79 = gm_l2;
                            uint8_t t80 = ((t79 == ((uint8_t)45ULL)));
                            if (!t80) {
                                goto gm_lb8;
                            }
                            uint8_t t81 = gm_l3;
                            uint8_t t82 = ((t81 == ((uint8_t)45ULL)));
                            if (!t82) {
                                goto gm_lb8;
                            }
                            gm_t9qkh0rsooc8d t83;
                            GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)53ULL));
                            gm_te0mgnjdphs16 t84;
                            GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t84 + 0), t83);
                            GM_ST(uint64_t, ((uint8_t *)&t84 + 8), ((uint64_t)3ULL));
                            gm_l14 = t84;
                            gm_t9qkh0rsooc8d t85 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l14));
                            gm_l5 = t85;
                            uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                            gm_l6 = t86;
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            {
                                uint8_t t87 = gm_l1;
                                uint8_t t88 = ((t87 == ((uint8_t)45ULL)));
                                if (!t88) {
                                    goto gm_lb10;
                                }
                                uint8_t t89 = gm_l2;
                                uint8_t t90 = ((t89 == ((uint8_t)62ULL)));
                                if (!t90) {
                                    goto gm_lb10;
                                }
                                gm_t9qkh0rsooc8d t91;
                                GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)60ULL));
                                gm_te0mgnjdphs16 t92;
                                GM_ST(uint8_t, (uint8_t *)&t92 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t92 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t92 + 4, 0);
                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t92 + 0), t91);
                                GM_ST(uint64_t, ((uint8_t *)&t92 + 8), ((uint64_t)2ULL));
                                gm_l15 = t92;
                                gm_t9qkh0rsooc8d t93 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l15));
                                gm_l5 = t93;
                                uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                gm_l6 = t94;
                                goto gm_lb9;
                            }
gm_lb10:;
                            uint8_t t95 = gm_l2;
                            uint8_t t96 = ((t95 == ((uint8_t)61ULL)));
                            if (t96) {
                                gm_t9qkh0rsooc8d t97 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 1));
                                gm_te0mgnjdphs16 t98;
                                GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t98 + 0), t97);
                                GM_ST(uint64_t, ((uint8_t *)&t98 + 8), ((uint64_t)2ULL));
                                gm_l16 = t98;
                                gm_t9qkh0rsooc8d t99 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l16));
                                gm_l5 = t99;
                                uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                gm_l6 = t100;
                            } else {
                                {
                                    {
                                        uint8_t t101 = gm_l2;
                                        uint8_t t102 = ((t101 == ((uint8_t)37ULL)));
                                        if (!t102) {
                                            goto gm_lb12;
                                        }
                                        uint8_t t103 = gm_l3;
                                        uint8_t t104 = ((t103 == ((uint8_t)61ULL)));
                                        if (!t104) {
                                            goto gm_lb12;
                                        }
                                        gm_t9qkh0rsooc8d t105 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 5));
                                        gm_te0mgnjdphs16 t106;
                                        GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t106 + 0), t105);
                                        GM_ST(uint64_t, ((uint8_t *)&t106 + 8), ((uint64_t)3ULL));
                                        gm_l17 = t106;
                                        gm_t9qkh0rsooc8d t107 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l17));
                                        gm_l5 = t107;
                                        uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                        gm_l6 = t108;
                                        goto gm_lb11;
                                    }
gm_lb12:;
                                    uint8_t t109 = gm_l2;
                                    uint8_t t110 = ((t109 == ((uint8_t)37ULL)));
                                    if (t110) {
                                        gm_t9qkh0rsooc8d t111 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 2));
                                        gm_te0mgnjdphs16 t112;
                                        GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t112 + 0), t111);
                                        GM_ST(uint64_t, ((uint8_t *)&t112 + 8), ((uint64_t)2ULL));
                                        gm_l18 = t112;
                                        gm_t9qkh0rsooc8d t113 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l18));
                                        gm_l5 = t113;
                                        uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                        gm_l6 = t114;
                                    } else {
                                        {
                                            {
                                                uint8_t t115 = gm_l2;
                                                uint8_t t116 = ((t115 == ((uint8_t)124ULL)));
                                                if (!t116) {
                                                    goto gm_lb14;
                                                }
                                                uint8_t t117 = gm_l3;
                                                uint8_t t118 = ((t117 == ((uint8_t)61ULL)));
                                                if (!t118) {
                                                    goto gm_lb14;
                                                }
                                                gm_t9qkh0rsooc8d t119 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 6));
                                                gm_te0mgnjdphs16 t120;
                                                GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t120 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t120 + 4, 0);
                                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t120 + 0), t119);
                                                GM_ST(uint64_t, ((uint8_t *)&t120 + 8), ((uint64_t)3ULL));
                                                gm_l19 = t120;
                                                gm_t9qkh0rsooc8d t121 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l19));
                                                gm_l5 = t121;
                                                uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                                gm_l6 = t122;
                                                goto gm_lb13;
                                            }
gm_lb14:;
                                            uint8_t t123 = gm_l2;
                                            uint8_t t124 = ((t123 == ((uint8_t)124ULL)));
                                            if (t124) {
                                                gm_t9qkh0rsooc8d t125 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 3));
                                                gm_te0mgnjdphs16 t126;
                                                GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
                                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t126 + 0), t125);
                                                GM_ST(uint64_t, ((uint8_t *)&t126 + 8), ((uint64_t)2ULL));
                                                gm_l20 = t126;
                                                gm_t9qkh0rsooc8d t127 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l20));
                                                gm_l5 = t127;
                                                uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                                gm_l6 = t128;
                                            } else {
                                                uint8_t t129 = gm_l2;
                                                uint8_t t130 = ((t129 == ((uint8_t)63ULL)));
                                                if (t130) {
                                                    gm_t9qkh0rsooc8d t131 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l7) + 4));
                                                    gm_te0mgnjdphs16 t132;
                                                    GM_ST(uint8_t, (uint8_t *)&t132 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t132 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t132 + 4, 0);
                                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t132 + 0), t131);
                                                    GM_ST(uint64_t, ((uint8_t *)&t132 + 8), ((uint64_t)2ULL));
                                                    gm_l21 = t132;
                                                    gm_t9qkh0rsooc8d t133 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l21));
                                                    gm_l5 = t133;
                                                    uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                                    gm_l6 = t134;
                                                } else {
                                                    gm_t9qkh0rsooc8d t135 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l7));
                                                    gm_l5 = t135;
                                                }
                                            }
                                        }
gm_lb13:;
                                    }
                                }
gm_lb11:;
                            }
                        }
gm_lb9:;
                    }
gm_lb7:;
                    goto gm_lb1;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t136 = gm_l1;
                    uint8_t t137 = ((t136 == ((uint8_t)47ULL)));
                    if (!t137) {
                        goto gm_lb15;
                    }
                    uint8_t t138 = gm_l2;
                    uint8_t t139 = ((t138 == ((uint8_t)61ULL)));
                    gm_t9qkh0rsooc8d t140;
                    GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)26ULL));
                    gm_t9qkh0rsooc8d t141;
                    GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)3ULL));
                    gm_te0mgnjdphs16 t142 = gm_f_one_or_two_lava5tu(t139, t140, t141);
                    gm_l22 = t142;
                    gm_t9qkh0rsooc8d t143 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l22));
                    gm_l5 = t143;
                    uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                    gm_l6 = t144;
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t145 = gm_l1;
                    uint8_t t146 = ((t145 == ((uint8_t)37ULL)));
                    if (!t146) {
                        goto gm_lb16;
                    }
                    uint8_t t147 = gm_l2;
                    uint8_t t148 = ((t147 == ((uint8_t)61ULL)));
                    gm_t9qkh0rsooc8d t149;
                    GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)27ULL));
                    gm_t9qkh0rsooc8d t150;
                    GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)4ULL));
                    gm_te0mgnjdphs16 t151 = gm_f_one_or_two_lava5tu(t148, t149, t150);
                    gm_l23 = t151;
                    gm_t9qkh0rsooc8d t152 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l23));
                    gm_l5 = t152;
                    uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                    gm_l6 = t153;
                    goto gm_lb1;
                }
gm_lb16:;
                {
                    uint8_t t154 = gm_l1;
                    uint8_t t155 = ((t154 == ((uint8_t)60ULL)));
                    if (!t155) {
                        goto gm_lb17;
                    }
                    uint8_t t156 = gm_l2;
                    uint8_t t157 = ((t156 == ((uint8_t)60ULL)));
                    if (t157) {
                        {
                            {
                                uint8_t t158 = gm_l3;
                                uint8_t t159 = ((t158 == ((uint8_t)37ULL)));
                                if (!t159) {
                                    goto gm_lb19;
                                }
                                uint8_t t160 = gm_l4;
                                uint8_t t161 = ((t160 == ((uint8_t)61ULL)));
                                if (!t161) {
                                    goto gm_lb19;
                                }
                                gm_t9qkh0rsooc8d t162;
                                GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)36ULL));
                                gm_te0mgnjdphs16 t163;
                                GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t163 + 0), t162);
                                GM_ST(uint64_t, ((uint8_t *)&t163 + 8), ((uint64_t)4ULL));
                                gm_l24 = t163;
                                gm_t9qkh0rsooc8d t164 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l24));
                                gm_l5 = t164;
                                uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                gm_l6 = t165;
                                goto gm_lb18;
                            }
gm_lb19:;
                            {
                                {
                                    uint8_t t166 = gm_l3;
                                    uint8_t t167 = ((t166 == ((uint8_t)124ULL)));
                                    if (!t167) {
                                        goto gm_lb21;
                                    }
                                    uint8_t t168 = gm_l4;
                                    uint8_t t169 = ((t168 == ((uint8_t)61ULL)));
                                    if (!t169) {
                                        goto gm_lb21;
                                    }
                                    gm_t9qkh0rsooc8d t170;
                                    GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)40ULL));
                                    gm_te0mgnjdphs16 t171;
                                    GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t171 + 0), t170);
                                    GM_ST(uint64_t, ((uint8_t *)&t171 + 8), ((uint64_t)4ULL));
                                    gm_l25 = t171;
                                    gm_t9qkh0rsooc8d t172 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l25));
                                    gm_l5 = t172;
                                    uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                    gm_l6 = t173;
                                    goto gm_lb20;
                                }
gm_lb21:;
                                uint8_t t174 = gm_l3;
                                uint8_t t175 = ((t174 == ((uint8_t)37ULL)));
                                if (t175) {
                                    gm_t9qkh0rsooc8d t176;
                                    GM_ST(uint8_t, ((uint8_t *)&t176 + 0), ((uint8_t)13ULL));
                                    gm_te0mgnjdphs16 t177;
                                    GM_ST(uint8_t, (uint8_t *)&t177 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t177 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t177 + 4, 0);
                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t177 + 0), t176);
                                    GM_ST(uint64_t, ((uint8_t *)&t177 + 8), ((uint64_t)3ULL));
                                    gm_l26 = t177;
                                    gm_t9qkh0rsooc8d t178 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l26));
                                    gm_l5 = t178;
                                    uint64_t t179 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                    gm_l6 = t179;
                                } else {
                                    uint8_t t180 = gm_l3;
                                    uint8_t t181 = ((t180 == ((uint8_t)124ULL)));
                                    if (t181) {
                                        gm_t9qkh0rsooc8d t182;
                                        GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)17ULL));
                                        gm_te0mgnjdphs16 t183;
                                        GM_ST(uint8_t, (uint8_t *)&t183 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t183 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t183 + 4, 0);
                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t183 + 0), t182);
                                        GM_ST(uint64_t, ((uint8_t *)&t183 + 8), ((uint64_t)3ULL));
                                        gm_l27 = t183;
                                        gm_t9qkh0rsooc8d t184 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l27));
                                        gm_l5 = t184;
                                        uint64_t t185 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                                        gm_l6 = t185;
                                    } else {
                                        uint8_t t186 = gm_l3;
                                        uint8_t t187 = ((t186 == ((uint8_t)63ULL)));
                                        if (t187) {
                                            gm_t9qkh0rsooc8d t188;
                                            GM_ST(uint8_t, ((uint8_t *)&t188 + 0), ((uint8_t)21ULL));
                                            gm_te0mgnjdphs16 t189;
                                            GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
                                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t189 + 0), t188);
                                            GM_ST(uint64_t, ((uint8_t *)&t189 + 8), ((uint64_t)3ULL));
                                            gm_l28 = t189;
                                            gm_t9qkh0rsooc8d t190 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l28));
                                            gm_l5 = t190;
                                            uint64_t t191 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                            gm_l6 = t191;
                                        } else {
                                            uint8_t t192 = gm_l3;
                                            uint8_t t193 = ((t192 == ((uint8_t)61ULL)));
                                            if (t193) {
                                                gm_t9qkh0rsooc8d t194;
                                                GM_ST(uint8_t, ((uint8_t *)&t194 + 0), ((uint8_t)28ULL));
                                                gm_te0mgnjdphs16 t195;
                                                GM_ST(uint8_t, (uint8_t *)&t195 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t195 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t195 + 4, 0);
                                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t195 + 0), t194);
                                                GM_ST(uint64_t, ((uint8_t *)&t195 + 8), ((uint64_t)3ULL));
                                                gm_l29 = t195;
                                                gm_t9qkh0rsooc8d t196 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l29));
                                                gm_l5 = t196;
                                                uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                                gm_l6 = t197;
                                            } else {
                                                gm_t9qkh0rsooc8d t198;
                                                GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)5ULL));
                                                gm_te0mgnjdphs16 t199;
                                                GM_ST(uint8_t, (uint8_t *)&t199 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t199 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t199 + 4, 0);
                                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t199 + 0), t198);
                                                GM_ST(uint64_t, ((uint8_t *)&t199 + 8), ((uint64_t)2ULL));
                                                gm_l30 = t199;
                                                gm_t9qkh0rsooc8d t200 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l30));
                                                gm_l5 = t200;
                                                uint64_t t201 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                                gm_l6 = t201;
                                            }
                                        }
                                    }
                                }
                            }
gm_lb20:;
                        }
gm_lb18:;
                    } else {
                        uint8_t t202 = gm_l2;
                        uint8_t t203 = ((t202 == ((uint8_t)61ULL)));
                        if (t203) {
                            gm_t9qkh0rsooc8d t204;
                            GM_ST(uint8_t, ((uint8_t *)&t204 + 0), ((uint8_t)42ULL));
                            gm_te0mgnjdphs16 t205;
                            GM_ST(uint8_t, (uint8_t *)&t205 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t205 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t205 + 4, 0);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t205 + 0), t204);
                            GM_ST(uint64_t, ((uint8_t *)&t205 + 8), ((uint64_t)2ULL));
                            gm_l31 = t205;
                            gm_t9qkh0rsooc8d t206 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l31));
                            gm_l5 = t206;
                            uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                            gm_l6 = t207;
                        } else {
                            gm_t9qkh0rsooc8d t208;
                            GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)41ULL));
                            gm_l5 = t208;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb17:;
            }
            {
                {
                    uint8_t t209 = gm_l1;
                    uint8_t t210 = ((t209 == ((uint8_t)62ULL)));
                    if (!t210) {
                        goto gm_lb22;
                    }
                    {
                        {
                            uint8_t t211 = gm_l2;
                            uint8_t t212 = ((t211 == ((uint8_t)62ULL)));
                            if (!t212) {
                                goto gm_lb24;
                            }
                            uint8_t t213 = gm_l3;
                            uint8_t t214 = ((t213 == ((uint8_t)61ULL)));
                            if (!t214) {
                                goto gm_lb24;
                            }
                            gm_t9qkh0rsooc8d t215;
                            GM_ST(uint8_t, ((uint8_t *)&t215 + 0), ((uint8_t)29ULL));
                            gm_te0mgnjdphs16 t216;
                            GM_ST(uint8_t, (uint8_t *)&t216 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t216 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t216 + 4, 0);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t216 + 0), t215);
                            GM_ST(uint64_t, ((uint8_t *)&t216 + 8), ((uint64_t)3ULL));
                            gm_l32 = t216;
                            gm_t9qkh0rsooc8d t217 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l32));
                            gm_l5 = t217;
                            uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                            gm_l6 = t218;
                            goto gm_lb23;
                        }
gm_lb24:;
                        uint8_t t219 = gm_l2;
                        uint8_t t220 = ((t219 == ((uint8_t)62ULL)));
                        if (t220) {
                            gm_t9qkh0rsooc8d t221;
                            GM_ST(uint8_t, ((uint8_t *)&t221 + 0), ((uint8_t)6ULL));
                            gm_te0mgnjdphs16 t222;
                            GM_ST(uint8_t, (uint8_t *)&t222 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t222 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t222 + 4, 0);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t222 + 0), t221);
                            GM_ST(uint64_t, ((uint8_t *)&t222 + 8), ((uint64_t)2ULL));
                            gm_l33 = t222;
                            gm_t9qkh0rsooc8d t223 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l33));
                            gm_l5 = t223;
                            uint64_t t224 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                            gm_l6 = t224;
                        } else {
                            uint8_t t225 = gm_l2;
                            uint8_t t226 = ((t225 == ((uint8_t)61ULL)));
                            if (t226) {
                                gm_t9qkh0rsooc8d t227;
                                GM_ST(uint8_t, ((uint8_t *)&t227 + 0), ((uint8_t)44ULL));
                                gm_te0mgnjdphs16 t228;
                                GM_ST(uint8_t, (uint8_t *)&t228 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t228 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t228 + 4, 0);
                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t228 + 0), t227);
                                GM_ST(uint64_t, ((uint8_t *)&t228 + 8), ((uint64_t)2ULL));
                                gm_l34 = t228;
                                gm_t9qkh0rsooc8d t229 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l34));
                                gm_l5 = t229;
                                uint64_t t230 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                gm_l6 = t230;
                            } else {
                                gm_t9qkh0rsooc8d t231;
                                GM_ST(uint8_t, ((uint8_t *)&t231 + 0), ((uint8_t)43ULL));
                                gm_l5 = t231;
                            }
                        }
                    }
gm_lb23:;
                    goto gm_lb1;
                }
gm_lb22:;
                {
                    uint8_t t232 = gm_l1;
                    uint8_t t233 = ((t232 == ((uint8_t)38ULL)));
                    if (!t233) {
                        goto gm_lb25;
                    }
                    uint8_t t234 = gm_l2;
                    uint8_t t235 = ((t234 == ((uint8_t)61ULL)));
                    gm_t9qkh0rsooc8d t236;
                    GM_ST(uint8_t, ((uint8_t *)&t236 + 0), ((uint8_t)30ULL));
                    gm_t9qkh0rsooc8d t237;
                    GM_ST(uint8_t, ((uint8_t *)&t237 + 0), ((uint8_t)7ULL));
                    gm_te0mgnjdphs16 t238 = gm_f_one_or_two_lava5tu(t235, t236, t237);
                    gm_l35 = t238;
                    gm_t9qkh0rsooc8d t239 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l35));
                    gm_l5 = t239;
                    uint64_t t240 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                    gm_l6 = t240;
                    goto gm_lb1;
                }
gm_lb25:;
                {
                    uint8_t t241 = gm_l1;
                    uint8_t t242 = ((t241 == ((uint8_t)124ULL)));
                    if (!t242) {
                        goto gm_lb26;
                    }
                    uint8_t t243 = gm_l2;
                    uint8_t t244 = ((t243 == ((uint8_t)61ULL)));
                    gm_t9qkh0rsooc8d t245;
                    GM_ST(uint8_t, ((uint8_t *)&t245 + 0), ((uint8_t)31ULL));
                    gm_t9qkh0rsooc8d t246;
                    GM_ST(uint8_t, ((uint8_t *)&t246 + 0), ((uint8_t)8ULL));
                    gm_te0mgnjdphs16 t247 = gm_f_one_or_two_lava5tu(t244, t245, t246);
                    gm_l36 = t247;
                    gm_t9qkh0rsooc8d t248 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l36));
                    gm_l5 = t248;
                    uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                    gm_l6 = t249;
                    goto gm_lb1;
                }
gm_lb26:;
            }
            {
                {
                    uint8_t t250 = gm_l1;
                    uint8_t t251 = ((t250 == ((uint8_t)126ULL)));
                    if (!t251) {
                        goto gm_lb27;
                    }
                    uint8_t t252 = gm_l2;
                    uint8_t t253 = ((t252 == ((uint8_t)61ULL)));
                    gm_t9qkh0rsooc8d t254;
                    GM_ST(uint8_t, ((uint8_t *)&t254 + 0), ((uint8_t)32ULL));
                    gm_t9qkh0rsooc8d t255;
                    GM_ST(uint8_t, ((uint8_t *)&t255 + 0), ((uint8_t)9ULL));
                    gm_te0mgnjdphs16 t256 = gm_f_one_or_two_lava5tu(t253, t254, t255);
                    gm_l37 = t256;
                    gm_t9qkh0rsooc8d t257 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l37));
                    gm_l5 = t257;
                    uint64_t t258 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                    gm_l6 = t258;
                    goto gm_lb1;
                }
gm_lb27:;
                {
                    uint8_t t259 = gm_l1;
                    uint8_t t260 = ((t259 == ((uint8_t)61ULL)));
                    if (!t260) {
                        goto gm_lb28;
                    }
                    uint8_t t261 = gm_l2;
                    uint8_t t262 = ((t261 == ((uint8_t)62ULL)));
                    gm_t9qkh0rsooc8d t263;
                    GM_ST(uint8_t, ((uint8_t *)&t263 + 0), ((uint8_t)59ULL));
                    gm_t9qkh0rsooc8d t264;
                    GM_ST(uint8_t, ((uint8_t *)&t264 + 0), ((uint8_t)22ULL));
                    gm_te0mgnjdphs16 t265 = gm_f_one_or_two_lava5tu(t262, t263, t264);
                    gm_l38 = t265;
                    gm_t9qkh0rsooc8d t266 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l38));
                    gm_l5 = t266;
                    uint64_t t267 = GM_LD(uint64_t, (((uint8_t *)&gm_l38) + 8));
                    gm_l6 = t267;
                    goto gm_lb1;
                }
gm_lb28:;
                {
                    uint8_t t268 = gm_l1;
                    uint8_t t269 = ((t268 == ((uint8_t)46ULL)));
                    if (!t269) {
                        goto gm_lb29;
                    }
                    {
                        {
                            uint8_t t270 = gm_l2;
                            uint8_t t271 = ((t270 == ((uint8_t)46ULL)));
                            if (!t271) {
                                goto gm_lb31;
                            }
                            uint8_t t272 = gm_l3;
                            uint8_t t273 = ((t272 == ((uint8_t)46ULL)));
                            if (!t273) {
                                goto gm_lb31;
                            }
                            gm_t9qkh0rsooc8d t274;
                            GM_ST(uint8_t, ((uint8_t *)&t274 + 0), ((uint8_t)52ULL));
                            gm_te0mgnjdphs16 t275;
                            GM_ST(uint8_t, (uint8_t *)&t275 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t275 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t275 + 4, 0);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t275 + 0), t274);
                            GM_ST(uint64_t, ((uint8_t *)&t275 + 8), ((uint64_t)3ULL));
                            gm_l39 = t275;
                            gm_t9qkh0rsooc8d t276 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l39));
                            gm_l5 = t276;
                            uint64_t t277 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                            gm_l6 = t277;
                            goto gm_lb30;
                        }
gm_lb31:;
                        {
                            {
                                uint8_t t278 = gm_l2;
                                uint8_t t279 = ((t278 == ((uint8_t)46ULL)));
                                if (!t279) {
                                    goto gm_lb33;
                                }
                                uint8_t t280 = gm_l3;
                                uint8_t t281 = ((t280 == ((uint8_t)61ULL)));
                                if (!t281) {
                                    goto gm_lb33;
                                }
                                gm_t9qkh0rsooc8d t282;
                                GM_ST(uint8_t, ((uint8_t *)&t282 + 0), ((uint8_t)51ULL));
                                gm_te0mgnjdphs16 t283;
                                GM_ST(uint8_t, (uint8_t *)&t283 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t283 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t283 + 4, 0);
                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t283 + 0), t282);
                                GM_ST(uint64_t, ((uint8_t *)&t283 + 8), ((uint64_t)3ULL));
                                gm_l40 = t283;
                                gm_t9qkh0rsooc8d t284 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l40));
                                gm_l5 = t284;
                                uint64_t t285 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                gm_l6 = t285;
                                goto gm_lb32;
                            }
gm_lb33:;
                            uint8_t t286 = gm_l2;
                            uint8_t t287 = ((t286 == ((uint8_t)46ULL)));
                            if (t287) {
                                gm_t9qkh0rsooc8d t288;
                                GM_ST(uint8_t, ((uint8_t *)&t288 + 0), ((uint8_t)50ULL));
                                gm_te0mgnjdphs16 t289;
                                GM_ST(uint8_t, (uint8_t *)&t289 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t289 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t289 + 4, 0);
                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t289 + 0), t288);
                                GM_ST(uint64_t, ((uint8_t *)&t289 + 8), ((uint64_t)2ULL));
                                gm_l41 = t289;
                                gm_t9qkh0rsooc8d t290 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l41));
                                gm_l5 = t290;
                                uint64_t t291 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                                gm_l6 = t291;
                            } else {
                                gm_t9qkh0rsooc8d t292;
                                GM_ST(uint8_t, ((uint8_t *)&t292 + 0), ((uint8_t)58ULL));
                                gm_l5 = t292;
                            }
                        }
gm_lb32:;
                    }
gm_lb30:;
                    goto gm_lb1;
                }
gm_lb29:;
            }
            {
                {
                    uint8_t t293 = gm_l1;
                    uint8_t t294 = ((t293 == ((uint8_t)58ULL)));
                    if (!t294) {
                        goto gm_lb34;
                    }
                    uint8_t t295 = gm_l2;
                    uint8_t t296 = ((t295 == ((uint8_t)58ULL)));
                    if (t296) {
                        gm_t9qkh0rsooc8d t297;
                        GM_ST(uint8_t, ((uint8_t *)&t297 + 0), ((uint8_t)54ULL));
                        gm_te0mgnjdphs16 t298;
                        GM_ST(uint8_t, (uint8_t *)&t298 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t298 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t298 + 4, 0);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t298 + 0), t297);
                        GM_ST(uint64_t, ((uint8_t *)&t298 + 8), ((uint64_t)2ULL));
                        gm_l42 = t298;
                        gm_t9qkh0rsooc8d t299 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l42));
                        gm_l5 = t299;
                        uint64_t t300 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                        gm_l6 = t300;
                    } else {
                        uint8_t t301 = gm_l2;
                        uint8_t t302 = ((t301 == ((uint8_t)61ULL)));
                        if (t302) {
                            gm_t9qkh0rsooc8d t303;
                            GM_ST(uint8_t, ((uint8_t *)&t303 + 0), ((uint8_t)55ULL));
                            gm_te0mgnjdphs16 t304;
                            GM_ST(uint8_t, (uint8_t *)&t304 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t304 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t304 + 4, 0);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t304 + 0), t303);
                            GM_ST(uint64_t, ((uint8_t *)&t304 + 8), ((uint64_t)2ULL));
                            gm_l43 = t304;
                            gm_t9qkh0rsooc8d t305 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l43));
                            gm_l5 = t305;
                            uint64_t t306 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                            gm_l6 = t306;
                        } else {
                            gm_t9qkh0rsooc8d t307;
                            GM_ST(uint8_t, ((uint8_t *)&t307 + 0), ((uint8_t)56ULL));
                            gm_l5 = t307;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb34:;
                {
                    uint8_t t308 = gm_l1;
                    uint8_t t309 = ((t308 == ((uint8_t)63ULL)));
                    if (!t309) {
                        goto gm_lb35;
                    }
                    gm_t9qkh0rsooc8d t310;
                    GM_ST(uint8_t, ((uint8_t *)&t310 + 0), ((uint8_t)45ULL));
                    gm_l5 = t310;
                    goto gm_lb1;
                }
gm_lb35:;
                {
                    uint8_t t311 = gm_l1;
                    uint8_t t312 = ((t311 == ((uint8_t)33ULL)));
                    if (!t312) {
                        goto gm_lb36;
                    }
                    gm_t9qkh0rsooc8d t313;
                    GM_ST(uint8_t, ((uint8_t *)&t313 + 0), ((uint8_t)46ULL));
                    gm_l5 = t313;
                    goto gm_lb1;
                }
gm_lb36:;
                {
                    uint8_t t314 = gm_l1;
                    uint8_t t315 = ((t314 == ((uint8_t)94ULL)));
                    if (!t315) {
                        goto gm_lb37;
                    }
                    gm_t9qkh0rsooc8d t316;
                    GM_ST(uint8_t, ((uint8_t *)&t316 + 0), ((uint8_t)47ULL));
                    gm_l5 = t316;
                    goto gm_lb1;
                }
gm_lb37:;
                {
                    uint8_t t317 = gm_l1;
                    uint8_t t318 = ((t317 == ((uint8_t)44ULL)));
                    if (!t318) {
                        goto gm_lb38;
                    }
                    gm_t9qkh0rsooc8d t319;
                    GM_ST(uint8_t, ((uint8_t *)&t319 + 0), ((uint8_t)57ULL));
                    gm_l5 = t319;
                    goto gm_lb1;
                }
gm_lb38:;
                {
                    uint8_t t320 = gm_l1;
                    uint8_t t321 = ((t320 == ((uint8_t)40ULL)));
                    if (!t321) {
                        goto gm_lb39;
                    }
                    gm_t9qkh0rsooc8d t322;
                    GM_ST(uint8_t, ((uint8_t *)&t322 + 0), ((uint8_t)61ULL));
                    gm_l5 = t322;
                    goto gm_lb1;
                }
gm_lb39:;
                {
                    uint8_t t323 = gm_l1;
                    uint8_t t324 = ((t323 == ((uint8_t)41ULL)));
                    if (!t324) {
                        goto gm_lb40;
                    }
                    gm_t9qkh0rsooc8d t325;
                    GM_ST(uint8_t, ((uint8_t *)&t325 + 0), ((uint8_t)62ULL));
                    gm_l5 = t325;
                    goto gm_lb1;
                }
gm_lb40:;
            }
            {
                {
                    uint8_t t326 = gm_l1;
                    uint8_t t327 = ((t326 == ((uint8_t)91ULL)));
                    if (!t327) {
                        goto gm_lb41;
                    }
                    gm_t9qkh0rsooc8d t328;
                    GM_ST(uint8_t, ((uint8_t *)&t328 + 0), ((uint8_t)63ULL));
                    gm_l5 = t328;
                    goto gm_lb1;
                }
gm_lb41:;
                {
                    uint8_t t329 = gm_l1;
                    uint8_t t330 = ((t329 == ((uint8_t)93ULL)));
                    if (!t330) {
                        goto gm_lb42;
                    }
                    gm_t9qkh0rsooc8d t331;
                    GM_ST(uint8_t, ((uint8_t *)&t331 + 0), ((uint8_t)64ULL));
                    gm_l5 = t331;
                    goto gm_lb1;
                }
gm_lb42:;
                {
                    uint8_t t332 = gm_l1;
                    uint8_t t333 = ((t332 == ((uint8_t)123ULL)));
                    if (!t333) {
                        goto gm_lb43;
                    }
                    gm_t9qkh0rsooc8d t334;
                    GM_ST(uint8_t, ((uint8_t *)&t334 + 0), ((uint8_t)65ULL));
                    gm_l5 = t334;
                    goto gm_lb1;
                }
gm_lb43:;
                {
                    uint8_t t335 = gm_l1;
                    uint8_t t336 = ((t335 == ((uint8_t)125ULL)));
                    if (!t336) {
                        goto gm_lb44;
                    }
                    gm_t9qkh0rsooc8d t337;
                    GM_ST(uint8_t, ((uint8_t *)&t337 + 0), ((uint8_t)66ULL));
                    gm_l5 = t337;
                    goto gm_lb1;
                }
gm_lb44:;
                {
                    gm_taari1s0m51a7 t338;
                    memset(&t338, 0, sizeof t338);
                    F.ret = t338;
                    goto gm_fret;
                }
gm_lb45:;
                gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 435, 5);
            }
        }
gm_lb1:;
    }
    {
        void * t339 = gm_l0;
        void * t340 = gm_l0;
        uint64_t t341 = GM_LD(uint64_t, (((uint8_t *)t340) + 24));
        uint64_t t342 = gm_l6;
        uint64_t r343; bool o344 = __builtin_add_overflow(t341, t342, &r343);
        if (o344) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 530, 5);
        uint64_t t345 = r343;
        GM_ST(uint64_t, (((uint8_t *)t339) + 24), t345);
        gm_t9qkh0rsooc8d t346 = gm_l5;
        gm_taari1s0m51a7 t347;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t347), t346);
        ((uint8_t *)&t347)[1] = 1;
        F.ret = t347;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_second_word_end_dpnj7g8(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_second_word_end_dpnj7g8 F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* from */
    gm_b16_8 gm_l2; /* word */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* b */
    uint64_t gm_l5; /* end */
    uint8_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 465u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tqs5p4n2tq19v gm_l7;
    uint8_t gm_l8;
    gm_tf5srh4mp4b3i gm_l9;
    uint64_t gm_l10;
    gm_tqs5p4n2tq19v gm_l11;
    uint64_t t9 = gm_l1;
    gm_l3 = t9;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t10 = gm_l0;
                uint64_t t11 = gm_l3;
                gm_tqs5p4n2tq19v t12 = gm_f_byte_b4v8vul(t10, t11);
                gm_l7 = t12;
                uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l7))[1] != 0);
                if (t13) {
                    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                    gm_l8 = t14;
                } else {
                    goto gm_lb1;
                }
                uint8_t t15 = gm_l8;
                gm_l4 = t15;
                uint8_t t16 = gm_l4;
                gm_tqs5p4n2tq19v t17;
                GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)32ULL));
                GM_ST(uint8_t, ((uint8_t *)&t17 + 1), ((uint8_t)9ULL));
                uint8_t t18 = 0;
                if (gm_eqjmccqdb90f(((uint8_t *)&t17) + 0, ((uint8_t *)&t16))) t18 = 1;
                if (gm_eqjmccqdb90f(((uint8_t *)&t17) + 1, ((uint8_t *)&t16))) t18 = 1;
                uint8_t t19 = (uint8_t)(t18);
                if (t19) {
                    uint64_t t20 = gm_l3;
                    uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                    if (o22) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 273, 13);
                    uint64_t t23 = r21;
                    gm_l3 = t23;
                } else {
                    {
                        {
                            uint8_t t24 = gm_l4;
                            uint8_t t25 = ((t24 == ((uint8_t)47ULL)));
                            if (!t25) {
                                goto gm_lb5;
                            }
                            void * t26 = gm_l0;
                            uint64_t t27 = gm_l3;
                            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                            if (o29) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 274, 40);
                            uint64_t t30 = r28;
                            gm_tqs5p4n2tq19v t31 = gm_f_byte_b4v8vul(t26, t30);
                            gm_tqs5p4n2tq19v t32;
                            GM_ST(uint8_t, ((uint8_t *)&t32), ((uint8_t)42ULL));
                            ((uint8_t *)&t32)[1] = 1;
                            uint8_t t33 = (gm_eqb44cb3cf1n(((uint8_t *)&t31), ((uint8_t *)&t32)));
                            if (!t33) {
                                goto gm_lb5;
                            }
                            void * t34 = gm_l0;
                            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t34) + 8));
                            uint64_t t36 = gm_l3;
                            gm_tf5srh4mp4b3i t37 = gm_f_inline_comment_end_8i5h1eq(t35, t36);
                            gm_l9 = t37;
                            uint8_t t38 = (uint8_t)((((uint8_t *)&gm_l9))[8] != 0);
                            if (t38) {
                                uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                                gm_l10 = t39;
                            } else {
                                gm_tf5srh4mp4b3i t40;
                                memset(&t40, 0, sizeof t40);
                                F.ret = t40;
                                goto gm_fret;
                            }
                            uint64_t t41 = gm_l10;
                            gm_l3 = t41;
                            goto gm_lb4;
                        }
gm_lb5:;
                        goto gm_lb1;
                    }
gm_lb4:;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t42 = gm_l3;
    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t r44; bool o45 = __builtin_add_overflow(t42, t43, &r44);
    if (o45) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 280, 12);
    uint64_t t46 = r44;
    gm_l5 = t46;
    void * t47 = gm_l0;
    void * t48 = GM_LD(void *, (((uint8_t *)t47) + 8));
    uint64_t t49 = GM_LD(uint64_t, ((((uint8_t *)t47) + 8) + 8));
    uint64_t t50 = gm_l3;
    uint64_t t51 = (uintptr_t)t50;
    uint64_t t52 = (uintptr_t)t49;
    if (t51 > t52 || t52 > (uintptr_t)t49) gm_trap_slice(t51, t52, t49, gm_srcvmmksi0829pa, 281, 12);
    uint64_t t53 = ((uintptr_t)t49) - (uintptr_t)t50;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t48 + (uintptr_t)t50 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
    gm_b16_8 t55 = gm_l2;
    uint8_t t56 = gm_f_starts_with_6vvt01e(t54, t55);
    uint8_t t57 = (uint8_t)!t56;
    if (t57) {
        gm_tf5srh4mp4b3i t58;
        memset(&t58, 0, sizeof t58);
        F.ret = t58;
        goto gm_fret;
    }
    {
        {
            void * t59 = gm_l0;
            uint64_t t60 = gm_l5;
            gm_tqs5p4n2tq19v t61 = gm_f_byte_b4v8vul(t59, t60);
            gm_l11 = t61;
            uint8_t t62 = (uint8_t)((((uint8_t *)&gm_l11))[1] != 0);
            if (!t62) {
                goto gm_lb7;
            }
            uint8_t t63 = GM_LD(uint8_t, ((uint8_t *)&gm_l11));
            gm_l6 = t63;
            uint8_t t64 = gm_l6;
            uint8_t t65 = gm_f_is_ident_cont_317fpdb(t64);
            if (t65) {
                gm_tf5srh4mp4b3i t66;
                memset(&t66, 0, sizeof t66);
                F.ret = t66;
                goto gm_fret;
            }
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    uint64_t t67 = gm_l5;
    gm_tf5srh4mp4b3i t68;
    GM_ST(uint8_t, (uint8_t *)&t68 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t68 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t68 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t68), t67);
    ((uint8_t *)&t68)[8] = 1;
    F.ret = t68;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_skip_trivia_014s7ai(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_skip_trivia_014s7ai F;
    void * gm_l0; /* lx */
    uint8_t gm_l1; /* b */
    gm_tqs5p4n2tq19v gm_l2; /* next */
    GM_STACK_CHECK(&F, 284u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l3;
    {
        for (;;) {
            void * t14 = gm_l0;
            gm_tqs5p4n2tq19v t15 = gm_f_peek_72bostn(t14);
            gm_l3 = t15;
            uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l3))[1] != 0);
            if (!t16) {
                goto gm_lb1;
            }
            uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
            gm_l1 = t17;
            {
                {
                    {
                        {
                            {
                                uint8_t t18 = gm_l1;
                                uint8_t t19 = ((t18 == ((uint8_t)32ULL)));
                                if (!t19) {
                                    goto gm_lb7;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t20 = gm_l1;
                                uint8_t t21 = ((t20 == ((uint8_t)9ULL)));
                                if (!t21) {
                                    goto gm_lb8;
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            goto gm_lb5;
                        }
gm_lb6:;
                        void * t22 = gm_l0;
                        void * t23 = gm_l0;
                        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 24));
                        uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                        if (o26) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 166, 28);
                        uint64_t t27 = r25;
                        GM_ST(uint64_t, (((uint8_t *)t22) + 24), t27);
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        uint8_t t28 = gm_l1;
                        uint8_t t29 = ((t28 == ((uint8_t)10ULL)));
                        if (!t29) {
                            goto gm_lb9;
                        }
                        void * t30 = gm_l0;
                        void * t31 = gm_l0;
                        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 24));
                        void * t33 = gm_l0;
                        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 24));
                        uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                        if (o36) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 168, 41);
                        uint64_t t37 = r35;
                        gm_f_note_newline_cchbr7g(t30, t32, t37);
                        void * t38 = gm_l0;
                        void * t39 = gm_l0;
                        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 24));
                        uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                        if (o42) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 169, 17);
                        uint64_t t43 = r41;
                        GM_ST(uint64_t, (((uint8_t *)t38) + 24), t43);
                        goto gm_lb4;
                    }
gm_lb9:;
                    {
                        uint8_t t44 = gm_l1;
                        uint8_t t45 = ((t44 == ((uint8_t)13ULL)));
                        if (!t45) {
                            goto gm_lb10;
                        }
                        void * t46 = gm_l0;
                        gm_tqs5p4n2tq19v t47 = gm_f_peek_at_74p8lkq(t46, ((uint64_t)1ULL));
                        gm_tqs5p4n2tq19v t48;
                        GM_ST(uint8_t, ((uint8_t *)&t48), ((uint8_t)10ULL));
                        ((uint8_t *)&t48)[1] = 1;
                        uint8_t t49 = (gm_eqb44cb3cf1n(((uint8_t *)&t47), ((uint8_t *)&t48)));
                        if (t49) {
                            void * t50 = gm_l0;
                            void * t51 = gm_l0;
                            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t51) + 24));
                            void * t53 = gm_l0;
                            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)t53) + 24));
                            uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)2ULL), &r55);
                            if (o56) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 173, 45);
                            uint64_t t57 = r55;
                            gm_f_note_newline_cchbr7g(t50, t52, t57);
                            void * t58 = gm_l0;
                            void * t59 = gm_l0;
                            uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)t59) + 24));
                            uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)2ULL), &r61);
                            if (o62) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 174, 21);
                            uint64_t t63 = r61;
                            GM_ST(uint64_t, (((uint8_t *)t58) + 24), t63);
                        } else {
                            void * t64 = gm_l0;
                            gm_b0_1 t65 = gm_f_lone_cr_ktb0jvm((void *)NULL, cx, NULL, t64);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb10:;
                    {
                        uint8_t t66 = gm_l1;
                        uint8_t t67 = ((t66 == ((uint8_t)47ULL)));
                        if (!t67) {
                            goto gm_lb11;
                        }
                        void * t68 = gm_l0;
                        gm_tqs5p4n2tq19v t69 = gm_f_peek_at_74p8lkq(t68, ((uint64_t)1ULL));
                        gm_l2 = t69;
                        gm_tqs5p4n2tq19v t70 = gm_l2;
                        gm_tqs5p4n2tq19v t71;
                        GM_ST(uint8_t, ((uint8_t *)&t71), ((uint8_t)47ULL));
                        ((uint8_t *)&t71)[1] = 1;
                        uint8_t t72 = (gm_eqb44cb3cf1n(((uint8_t *)&t70), ((uint8_t *)&t71)));
                        if (t72) {
                            void * t73 = gm_l0;
                            gm_b0_1 t74 = gm_f_line_comment_s71u8hq((void *)NULL, cx, NULL, t73);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        } else {
                            gm_tqs5p4n2tq19v t75 = gm_l2;
                            gm_tqs5p4n2tq19v t76;
                            GM_ST(uint8_t, ((uint8_t *)&t76), ((uint8_t)42ULL));
                            ((uint8_t *)&t76)[1] = 1;
                            uint8_t t77 = (gm_eqb44cb3cf1n(((uint8_t *)&t75), ((uint8_t *)&t76)));
                            if (t77) {
                                void * t78 = gm_l0;
                                gm_b0_1 t79 = gm_f_block_comment_fvce7qt((void *)NULL, cx, NULL, t78);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            } else {
                                F.ret = GM_ZERO(gm_b0_1);
                                goto gm_fret;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb11:;
                    {
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 165, 9);
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
inline __attribute__((aligned(4))) uint64_t gm_f_slot_4oe07nr(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_slot_4oe07nr F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t gm_l1;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)0ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_l1 = ((uint64_t)0ULL);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)1ULL));
            if (!t12) {
                goto gm_lb3;
            }
            gm_l1 = ((uint64_t)1ULL);
            goto gm_lb1;
        }
gm_lb3:;
        {
            {
                {
                    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t14 = (uint8_t)(t13 == ((uint8_t)2ULL));
                    if (!t14) {
                        goto gm_lb6;
                    }
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t16 = (uint8_t)(t15 == ((uint8_t)3ULL));
                    if (!t16) {
                        goto gm_lb7;
                    }
                    goto gm_lb5;
                }
gm_lb7:;
                goto gm_lb4;
            }
gm_lb5:;
            gm_l1 = ((uint64_t)2ULL);
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 60, 40);
    }
gm_lb1:;
    uint64_t t17 = gm_l1;
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_tg7cev6(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_continuation_n3333i7(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_starts_continuation_n3333i7 F;
    gm_t3fcbuehh3bs2 gm_l0; /* kind */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)2ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l1 = t7;
            gm_t9qkh0rsooc8d t8 = gm_l1;
            gm_t9qkh0rsooc8d t9;
            GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)7ULL));
            gm_t9qkh0rsooc8d t10;
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)20ULL));
            gm_tk22flr9mh3ql t12;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 0), t9);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 1), t10);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 2), t11);
            uint8_t t13 = 0;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t12) + 0, ((uint8_t *)&t8))) t13 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t12) + 1, ((uint8_t *)&t8))) t13 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t12) + 2, ((uint8_t *)&t8))) t13 = 1;
            uint8_t t14 = (uint8_t)(t13);
            gm_l2 = t14;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 703, 55);
    }
gm_lb1:;
    uint8_t t15 = gm_l2;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_starts_continuation_n3333i7_c(gm_t3fcbuehh3bs2 a0) { return gm_f_starts_continuation_n3333i7(&a0); }
inline __attribute__((aligned(4))) uint8_t gm_f_suppresses_newline_after_o08ord2(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_suppresses_newline_after_o08ord2 F;
    gm_t3fcbuehh3bs2 gm_l0; /* kind */
    gm_t9qkh0rsooc8d gm_l1; /* k */
    gm_t9qkh0rsooc8d gm_l2; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l3;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)2ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t8 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l1 = t8;
            gm_t9qkh0rsooc8d t9 = gm_l1;
            gm_t9qkh0rsooc8d t10;
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)16ULL));
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)15ULL));
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)20ULL));
            gm_t9qkh0rsooc8d t14;
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)7ULL));
            gm_t724v60om7sh5 t15;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 0), t10);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t11);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 2), t12);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 3), t13);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 4), t14);
            uint8_t t16 = 0;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t15) + 0, ((uint8_t *)&t9))) t16 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t15) + 1, ((uint8_t *)&t9))) t16 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t15) + 2, ((uint8_t *)&t9))) t16 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t15) + 3, ((uint8_t *)&t9))) t16 = 1;
            if (gm_eq2ceic3f8rp(((uint8_t *)&t15) + 4, ((uint8_t *)&t9))) t16 = 1;
            uint8_t t17 = (uint8_t)(t16);
            gm_l3 = t17;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)8ULL));
            if (!t19) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t20 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)a0) + 8));
            gm_l2 = t20;
            gm_t9qkh0rsooc8d t21 = gm_l2;
            gm_t9qkh0rsooc8d t22;
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)45ULL));
            gm_t9qkh0rsooc8d t23;
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)46ULL));
            gm_t9qkh0rsooc8d t24;
            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)47ULL));
            gm_t9qkh0rsooc8d t25;
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)48ULL));
            gm_t9qkh0rsooc8d t26;
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)49ULL));
            gm_t9qkh0rsooc8d t27;
            GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)52ULL));
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)53ULL));
            gm_t9qkh0rsooc8d t29;
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)62ULL));
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)64ULL));
            gm_t9qkh0rsooc8d t31;
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)66ULL));
            gm_tfh78knfg7urq t32;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 0), t22);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 1), t23);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 2), t24);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 3), t25);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 4), t26);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 5), t27);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 6), t28);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 7), t29);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 8), t30);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t32 + 9), t31);
            uint8_t t33 = 0;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t32) + 0, ((uint8_t *)&t21))) t33 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t32) + 1, ((uint8_t *)&t21))) t33 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t32) + 2, ((uint8_t *)&t21))) t33 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t32) + 3, ((uint8_t *)&t21))) t33 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t32) + 4, ((uint8_t *)&t21))) t33 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t32) + 5, ((uint8_t *)&t21))) t33 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t32) + 6, ((uint8_t *)&t21))) t33 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t32) + 7, ((uint8_t *)&t21))) t33 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t32) + 8, ((uint8_t *)&t21))) t33 = 1;
            if (gm_eqv3tj1pu43g(((uint8_t *)&t32) + 9, ((uint8_t *)&t21))) t33 = 1;
            uint8_t t34 = (uint8_t)(!t33);
            gm_l3 = t34;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_l3 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcvmmksi0829pa, 691, 60);
    }
gm_lb1:;
    uint8_t t35 = gm_l3;
    F.ret = t35;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_suppresses_newline_after_o08ord2_c(gm_t3fcbuehh3bs2 a0) { return gm_f_suppresses_newline_after_o08ord2(&a0); }
__attribute__((aligned(4))) gm_b0_1 gm_f_unexpected_char_r3t8d4c(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unexpected_char_r3t8d4c F;
    void * gm_l0; /* lx */
    uint64_t gm_l1; /* lo */
    gm_tb31k5l794t7p gm_l2; /* c */
    gm_b16_8 gm_l3; /* message */
    uint32_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 691u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l5;
    gm_tabb710d9mq2e gm_l6;
    uint8_t gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 24));
    gm_l1 = t5;
    void * t6 = gm_l0;
    gm_tb31k5l794t7p t7 = GM_CONV(gm_tb31k5l794t7p, gm_f_current_char_ctktsl2(t6));
    gm_l2 = t7;
    void * t8 = gm_l0;
    void * t9 = gm_l0;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 24));
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t r12; bool o13 = __builtin_add_overflow(t10, t11, &r12);
    if (o13) gm_trap("integer overflow in `+`", gm_srcvmmksi0829pa, 403, 5);
    uint64_t t14 = r12;
    GM_ST(uint64_t, (((uint8_t *)t8) + 24), t14);
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(0));
    gm_l3 = t15;
    {
        {
            uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
            uint8_t t17 = (t16 >= ((uint32_t)128ULL));
            if (!t17) {
                goto gm_lb2;
            }
            uint32_t t18 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
            uint8_t t19 = gm_f_is_unicode_space_b2m9rku(t18);
            if (!t19) {
                goto gm_lb2;
            }
            uint32_t t20 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
            gm_t88rcucg1b7uk t21;
            GM_ST(uint32_t, ((uint8_t *)&t21 + 0), t20);
            gm_f_format_t4ivgd1((void *)&gm_l5, (void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcvmmksi0829pa, 406, 19);
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
            gm_l3 = t24;
            goto gm_lb1;
        }
gm_lb2:;
        uint32_t t25 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
        uint8_t t26 = (t25 >= ((uint32_t)128ULL));
        if (t26) {
            void * t27 = gm_l0;
            void * t28 = GM_LD(void *, (((uint8_t *)t27) + 8));
            uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)t27) + 8) + 8));
            uint64_t t30 = gm_l1;
            void * t31 = gm_l0;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 24));
            uint64_t t33 = (uintptr_t)t30;
            uint64_t t34 = (uintptr_t)t32;
            if (t33 > t34 || t34 > (uintptr_t)t29) gm_trap_slice(t33, t34, t29, gm_srcvmmksi0829pa, 410, 13);
            uint64_t t35 = ((uintptr_t)t32) - (uintptr_t)t30;
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t28 + (uintptr_t)t30 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35));
            uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
            gm_tommf4j9upomc t38;
            GM_ST(uint32_t, (uint8_t *)&t38 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t36);
            GM_ST(uint32_t, ((uint8_t *)&t38 + 16), t37);
            gm_f_format_pscs6r9((void *)&gm_l6, (void *)NULL, cx, NULL, &t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcvmmksi0829pa, 408, 19);
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
            gm_l3 = t41;
        } else {
            uint32_t t42 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
            gm_l4 = t42;
            uint32_t t43 = gm_l4;
            uint8_t t44 = (((uint32_t)33ULL) <= t43);
            if (t44) {
                uint32_t t45 = gm_l4;
                uint8_t t46 = (t45 <= ((uint32_t)126ULL));
                gm_l7 = t46;
            } else {
                gm_l7 = ((uint8_t)0);
            }
            uint8_t t47 = gm_l7;
            if (t47) {
                void * t48 = gm_l0;
                void * t49 = GM_LD(void *, (((uint8_t *)t48) + 8));
                uint64_t t50 = GM_LD(uint64_t, ((((uint8_t *)t48) + 8) + 8));
                uint64_t t51 = gm_l1;
                void * t52 = gm_l0;
                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)t52) + 24));
                uint64_t t54 = (uintptr_t)t51;
                uint64_t t55 = (uintptr_t)t53;
                if (t54 > t55 || t55 > (uintptr_t)t50) gm_trap_slice(t54, t55, t50, gm_srcvmmksi0829pa, 414, 59);
                uint64_t t56 = ((uintptr_t)t53) - (uintptr_t)t51;
                gm_b16_8 t57;
                GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t49 + (uintptr_t)t51 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(t56));
                gm_tnpifv20irnph t58;
                GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t57);
                gm_f_format_fuvjr35((void *)&gm_l8, (void *)NULL, cx, NULL, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcvmmksi0829pa, 414, 19);
                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                gm_l3 = t61;
            } else {
                uint32_t t62 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
                gm_t88rcucg1b7uk t63;
                GM_ST(uint32_t, ((uint8_t *)&t63 + 0), t62);
                gm_f_format_i801868((void *)&gm_l9, (void *)NULL, cx, NULL, t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcvmmksi0829pa, 416, 19);
                gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                gm_l3 = t66;
            }
        }
    }
gm_lb1:;
    void * t67 = gm_l0;
    uint64_t t68 = gm_l1;
    void * t69 = gm_l0;
    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)t69) + 24));
    gm_b16_8 t71 = gm_l3;
    gm_b0_1 t72 = gm_f_error_p5r0d56((void *)NULL, cx, NULL, t67, t68, t70, t71);
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
