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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; gm_tl874t2atnlh3 f3; uint32_t f4; } gm_t0ut88vjdgrt1;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(base: ^Expr, index: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_tf5srh4mp4b3i f1; } gm_tj64ui6kruni6;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0ut88vjdgrt1) == 40 && _Alignof(gm_t0ut88vjdgrt1) == 8 && offsetof(gm_t0ut88vjdgrt1, f0) == 0 && offsetof(gm_t0ut88vjdgrt1, f1) == 4 && offsetof(gm_t0ut88vjdgrt1, f2) == 8 && offsetof(gm_t0ut88vjdgrt1, f3) == 24 && offsetof(gm_t0ut88vjdgrt1, f4) == 36, "layout of `MonoRequest`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tfeab279c16rr v0; gm_tj64ui6kruni6 v1; } f1; } gm_t1jll27j7ae34;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_tl874t2atnlh3 f3; uint8_t f4; uint8_t p5[3]; void * f5; gm_b16_8 f6; void * f7; } gm_tblkd491pk4cr;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; uint64_t f4; uint8_t f5; uint8_t p6[7]; gm_trqfvmjtt4sij f6; } gm_tj607dkcelqbd;
_Static_assert(sizeof(gm_tj64ui6kruni6) == 32 && _Alignof(gm_tj64ui6kruni6) == 8 && offsetof(gm_tj64ui6kruni6, f0) == 0 && offsetof(gm_tj64ui6kruni6, f1) == 16, "layout of `(string, ?usize)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_tplpbqf3duvn3 a[1]; } gm_tmcj65fi8d3nd;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { uint32_t f0; gm_tkn42u7na3t81 f1; } gm_tuo61a5rdrbtf;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1jll27j7ae34) == 56 && _Alignof(gm_t1jll27j7ae34) == 8 && offsetof(gm_t1jll27j7ae34, f0) == 0 && offsetof(gm_t1jll27j7ae34, f1) == 8, "layout of `Parsed`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tblkd491pk4cr) == 144 && _Alignof(gm_tblkd491pk4cr) == 8 && offsetof(gm_tblkd491pk4cr, f0) == 0 && offsetof(gm_tblkd491pk4cr, f1) == 16 && offsetof(gm_tblkd491pk4cr, f2) == 56 && offsetof(gm_tblkd491pk4cr, f3) == 96 && offsetof(gm_tblkd491pk4cr, f4) == 108 && offsetof(gm_tblkd491pk4cr, f5) == 112 && offsetof(gm_tblkd491pk4cr, f6) == 120 && offsetof(gm_tblkd491pk4cr, f7) == 136, "layout of `MonoCx`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tj607dkcelqbd) == 88 && _Alignof(gm_tj607dkcelqbd) == 8 && offsetof(gm_tj607dkcelqbd, f0) == 0 && offsetof(gm_tj607dkcelqbd, f1) == 8 && offsetof(gm_tj607dkcelqbd, f2) == 12 && offsetof(gm_tj607dkcelqbd, f3) == 16 && offsetof(gm_tj607dkcelqbd, f4) == 32 && offsetof(gm_tj607dkcelqbd, f5) == 40 && offsetof(gm_tj607dkcelqbd, f6) == 48, "layout of `TemplateUses`");
_Static_assert(sizeof(gm_tmcj65fi8d3nd) == 8 && _Alignof(gm_tmcj65fi8d3nd) == 4 && offsetof(gm_tmcj65fi8d3nd, a) == 0, "layout of `[1]Subst`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tuo61a5rdrbtf) == 12 && _Alignof(gm_tuo61a5rdrbtf) == 4 && offsetof(gm_tuo61a5rdrbtf, f0) == 0 && offsetof(gm_tuo61a5rdrbtf, f1) == 4, "layout of `(param: u32, args: ?Ty)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
struct gm_fr_add_fn_t4t83qh {
    gm_frame h;
    gm_tqqhoivahirr0 l1; /* sig */
    gm_tdgfduqjheqdm l2; /* def */
    uint32_t ret;
};
struct gm_fr_add_shared_fn_f8l99p1 {
    gm_frame h;
    gm_tqqhoivahirr0 l1; /* sig */
    uint32_t ret;
};
struct gm_fr_any_child_ffc8h1k {
    gm_frame h;
    gm_closure l1; /* pred */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_bodiless_def_u9kqo09 {
    gm_frame h;
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_deinit_padgogk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1fu8oa9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6trg8r7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_crpekm1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jviatgq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n936iee {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ot5ttb7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s65n31g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_extend_7hs67uj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_gig5o6g {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_7u87bne {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_m1ukc9a {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_m232ppe {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_jvf1ma2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_2plb4qp {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_31sqk5v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7l73vjb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_allvvbh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_oalq03d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_p9s2t99 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_shared_r51o03d {
    gm_frame h;
    gm_t0ut88vjdgrt1 l1; /* req */
    gm_tqqhoivahirr0 l5; /* gsig */
    gm_tdgfduqjheqdm l6; /* def */
    gm_trqfvmjtt4sij l7; /* map */
    gm_trqfvmjtt4sij l13; /* params */
    gm_t5v0c8ia6a5mo l16; /* p */
    gm_t5v0c8ia6a5mo l17; /* q */
    gm_tqqhoivahirr0 l20; /* sig */
    gm_trqfvmjtt4sij l22; /* locals */
    gm_trqfvmjtt4sij l24; /* hidden */
    gm_trqfvmjtt4sij l25; /* sites */
    gm_tblkd491pk4cr l26; /* mc */
    gm_tsoie1ppgdvcg l29; /* l */
    gm_tsoie1ppgdvcg l30; /* x */
    gm_trqfvmjtt4sij l31; /* ps */
    gm_tsoie1ppgdvcg l41; /* l */
    gm_tiilmfd16v3fc ret;
};
struct gm_fr_mentions_value_2a40t7e {
    gm_frame h;
    uint32_t l1; /* g */
    uint8_t ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_t2b6tm8c0vgo1 l1; /* kind */
    void * ret;
};
struct gm_fr_only_templates_8l3e5t6 {
    gm_frame h;
    gm_tj607dkcelqbd l5; /* u */
    uint8_t ret;
};
struct gm_fr_push_1ncolkq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_472i7vl {
    gm_frame h;
    gm_tqqhoivahirr0 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_am7bo0v {
    gm_frame h;
    gm_tsoie1ppgdvcg l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tcgrjq9toscke l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_gtq94gd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_h69n5fi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_hbhlgi6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o8u01sh {
    gm_frame h;
    gm_t5v0c8ia6a5mo l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_roqi806 {
    gm_frame h;
    gm_tdgfduqjheqdm l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1sqb3ir {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_7sj41hq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8r12kcb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_9a4nica {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_icepq2q {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_jd8h83s {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k8jaegk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o08qncv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_scan_uses_iuphl4h {
    gm_frame h;
    void * l0; /* u */
    uint8_t l2; /* top */
    gm_tdgfduqjheqdm l20; /* d */
    gm_b0_1 ret;
};
struct gm_fr_set_inst_def_tcpabg0 {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_b0_1 ret;
};
struct gm_fr_share_instance_1q66m8k {
    gm_frame h;
    gm_t0ut88vjdgrt1 l1; /* req */
    gm_trqfvmjtt4sij l5; /* erased */
    gm_trqfvmjtt4sij l6; /* texts */
    gm_trqfvmjtt4sij l13; /* map */
    gm_trqfvmjtt4sij l18; /* tables */
    gm_tfeab279c16rr l22; /* p */
    gm_tfeab279c16rr l24; /* parsed */
    gm_trqfvmjtt4sij l28; /* index */
    gm_tdgfduqjheqdm l33; /* def */
    gm_tqqhoivahirr0 l34; /* sig */
    gm_trqfvmjtt4sij l35; /* locals */
    gm_tsoie1ppgdvcg l38; /* l */
    gm_tsoie1ppgdvcg l39; /* x */
    gm_trqfvmjtt4sij l42; /* args */
    uint8_t ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_template_marker_heil9i9 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_template_params_6stj17b {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_tdgfduqjheqdm l4; /* def */
    gm_b16_8 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) uint32_t gm_f_add_shared_fn_f8l99p1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2);
__attribute__((aligned(4))) void gm_f_check_template_brtb8t0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_padgogk(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_crpekm1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jviatgq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n936iee(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s65n31g(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_gig5o6g(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_m1ukc9a(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m232ppe(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_format_jvf1ma2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_get_hi5nrra(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_s7blvas(void *gm_out, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_2plb4qp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_31sqk5v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7l73vjb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_allvvbh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_shared_r51o03d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_value_2a40t7e(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mono_expr_p8qb7fe(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mono_ty_atcu0jn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_4n5f3sc(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_only_templates_8l3e5t6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_parse_template_rlpl2k8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1ncolkq(void * a0, gm_cs9ufftihtbpo2 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_472i7vl(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_gtq94gd(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hbhlgi6(void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_roqi806(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3pvr70r(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_6him49q(void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_i2clb58(void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9a4nica(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_icepq2q(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_jd8h83s(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k8jaegk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o08qncv(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_uses_iuphl4h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_share_instance_1q66m8k(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_template_marker_heil9i9(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_template_params_6stj17b(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_type_size_ok_tr1llg4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern uint8_t gm_kkbiditauji[40] __attribute__((aligned(8)));
extern const uint8_t gm_seb2o268bfiia[11];
extern const uint8_t gm_sj3cmvbmi7cm6[9];
extern const uint8_t gm_sqcvjnsub5bfs[53];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcb5s85si25i3o[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcmhupam4se3h5[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srkol0piugptj[14];
extern const gm_tmpl gm_tploj8vgi84m11i;
extern const gm_type gm_tyo86ks9qkvt;
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
inline __attribute__((aligned(4))) uint32_t gm_f_add_shared_fn_f8l99p1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_add_shared_fn_f8l99p1 F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l2; /* kind */
    GM_STACK_CHECK(&F, 230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = gm_l2;
    gm_tqqhoivahirr0 t4 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)a1));
    gm_tdgfduqjheqdm t5;
    gm_f_bodiless_def_u9kqo09((void *)&t5, t3);
    uint32_t t6 = gm_f_add_fn_t4t83qh((void *)NULL, cx, NULL, t2, &t4, &t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_child_ffc8h1k F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    F.l2 = ((uint8_t)0);
    void * t2 = gm_l0;
    gm_closure t3 = ((gm_closure){(void *)gm_f_fn_7u87bne, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = F.l2;
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
inline __attribute__((aligned(4))) void gm_f_deinit_padgogk(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_padgogk F;
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
    gm_b16_8 t21 = gm_f_empty_slice_d914i5u();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8fnsr5h F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_crpekm1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_crpekm1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jviatgq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jviatgq F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n936iee(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n936iee F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 12)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s65n31g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s65n31g F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 12)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_7hs67uj F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_2onqfca(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 4);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_gig5o6g(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_gig5o6g F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_61fb705_2(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 88)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 88);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 88)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_7u87bne F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        gm_closure t4 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l1));
        void * t5 = gm_l0;
        gm_closure t6 = t4;
        gm_node *ccx7 = t6.cx;
        void * t8 = t6.code;
        uint8_t t9;
        if (GM_PTR_TAGGED(t8)) {
            t9 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t8))(t5);
        } else {
            t9 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t8)(t6.env, ccx7, NULL, t5);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2), t9);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_m1ukc9a(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_m1ukc9a F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_mentions_value_2a40t7e *)((&F.h)->up))->l1));
    uint8_t t4 = gm_f_mentions_value_2a40t7e(t2, t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m232ppe(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_m232ppe F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 25u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_scan_uses_iuphl4h *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    uint8_t t4 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_scan_uses_iuphl4h *)((&F.h)->up))->l2));
    gm_b0_1 t5 = gm_f_scan_uses_iuphl4h((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_format_jvf1ma2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jvf1ma2 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqcvjnsub5bfs));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tploj8vgi84m11i), t3);
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
inline __attribute__((aligned(4))) void gm_f_list_2plb4qp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_2plb4qp F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n936iee();
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
inline __attribute__((aligned(4))) void gm_f_list_31sqk5v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_31sqk5v F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_s65n31g();
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
inline __attribute__((aligned(4))) void gm_f_list_7l73vjb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7l73vjb F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_crpekm1();
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
inline __attribute__((aligned(4))) void gm_f_list_allvvbh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_allvvbh F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jviatgq();
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
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oalq03d F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8fnsr5h();
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
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_p9s2t99 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_6trg8r7();
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
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_value_2a40t7e(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mentions_value_2a40t7e F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* p */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)9ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint32_t t7 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
            gm_l2 = t7;
            uint32_t t8 = gm_l2;
            uint32_t t9 = F.l1;
            uint8_t t10 = ((t8 == t9));
            if (!t10) {
                goto gm_lb2;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t11 = gm_l0;
    gm_closure t12 = ((gm_closure){(void *)gm_f_fn_m1ukc9a, (void *)&F.h, cx});
    uint8_t t13 = gm_f_any_child_ffc8h1k(t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1ncolkq(void * a0, gm_cs9ufftihtbpo2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1ncolkq F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tcc1rq4n1avb2, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_crpekm1();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_o08qncv(t8, ((uint64_t)1ULL), t10);
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
    gm_tcc1rq4n1avb2 t18 = gm_l1;
    GM_ST(gm_tcc1rq4n1avb2, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_am7bo0v F;
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
        gm_b16_8 t9 = gm_f_empty_slice_6trg8r7();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8r12kcb(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_gtq94gd(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_gtq94gd F;
    void * gm_l0; /* l */
    gm_tiilmfd16v3fc gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_jviatgq();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_jd8h83s(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_h69n5fi F;
    void * gm_l0; /* l */
    gm_tplpbqf3duvn3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tplpbqf3duvn3, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_8fnsr5h();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_1sqb3ir(t8, ((uint64_t)1ULL), t10);
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
    gm_tplpbqf3duvn3 t18 = gm_l1;
    GM_ST(gm_tplpbqf3duvn3, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hbhlgi6(void * a0, gm_tl874t2atnlh3 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_hbhlgi6 F;
    void * gm_l0; /* l */
    gm_tl874t2atnlh3 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_s65n31g();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_k8jaegk(t8, ((uint64_t)1ULL), t10);
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
    gm_tl874t2atnlh3 t18 = gm_l1;
    GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t16) + (uintptr_t)t17 * 12), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 12)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_1sqb3ir F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8r12kcb F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_jd8h83s(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_jd8h83s F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k8jaegk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_k8jaegk F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)12ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o08qncv(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o08qncv F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2) {
    struct gm_fr_set_inst_def_tcpabg0 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 37u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 2184);
    uint32_t t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_put_3pvr70r(t3, t4, &(*(gm_tdgfduqjheqdm *)((uint8_t *)a2)));
    gm_l3 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srclv0dcbbfhves, 615, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_template_marker_heil9i9(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_template_marker_heil9i9 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_seb2o268bfiia));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_t428ue2u3hf5g t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)3ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_t53at80pfdus3 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)21ULL));
    GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t6 + 8), t5);
    gm_t88rcucg1b7uk t7 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t3, &t6);
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_m1ukc9a(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m232ppe(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_make_shared_r51o03d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_make_shared_r51o03d F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* tps */
    gm_b16_8 gm_l3; /* erased */
    uint32_t gm_l4; /* f */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint32_t gm_l11; /* g */
    gm_t88rcucg1b7uk gm_l12; /* raw */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint32_t gm_l21; /* id */
    uint32_t gm_l23; /* outer */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    uint32_t gm_l34; /* g */
    uint32_t gm_l35; /* l */
    void * gm_l36; /* body */
    void * gm_l37; /* b */
    void * gm_l38; /* copy */
    gm_b16_8 gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    gm_tiilmfd16v3fc gm_l42; /* shared */
    GM_STACK_CHECK(&F, 3100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tn68el8d14vku gm_l43;
    gm_tftb1odc739t5 gm_l44;
    uint8_t gm_l45;
    gm_tftb1odc739t5 gm_l46;
    gm_tftb1odc739t5 gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_tftb1odc739t5 gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_tftb1odc739t5 gm_l57;
    {
        uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)a1));
        gm_l4 = t37;
        void * t38 = gm_l0;
        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 344));
        uint64_t t40 = GM_LD(uint64_t, ((((uint8_t *)t38) + 344) + 8));
        uint32_t t41 = gm_l4;
        uint64_t t42 = (uint64_t)t41;
        if ((uintptr_t)t42 >= (uintptr_t)(t40)) gm_trap_index((uintptr_t)t42, t40, gm_srcb5s85si25i3o, 160, 13);
        memmove(((uint8_t *)&F.l5), (((uint8_t *)t39) + (uintptr_t)t42 * 136), 136);
        void * t43 = gm_l0;
        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 384));
        uint64_t t45 = GM_LD(uint64_t, ((((uint8_t *)t43) + 384) + 8));
        uint32_t t46 = gm_l4;
        uint64_t t47 = (uint64_t)t46;
        if ((uintptr_t)t47 >= (uintptr_t)(t45)) gm_trap_index((uintptr_t)t47, t45, gm_srcb5s85si25i3o, 161, 12);
        memmove(((uint8_t *)&F.l6), (((uint8_t *)t44) + (uintptr_t)t47 * 64), 64);
        gm_trqfvmjtt4sij t48;
        gm_f_list_oalq03d((void *)&t48, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t48;
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 80));
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        gm_l8 = t50;
        gm_l9 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t51 = gm_l9;
                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint8_t t53 = (t51 < t52);
                if (!t53) {
                    goto gm_lb1;
                }
                {
                    uint64_t t54 = gm_l9;
                    void * t55 = GM_LD(void *, ((uint8_t *)&gm_l8));
                    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t57 = gm_l9;
                    uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)t55) + (uintptr_t)t57 * 4));
                    gm_tn68el8d14vku t59;
                    GM_ST(uint32_t, (uint8_t *)&t59 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t59 + 0), t54);
                    GM_ST(uint32_t, ((uint8_t *)&t59 + 8), t58);
                    gm_l43 = t59;
                    {
                        {
                            uint64_t t60 = GM_LD(uint64_t, ((uint8_t *)&gm_l43));
                            gm_l10 = t60;
                            uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&gm_l43) + 8));
                            gm_l11 = t61;
                            void * t62 = (void *)((uint8_t *)&F.l7);
                            uint32_t t63 = gm_l11;
                            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t66 = gm_l10;
                            if ((uintptr_t)t66 >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)t66, t65, gm_srcb5s85si25i3o, 163, 78);
                            gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t64) + (uintptr_t)t66 * 4));
                            gm_tplpbqf3duvn3 t68;
                            GM_ST(uint32_t, ((uint8_t *)&t68 + 0), t63);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t68 + 4), t67);
                            gm_tftb1odc739t5 t69 = gm_f_push_h69n5fi(t62, GM_CONV(gm_cs7e9ppsd384bj, t68));
                            gm_l44 = t69;
                            uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                            if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_srcb5s85si25i3o, 163, 44);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 163, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t71 = gm_l9;
                uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                uint64_t t74 = r72;
                gm_l9 = t74;
            }
        }
gm_lb1:;
        void * t75 = gm_l0;
        void * t76 = (void *)(((uint8_t *)t75) + 8);
        gm_t53at80pfdus3 t77;
        memset(&t77, 0, sizeof t77);
        GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)7ULL));
        gm_t88rcucg1b7uk t78 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t76, &t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t78;
    }
    {
        gm_trqfvmjtt4sij t79;
        gm_f_list_af74s0q((void *)&t79, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t79;
        gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
        gm_l14 = t80;
        gm_l15 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t81 = gm_l15;
                uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t83 = (t81 < t82);
                if (!t83) {
                    goto gm_lb6;
                }
                {
                    void * t84 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint64_t t86 = gm_l15;
                    {
                        {
                            memmove(((uint8_t *)&F.l16), (((uint8_t *)t84) + (uintptr_t)t86 * 56), 56);
                            {
                                {
                                    uint8_t t87 = (uint8_t)(((((uint8_t *)&F.l16) + 40))[4] != 0);
                                    if (t87) {
                                        goto gm_lb12;
                                    }
                                    gm_l45 = ((uint8_t)0);
                                    goto gm_lb11;
                                }
gm_lb12:;
                                {
                                    gm_l45 = ((uint8_t)1);
                                    goto gm_lb11;
                                }
gm_lb13:;
                                gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 167, 12);
                            }
gm_lb11:;
                            uint8_t t88 = gm_l45;
                            if (t88) {
                                goto gm_lb8;
                            }
                            memmove(((uint8_t *)&F.l17), ((uint8_t *)&F.l16), 56);
                            void * t89 = gm_l0;
                            gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                            gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                            gm_t88rcucg1b7uk t93 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t89, t90, t92);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 16), t93);
                            void * t94 = (void *)((uint8_t *)&F.l13);
                            gm_tftb1odc739t5 t95 = gm_f_push_o8u01sh(t94, &(*(gm_t5v0c8ia6a5mo *)((uint8_t *)&F.l17)));
                            gm_l46 = t95;
                            uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                            if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_srcb5s85si25i3o, 170, 9);
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 166, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t97 = gm_l15;
                uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                uint64_t t100 = r98;
                gm_l15 = t100;
            }
        }
gm_lb6:;
        gm_b16_8 t101 = gm_l2;
        gm_l18 = t101;
        gm_l19 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t102 = gm_l19;
                uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                uint8_t t104 = (t102 < t103);
                if (!t104) {
                    goto gm_lb14;
                }
                {
                    void * t105 = GM_LD(void *, ((uint8_t *)&gm_l18));
                    uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint64_t t107 = gm_l19;
                    {
                        {
                            void * t108 = (void *)((uint8_t *)&F.l13);
                            gm_b16_8 t109;
                            GM_ST(void *, (uint8_t *)&t109, (void *)(gm_sj3cmvbmi7cm6));
                            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(8));
                            gm_t88rcucg1b7uk t110 = gm_l12;
                            gm_tl874t2atnlh3 t111 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 16));
                            gm_t9805qtonbsag t112;
                            memset(&t112, 0, sizeof t112);
                            gm_t5v0c8ia6a5mo t113;
                            GM_ST(uint8_t, (uint8_t *)&t113 + 49, 0);
                            GM_ST(uint16_t, (uint8_t *)&t113 + 50, 0);
                            GM_ST(uint32_t, (uint8_t *)&t113 + 52, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t109);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t113 + 16), t110);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t113 + 20), t111);
                            GM_ST(void *, ((uint8_t *)&t113 + 32), ((void *)0));
                            GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t113 + 40), t112);
                            GM_ST(uint8_t, ((uint8_t *)&t113 + 48), ((uint8_t)0));
                            gm_tftb1odc739t5 t114 = gm_f_push_o8u01sh(t108, &t113);
                            gm_l47 = t114;
                            uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                            if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_srcb5s85si25i3o, 173, 9);
                            void * t116 = (void *)((uint8_t *)&F.l13);
                            gm_b16_8 t117;
                            GM_ST(void *, (uint8_t *)&t117, (void *)(gm_srkol0piugptj));
                            GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(13));
                            void * t118 = gm_l0;
                            void * t119 = (void *)(((uint8_t *)t118) + 8);
                            gm_t88rcucg1b7uk t120 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t119);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tl874t2atnlh3 t121 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 16));
                            gm_t9805qtonbsag t122;
                            memset(&t122, 0, sizeof t122);
                            gm_t5v0c8ia6a5mo t123;
                            GM_ST(uint8_t, (uint8_t *)&t123 + 49, 0);
                            GM_ST(uint16_t, (uint8_t *)&t123 + 50, 0);
                            GM_ST(uint32_t, (uint8_t *)&t123 + 52, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t123 + 0), t117);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t123 + 16), t120);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t123 + 20), t121);
                            GM_ST(void *, ((uint8_t *)&t123 + 32), ((void *)0));
                            GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t123 + 40), t122);
                            GM_ST(uint8_t, ((uint8_t *)&t123 + 48), ((uint8_t)0));
                            gm_tftb1odc739t5 t124 = gm_f_push_o8u01sh(t116, &t123);
                            gm_l48 = t124;
                            uint8_t t125 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                            if (!t125) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srcb5s85si25i3o, 174, 9);
                            goto gm_lb17;
                        }
gm_lb18:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 172, 5);
                    }
gm_lb17:;
                }
gm_lb16:;
                uint64_t t126 = gm_l19;
                uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                uint64_t t129 = r127;
                gm_l19 = t129;
            }
        }
gm_lb14:;
    }
    {
        gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_tl874t2atnlh3 t131 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 16));
        uint64_t t132 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 32));
        gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
        void * t134 = gm_l0;
        gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 56));
        gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
        gm_t88rcucg1b7uk t138 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t134, t135, t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t139 = gm_l0;
        void * t140 = (void *)(((uint8_t *)&F.l5) + 64);
        gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
        gm_tnpifv20irnph t143 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t139, t140, t142);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t144 = gm_f_empty_slice_d914i5u();
        uint32_t t145 = gm_l4;
        gm_b16_8 t146 = gm_l3;
        gm_tiilmfd16v3fc t147;
        GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t147 + 0), t145);
        GM_ST(gm_b16_8, ((uint8_t *)&t147 + 8), t146);
        gm_tqu3m2l3n8mnc t148;
        GM_ST(uint8_t, (uint8_t *)&t148 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t148 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t148 + 28, 0);
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t148), t147);
        ((uint8_t *)&t148)[24] = 1;
        gm_tqqhoivahirr0 t149;
        GM_ST(uint32_t, (uint8_t *)&t149 + 28, 0);
        GM_ST(uint32_t, (uint8_t *)&t149 + 60, 0);
        GM_ST(uint8_t, (uint8_t *)&t149 + 129, 0);
        GM_ST(uint16_t, (uint8_t *)&t149 + 130, 0);
        GM_ST(uint32_t, (uint8_t *)&t149 + 132, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t149 + 0), t130);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t149 + 16), t131);
        GM_ST(uint64_t, ((uint8_t *)&t149 + 32), t132);
        GM_ST(gm_b16_8, ((uint8_t *)&t149 + 40), t133);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t149 + 56), t138);
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t149 + 64), t143);
        GM_ST(gm_b16_8, ((uint8_t *)&t149 + 80), t144);
        GM_ST(gm_tqu3m2l3n8mnc, ((uint8_t *)&t149 + 96), t148);
        GM_ST(uint8_t, ((uint8_t *)&t149 + 128), ((uint8_t)0));
        F.l20 = t149;
        void * t150 = gm_l0;
        gm_t9qkh0rsooc8d t151 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l6));
        uint32_t t152 = gm_f_add_shared_fn_f8l99p1((void *)NULL, cx, NULL, t150, &(*(gm_tqqhoivahirr0 *)((uint8_t *)&F.l20)), t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l21 = t152;
        gm_trqfvmjtt4sij t153;
        gm_f_list_p9s2t99((void *)&t153, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l22 = t153;
        void * t154 = gm_l0;
        uint32_t t155 = GM_LD(uint32_t, (((uint8_t *)t154) + 1360));
        gm_l23 = t155;
        void * t156 = gm_l0;
        uint32_t t157 = GM_LD(uint32_t, (((uint8_t *)a1) + 36));
        GM_ST(uint32_t, (((uint8_t *)t156) + 1360), t157);
        gm_trqfvmjtt4sij t158;
        gm_f_list_31sqk5v((void *)&t158, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l24 = t158;
        gm_trqfvmjtt4sij t159;
        gm_f_list_2plb4qp((void *)&t159, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l25 = t159;
        gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
        gm_trqfvmjtt4sij t162;
        gm_f_list_7l73vjb((void *)&t162, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t163;
        gm_f_list_7l73vjb((void *)&t163, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t164 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
        void * t165 = (void *)((uint8_t *)&F.l22);
        gm_b16_8 t166 = gm_f_empty_slice_s65n31g();
        gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
        void * t168 = (void *)((uint8_t *)&F.l25);
        gm_tblkd491pk4cr t169;
        GM_ST(uint8_t, (uint8_t *)&t169 + 109, 0);
        GM_ST(uint16_t, (uint8_t *)&t169 + 110, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t169 + 0), t161);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t169 + 16), t162);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t169 + 56), t163);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t169 + 96), t164);
        GM_ST(uint8_t, ((uint8_t *)&t169 + 108), ((uint8_t)0));
        GM_ST(void *, ((uint8_t *)&t169 + 112), t165);
        GM_ST(gm_b16_8, ((uint8_t *)&t169 + 120), t167);
        GM_ST(void *, ((uint8_t *)&t169 + 136), t168);
        F.l26 = t169;
        void * t170 = (void *)(((uint8_t *)&F.l26) + 16);
        uint32_t t171 = gm_l4;
        uint32_t t172 = gm_l21;
        gm_tcc1rq4n1avb2 t173;
        GM_ST(uint32_t, ((uint8_t *)&t173 + 0), t171);
        GM_ST(uint32_t, ((uint8_t *)&t173 + 4), t172);
        gm_tftb1odc739t5 t174 = gm_f_push_1ncolkq(t170, GM_CONV(gm_cs9ufftihtbpo2, t173));
        gm_l49 = t174;
        uint8_t t175 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
        if (!t175) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srcb5s85si25i3o, 194, 5);
        gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 24));
        gm_l27 = t176;
        gm_l28 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t177 = gm_l28;
                uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                uint8_t t179 = (t177 < t178);
                if (!t179) {
                    goto gm_lb19;
                }
                {
                    void * t180 = GM_LD(void *, ((uint8_t *)&gm_l27));
                    uint64_t t181 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                    uint64_t t182 = gm_l28;
                    {
                        {
                            memmove(((uint8_t *)&F.l29), (((uint8_t *)t180) + (uintptr_t)t182 * 40), 40);
                            memmove(((uint8_t *)&F.l30), ((uint8_t *)&F.l29), 40);
                            void * t183 = gm_l0;
                            void * t184 = (void *)((uint8_t *)&F.l26);
                            gm_t88rcucg1b7uk t185 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l29) + 16));
                            gm_t88rcucg1b7uk t186 = gm_f_mono_ty_atcu0jn((void *)NULL, cx, NULL, t183, t184, t185);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l30) + 16), t186);
                            void * t187 = (void *)((uint8_t *)&F.l22);
                            gm_tftb1odc739t5 t188 = gm_f_push_am7bo0v(t187, &(*(gm_tsoie1ppgdvcg *)((uint8_t *)&F.l30)));
                            gm_l50 = t188;
                            uint8_t t189 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                            if (!t189) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srcb5s85si25i3o, 198, 9);
                            goto gm_lb22;
                        }
gm_lb23:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 195, 5);
                    }
gm_lb22:;
                }
gm_lb21:;
                uint64_t t190 = gm_l28;
                uint64_t r191; bool o192 = __builtin_add_overflow(t190, ((uint64_t)1ULL), &r191);
                uint64_t t193 = r191;
                gm_l28 = t193;
            }
        }
gm_lb19:;
    }
    {
        gm_trqfvmjtt4sij t194;
        gm_f_list_go2qg0b((void *)&t194, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l31 = t194;
        void * t195 = (void *)((uint8_t *)&F.l31);
        gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 8));
        gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
        gm_tftb1odc739t5 t198 = gm_f_extend_7hs67uj(t195, t197);
        gm_l51 = t198;
        uint8_t t199 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
        if (!t199) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srcb5s85si25i3o, 201, 5);
        gm_b16_8 t200 = gm_l2;
        gm_l32 = t200;
        gm_l33 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t201 = gm_l33;
                uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                uint8_t t203 = (t201 < t202);
                if (!t203) {
                    goto gm_lb24;
                }
                {
                    void * t204 = GM_LD(void *, ((uint8_t *)&gm_l32));
                    uint64_t t205 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                    uint64_t t206 = gm_l33;
                    {
                        {
                            uint32_t t207 = GM_LD(uint32_t, (((uint8_t *)t204) + (uintptr_t)t206 * 4));
                            gm_l34 = t207;
                            uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 8));
                            uint32_t t209 = (uint32_t)t208;
                            gm_l35 = t209;
                            void * t210 = (void *)((uint8_t *)&F.l22);
                            gm_b16_8 t211;
                            GM_ST(void *, (uint8_t *)&t211, (void *)(gm_sj3cmvbmi7cm6));
                            GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(8));
                            gm_t88rcucg1b7uk t212 = gm_l12;
                            gm_tl874t2atnlh3 t213 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 16));
                            gm_tsoie1ppgdvcg t214;
                            GM_ST(uint8_t, (uint8_t *)&t214 + 21, 0);
                            GM_ST(uint16_t, (uint8_t *)&t214 + 22, 0);
                            GM_ST(uint32_t, (uint8_t *)&t214 + 36, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t214 + 0), t211);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t214 + 16), t212);
                            GM_ST(uint8_t, ((uint8_t *)&t214 + 20), ((uint8_t)0));
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t214 + 24), t213);
                            gm_tftb1odc739t5 t215 = gm_f_push_am7bo0v(t210, &t214);
                            gm_l52 = t215;
                            uint8_t t216 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                            if (!t216) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_srcb5s85si25i3o, 204, 9);
                            void * t217 = (void *)((uint8_t *)&F.l22);
                            gm_b16_8 t218;
                            GM_ST(void *, (uint8_t *)&t218, (void *)(gm_srkol0piugptj));
                            GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(13));
                            void * t219 = gm_l0;
                            void * t220 = (void *)(((uint8_t *)t219) + 8);
                            gm_t88rcucg1b7uk t221 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t220);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tl874t2atnlh3 t222 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 16));
                            gm_tsoie1ppgdvcg t223;
                            GM_ST(uint8_t, (uint8_t *)&t223 + 21, 0);
                            GM_ST(uint16_t, (uint8_t *)&t223 + 22, 0);
                            GM_ST(uint32_t, (uint8_t *)&t223 + 36, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t223 + 0), t218);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t223 + 16), t221);
                            GM_ST(uint8_t, ((uint8_t *)&t223 + 20), ((uint8_t)0));
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t223 + 24), t222);
                            gm_tftb1odc739t5 t224 = gm_f_push_am7bo0v(t217, &t223);
                            gm_l53 = t224;
                            uint8_t t225 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                            if (!t225) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_srcb5s85si25i3o, 205, 9);
                            void * t226 = (void *)((uint8_t *)&F.l24);
                            uint32_t t227 = gm_l34;
                            uint32_t t228 = gm_l35;
                            uint32_t t229 = gm_l35;
                            uint32_t r230; bool o231 = __builtin_add_overflow(t229, ((uint32_t)1ULL), &r230);
                            if (o231) gm_trap("integer overflow in `+`", gm_srcb5s85si25i3o, 206, 48);
                            uint32_t t232 = r230;
                            gm_tl874t2atnlh3 t233;
                            GM_ST(uint32_t, ((uint8_t *)&t233 + 0), t227);
                            GM_ST(uint32_t, ((uint8_t *)&t233 + 4), t228);
                            GM_ST(uint32_t, ((uint8_t *)&t233 + 8), t232);
                            gm_tftb1odc739t5 t234 = gm_f_push_hbhlgi6(t226, t233);
                            gm_l54 = t234;
                            uint8_t t235 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                            if (!t235) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_srcb5s85si25i3o, 206, 9);
                            void * t236 = (void *)((uint8_t *)&F.l31);
                            uint32_t t237 = gm_l35;
                            gm_tftb1odc739t5 t238 = gm_f_push_im9d4ro(t236, t237);
                            gm_l55 = t238;
                            uint8_t t239 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                            if (!t239) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 1, gm_srcb5s85si25i3o, 207, 9);
                            void * t240 = (void *)((uint8_t *)&F.l31);
                            uint32_t t241 = gm_l35;
                            uint32_t r242; bool o243 = __builtin_add_overflow(t241, ((uint32_t)1ULL), &r242);
                            if (o243) gm_trap("integer overflow in `+`", gm_srcb5s85si25i3o, 208, 17);
                            uint32_t t244 = r242;
                            gm_tftb1odc739t5 t245 = gm_f_push_im9d4ro(t240, t244);
                            gm_l56 = t245;
                            uint8_t t246 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                            if (!t246) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_srcb5s85si25i3o, 208, 9);
                            goto gm_lb27;
                        }
gm_lb28:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 202, 5);
                    }
gm_lb27:;
                }
gm_lb26:;
                uint64_t t247 = gm_l33;
                uint64_t r248; bool o249 = __builtin_add_overflow(t247, ((uint64_t)1ULL), &r248);
                uint64_t t250 = r248;
                gm_l33 = t250;
            }
        }
gm_lb24:;
    }
    {
        gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
        gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&t251));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l26) + 120), t252);
        gm_l36 = ((void *)0);
        {
            {
                uint8_t t253 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l6) + 40)) != 0);
                if (!t253) {
                    goto gm_lb30;
                }
                void * t254 = GM_LD(void *, (((uint8_t *)&F.l6) + 40));
                gm_l37 = t254;
                void * t255 = gm_l37;
                void * t256 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t255);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l38 = t256;
                void * t257 = gm_l0;
                void * t258 = (void *)((uint8_t *)&F.l26);
                void * t259 = gm_l38;
                gm_b0_1 t260 = gm_f_mono_expr_p8qb7fe((void *)NULL, cx, NULL, t257, t258, t259);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t261 = gm_l38;
                gm_l36 = t261;
                goto gm_lb29;
            }
gm_lb30:;
        }
gm_lb29:;
        void * t262 = gm_l0;
        gm_tl874t2atnlh3 t263 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
        gm_b0_1 t264 = gm_f_report_soa_errors_m1bqopj((void *)NULL, cx, NULL, t262, t263);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t265 = gm_l0;
        uint32_t t266 = gm_l23;
        GM_ST(uint32_t, (((uint8_t *)t265) + 1360), t266);
        void * t267 = gm_l0;
        uint32_t t268 = gm_l21;
        gm_t9qkh0rsooc8d t269 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l6));
        gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
        gm_b16_8 t271 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
        void * t272 = gm_l36;
        gm_t9805qtonbsag t273 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l6) + 48));
        uint8_t t274 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 56));
        gm_tdgfduqjheqdm t275;
        GM_ST(uint8_t, (uint8_t *)&t275 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t275 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t275 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t275 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t275 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t275 + 60, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t275 + 0), t269);
        GM_ST(gm_b16_8, ((uint8_t *)&t275 + 8), t270);
        GM_ST(gm_b16_8, ((uint8_t *)&t275 + 24), t271);
        GM_ST(void *, ((uint8_t *)&t275 + 40), t272);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t275 + 48), t273);
        GM_ST(uint8_t, ((uint8_t *)&t275 + 56), t274);
        gm_b0_1 t276 = gm_f_set_inst_def_tcpabg0((void *)NULL, cx, NULL, t267, t268, &t275);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t277 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 24));
        gm_l39 = t277;
        gm_l40 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t278 = gm_l40;
                uint64_t t279 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                uint8_t t280 = (t278 < t279);
                if (!t280) {
                    goto gm_lb31;
                }
                {
                    void * t281 = GM_LD(void *, ((uint8_t *)&gm_l39));
                    uint64_t t282 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                    uint64_t t283 = gm_l40;
                    {
                        {
                            memmove(((uint8_t *)&F.l41), (((uint8_t *)t281) + (uintptr_t)t283 * 40), 40);
                            void * t284 = gm_l0;
                            void * t285 = gm_l0;
                            gm_t88rcucg1b7uk t286 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l41) + 16));
                            gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                            gm_b16_8 t288 = GM_LD(gm_b16_8, ((uint8_t *)&t287));
                            gm_t88rcucg1b7uk t289 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t285, t286, t288);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tl874t2atnlh3 t290 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
                            uint8_t t291 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t284, t289, t290);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t292 = (uint8_t)!t291;
                            if (t292) {
                                goto gm_lb31;
                            }
                            goto gm_lb34;
                        }
gm_lb35:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 220, 5);
                    }
gm_lb34:;
                }
gm_lb33:;
                uint64_t t293 = gm_l40;
                uint64_t r294; bool o295 = __builtin_add_overflow(t293, ((uint64_t)1ULL), &r294);
                uint64_t t296 = r294;
                gm_l40 = t296;
            }
        }
gm_lb31:;
    }
    {
        uint32_t t297 = gm_l21;
        gm_b16_8 t298 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
        gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&t298));
        gm_tiilmfd16v3fc t300;
        GM_ST(uint32_t, (uint8_t *)&t300 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t300 + 0), t297);
        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 8), t299);
        gm_l42 = t300;
        void * t301 = gm_l0;
        void * t302 = (void *)(((uint8_t *)t301) + 1424);
        uint32_t t303 = gm_l4;
        gm_b16_8 t304 = gm_l3;
        gm_tiilmfd16v3fc t305;
        GM_ST(uint32_t, (uint8_t *)&t305 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t305 + 0), t303);
        GM_ST(gm_b16_8, ((uint8_t *)&t305 + 8), t304);
        gm_tftb1odc739t5 t306 = gm_f_put_6him49q(t302, &t305, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l42)));
        gm_l57 = t306;
        uint8_t t307 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
        if (!t307) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srcb5s85si25i3o, 224, 5);
        memmove(gm_out, ((uint8_t *)&gm_l42), 24);
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
extern __attribute__((aligned(4))) uint8_t gm_f_mentions_value_2a40t7e(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_only_templates_8l3e5t6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, uint32_t a3) {
    struct gm_fr_only_templates_8l3e5t6 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* body */
    uint32_t gm_l3; /* g */
    gm_tmcj65fi8d3nd gm_l4; /* sub */
    GM_STACK_CHECK(&F, 319u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l6;
    {
        uint32_t t4 = gm_l3;
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_f_template_marker_heil9i9((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tplpbqf3duvn3 t7;
        GM_ST(uint32_t, ((uint8_t *)&t7 + 0), t4);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t7 + 4), t6);
        gm_tmcj65fi8d3nd t8;
        GM_ST(gm_tplpbqf3duvn3, ((uint8_t *)&t8 + 0), t7);
        gm_l4 = t8;
        void * t9 = gm_l0;
        uint32_t t10 = gm_l1;
        uint32_t t11 = gm_l3;
        void * t12 = (void *)((uint8_t *)&gm_l4);
        uint64_t t13 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
        if (t13 > t14 || t14 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t13, t14, ((uint64_t)1ULL), gm_srcb5s85si25i3o, 33, 46);
        uint64_t t15 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t12 + (uintptr_t)((uint64_t)0ULL) * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(t15));
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        gm_trqfvmjtt4sij t18;
        gm_f_list_go2qg0b((void *)&t18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tj607dkcelqbd t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t19 + 44, 0);
        GM_ST(void *, ((uint8_t *)&t19 + 0), t9);
        GM_ST(uint32_t, ((uint8_t *)&t19 + 8), t10);
        GM_ST(uint32_t, ((uint8_t *)&t19 + 12), t11);
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 16), t17);
        GM_ST(uint64_t, ((uint8_t *)&t19 + 32), ((uint64_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t19 + 40), ((uint8_t)0));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t19 + 48), t18);
        F.l5 = t19;
        void * t20 = (void *)((uint8_t *)&F.l5);
        void * t21 = gm_l2;
        gm_b0_1 t22 = gm_f_scan_uses_iuphl4h((void *)NULL, cx, NULL, t20, t21, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_ld3_1;
        }
        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 32));
        uint8_t t24 = (t23 > ((uint64_t)0ULL));
        if (t24) {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 40));
            uint8_t t26 = (uint8_t)!t25;
            gm_l6 = t26;
        } else {
            gm_l6 = ((uint8_t)0);
        }
        uint8_t t27 = gm_l6;
        F.ret = t27;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld3_1;
    }
    {
        void * t28 = (void *)(((uint8_t *)&F.l5) + 48);
        gm_f_deinit_padgogk(t28);
    }
    goto gm_ls29;
gm_ld3_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t30 = (void *)(((uint8_t *)&F.l5) + 48);
        gm_f_deinit_padgogk(t30);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls29:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_uses_iuphl4h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_scan_uses_iuphl4h F;
    void * gm_l1; /* e */
    void * gm_l3; /* s */
    void * gm_l4; /* w */
    uint32_t gm_l5; /* p */
    void * gm_l6; /* x */
    void * gm_l7; /* a */
    uint32_t gm_l8; /* p */
    gm_b16_8 gm_l9; /* args */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    uint32_t gm_l13; /* c */
    uint32_t gm_l14; /* c */
    uint32_t gm_l15; /* c */
    gm_tiilmfd16v3fc gm_l16; /* inst */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_t88rcucg1b7uk gm_l19; /* a */
    void * gm_l21; /* nb */
    GM_STACK_CHECK(&F, 869u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    gm_tftb1odc739t5 gm_l22;
    void * t36 = F.l0;
    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t36) + 40));
    if (t37) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t38 = F.l0;
    void * t39 = GM_LD(void *, ((uint8_t *)t38));
    gm_l3 = t39;
    void * t40 = gm_l1;
    {
        {
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)56ULL));
            if (!t42) {
                goto gm_lb2;
            }
            void * t43 = GM_LD(void *, ((((uint8_t *)t40) + 8) + 8));
            gm_l4 = t43;
            {
                {
                    void * t44 = gm_l4;
                    void * t45 = GM_LD(void *, (((uint8_t *)t44) + 40));
                    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)9ULL));
                    if (!t47) {
                        goto gm_lb4;
                    }
                    uint32_t t48 = GM_LD(uint32_t, ((((uint8_t *)t45) + 8) + 8));
                    gm_l5 = t48;
                    uint32_t t49 = gm_l5;
                    void * t50 = F.l0;
                    uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)t50) + 12));
                    uint8_t t52 = ((t49 == t51));
                    if (!t52) {
                        goto gm_lb4;
                    }
                    uint8_t t53 = F.l2;
                    uint8_t t54 = (uint8_t)!t53;
                    if (t54) {
                        void * t55 = F.l0;
                        GM_ST(uint8_t, (((uint8_t *)t55) + 40), ((uint8_t)1));
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    void * t56 = F.l0;
                    void * t57 = F.l0;
                    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)t57) + 32));
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    if (o60) gm_trap("integer overflow in `+`", gm_srcb5s85si25i3o, 67, 13);
                    uint64_t t61 = r59;
                    GM_ST(uint64_t, (((uint8_t *)t56) + 32), t61);
                    {
                        {
                            void * t62 = gm_l4;
                            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)t62) + 0));
                            uint8_t t64 = (uint8_t)(t63 == ((uint8_t)1ULL));
                            if (!t64) {
                                goto gm_lb6;
                            }
                            void * t65 = GM_LD(void *, (((uint8_t *)t62) + 8));
                            gm_l6 = t65;
                            void * t66 = F.l0;
                            void * t67 = gm_l6;
                            uint8_t t68 = F.l2;
                            gm_b0_1 t69 = gm_f_scan_uses_iuphl4h((void *)NULL, cx, NULL, t66, t67, t68);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb5;
                        }
gm_lb6:;
                    }
gm_lb5:;
                    {
                        {
                            void * t70 = gm_l4;
                            uint8_t t71 = (uint8_t)(GM_LD(void *, (((uint8_t *)t70) + 48)) != 0);
                            if (!t71) {
                                goto gm_lb8;
                            }
                            void * t72 = GM_LD(void *, (((uint8_t *)t70) + 48));
                            gm_l7 = t72;
                            void * t73 = F.l0;
                            void * t74 = gm_l7;
                            uint8_t t75 = F.l2;
                            gm_b0_1 t76 = gm_f_scan_uses_iuphl4h((void *)NULL, cx, NULL, t73, t74, t75);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb7;
                        }
gm_lb8:;
                    }
gm_lb7:;
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
            uint8_t t78 = (uint8_t)(t77 == ((uint8_t)9ULL));
            if (!t78) {
                goto gm_lb9;
            }
            uint32_t t79 = GM_LD(uint32_t, ((((uint8_t *)t40) + 8) + 8));
            gm_l8 = t79;
            {
                {
                    uint32_t t80 = gm_l8;
                    void * t81 = F.l0;
                    uint32_t t82 = GM_LD(uint32_t, (((uint8_t *)t81) + 12));
                    uint8_t t83 = ((t80 == t82));
                    if (!t83) {
                        goto gm_lb11;
                    }
                    uint8_t t84 = F.l2;
                    uint8_t t85 = (uint8_t)!t84;
                    if (!t85) {
                        goto gm_lb11;
                    }
                    void * t86 = F.l0;
                    GM_ST(uint8_t, (((uint8_t *)t86) + 40), ((uint8_t)1));
                    goto gm_lb10;
                }
gm_lb11:;
            }
gm_lb10:;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
            uint8_t t88 = (uint8_t)(t87 == ((uint8_t)33ULL));
            if (!t88) {
                goto gm_lb12;
            }
            uint8_t t89 = GM_LD(uint8_t, (((((uint8_t *)t40) + 8) + 8) + 0));
            uint8_t t90 = (uint8_t)(t89 == ((uint8_t)80ULL));
            if (!t90) {
                goto gm_lb12;
            }
            gm_b16_8 t91 = GM_LD(gm_b16_8, (((((uint8_t *)t40) + 8) + 8) + 16));
            gm_l9 = t91;
            gm_l11 = ((uint64_t)0ULL);
            uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            gm_l10 = t92;
            {
                for (;;) {
                    uint64_t t94 = gm_l11;
                    uint64_t t95 = gm_l10;
                    uint8_t t96 = (t94 < t95);
                    if (!t96) {
                        goto gm_lb13;
                    }
                    {
                        {
                            {
                                uint64_t t97 = gm_l11;
                                gm_l12 = t97;
                                void * t98 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                uint64_t t100 = gm_l12;
                                void * t101 = (void *)(((uint8_t *)t98) + (uintptr_t)t100 * 88);
                                void * t102 = F.l0;
                                uint32_t t103 = GM_LD(uint32_t, (((uint8_t *)t102) + 12));
                                uint8_t t104 = gm_f_mentions_value_2a40t7e(t101, t103);
                                if (t104) {
                                    void * t105 = F.l0;
                                    GM_ST(uint8_t, (((uint8_t *)t105) + 40), ((uint8_t)1));
                                }
                                goto gm_lb16;
                            }
gm_lb17:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 73, 45);
                        }
gm_lb16:;
                    }
gm_lb15:;
                    uint64_t t106 = gm_l11;
                    uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                    uint64_t t109 = r107;
                    gm_l11 = t109;
                }
            }
gm_lb13:;
            goto gm_lb1;
        }
gm_lb12:;
        {
            {
                {
                    uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t111 = (uint8_t)(t110 == ((uint8_t)34ULL));
                    if (!t111) {
                        goto gm_lb20;
                    }
                    uint8_t t112 = GM_LD(uint8_t, (((((uint8_t *)t40) + 8) + 8) + 0));
                    uint8_t t113 = (uint8_t)(t112 == ((uint8_t)0ULL));
                    if (!t113) {
                        goto gm_lb20;
                    }
                    uint32_t t114 = GM_LD(uint32_t, (((((uint8_t *)t40) + 8) + 8) + 8));
                    gm_l13 = t114;
                    goto gm_lb19;
                }
gm_lb20:;
                {
                    uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t116 = (uint8_t)(t115 == ((uint8_t)26ULL));
                    if (!t116) {
                        goto gm_lb21;
                    }
                    uint32_t t117 = GM_LD(uint32_t, ((((uint8_t *)t40) + 8) + 8));
                    gm_l13 = t117;
                    goto gm_lb19;
                }
gm_lb21:;
                {
                    uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
                    uint8_t t119 = (uint8_t)(t118 == ((uint8_t)27ULL));
                    if (!t119) {
                        goto gm_lb22;
                    }
                    uint32_t t120 = GM_LD(uint32_t, ((((uint8_t *)t40) + 8) + 8));
                    gm_l13 = t120;
                    goto gm_lb19;
                }
gm_lb22:;
                goto gm_lb18;
            }
gm_lb19:;
            uint32_t t121 = gm_l13;
            void * t122 = F.l0;
            uint32_t t123 = GM_LD(uint32_t, (((uint8_t *)t122) + 8));
            uint8_t t124 = ((t121 == t123));
            if (t124) {
                void * t125 = F.l0;
                GM_ST(uint8_t, (((uint8_t *)t125) + 40), ((uint8_t)1));
            }
            {
                {
                    void * t126 = gm_l3;
                    void * t127 = GM_LD(void *, (((uint8_t *)t126) + 344));
                    uint64_t t128 = GM_LD(uint64_t, ((((uint8_t *)t126) + 344) + 8));
                    uint32_t t129 = gm_l13;
                    uint64_t t130 = (uint64_t)t129;
                    if ((uintptr_t)t130 >= (uintptr_t)(t128)) gm_trap_index((uintptr_t)t130, t128, gm_srcb5s85si25i3o, 79, 24);
                    uint8_t t131 = (uint8_t)((((((uint8_t *)t127) + (uintptr_t)t130 * 136) + 96))[24] != 0);
                    if (!t131) {
                        goto gm_lb24;
                    }
                    memmove(((uint8_t *)&gm_l16), ((((uint8_t *)t127) + (uintptr_t)t130 * 136) + 96), 24);
                    gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                    gm_l17 = t132;
                    gm_l18 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t134 = gm_l18;
                            uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                            uint8_t t136 = (t134 < t135);
                            if (!t136) {
                                goto gm_lb25;
                            }
                            {
                                void * t137 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                uint64_t t139 = gm_l18;
                                {
                                    {
                                        gm_t88rcucg1b7uk t140 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t137) + (uintptr_t)t139 * 4));
                                        gm_l19 = t140;
                                        void * t141 = gm_l3;
                                        gm_t88rcucg1b7uk t142 = gm_l19;
                                        void * t143 = F.l0;
                                        gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)t143) + 16));
                                        gm_t88rcucg1b7uk t145 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t141, t142, t144);
                                        if (gm_unw.kind) {
                                            goto gm_ll133;
                                        }
                                        gm_t88rcucg1b7uk t146 = gm_l19;
                                        uint8_t t147 = (!gm_eq9m6mvtmgde(((uint8_t *)&t145), ((uint8_t *)&t146)));
                                        if (t147) {
                                            void * t148 = F.l0;
                                            GM_ST(uint8_t, (((uint8_t *)t148) + 40), ((uint8_t)1));
                                        }
                                        goto gm_lb28;
                                    }
gm_lb29:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 80, 17);
                                }
gm_lb28:;
                            }
gm_lb27:;
                            uint64_t t149 = gm_l18;
                            uint64_t r150; bool o151 = __builtin_add_overflow(t149, ((uint64_t)1ULL), &r150);
                            uint64_t t152 = r150;
                            gm_l18 = t152;
                        }
                    }
                    goto gm_ls153;
gm_ll133:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb25; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb27; }
                    }
                    goto gm_lad;
gm_ls153:;
gm_lb25:;
                    goto gm_lb23;
                }
gm_lb24:;
            }
gm_lb23:;
            void * t154 = gm_l3;
            void * t155 = GM_LD(void *, (((uint8_t *)t154) + 384));
            uint64_t t156 = GM_LD(uint64_t, ((((uint8_t *)t154) + 384) + 8));
            uint32_t t157 = gm_l13;
            uint64_t t158 = (uint64_t)t157;
            if ((uintptr_t)t158 >= (uintptr_t)(t156)) gm_trap_index((uintptr_t)t158, t156, gm_srcb5s85si25i3o, 85, 18);
            memmove(((uint8_t *)&F.l20), (((uint8_t *)t155) + (uintptr_t)t158 * 64), 64);
            {
                {
                    uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&F.l20) + 56));
                    if (!t159) {
                        goto gm_lb31;
                    }
                    uint32_t t160 = gm_l13;
                    void * t161 = F.l0;
                    gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)t161) + 48));
                    uint8_t t163 = 0;
                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t162) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t162))) + i * 4, ((uint8_t *)&t160))) { t163 = 1; break; }
                    uint8_t t164 = (uint8_t)(!t163);
                    if (!t164) {
                        goto gm_lb31;
                    }
                    void * t165 = F.l0;
                    void * t166 = (void *)(((uint8_t *)t165) + 48);
                    uint32_t t167 = gm_l13;
                    gm_tftb1odc739t5 t168 = gm_f_push_im9d4ro(t166, t167);
                    gm_l22 = t168;
                    uint8_t t169 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                    if (!t169) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcb5s85si25i3o, 87, 17);
                    {
                        {
                            uint8_t t170 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l20) + 40)) != 0);
                            if (!t170) {
                                goto gm_lb33;
                            }
                            void * t171 = GM_LD(void *, (((uint8_t *)&F.l20) + 40));
                            gm_l21 = t171;
                            void * t172 = F.l0;
                            void * t173 = gm_l21;
                            gm_b0_1 t174 = gm_f_scan_uses_iuphl4h((void *)NULL, cx, NULL, t172, t173, ((uint8_t)0));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb32;
                        }
gm_lb33:;
                    }
gm_lb32:;
                    goto gm_lb30;
                }
gm_lb31:;
            }
gm_lb30:;
            goto gm_lb1;
        }
gm_lb18:;
        {
            goto gm_lb1;
        }
gm_lb34:;
        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 61, 5);
    }
gm_lb1:;
    void * t175 = gm_l1;
    gm_closure t176 = ((gm_closure){(void *)gm_f_fn_m232ppe, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t175, t176);
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
__attribute__((aligned(4))) uint8_t gm_f_share_instance_1q66m8k(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2) {
    struct gm_fr_share_instance_1q66m8k F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* tps */
    uint32_t gm_l3; /* f */
    gm_b16_8 gm_l4; /* generics */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint32_t gm_l10; /* g */
    gm_b16_8 gm_l11; /* text */
    gm_tiilmfd16v3fc gm_l12; /* shared */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint32_t gm_l17; /* g */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_tiilmfd16v3fc gm_l21; /* t */
    gm_tj64ui6kruni6 gm_l23; /* err */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_tuo61a5rdrbtf gm_l27; /* site */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* k */
    void * gm_l32; /* table */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    void * gm_l40; /* b */
    gm_tl874t2atnlh3 gm_l41; /* span */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    uint32_t gm_l45; /* p */
    void * gm_l46; /* body */
    GM_STACK_CHECK(&F, 3554u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_tn68el8d14vku gm_l47;
    gm_t53at80pfdus3 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tqu3m2l3n8mnc gm_l52;
    gm_tiilmfd16v3fc gm_l53;
    gm_tn68el8d14vku gm_l54;
    gm_tftb1odc739t5 gm_l55;
    gm_tfeab279c16rr gm_l56;
    gm_t1jll27j7ae34 gm_l57;
    gm_tabb710d9mq2e gm_l58;
    uint32_t gm_l59;
    gm_tftb1odc739t5 gm_l60;
    gm_tq7s98ihonkju gm_l61;
    gm_tftb1odc739t5 gm_l62;
    gm_tftb1odc739t5 gm_l63;
    gm_tftb1odc739t5 gm_l64;
    gm_tl874t2atnlh3 gm_l65;
    gm_tftb1odc739t5 gm_l66;
    gm_tftb1odc739t5 gm_l67;
    {
        uint32_t t44 = GM_LD(uint32_t, ((uint8_t *)a1));
        gm_l3 = t44;
        void * t45 = gm_l0;
        void * t46 = GM_LD(void *, (((uint8_t *)t45) + 344));
        uint64_t t47 = GM_LD(uint64_t, ((((uint8_t *)t45) + 344) + 8));
        uint32_t t48 = gm_l3;
        uint64_t t49 = (uint64_t)t48;
        if ((uintptr_t)t49 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t49, t47, gm_srcb5s85si25i3o, 101, 17);
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((((uint8_t *)t46) + (uintptr_t)t49 * 136) + 80));
        gm_l4 = t50;
        gm_trqfvmjtt4sij t51;
        gm_f_list_nof7vfj((void *)&t51, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t51;
        gm_trqfvmjtt4sij t52;
        gm_f_list_allvvbh((void *)&t52, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t52;
        gm_b16_8 t53 = gm_l4;
        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
        gm_l7 = t54;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t55 = gm_l8;
                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t57 = (t55 < t56);
                if (!t57) {
                    goto gm_lb1;
                }
                {
                    uint64_t t58 = gm_l8;
                    void * t59 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t61 = gm_l8;
                    uint32_t t62 = GM_LD(uint32_t, (((uint8_t *)t59) + (uintptr_t)t61 * 4));
                    gm_tn68el8d14vku t63;
                    GM_ST(uint32_t, (uint8_t *)&t63 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t63 + 0), t58);
                    GM_ST(uint32_t, ((uint8_t *)&t63 + 8), t62);
                    gm_l47 = t63;
                    {
                        {
                            uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)&gm_l47));
                            gm_l9 = t64;
                            uint32_t t65 = GM_LD(uint32_t, (((uint8_t *)&gm_l47) + 8));
                            gm_l10 = t65;
                            uint32_t t66 = gm_l10;
                            gm_b16_8 t67 = gm_l2;
                            uint8_t t68 = 0;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t67) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t67))) + i * 4, ((uint8_t *)&t66))) { t68 = 1; break; }
                            uint8_t t69 = (uint8_t)(t68);
                            if (t69) {
                                {
                                    {
                                        void * t70 = gm_l0;
                                        void * t71 = (void *)(((uint8_t *)t70) + 8);
                                        void * t72 = GM_LD(void *, (((uint8_t *)a1) + 8));
                                        uint64_t t73 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 8));
                                        uint64_t t74 = gm_l9;
                                        if ((uintptr_t)t74 >= (uintptr_t)(t73)) gm_trap_index((uintptr_t)t74, t73, gm_srcb5s85si25i3o, 106, 46);
                                        gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t72) + (uintptr_t)t74 * 4));
                                        gm_f_kind_jcnt4hm((void *)&gm_l48, t71, GM_CONV(gm_csguankko4lmhd, t75));
                                        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l48) + 0));
                                        uint8_t t77 = (uint8_t)(t76 == ((uint8_t)21ULL));
                                        if (!t77) {
                                            goto gm_lb7;
                                        }
                                        uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)&gm_l48) + 8) + 0));
                                        uint8_t t79 = (uint8_t)(t78 == ((uint8_t)3ULL));
                                        if (!t79) {
                                            goto gm_lb7;
                                        }
                                        gm_b16_8 t80 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l48) + 8) + 8));
                                        gm_l11 = t80;
                                        void * t81 = (void *)((uint8_t *)&F.l6);
                                        uint32_t t82 = gm_l10;
                                        gm_b16_8 t83 = gm_l11;
                                        gm_tiilmfd16v3fc t84;
                                        GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
                                        GM_ST(uint32_t, ((uint8_t *)&t84 + 0), t82);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t84 + 8), t83);
                                        gm_tftb1odc739t5 t85 = gm_f_push_gtq94gd(t81, &t84);
                                        gm_l49 = t85;
                                        uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                        if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srcb5s85si25i3o, 107, 13);
                                        void * t87 = (void *)((uint8_t *)&F.l5);
                                        void * t88 = gm_l0;
                                        gm_t88rcucg1b7uk t89 = gm_f_template_marker_heil9i9((void *)NULL, cx, NULL, t88);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t90 = gm_f_push_k3l9nic(t87, GM_CONV(gm_csguankko4lmhd, t89));
                                        gm_l50 = t90;
                                        uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                                        if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srcb5s85si25i3o, 108, 13);
                                        goto gm_lb6;
                                    }
gm_lb7:;
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
gm_lb6:;
                            } else {
                                void * t92 = (void *)((uint8_t *)&F.l5);
                                void * t93 = GM_LD(void *, (((uint8_t *)a1) + 8));
                                uint64_t t94 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 8));
                                uint64_t t95 = gm_l9;
                                if ((uintptr_t)t95 >= (uintptr_t)(t94)) gm_trap_index((uintptr_t)t95, t94, gm_srcb5s85si25i3o, 110, 25);
                                gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t93) + (uintptr_t)t95 * 4));
                                gm_tftb1odc739t5 t97 = gm_f_push_k3l9nic(t92, GM_CONV(gm_csguankko4lmhd, t96));
                                gm_l51 = t97;
                                uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                                if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srcb5s85si25i3o, 110, 13);
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 104, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t99 = gm_l8;
                uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                uint64_t t102 = r100;
                gm_l8 = t102;
            }
        }
gm_lb1:;
    }
    {
        void * t103 = gm_l0;
        void * t104 = (void *)(((uint8_t *)t103) + 1424);
        uint32_t t105 = gm_l3;
        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
        gm_tiilmfd16v3fc t108;
        GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t108 + 0), t105);
        GM_ST(gm_b16_8, ((uint8_t *)&t108 + 8), t107);
        gm_f_get_s7blvas((void *)&gm_l52, t104, &t108);
        uint8_t t109 = (uint8_t)((((uint8_t *)&gm_l52))[24] != 0);
        if (t109) {
            memmove(((uint8_t *)&gm_l53), ((uint8_t *)&gm_l52), 24);
        } else {
            void * t110 = gm_l0;
            gm_b16_8 t111 = gm_l2;
            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
            gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
            gm_t0ut88vjdgrt1 t114 = GM_LD(gm_t0ut88vjdgrt1, ((uint8_t *)a1));
            gm_f_make_shared_r51o03d((void *)&gm_l53, (void *)NULL, cx, NULL, t110, &t114, t111, t113);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l53), 24);
        gm_trqfvmjtt4sij t115;
        gm_f_list_oalq03d((void *)&t115, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t115;
        gm_b16_8 t116 = gm_l4;
        gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
        gm_l14 = t117;
        gm_l15 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t118 = gm_l15;
                uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t120 = (t118 < t119);
                if (!t120) {
                    goto gm_lb8;
                }
                {
                    uint64_t t121 = gm_l15;
                    void * t122 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint64_t t124 = gm_l15;
                    uint32_t t125 = GM_LD(uint32_t, (((uint8_t *)t122) + (uintptr_t)t124 * 4));
                    gm_tn68el8d14vku t126;
                    GM_ST(uint32_t, (uint8_t *)&t126 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t126 + 0), t121);
                    GM_ST(uint32_t, ((uint8_t *)&t126 + 8), t125);
                    gm_l54 = t126;
                    {
                        {
                            uint64_t t127 = GM_LD(uint64_t, ((uint8_t *)&gm_l54));
                            gm_l16 = t127;
                            uint32_t t128 = GM_LD(uint32_t, (((uint8_t *)&gm_l54) + 8));
                            gm_l17 = t128;
                            void * t129 = (void *)((uint8_t *)&F.l13);
                            uint32_t t130 = gm_l17;
                            void * t131 = GM_LD(void *, (((uint8_t *)a1) + 8));
                            uint64_t t132 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 8));
                            uint64_t t133 = gm_l16;
                            if ((uintptr_t)t133 >= (uintptr_t)(t132)) gm_trap_index((uintptr_t)t133, t132, gm_srcb5s85si25i3o, 115, 73);
                            gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t131) + (uintptr_t)t133 * 4));
                            gm_tplpbqf3duvn3 t135;
                            GM_ST(uint32_t, ((uint8_t *)&t135 + 0), t130);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t135 + 4), t134);
                            gm_tftb1odc739t5 t136 = gm_f_push_h69n5fi(t129, GM_CONV(gm_cs7e9ppsd384bj, t135));
                            gm_l55 = t136;
                            uint8_t t137 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                            if (!t137) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 1, gm_srcb5s85si25i3o, 115, 39);
                            goto gm_lb11;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 115, 5);
                    }
gm_lb11:;
                }
gm_lb10:;
                uint64_t t138 = gm_l15;
                uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                uint64_t t141 = r139;
                gm_l15 = t141;
            }
        }
gm_lb8:;
    }
    {
        void * t142 = gm_l0;
        gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
        gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
        gm_tl874t2atnlh3 t145 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
        gm_toceffb697p0u t146;
        GM_ST(uint8_t, (uint8_t *)&t146 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t146 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t146), t145);
        ((uint8_t *)&t146)[12] = 1;
        gm_b0_1 t147 = gm_f_check_len_exprs_m8s6m0h((void *)NULL, cx, NULL, t142, t144, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t148;
        gm_f_list_vp1djv1_2((void *)&t148, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l18 = t148;
        gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_l19 = t149;
        gm_l20 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t150 = gm_l20;
                uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                uint8_t t152 = (t150 < t151);
                if (!t152) {
                    goto gm_lb13;
                }
                {
                    void * t153 = GM_LD(void *, ((uint8_t *)&gm_l19));
                    uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                    uint64_t t155 = gm_l20;
                    {
                        {
                            memmove(((uint8_t *)&gm_l21), (((uint8_t *)t153) + (uintptr_t)t155 * 24), 24);
                            gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                            gm_f_parse_template_rlpl2k8((void *)&gm_l57, (void *)NULL, cx, NULL, t156);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                {
                                    uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l57) + 0));
                                    uint8_t t158 = (uint8_t)(t157 == ((uint8_t)0ULL));
                                    if (!t158) {
                                        goto gm_lb19;
                                    }
                                    memmove(((uint8_t *)&F.l22), (((uint8_t *)&gm_l57) + 8), 48);
                                    memmove(((uint8_t *)&gm_l56), ((uint8_t *)&F.l22), 48);
                                    goto gm_lb18;
                                }
gm_lb19:;
                                {
                                    uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l57) + 0));
                                    uint8_t t160 = (uint8_t)(t159 == ((uint8_t)1ULL));
                                    if (!t160) {
                                        goto gm_lb20;
                                    }
                                    memmove(((uint8_t *)&gm_l23), (((uint8_t *)&gm_l57) + 8), 32);
                                    void * t161 = gm_l0;
                                    gm_tl874t2atnlh3 t162 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
                                    gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                                    gm_tnpifv20irnph t164;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t164 + 0), t163);
                                    gm_f_format_jvf1ma2((void *)&gm_l58, (void *)NULL, cx, NULL, t164);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t165 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                    if (!t165) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 8, gm_srcb5s85si25i3o, 123, 35);
                                    gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                                    gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
                                    gm_b0_1 t168 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t161, t162, t167);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb15;
                                }
gm_lb20:;
                                gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 120, 19);
                            }
gm_lb18:;
                            memmove(((uint8_t *)&F.l24), ((uint8_t *)&gm_l56), 48);
                            gm_b16_8 t169 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                            gm_l25 = t169;
                            gm_l26 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t170 = gm_l26;
                                    uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                    uint8_t t172 = (t170 < t171);
                                    if (!t172) {
                                        goto gm_lb21;
                                    }
                                    {
                                        void * t173 = GM_LD(void *, ((uint8_t *)&gm_l25));
                                        uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                        uint64_t t175 = gm_l26;
                                        {
                                            {
                                                gm_tuo61a5rdrbtf t176 = GM_LD(gm_tuo61a5rdrbtf, (((uint8_t *)t173) + (uintptr_t)t175 * 12));
                                                gm_l27 = t176;
                                                uint32_t t177 = GM_LD(uint32_t, ((uint8_t *)&gm_l27));
                                                uint32_t t178 = GM_LD(uint32_t, ((uint8_t *)&gm_l21));
                                                uint8_t t179 = ((t177 == t178));
                                                if (t179) {
                                                    void * t180 = gm_l0;
                                                    gm_tl874t2atnlh3 t181 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
                                                    gm_tkn42u7na3t81 t182 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&gm_l27) + 4));
                                                    gm_th2lqbo77pvsc t183;
                                                    gm_f_check_template_brtb8t0((void *)&t183, (void *)NULL, cx, NULL, t180, t181, &(*(gm_tfeab279c16rr *)((uint8_t *)&F.l24)), t182);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                goto gm_lb24;
                                            }
gm_lb25:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 127, 9);
                                        }
gm_lb24:;
                                    }
gm_lb23:;
                                    uint64_t t184 = gm_l26;
                                    uint64_t r185; bool o186 = __builtin_add_overflow(t184, ((uint64_t)1ULL), &r185);
                                    uint64_t t187 = r185;
                                    gm_l26 = t187;
                                }
                            }
gm_lb21:;
                            gm_trqfvmjtt4sij t188;
                            gm_f_list_go2qg0b((void *)&t188, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l28 = t188;
                            gm_l30 = ((uint64_t)0ULL);
                            uint64_t t189 = GM_LD(uint64_t, ((((uint8_t *)&F.l24) + 16) + 8));
                            gm_l29 = t189;
                            {
                                for (;;) {
                                    uint64_t t190 = gm_l30;
                                    uint64_t t191 = gm_l29;
                                    uint8_t t192 = (t190 < t191);
                                    if (!t192) {
                                        goto gm_lb26;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t193 = gm_l30;
                                                gm_l31 = t193;
                                                void * t194 = (void *)((uint8_t *)&F.l28);
                                                uint64_t t195 = GM_LD(uint64_t, ((((uint8_t *)&F.l24) + 32) + 8));
                                                uint8_t t196 = (t195 > ((uint64_t)0ULL));
                                                if (t196) {
                                                    void * t197 = GM_LD(void *, (((uint8_t *)&F.l24) + 32));
                                                    uint64_t t198 = GM_LD(uint64_t, ((((uint8_t *)&F.l24) + 32) + 8));
                                                    uint64_t t199 = gm_l31;
                                                    if ((uintptr_t)t199 >= (uintptr_t)(t198)) gm_trap_index((uintptr_t)t199, t198, gm_srcb5s85si25i3o, 131, 79);
                                                    uint64_t t200 = GM_LD(uint64_t, (((uint8_t *)t197) + (uintptr_t)t199 * 8));
                                                    uint32_t t201 = (uint32_t)t200;
                                                    gm_l59 = t201;
                                                } else {
                                                    uint64_t t202 = gm_l31;
                                                    uint32_t t203 = (uint32_t)t202;
                                                    gm_l59 = t203;
                                                }
                                                uint32_t t204 = gm_l59;
                                                gm_tftb1odc739t5 t205 = gm_f_push_im9d4ro(t194, t204);
                                                gm_l60 = t205;
                                                uint8_t t206 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                                if (!t206) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_srcb5s85si25i3o, 131, 40);
                                                goto gm_lb29;
                                            }
gm_lb30:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 131, 9);
                                        }
gm_lb29:;
                                    }
gm_lb28:;
                                    uint64_t t207 = gm_l30;
                                    uint64_t r208; bool o209 = __builtin_add_overflow(t207, ((uint64_t)1ULL), &r208);
                                    uint64_t t210 = r208;
                                    gm_l30 = t210;
                                }
                            }
gm_lb26:;
                            gm_b16_8 t211 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
                            gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 16));
                            gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&F.l28));
                            gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&t213));
                            gm_tfeab279c16rr t215;
                            GM_ST(gm_b16_8, ((uint8_t *)&t215 + 0), t211);
                            GM_ST(gm_b16_8, ((uint8_t *)&t215 + 16), t212);
                            GM_ST(gm_b16_8, ((uint8_t *)&t215 + 32), t214);
                            gm_tq7s98ihonkju t216 = gm_f_new_4n5f3sc((void *)NULL, cx, NULL, &t215, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kkbiditauji)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l61 = t216;
                            uint8_t t217 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                            if (!t217) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 8, gm_srcb5s85si25i3o, 132, 18);
                            void * t218 = GM_LD(void *, (((uint8_t *)&gm_l61) + 8));
                            gm_l32 = t218;
                            void * t219 = (void *)((uint8_t *)&F.l18);
                            void * t220 = gm_l0;
                            void * t221 = (void *)(((uint8_t *)t220) + 8);
                            gm_t53at80pfdus3 t222;
                            memset(&t222, 0, sizeof t222);
                            GM_ST(uint8_t, ((uint8_t *)&t222 + 0), ((uint8_t)7ULL));
                            gm_t88rcucg1b7uk t223 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t221, &t222);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t224 = gm_l32;
                            gm_tl874t2atnlh3 t225 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
                            gm_t2b6tm8c0vgo1 t226;
                            memset(&t226, 0, sizeof t226);
                            GM_ST(uint8_t, ((uint8_t *)&t226 + 0), ((uint8_t)57ULL));
                            GM_ST(void *, ((uint8_t *)&t226 + 8), t224);
                            void * t227 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t223, &t226, t225);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t228 = gm_f_push_d943vgp_2(t219, &(*(gm_tcgrjq9toscke *)((uint8_t *)t227)));
                            gm_l62 = t228;
                            uint8_t t229 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                            if (!t229) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_srcb5s85si25i3o, 133, 9);
                            void * t230 = (void *)((uint8_t *)&F.l18);
                            void * t231 = gm_l0;
                            void * t232 = (void *)(((uint8_t *)t231) + 8);
                            gm_t88rcucg1b7uk t233 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t232);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                            gm_tl874t2atnlh3 t235 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
                            gm_t2b6tm8c0vgo1 t236;
                            memset(&t236, 0, sizeof t236);
                            GM_ST(uint8_t, ((uint8_t *)&t236 + 0), ((uint8_t)5ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t236 + 8), t234);
                            void * t237 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t233, &t236, t235);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t238 = gm_f_push_d943vgp_2(t230, &(*(gm_tcgrjq9toscke *)((uint8_t *)t237)));
                            gm_l63 = t238;
                            uint8_t t239 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                            if (!t239) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 1, gm_srcb5s85si25i3o, 134, 9);
                            goto gm_lb16;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 119, 5);
                    }
gm_lb16:;
                }
gm_lb15:;
                uint64_t t240 = gm_l20;
                uint64_t r241; bool o242 = __builtin_add_overflow(t240, ((uint64_t)1ULL), &r241);
                uint64_t t243 = r241;
                gm_l20 = t243;
            }
        }
gm_lb13:;
    }
    {
        void * t244 = gm_l0;
        void * t245 = GM_LD(void *, (((uint8_t *)t244) + 384));
        uint64_t t246 = GM_LD(uint64_t, ((((uint8_t *)t244) + 384) + 8));
        uint32_t t247 = gm_l3;
        uint64_t t248 = (uint64_t)t247;
        if ((uintptr_t)t248 >= (uintptr_t)(t246)) gm_trap_index((uintptr_t)t248, t246, gm_srcb5s85si25i3o, 137, 12);
        memmove(((uint8_t *)&F.l33), (((uint8_t *)t245) + (uintptr_t)t248 * 64), 64);
        void * t249 = gm_l0;
        void * t250 = GM_LD(void *, (((uint8_t *)t249) + 344));
        uint64_t t251 = GM_LD(uint64_t, ((((uint8_t *)t249) + 344) + 8));
        uint32_t t252 = GM_LD(uint32_t, (((uint8_t *)a1) + 4));
        uint64_t t253 = (uint64_t)t252;
        if ((uintptr_t)t253 >= (uintptr_t)(t251)) gm_trap_index((uintptr_t)t253, t251, gm_srcb5s85si25i3o, 138, 12);
        memmove(((uint8_t *)&F.l34), (((uint8_t *)t250) + (uintptr_t)t253 * 136), 136);
        gm_trqfvmjtt4sij t254;
        gm_f_list_p9s2t99((void *)&t254, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l35 = t254;
        gm_b16_8 t255 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 24));
        gm_l36 = t255;
        gm_l37 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t256 = gm_l37;
                uint64_t t257 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                uint8_t t258 = (t256 < t257);
                if (!t258) {
                    goto gm_lb31;
                }
                {
                    void * t259 = GM_LD(void *, ((uint8_t *)&gm_l36));
                    uint64_t t260 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                    uint64_t t261 = gm_l37;
                    {
                        {
                            memmove(((uint8_t *)&F.l38), (((uint8_t *)t259) + (uintptr_t)t261 * 40), 40);
                            memmove(((uint8_t *)&F.l39), ((uint8_t *)&F.l38), 40);
                            void * t262 = gm_l0;
                            gm_t88rcucg1b7uk t263 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l38) + 16));
                            gm_b16_8 t264 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                            gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
                            gm_t88rcucg1b7uk t266 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t262, t263, t265);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l39) + 16), t266);
                            void * t267 = (void *)((uint8_t *)&F.l35);
                            gm_tftb1odc739t5 t268 = gm_f_push_am7bo0v(t267, &(*(gm_tsoie1ppgdvcg *)((uint8_t *)&F.l39)));
                            gm_l64 = t268;
                            uint8_t t269 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                            if (!t269) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_srcb5s85si25i3o, 143, 9);
                            goto gm_lb34;
                        }
gm_lb35:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 140, 5);
                    }
gm_lb34:;
                }
gm_lb33:;
                uint64_t t270 = gm_l37;
                uint64_t r271; bool o272 = __builtin_add_overflow(t270, ((uint64_t)1ULL), &r271);
                uint64_t t273 = r271;
                gm_l37 = t273;
            }
        }
gm_lb31:;
        {
            {
                uint8_t t274 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l33) + 40)) != 0);
                if (!t274) {
                    goto gm_lb37;
                }
                void * t275 = GM_LD(void *, (((uint8_t *)&F.l33) + 40));
                gm_l40 = t275;
                void * t276 = gm_l40;
                gm_tl874t2atnlh3 t277 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t276) + 72));
                gm_l65 = t277;
                goto gm_lb36;
            }
gm_lb37:;
            gm_tl874t2atnlh3 t278 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
            gm_l65 = t278;
        }
gm_lb36:;
    }
    {
        gm_tl874t2atnlh3 t279 = gm_l65;
        gm_l41 = t279;
        gm_trqfvmjtt4sij t280;
        gm_f_list_vp1djv1_2((void *)&t280, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l42 = t280;
        gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 8));
        gm_l43 = t281;
        gm_l44 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t282 = gm_l44;
                uint64_t t283 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                uint8_t t284 = (t282 < t283);
                if (!t284) {
                    goto gm_lb38;
                }
                {
                    void * t285 = GM_LD(void *, ((uint8_t *)&gm_l43));
                    uint64_t t286 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                    uint64_t t287 = gm_l44;
                    {
                        {
                            uint32_t t288 = GM_LD(uint32_t, (((uint8_t *)t285) + (uintptr_t)t287 * 4));
                            gm_l45 = t288;
                            void * t289 = (void *)((uint8_t *)&F.l42);
                            void * t290 = GM_LD(void *, ((uint8_t *)&F.l35));
                            uint64_t t291 = GM_LD(uint64_t, (((uint8_t *)&F.l35) + 8));
                            uint32_t t292 = gm_l45;
                            uint64_t t293 = (uint64_t)t292;
                            if ((uintptr_t)t293 >= (uintptr_t)(t291)) gm_trap_index((uintptr_t)t293, t291, gm_srcb5s85si25i3o, 147, 44);
                            gm_t88rcucg1b7uk t294 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t290) + (uintptr_t)t293 * 40) + 16));
                            uint32_t t295 = gm_l45;
                            gm_tl874t2atnlh3 t296 = gm_l41;
                            gm_t2b6tm8c0vgo1 t297;
                            memset(&t297, 0, sizeof t297);
                            GM_ST(uint8_t, ((uint8_t *)&t297 + 0), ((uint8_t)15ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t297 + 8), t295);
                            void * t298 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t294, &t297, t296);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t299 = gm_f_push_d943vgp_2(t289, &(*(gm_tcgrjq9toscke *)((uint8_t *)t298)));
                            gm_l66 = t299;
                            uint8_t t300 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                            if (!t300) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_srcb5s85si25i3o, 147, 27);
                            goto gm_lb41;
                        }
gm_lb42:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 147, 5);
                    }
gm_lb41:;
                }
gm_lb40:;
                uint64_t t301 = gm_l44;
                uint64_t r302; bool o303 = __builtin_add_overflow(t301, ((uint64_t)1ULL), &r302);
                uint64_t t304 = r302;
                gm_l44 = t304;
            }
        }
gm_lb38:;
        uint64_t t305 = GM_LD(uint64_t, (((uint8_t *)&F.l18) + 8));
        uint64_t t306 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
        uint64_t r307; bool o308 = __builtin_mul_overflow(((uint64_t)2ULL), t306, &r307);
        if (o308) gm_trap("integer overflow in `*`", gm_srcb5s85si25i3o, 148, 32);
        uint64_t t309 = r307;
        uint8_t t310 = (!(t305 == t309));
        if (t310) {
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        void * t311 = (void *)((uint8_t *)&F.l42);
        gm_b16_8 t312 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
        gm_b16_8 t313 = GM_LD(gm_b16_8, ((uint8_t *)&t312));
        gm_tftb1odc739t5 t314 = gm_f_extend_gig5o6g(t311, t313);
        gm_l67 = t314;
        uint8_t t315 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
        if (!t315) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 1, gm_srcb5s85si25i3o, 149, 5);
        gm_t88rcucg1b7uk t316 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l34) + 56));
        uint32_t t317 = GM_LD(uint32_t, ((uint8_t *)&gm_l12));
        gm_b16_8 t318 = GM_LD(gm_b16_8, ((uint8_t *)&F.l42));
        gm_tsth9qu1dv0g3 t319;
        memset(&t319, 0, sizeof t319);
        GM_ST(uint8_t, ((uint8_t *)&t319 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t319 + 8), t317);
        gm_to76jli3bqddq t320;
        GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t320 + 0), t319);
        GM_ST(gm_b16_8, ((uint8_t *)&t320 + 40), t318);
        gm_tl874t2atnlh3 t321 = gm_l41;
        gm_t2b6tm8c0vgo1 t322;
        GM_ST(uint8_t, (uint8_t *)&t322 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t322 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t322 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t322 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t322 + 8), t320);
        void * t323 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t316, &t322, t321);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l46 = t323;
    }
    {
        void * t324 = gm_l0;
        uint32_t t325 = GM_LD(uint32_t, (((uint8_t *)a1) + 4));
        gm_t9qkh0rsooc8d t326 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l33));
        gm_b16_8 t327 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 8));
        gm_b16_8 t328 = GM_LD(gm_b16_8, ((uint8_t *)&F.l35));
        void * t329 = gm_l46;
        gm_t9805qtonbsag t330 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l33) + 48));
        uint8_t t331 = GM_LD(uint8_t, (((uint8_t *)&F.l33) + 56));
        gm_tdgfduqjheqdm t332;
        GM_ST(uint8_t, (uint8_t *)&t332 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t332 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t332 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t332 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t332 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t332 + 60, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t332 + 0), t326);
        GM_ST(gm_b16_8, ((uint8_t *)&t332 + 8), t327);
        GM_ST(gm_b16_8, ((uint8_t *)&t332 + 24), t328);
        GM_ST(void *, ((uint8_t *)&t332 + 40), t329);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t332 + 48), t330);
        GM_ST(uint8_t, ((uint8_t *)&t332 + 56), t331);
        gm_b0_1 t333 = gm_f_set_inst_def_tcpabg0((void *)NULL, cx, NULL, t324, t325, &t332);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)1);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_template_params_6stj17b(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_template_params_6stj17b F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* r */
    void * gm_l5; /* b */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint32_t gm_l8; /* g */
    GM_STACK_CHECK(&F, 506u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    {
        {
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 1368);
            uint32_t t13 = gm_l1;
            gm_f_get_hi5nrra((void *)&gm_l9, t12, t13);
            uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
            if (!t14) {
                goto gm_lb2;
            }
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
            gm_l2 = t15;
            gm_b16_8 t16 = gm_l2;
            F.ret = t16;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_trqfvmjtt4sij t17;
    gm_f_list_go2qg0b((void *)&t17, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t17;
    void * t18 = gm_l0;
    void * t19 = GM_LD(void *, (((uint8_t *)t18) + 384));
    uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)t18) + 384) + 8));
    uint32_t t21 = gm_l1;
    uint64_t t22 = (uint64_t)t21;
    if ((uintptr_t)t22 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t22, t20, gm_srcb5s85si25i3o, 19, 12);
    memmove(((uint8_t *)&F.l4), (((uint8_t *)t19) + (uintptr_t)t22 * 64), 64);
    {
        {
            gm_t9qkh0rsooc8d t23 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l4));
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
            uint8_t t25 = (uint8_t)((t24 == ((uint8_t)0ULL)));
            if (!t25) {
                goto gm_lb4;
            }
            uint8_t t26 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l4) + 40)) != 0);
            if (!t26) {
                goto gm_lb4;
            }
            void * t27 = GM_LD(void *, (((uint8_t *)&F.l4) + 40));
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_l5 = t27;
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 344));
            uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t29) + 344) + 8));
            uint32_t t32 = gm_l1;
            uint64_t t33 = (uint64_t)t32;
            if ((uintptr_t)t33 >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)t33, t31, gm_srcb5s85si25i3o, 21, 18);
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((((uint8_t *)t30) + (uintptr_t)t33 * 136) + 80));
            gm_l6 = t34;
            gm_l7 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t35 = gm_l7;
                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t37 = (t35 < t36);
                    if (!t37) {
                        goto gm_lb5;
                    }
                    {
                        void * t38 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t40 = gm_l7;
                        {
                            {
                                uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)t38) + (uintptr_t)t40 * 4));
                                gm_l8 = t41;
                                void * t42 = gm_l0;
                                uint32_t t43 = gm_l1;
                                void * t44 = gm_l5;
                                uint32_t t45 = gm_l8;
                                uint8_t t46 = gm_f_only_templates_8l3e5t6((void *)NULL, cx, NULL, t42, t43, t44, t45);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t46) {
                                    void * t47 = (void *)((uint8_t *)&F.l3);
                                    uint32_t t48 = gm_l8;
                                    gm_tftb1odc739t5 t49 = gm_f_push_im9d4ro(t47, t48);
                                    gm_l10 = t49;
                                    uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                    if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcb5s85si25i3o, 22, 45);
                                }
                                goto gm_lb8;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 21, 9);
                        }
gm_lb8:;
                    }
gm_lb7:;
                    uint64_t t51 = gm_l7;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l7 = t54;
                }
            }
gm_lb5:;
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t55 = gm_l0;
    void * t56 = (void *)(((uint8_t *)t55) + 1368);
    uint32_t t57 = gm_l1;
    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
    gm_tftb1odc739t5 t60 = gm_f_put_i2clb58(t56, t57, t59);
    gm_l11 = t60;
    uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcb5s85si25i3o, 25, 5);
    gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
    F.ret = t63;
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
