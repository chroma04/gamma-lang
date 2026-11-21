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
gm_b0_1 gm_dtvslis4o40u_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_terksaa(void *env, gm_node *cx, gm_node *inst, const void * a0);
extern uint8_t gm_c0fhj0gi05s[2] __attribute__((aligned(1)));
extern uint8_t gm_c0m0l8q79po[17] __attribute__((aligned(1)));
extern uint8_t gm_c0u09tvsmdv[1] __attribute__((aligned(1)));
extern uint8_t gm_c2keo1qaake[4] __attribute__((aligned(1)));
extern uint8_t gm_c2t4q8gsqsl[3] __attribute__((aligned(1)));
extern uint8_t gm_c2vlljohddh[10] __attribute__((aligned(1)));
extern uint8_t gm_c32pk0vs0kc[5] __attribute__((aligned(1)));
extern uint8_t gm_c3i8biff6gm[11] __attribute__((aligned(1)));
extern uint8_t gm_c4crumb5mc9[1] __attribute__((aligned(1)));
extern uint8_t gm_c4f2cgckmo6[7] __attribute__((aligned(1)));
extern uint8_t gm_c5urm6ehcs8[8] __attribute__((aligned(1)));
extern uint8_t gm_c5v8jddvmh5[13] __attribute__((aligned(1)));
extern uint8_t gm_c6iib8ocmqh[11] __attribute__((aligned(1)));
extern uint8_t gm_c6iptqebh84[6] __attribute__((aligned(1)));
extern uint8_t gm_c708i8j8tcn[6] __attribute__((aligned(1)));
extern uint8_t gm_c71idar1l1k[1] __attribute__((aligned(1)));
extern uint8_t gm_c7379qlpin9[12] __attribute__((aligned(1)));
extern uint8_t gm_c781dt962ao[16] __attribute__((aligned(1)));
extern uint8_t gm_c8ps00qu8ur[9] __attribute__((aligned(1)));
extern uint8_t gm_cas4sbdf6ub[12] __attribute__((aligned(1)));
extern uint8_t gm_cbe5loot5ou[9] __attribute__((aligned(1)));
extern uint8_t gm_cbhtvjhhm2v[39] __attribute__((aligned(1)));
extern uint8_t gm_cc8hnl5rm6d[9] __attribute__((aligned(1)));
extern uint8_t gm_cc8mmf5go3e[4] __attribute__((aligned(1)));
extern uint8_t gm_cch1hm6o00s[13] __attribute__((aligned(1)));
extern uint8_t gm_ccimdvgv2uo[5] __attribute__((aligned(1)));
extern uint8_t gm_cdb2briosl4[12] __attribute__((aligned(1)));
extern uint8_t gm_cdg9l95tj3v[10] __attribute__((aligned(1)));
extern uint8_t gm_cdll3irir7s[10] __attribute__((aligned(1)));
extern uint8_t gm_cdvl4a09e32[18] __attribute__((aligned(1)));
extern uint8_t gm_cenbs8unhg6[4] __attribute__((aligned(1)));
extern uint8_t gm_ceperl1a3fj[5] __attribute__((aligned(1)));
extern uint8_t gm_cfd1ujo4cs9[4] __attribute__((aligned(1)));
extern uint8_t gm_cfj1ctfn4pv[16] __attribute__((aligned(8)));
extern uint8_t gm_cfosr5mhlig[11] __attribute__((aligned(1)));
extern uint8_t gm_cfsqere63u7[146] __attribute__((aligned(1)));
extern uint8_t gm_cftdgoeqqrn[12] __attribute__((aligned(1)));
extern uint8_t gm_cgmt38nbmrp[17] __attribute__((aligned(1)));
extern uint8_t gm_ch06gde2041[13] __attribute__((aligned(1)));
extern uint8_t gm_ch70o9r96kg[16] __attribute__((aligned(1)));
extern uint8_t gm_ci7lmabjiao[2] __attribute__((aligned(1)));
extern uint8_t gm_cijmd8makat[10] __attribute__((aligned(1)));
extern uint8_t gm_cilec57f4ei[8] __attribute__((aligned(8)));
extern uint8_t gm_ck2s18je8ki[4] __attribute__((aligned(1)));
extern uint8_t gm_ck6o0oplinp[1] __attribute__((aligned(1)));
extern uint8_t gm_ckus03er382_2[4] __attribute__((aligned(4)));
extern uint8_t gm_clj49qvgrpn[8] __attribute__((aligned(1)));
extern uint8_t gm_cm9aodfhip7[5] __attribute__((aligned(1)));
extern uint8_t gm_cmbh1midkkg[11] __attribute__((aligned(1)));
extern uint8_t gm_cmd0i1ng0uh[16] __attribute__((aligned(1)));
extern uint8_t gm_cml63tkso1b[6] __attribute__((aligned(1)));
extern uint8_t gm_cnb3jfedmgn[3] __attribute__((aligned(1)));
extern uint8_t gm_cnlt8qov7cb[16] __attribute__((aligned(1)));
extern uint8_t gm_cp1bmt1lpnn[15] __attribute__((aligned(1)));
extern uint8_t gm_cp1l69rfj3m[11] __attribute__((aligned(1)));
extern uint8_t gm_cq9lk6ce28a[4] __attribute__((aligned(1)));
extern uint8_t gm_cr9tkd1skqs[4] __attribute__((aligned(1)));
extern uint8_t gm_crfnqbinhm6[8] __attribute__((aligned(1)));
extern uint8_t gm_crthrtmpk9i[2] __attribute__((aligned(1)));
extern uint8_t gm_ctd4c52p0f8[60] __attribute__((aligned(1)));
extern uint8_t gm_ctkboukdglo[8] __attribute__((aligned(8)));
extern uint8_t gm_cv8a63bhkjv[7] __attribute__((aligned(1)));
extern void *const gm_hdvslis4o40u[];
extern void *const gm_hl6s5gct9vkh[];
extern uint8_t gm_k28ocradvls[40] __attribute__((aligned(8)));
extern uint8_t gm_k3h72k4hfps[40] __attribute__((aligned(8)));
extern uint8_t gm_k5u3oseesd7[40] __attribute__((aligned(8)));
extern uint8_t gm_k5upcce8mpd[40] __attribute__((aligned(8)));
extern uint8_t gm_k6pllmcjbks[40] __attribute__((aligned(8)));
extern uint8_t gm_k6t8ggbfl65[40] __attribute__((aligned(8)));
extern uint8_t gm_k9o17uvn1g2[40] __attribute__((aligned(8)));
extern uint8_t gm_kbamu2ordri[40] __attribute__((aligned(8)));
extern uint8_t gm_ke9p7o91039[40] __attribute__((aligned(8)));
extern uint8_t gm_kebr2u4173v[40] __attribute__((aligned(8)));
extern uint8_t gm_kh0pci1a0en[40] __attribute__((aligned(8)));
extern uint8_t gm_km0lis6h9ua[40] __attribute__((aligned(8)));
extern uint8_t gm_ku35m9tsdej[40] __attribute__((aligned(8)));
extern uint8_t gm_ku6cmvc6apj[40] __attribute__((aligned(8)));
extern const uint8_t gm_s00odc15r3q8f[16];
extern const uint8_t gm_s00pihipe0jga[6];
extern const uint8_t gm_s0147vgtvilnq[19];
extern const uint8_t gm_s03c24qg7i06q[13];
extern const uint8_t gm_s07uh61s060o9[25];
extern const uint8_t gm_s09jc0n36v9gm[97];
extern const uint8_t gm_s09tkid4cqjmm[62];
extern const uint8_t gm_s0doambt0iqhe[4];
extern const uint8_t gm_s0ek1hibl01t2[47];
extern const uint8_t gm_s0f7epcithn0r[18];
extern const uint8_t gm_s0h9r2boh81d2[65];
extern const uint8_t gm_s0hfsnjdrghcm[9];
extern const uint8_t gm_s0jm0vg43i745[11];
extern const uint8_t gm_s0johk446liuj[28];
extern const uint8_t gm_s0kajv5j53ub7[38];
extern const uint8_t gm_s0l40gug6qpuq[17];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0nad8bs1qlvl[76];
extern const uint8_t gm_s0oocf03eil44[6];
extern const uint8_t gm_s0u9ru4tiqbe7[9];
extern const uint8_t gm_s0unahdn5j99v[14];
extern const uint8_t gm_s12khqa42i680[29];
extern const uint8_t gm_s13b13uieof5u[12];
extern const uint8_t gm_s13b5om17s13f[8];
extern const uint8_t gm_s16mf4cvajh59[13];
extern const uint8_t gm_s187rb18i1kgk[4];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1cn5ab5opg9f[131];
extern const uint8_t gm_s1crsce68cmnd[20];
extern const uint8_t gm_s1g69u68g0ra5[3];
extern const uint8_t gm_s1h3hvq26g2dl[47];
extern const uint8_t gm_s1idibqkta392[7];
extern const uint8_t gm_s1ishc3mvan41[31];
extern const uint8_t gm_s1lfsmjv8evdo[9];
extern const uint8_t gm_s1n4l05blor5r[40];
extern const uint8_t gm_s1r58jmps9mcc[7];
extern const uint8_t gm_s1spbnihet3ou[25];
extern const uint8_t gm_s1tdqlhdsnu5l[14];
extern const uint8_t gm_s1tkkenmpa3aa[13];
extern const uint8_t gm_s1ul47vja8nnc[16];
extern const uint8_t gm_s1vmtsmtbio1j[15];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s20t5qopq8n9u[5];
extern const uint8_t gm_s221m8qkvkf71[23];
extern const uint8_t gm_s23t712hkqlmp[5];
extern const uint8_t gm_s24s9g8r9gc0v[7];
extern const uint8_t gm_s27am6v0m91fv[87];
extern const uint8_t gm_s29jv7rttsnpq[52];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2d9sjnr88ud8[16];
extern const uint8_t gm_s2ma5cp41jk1a[7];
extern const uint8_t gm_s2pblv08uof55[41];
extern const uint8_t gm_s2q81sartj882[42];
extern const uint8_t gm_s2svafu2na27p[17];
extern const uint8_t gm_s2u0i5o1ej1mg[10];
extern const uint8_t gm_s2ud1phv1fk5e[15];
extern const uint8_t gm_s2uilckebopb3[12];
extern const uint8_t gm_s2umangg4hn1g[15];
extern const uint8_t gm_s2urait735ofq[5];
extern const uint8_t gm_s2v0mmbl3fojp[55];
extern const uint8_t gm_s2vmmrs8i9iqs[4];
extern const uint8_t gm_s31g9ftlb6lgj[11];
extern const uint8_t gm_s31vj4bjbj0gj[36];
extern const uint8_t gm_s34flpapqdf4s[14];
extern const uint8_t gm_s34hhp1ucrags[12];
extern const uint8_t gm_s3behnbdv0iv4[41];
extern const uint8_t gm_s3e9133b3pasn[3];
extern const uint8_t gm_s3hali1afgel3[28];
extern const uint8_t gm_s3hrsavigl7la[19];
extern const uint8_t gm_s3k4homl43m7q[74];
extern const uint8_t gm_s3kfdt7s6c0hi[15];
extern const uint8_t gm_s3ngn7me40qa4[5];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3sb6rrf98gpi[20];
extern const uint8_t gm_s3svhagp75tqj[38];
extern const uint8_t gm_s41sp7ht9l505[67];
extern const uint8_t gm_s42b9o00en8pl[34];
extern const uint8_t gm_s449uaav7f0l0[11];
extern const uint8_t gm_s460n81rl2c9c[5];
extern const uint8_t gm_s467d5cm1d5us[4];
extern const uint8_t gm_s480gcu6od53p[61];
extern const uint8_t gm_s48o8olsbqnph[12];
extern const uint8_t gm_s495moo6tjeh0[20];
extern const uint8_t gm_s4a980fahtblo[3];
extern const uint8_t gm_s4ccl84nrk27k[55];
extern const uint8_t gm_s4cdme6b7eogn[56];
extern const uint8_t gm_s4e3q3n6msi6m[6];
extern const uint8_t gm_s4hv5fsmnuq3r[15];
extern const uint8_t gm_s4ifh5kc7nc0d[62];
extern const uint8_t gm_s4inmcuim4088[46];
extern const uint8_t gm_s4j4d3l9da8i6[41];
extern const uint8_t gm_s4lj2bemc87jp[3];
extern const uint8_t gm_s4m2m0c0t74ak[46];
extern const uint8_t gm_s4m36jv5rcfat[13];
extern const uint8_t gm_s4puodol85qqo[55];
extern const uint8_t gm_s4qg6lddtahb8[4];
extern const uint8_t gm_s4qqlf0cno16q[45];
extern const uint8_t gm_s4t89div5i959[80];
extern const uint8_t gm_s4v6k193oo0cr[14];
extern const uint8_t gm_s50fbjkgs3ote[11];
extern const uint8_t gm_s54emlnl8lmoe[38];
extern const uint8_t gm_s54jmbv61vfjv[12];
extern const uint8_t gm_s557bv2sfqcom[6];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s589bsqckia3s[8];
extern const uint8_t gm_s5958kaiuh54g[84];
extern const uint8_t gm_s5cmg64f6av8k[70];
extern const uint8_t gm_s5cpmumv7vkcr[7];
extern const uint8_t gm_s5dhdddu73608[25];
extern const uint8_t gm_s5ea2jdgum6o9[33];
extern const uint8_t gm_s5epronuu4280[7];
extern const uint8_t gm_s5glpb7fbtk4c[27];
extern const uint8_t gm_s5hr2c7nkokp9[70];
extern const uint8_t gm_s5k6emuk51u47[27];
extern const uint8_t gm_s5kh0f7b7mijc[41];
extern const uint8_t gm_s5kue8bfcvqfr[27];
extern const uint8_t gm_s5kvaetfehsvi[13];
extern const uint8_t gm_s5lqhiu2bn6of[8];
extern const uint8_t gm_s5m9qimbv7e8k[3];
extern const uint8_t gm_s5n7j87m9k09e[36];
extern const uint8_t gm_s5ndb61nne1fg[58];
extern const uint8_t gm_s5ppm345dr9s1[15];
extern const uint8_t gm_s5pra70i8o734[14];
extern const uint8_t gm_s5qukklsuqpi4[17];
extern const uint8_t gm_s5rv6umk4akl0[6];
extern const uint8_t gm_s5s1m7053mh3n[8];
extern const uint8_t gm_s5s758m9u7gle[26];
extern const uint8_t gm_s5v7o1bkddp5s[7];
extern const uint8_t gm_s616b6iju29ec[5];
extern const uint8_t gm_s62an8gcfpfbj[53];
extern const uint8_t gm_s62k5o20indcj[12];
extern const uint8_t gm_s64s5a4ht3148[69];
extern const uint8_t gm_s64vef3if6vik[23];
extern const uint8_t gm_s654e61ni0n2t[67];
extern const uint8_t gm_s65k7rb52dupd[41];
extern const uint8_t gm_s65qt8qo64cje[3];
extern const uint8_t gm_s66b184memqnd[4];
extern const uint8_t gm_s66fbu3uhl900[20];
extern const uint8_t gm_s69k18vq2gid8[12];
extern const uint8_t gm_s6ab2s9s2fif9[9];
extern const uint8_t gm_s6aiq6b6gsv1o[18];
extern const uint8_t gm_s6cchf3tcs9e4[21];
extern const uint8_t gm_s6ecg17hs903q[9];
extern const uint8_t gm_s6f83og3gefc1[8];
extern const uint8_t gm_s6iln1emufss5[20];
extern const uint8_t gm_s6k5k2kcfu4ll[10];
extern const uint8_t gm_s6qvm08vrap69[4];
extern const uint8_t gm_s6r46262afe2h[7];
extern const uint8_t gm_s6sv4vhunlp6l[9];
extern const uint8_t gm_s6ths5rchgo8g[6];
extern const uint8_t gm_s6u49d04cqlg1[17];
extern const uint8_t gm_s6uegtmhpu1qn[41];
extern const uint8_t gm_s6uo8vuok1rk4[10];
extern const uint8_t gm_s70h6okja64qq[25];
extern const uint8_t gm_s70ks96e05b2g[39];
extern const uint8_t gm_s73vt0nuu7890[28];
extern const uint8_t gm_s74ohph6l9htf[21];
extern const uint8_t gm_s76hci78s6ds4[6];
extern const uint8_t gm_s77fggjckb5ln[41];
extern const uint8_t gm_s77n5hibks91d[30];
extern const uint8_t gm_s79fsf3j19oj8[39];
extern const uint8_t gm_s7c48l6el9tbd[12];
extern const uint8_t gm_s7c8ep4trp7bk[7];
extern const uint8_t gm_s7c9tkfjfc169[26];
extern const uint8_t gm_s7cu0m5qc7arr[43];
extern const uint8_t gm_s7f8crhgefl4u[28];
extern const uint8_t gm_s7fl8t4aje2e7[24];
extern const uint8_t gm_s7fto4aj9q7su[5];
extern const uint8_t gm_s7gn2sla7drif[3];
extern const uint8_t gm_s7hc5dru68qbt[15];
extern const uint8_t gm_s7i80akiq0d95[5];
extern const uint8_t gm_s7j165k7b6shl[32];
extern const uint8_t gm_s7nnecs6i9eki[41];
extern const uint8_t gm_s7pkv0u1oe9rv[102];
extern const uint8_t gm_s7qhcf46f682d[21];
extern const uint8_t gm_s7s5thcg1q6e2[26];
extern const uint8_t gm_s7t1ot7rgnuch[11];
extern const uint8_t gm_s7umjdfqes00m[52];
extern const uint8_t gm_s7uovnbe5u8va[13];
extern const uint8_t gm_s81trlqpvp2do[11];
extern const uint8_t gm_s829ksoojqn6l[41];
extern const uint8_t gm_s84hftqv0533g[5];
extern const uint8_t gm_s84p47580vnhr[12];
extern const uint8_t gm_s87e746eia9j4[6];
extern const uint8_t gm_s88lqrfvhc8i3[46];
extern const uint8_t gm_s8eg138piktj1[12];
extern const uint8_t gm_s8gkio2134kd6[70];
extern const uint8_t gm_s8hhhdlan8pqr[8];
extern const uint8_t gm_s8hv5sfsqarrt[25];
extern const uint8_t gm_s8krlr0c92m1s[15];
extern const uint8_t gm_s8o3vqolbfl8t[11];
extern const uint8_t gm_s8ok287s0vtoq[38];
extern const uint8_t gm_s8pk5tcnrk6p2[8];
extern const uint8_t gm_s8q575q0mjt4k[6];
extern const uint8_t gm_s8r83g99aqvfj[52];
extern const uint8_t gm_s8sv6qdepk5ba[4];
extern const uint8_t gm_s8sv99c8q8gbc[71];
extern const uint8_t gm_s91mn5q6gq6th[66];
extern const uint8_t gm_s959ni36b80a4[4];
extern const uint8_t gm_s967vts9f19o3[11];
extern const uint8_t gm_s985n04e0mov8[16];
extern const uint8_t gm_s9a8dkouqsfgp[9];
extern const uint8_t gm_s9hdvuc1np1st[10];
extern const uint8_t gm_s9lds9mpotrru[15];
extern const uint8_t gm_s9qu470anjl4i[8];
extern const uint8_t gm_s9r2a03bnhj06[58];
extern const uint8_t gm_s9r64t1166q1f[5];
extern const uint8_t gm_s9rujqsrrjcho[110];
extern const uint8_t gm_s9sakn2pk9s4e[19];
extern const uint8_t gm_s9tjtf1373ti5[5];
extern const uint8_t gm_s9tr38mqso4n7[93];
extern const uint8_t gm_s9uc7or8diipg[35];
extern const uint8_t gm_s9uhkcs7rcfn8[14];
extern const uint8_t gm_sa0l93d5jbk7k[69];
extern const uint8_t gm_sa16a135q4am6[13];
extern const uint8_t gm_sa525m041ir3n[17];
extern const uint8_t gm_sa70lf0p32gn5[28];
extern const uint8_t gm_sa8neoommnpov[24];
extern const uint8_t gm_sabhgcltm0thi[8];
extern const uint8_t gm_saempncqkf91r[3];
extern const uint8_t gm_sahd19pngn4vn[14];
extern const uint8_t gm_saja8vc684oud[41];
extern const uint8_t gm_salin070nim24[30];
extern const uint8_t gm_san7he7q20qtj[7];
extern const uint8_t gm_sand7bpicpp6t[26];
extern const uint8_t gm_sao9h06bhkdja[26];
extern const uint8_t gm_saoprtd6rfhf7[4];
extern const uint8_t gm_sapidpi7me2ee[95];
extern const uint8_t gm_saq0ier99o235[35];
extern const uint8_t gm_sas87vff32eaf[43];
extern const uint8_t gm_sasgv00j2p5nu[18];
extern const uint8_t gm_sauf6ibu7mnba[5];
extern const uint8_t gm_sb11lrfd85gub[14];
extern const uint8_t gm_sb2nfo787ropg[6];
extern const uint8_t gm_sb35te3dpkhv1[13];
extern const uint8_t gm_sb398tg9q30m5[6];
extern const uint8_t gm_sb3sdaa9odag6[13];
extern const uint8_t gm_sb6654l540rsv[921];
extern const uint8_t gm_sb6c4qgs6rpfi[8];
extern const uint8_t gm_sbbucsktpih4c[44];
extern const uint8_t gm_sbe9uv194cpmc[16];
extern const uint8_t gm_sbf1f7fu8bvpt[5];
extern const uint8_t gm_sbfc2b8kquvq8[9];
extern const uint8_t gm_sbg28ee7l5gjs[6];
extern const uint8_t gm_sbgcsq0m3astj[43];
extern const uint8_t gm_sbhq27eoeitmb[24];
extern const uint8_t gm_sbhui4f7fb5q5[9];
extern const uint8_t gm_sbjj41cb5lph3[60];
extern const uint8_t gm_sblo01ng5f1c2[1308];
extern const uint8_t gm_sblsu018nl4o3[3];
extern const uint8_t gm_sboedjgihdapp[81];
extern const uint8_t gm_sbpf3hgo18oq5[52];
extern const uint8_t gm_sbq9k9kvdokbf[20];
extern const uint8_t gm_sbsb3nmbfgbv2[4];
extern const uint8_t gm_sbsu8rcurukea[25];
extern const uint8_t gm_sbui4p06qp6ck[29];
extern const uint8_t gm_sbupitjaj376b[34];
extern const uint8_t gm_sc0gdocdik6sv[7];
extern const uint8_t gm_sc1ac3a85htl8[12];
extern const uint8_t gm_sc3ht2jgt5ee4[689];
extern const uint8_t gm_sc61gdfjf1pvd[38];
extern const uint8_t gm_sc7bkbe71it8c[10];
extern const uint8_t gm_sc9p2e0c29ni7[29];
extern const uint8_t gm_sccadmt684uri[23];
extern const uint8_t gm_sccm0heds95r6[60];
extern const uint8_t gm_scd599tsljaee[5];
extern const uint8_t gm_scdj734mhh9f0[17];
extern const uint8_t gm_scfppf8naer38[21];
extern const uint8_t gm_scj1kuqp0242l[6];
extern const uint8_t gm_sclhssivsh27a[37];
extern const uint8_t gm_scnj362636897[25];
extern const uint8_t gm_scold1pf1iq8n[11];
extern const uint8_t gm_scq06n33arhgv[5];
extern const uint8_t gm_scq7qb5df34b0[14];
extern const uint8_t gm_sct07pl02n02h[27];
extern const uint8_t gm_scueeuhqhtotc[30];
extern const uint8_t gm_scv8i3jrgh2rk[65];
extern const uint8_t gm_sd11ghdf4ciqp[6];
extern const uint8_t gm_sd1d50j1ea4vf[46];
extern const uint8_t gm_sd2a8ckkfb013[26];
extern const uint8_t gm_sd3a17sd7gp5u[9];
extern const uint8_t gm_sd4ibvv64si9n[45];
extern const uint8_t gm_sd4qrpv3lpilp[4];
extern const uint8_t gm_sd5fg79k33phf[37];
extern const uint8_t gm_sd5iqorsg0rc4[73];
extern const uint8_t gm_sd6d9fvngpchj[14];
extern const uint8_t gm_sddtbfibb719p[41];
extern const uint8_t gm_sdeq555foclko[3];
extern const uint8_t gm_sdfa87va44k3m[24];
extern const uint8_t gm_sdg7bdfivjinb[3];
extern const uint8_t gm_sdi44r3kug6n6[14];
extern const uint8_t gm_sdjbnp04akbbb[50];
extern const uint8_t gm_sdjk9c005ec9t[13];
extern const uint8_t gm_sdkah956ljhi9[19];
extern const uint8_t gm_sdlumbvroiufa[3];
extern const uint8_t gm_sdnbbauegomd2[7];
extern const uint8_t gm_sdnjmvrkc1r7r[12];
extern const uint8_t gm_sds1asc07m30k[16];
extern const uint8_t gm_sdslahffv7339[17];
extern const uint8_t gm_se0plntg0il3g[14];
extern const uint8_t gm_se19nf1421e37[4];
extern const uint8_t gm_se64as7gfqfsc[36];
extern const uint8_t gm_se7gs48u9l3d7[10];
extern const uint8_t gm_se8cpd8s09lkc[13];
extern const uint8_t gm_se8v3b7tfvt0e[23];
extern const uint8_t gm_seak3hcpfjfp2[70];
extern const uint8_t gm_sefm4vafv1a3l[3];
extern const uint8_t gm_sefrps06g7jfu[11];
extern const uint8_t gm_seg9l1usqbs3j[4];
extern const uint8_t gm_segcvuj6bu7e4[17];
extern const uint8_t gm_seje0bncv3tch[17];
extern const uint8_t gm_sem2htac3j4np[66];
extern const uint8_t gm_seor1k5gntiuc[52];
extern const uint8_t gm_sepo1h9bofnod[30];
extern const uint8_t gm_setv7ejg35cg9[43];
extern const uint8_t gm_sevfgu4cub43a[41];
extern const uint8_t gm_sf0f3lvsd76jf[17];
extern const uint8_t gm_sf0p9o7g7fr6l[7];
extern const uint8_t gm_sf4qi8ut6u3p1[5];
extern const uint8_t gm_sf5brl3u7mr5h[7];
extern const uint8_t gm_sf7l7ca9coehi[85];
extern const uint8_t gm_sf9regh1oaao3[14];
extern const uint8_t gm_sfd9hctnph9ef[12];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfdq8ss0dnpe2[8];
extern const uint8_t gm_sfg8mkf2826qa[5];
extern const uint8_t gm_sfj8otp2pq7qq[55];
extern const uint8_t gm_sfku76mf5fn5d[9];
extern const uint8_t gm_sfm6vc4bhnr4s[44];
extern const uint8_t gm_sfm739ta4hkl2[6];
extern const uint8_t gm_sfm8ps2e26dh1[3];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfpfdag7t7v0f[16];
extern const uint8_t gm_sfpopfqbubj89[35];
extern const uint8_t gm_sfrku7jk647q0[83];
extern const uint8_t gm_sfrrtib86oiig[23];
extern const uint8_t gm_sfs5rrntfu3mi[25];
extern const uint8_t gm_sfv61btcutqd4[21];
extern const uint8_t gm_sg0997vvtiund[65];
extern const uint8_t gm_sg4nlara1m052[14];
extern const uint8_t gm_sg4u1qvcufkum[5];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg5h7btla5ksa[29];
extern const uint8_t gm_sg5o5gf2u71g7[9];
extern const uint8_t gm_sg5uvbf14q54s[21];
extern const uint8_t gm_sg71mqv9mtu78[33];
extern const uint8_t gm_sg7920c5gghpv[13];
extern const uint8_t gm_sg9102k54psou[16];
extern const uint8_t gm_sg9g5p2e4brnh[62];
extern const uint8_t gm_sgb12qbh3gevi[5];
extern const uint8_t gm_sgb7sja94ppju[26];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_sgcporqt2gqqg[16];
extern const uint8_t gm_sgdrhdkbtd5gr[19];
extern const uint8_t gm_sgdvb6gobbii2[8];
extern const uint8_t gm_sgh0dvu15b9ta[10];
extern const uint8_t gm_sghk06svnbp7m[40];
extern const uint8_t gm_sgls2f653rbea[17];
extern const uint8_t gm_sglu5epjv38rl[92];
extern const uint8_t gm_sgqcrkl2nh339[4];
extern const uint8_t gm_sgr5hvs6tocci[12];
extern const uint8_t gm_sgs4qi36v3o5v[8];
extern const uint8_t gm_sgs96ai7rj93j[17];
extern const uint8_t gm_sgvvkov5kvprt[10];
extern const uint8_t gm_sh08f8dq9lenu[41];
extern const uint8_t gm_sh40na9qbga3l[2];
extern const uint8_t gm_sh5k7nstldaeg[7];
extern const uint8_t gm_sh8t790g5lerb[12];
extern const uint8_t gm_sh9ju7u678u0l[28];
extern const uint8_t gm_shcu73rsj445k[13];
extern const uint8_t gm_shes0dtqt9sqq[28];
extern const uint8_t gm_shh35vtq9t5v9[17];
extern const uint8_t gm_shi7nidbc64qf[4];
extern const uint8_t gm_shido3ioit5oe[10];
extern const uint8_t gm_shjfpng3ei3du[82];
extern const uint8_t gm_shl0ucmfrvk77[54];
extern const uint8_t gm_shofa8pdml9fb[13];
extern const uint8_t gm_shrelr880pj0m[7];
extern const uint8_t gm_shrtuk3jka1au[16];
extern const uint8_t gm_shsqur3mdh4ln[41];
extern const uint8_t gm_shtiokhdiebr1[3];
extern const uint8_t gm_shtvnkmjp804q[23];
extern const uint8_t gm_shudlfi0ri4tb[41];
extern const uint8_t gm_si00d7fs7b5aq[10];
extern const uint8_t gm_si0567rndd8dl[17];
extern const uint8_t gm_si2s3gai2c29i[16];
extern const uint8_t gm_si33msf4t0sbn[8];
extern const uint8_t gm_si3f75dnj7pb9[26];
extern const uint8_t gm_si430gujtnl9u[11];
extern const uint8_t gm_si82ti7ep94be[11];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sicq1bvp13t5d[23];
extern const uint8_t gm_sifq8gd3d748j[36];
extern const uint8_t gm_sigr245qbgr4o[14];
extern const uint8_t gm_sihsae37m31pr[15];
extern const uint8_t gm_siiqktevj2s2h[25];
extern const uint8_t gm_sijd693t5ccnb[4];
extern const uint8_t gm_sim03rp8pjg4e[44];
extern const uint8_t gm_sinmpfqtbo9in[5];
extern const uint8_t gm_sinogveu00g7m[36];
extern const uint8_t gm_sipf690cm2c6f[41];
extern const uint8_t gm_siqa65grpcsk9[99];
extern const uint8_t gm_siqsngf0mfo86[9];
extern const uint8_t gm_siscc5ella8ma[4];
extern const uint8_t gm_sitbu884cbr46[96];
extern const uint8_t gm_siun1n8hqooca[9];
extern const uint8_t gm_sj1gcl0qo4i9p[7];
extern const uint8_t gm_sj2ngf6ro6oja[65];
extern const uint8_t gm_sj4nhb5mobdan[37];
extern const uint8_t gm_sj62oio2kg58e[20];
extern const uint8_t gm_sj6enltl2qi3c[21];
extern const uint8_t gm_sj7gfkb5eknah[76];
extern const uint8_t gm_sj7kfef17i9q2[9];
extern const uint8_t gm_sj85nechopaet[54];
extern const uint8_t gm_sj8b32e846r90[11];
extern const uint8_t gm_sj9t4d2ku5m56[47];
extern const uint8_t gm_sjbtfuea8qauf[23];
extern const uint8_t gm_sjcm18p35o9rq[4];
extern const uint8_t gm_sjdbk1ada1ddm[8];
extern const uint8_t gm_sjdv8icqpfhc5[6];
extern const uint8_t gm_sjeh66tmmrhop[3];
extern const uint8_t gm_sjehit11lds9h[34];
extern const uint8_t gm_sjfg2291kjugh[4];
extern const uint8_t gm_sjfqm9gvu1om9[8];
extern const uint8_t gm_sjfrmur8rvp24[34];
extern const uint8_t gm_sjiaitbr3bp01[10];
extern const uint8_t gm_sjibktbbpgpq2[17];
extern const uint8_t gm_sjkqdj35ksd9p[3];
extern const uint8_t gm_sjmhgc2b6rnks[10];
extern const uint8_t gm_sjn3oh87mil2m[333];
extern const uint8_t gm_sjngmi12bbcu5[30];
extern const uint8_t gm_sjnkguo0atpm5[3];
extern const uint8_t gm_sjnv3p8t49clu[9];
extern const uint8_t gm_sjpvc4ek9dber[39];
extern const uint8_t gm_sjq0dshsov83m[20];
extern const uint8_t gm_sk04qg20jmac0[4];
extern const uint8_t gm_sk0bnuggr6sri[14];
extern const uint8_t gm_sk175hsmu7b99[22];
extern const uint8_t gm_sk1ojn82dahdn[13];
extern const uint8_t gm_sk2jb5rv3689k[34];
extern const uint8_t gm_sk5asgkllekl2[14];
extern const uint8_t gm_sk8fkllqjst4j[13];
extern const uint8_t gm_ska23dbjvthl3[15];
extern const uint8_t gm_ske882p1cpf5s[10];
extern const uint8_t gm_skfdo9qggtrbp[6];
extern const uint8_t gm_skg1c046vhqs5[45];
extern const uint8_t gm_skj1a463v050q[79];
extern const uint8_t gm_skjhai33494u2[9];
extern const uint8_t gm_sko4cgkdijve3[57];
extern const uint8_t gm_sks4g7rc6a1af[16];
extern const uint8_t gm_sl2lo3hki3t4d[36];
extern const uint8_t gm_sl34pggfk1a23[38];
extern const uint8_t gm_sl3acq1nje8kk[39];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl4ns6hobicht[6];
extern const uint8_t gm_sl55obh2ok05j[4];
extern const uint8_t gm_sl7doakbaoimv[7];
extern const uint8_t gm_sl7p05f6l6cga[10];
extern const uint8_t gm_sl7pe4skltkbu[9];
extern const uint8_t gm_sl8fmedd9kk64[34];
extern const uint8_t gm_slasd9g26mfnt[12];
extern const uint8_t gm_slbmfdf2lr7u3[15];
extern const uint8_t gm_slekeb8da8in6[5];
extern const uint8_t gm_sletp8nk609ee[17];
extern const uint8_t gm_slf2kl43ogurh[17];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slh6d6uosdbkg[35];
extern const uint8_t gm_slikoo8r5oupm[18];
extern const uint8_t gm_sllgtfi45c44a[8];
extern const uint8_t gm_sllrmadmbrcvv[54];
extern const uint8_t gm_slmas8vbhthkn[20];
extern const uint8_t gm_slmeqnp27emg7[9];
extern const uint8_t gm_slmqf5nrs9f6b[13];
extern const uint8_t gm_slnqcu8l15gll[12];
extern const uint8_t gm_slpekm89q5lb3[29];
extern const uint8_t gm_slphk0n858mv9[13];
extern const uint8_t gm_slqtt99r493q9[102];
extern const uint8_t gm_slrd70lo3b6iq[15];
extern const uint8_t gm_slskjtl6alfrd[6];
extern const uint8_t gm_sm06nsg0msdgd[9];
extern const uint8_t gm_sm3fgo8acha61[13];
extern const uint8_t gm_sm3sqa0e3amks[9];
extern const uint8_t gm_sm4ujt9jgj9au[8];
extern const uint8_t gm_sm55k23067j17[16];
extern const uint8_t gm_sm81b0bm9nrdl[30];
extern const uint8_t gm_sm9iv4f7cjs9t[62];
extern const uint8_t gm_smbgfcmcr0r8n[11];
extern const uint8_t gm_smdggivmb7eg1[41];
extern const uint8_t gm_smeu8adf6pdko[38];
extern const uint8_t gm_smfprcpea3ibe[10];
extern const uint8_t gm_smgdg1m3kad32[74];
extern const uint8_t gm_smglbbtmdmip2[6];
extern const uint8_t gm_smhli04hme60d[30];
extern const uint8_t gm_smhs805pum0dn[26];
extern const uint8_t gm_smi7k9v6u7cmo[11];
extern const uint8_t gm_smk33hl0qg9hs[4];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smovt8m5cpa13[7];
extern const uint8_t gm_smq842qg8d7u6[119];
extern const uint8_t gm_smr2fm6cfetj8[5];
extern const uint8_t gm_smraeij5q6ubc[11];
extern const uint8_t gm_smrahl8ppkcu9[34];
extern const uint8_t gm_smrba58613c3b[17];
extern const uint8_t gm_smsrao7h22muv[61];
extern const uint8_t gm_smuti066jdthl[24];
extern const uint8_t gm_smvgcg1j21t6v[27];
extern const uint8_t gm_sn449k8j7bj3v[51];
extern const uint8_t gm_sn88c2jg5b9gv[4];
extern const uint8_t gm_snb8htg7a7p5u[16];
extern const uint8_t gm_snbihmvq8hfip[29];
extern const uint8_t gm_snbn61h56c5fj[52];
extern const uint8_t gm_snc8fbp1a7m98[31];
extern const uint8_t gm_snf4l98ljss2u[89];
extern const uint8_t gm_snftqbg249ljm[41];
extern const uint8_t gm_sngdu4l8vh3l8[5];
extern const uint8_t gm_snh86nvrv0us3[19];
extern const uint8_t gm_snhmu20ccir10[16];
extern const uint8_t gm_sni9iu6vnvsjk[44];
extern const uint8_t gm_snlpcspgan3ri[16];
extern const uint8_t gm_sno2v4rqn9iav[61];
extern const uint8_t gm_snokcvhtidt97[6];
extern const uint8_t gm_snr5o5tak6405[4];
extern const uint8_t gm_sns1hl83hdlcn[5];
extern const uint8_t gm_snset5tb694as[15];
extern const uint8_t gm_snsu3jrv1dbtj[32];
extern const uint8_t gm_so3bl4q7kp6gl[21];
extern const uint8_t gm_so3t10be8g68d[10];
extern const uint8_t gm_so4dfspvuibt1[11];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so77ks9ivg559[9];
extern const uint8_t gm_so78814tnt5g5[2];
extern const uint8_t gm_so7em1q8clnib[8];
extern const uint8_t gm_so9fi6r28770a[3];
extern const uint8_t gm_sod9r7r024h9q[6];
extern const uint8_t gm_soefp7bosmvt1[12];
extern const uint8_t gm_son8q7pl3pv8g[72];
extern const uint8_t gm_soo0b43e4t7sn[12];
extern const uint8_t gm_sopacnadlsnva[99];
extern const uint8_t gm_sosa5l0k6d34c[15];
extern const uint8_t gm_sou5f0qr4vdgr[8];
extern const uint8_t gm_sovn1qud9dmh0[70];
extern const uint8_t gm_sovqp4emsh0q8[5];
extern const uint8_t gm_sp02qqgucmpt7[3];
extern const uint8_t gm_sp0kv61jd1btm[5];
extern const uint8_t gm_sp598n426d61b[40];
extern const uint8_t gm_sp6l5tr58ip76[23];
extern const uint8_t gm_sp9i9tubgmd3s[21];
extern const uint8_t gm_spajg3q27hr58[15];
extern const uint8_t gm_spbhhmdbp335u[4];
extern const uint8_t gm_spchsf3taendr[3];
extern const uint8_t gm_spdkbvjffkg4b[11];
extern const uint8_t gm_speaekk0qc740[9];
extern const uint8_t gm_spfe58u39phsl[21];
extern const uint8_t gm_sph476jbvrpt5[41];
extern const uint8_t gm_spm33surstj1l[8];
extern const uint8_t gm_spnr4ftjbm9m9[61];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sppi971hm2k0k[15];
extern const uint8_t gm_sps1dthki4jpu[50];
extern const uint8_t gm_spsjkc9sqdvl6[61];
extern const uint8_t gm_spts1f8sg22km[18];
extern const uint8_t gm_spu32ed945op3[49];
extern const uint8_t gm_sq02q1bjdgrej[123];
extern const uint8_t gm_sq0573h7j28rq[23];
extern const uint8_t gm_sq05ai6ts8qlk[17];
extern const uint8_t gm_sq0bes3qstkub[15];
extern const uint8_t gm_sq12h87ngj5nl[9];
extern const uint8_t gm_sq9f472jpmh59[64];
extern const uint8_t gm_sq9f8k00ocil6[35];
extern const uint8_t gm_sq9pusqm0kq2f[7];
extern const uint8_t gm_sqa5rf17c57e4[5];
extern const uint8_t gm_sqahda31evi4f[9];
extern const uint8_t gm_sqaji2d91cvct[19];
extern const uint8_t gm_sqc0827akcmi8[40];
extern const uint8_t gm_sqcjuf1r2epl2[7];
extern const uint8_t gm_sqfe8a5cd6b60[7];
extern const uint8_t gm_sqhghq789nvm0[8];
extern const uint8_t gm_sqj9769nr8pm0[48];
extern const uint8_t gm_sqjh8kseu4lti[6];
extern const uint8_t gm_sqjlosotosjfp[18];
extern const uint8_t gm_sqjomq37io03j[151];
extern const uint8_t gm_sql0in48r61nd[5];
extern const uint8_t gm_sqlorrkumh3an[23];
extern const uint8_t gm_sqmqlaupa64jq[7];
extern const uint8_t gm_sqpitork4qoam[21];
extern const uint8_t gm_sqqaphhmqv39s[5];
extern const uint8_t gm_sqslcbfraugjg[8];
extern const uint8_t gm_squv1ktk9abst[41];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sqvgecd0v5l54[208];
extern const uint8_t gm_sr00p3fdg5qn3[19];
extern const uint8_t gm_sr04t1tcf6jsd[6];
extern const uint8_t gm_sr1bm2v312fv6[15];
extern const uint8_t gm_sr43s52rmecnf[33];
extern const uint8_t gm_sr462lclo52fq[8];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr4utq83odb88[8];
extern const uint8_t gm_sr87tgv3ltq65[13];
extern const uint8_t gm_srav7444q331a[37];
extern const uint8_t gm_srb1158jlk9u7[75];
extern const char gm_src8kcterbt0p7b[];
extern const char gm_src9gqs06mvt4n2[];
extern const char gm_srce9tqfqu9u3el[];
extern const char gm_srcg0ns322676oc[];
extern const char gm_srcgs96ai7rj93j[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcoj9bno4fi1q4[];
extern const char gm_srcp2vt6rk8827h[];
extern const char gm_srcq0uibesf3n7k[];
extern const char gm_srcrvqeupiu1766[];
extern const uint8_t gm_sreop6h89ulb0[18];
extern const uint8_t gm_srfagufvviad7[6];
extern const uint8_t gm_srhms7dhd79bq[15];
extern const uint8_t gm_srjdrn9cldsu5[57];
extern const uint8_t gm_srnn7edb2gm48[42];
extern const uint8_t gm_srq72vm6d96b6[41];
extern const uint8_t gm_sru9l3qtrdjko[84];
extern const uint8_t gm_srv05plrkjhf8[7];
extern const uint8_t gm_srv0b9jht81lt[18];
extern const uint8_t gm_srv4dcfccuset[3];
extern const uint8_t gm_ss1ntuh8relgg[19];
extern const uint8_t gm_ss3kah49g222u[33];
extern const uint8_t gm_ss3op0i298nem[69];
extern const uint8_t gm_ss5c184rv03vi[18];
extern const uint8_t gm_ss7moisvq3l8h[6];
extern const uint8_t gm_ss9fj8tanu5jn[51];
extern const uint8_t gm_ssd7h3tuipgq5[55];
extern const uint8_t gm_sse5id8rmv069[8];
extern const uint8_t gm_ssi8bk0pvfr56[22];
extern const uint8_t gm_ssj1a8k9nspq4[82];
extern const uint8_t gm_ssmeaumlrdi7h[7];
extern const uint8_t gm_ssnb4or831s5l[78];
extern const uint8_t gm_ssnbkpbck3vbo[22];
extern const uint8_t gm_ssob5j6sas4h7[58];
extern const uint8_t gm_sst2khrvr4nus[10];
extern const uint8_t gm_ssu25mmrd1p46[40];
extern const uint8_t gm_ssuuud2f0uns5[9];
extern const uint8_t gm_st2s0ov9emddj[12];
extern const uint8_t gm_st2vlvkf674c2[250];
extern const uint8_t gm_st35tcurhvabs[31];
extern const uint8_t gm_st518h9bgt9n7[22];
extern const uint8_t gm_st7d4oeklkoma[11];
extern const uint8_t gm_stbjrsoqc456r[12];
extern const uint8_t gm_stc3e0j0moc2h[5];
extern const uint8_t gm_stco696vp9tto[14];
extern const uint8_t gm_std2slsc0ssrl[11];
extern const uint8_t gm_std59a9b6lpnn[10];
extern const uint8_t gm_stdefoi5sk91c[12];
extern const uint8_t gm_stfhv28imtfkb[6];
extern const uint8_t gm_stjlu12rqiie7[13];
extern const uint8_t gm_stk678hojgu6q[24];
extern const uint8_t gm_stkc6efbvodm1[14];
extern const uint8_t gm_stkh9r1ip5q2v[35];
extern const uint8_t gm_stkqau5o4elnq[15];
extern const uint8_t gm_stmdki8kna56a[24];
extern const uint8_t gm_stmsadvf54k73[3];
extern const uint8_t gm_storp3otq7orh[59];
extern const uint8_t gm_stpdu6olnph3l[33];
extern const uint8_t gm_stpnhgp46l3tu[9];
extern const uint8_t gm_strsld72u30ds[9];
extern const uint8_t gm_stuutd7nfq21h[3];
extern const uint8_t gm_su01gdacg3ppp[9];
extern const uint8_t gm_su1mil1q3eqgp[16];
extern const uint8_t gm_su61qr04nq335[13];
extern const uint8_t gm_su635cbqj0rpd[3];
extern const uint8_t gm_su6s568b7b4kr[5];
extern const uint8_t gm_sua4hqf3te7mn[15];
extern const uint8_t gm_sub1g4k5l4c07[11];
extern const uint8_t gm_subcm6amchfvv[5];
extern const uint8_t gm_sucs0qmdapqe8[41];
extern const uint8_t gm_sud4o49sgtb17[28];
extern const uint8_t gm_suevj69t71tjc[35];
extern const uint8_t gm_suidg89j7tg2d[5];
extern const uint8_t gm_sujpen8u5kmon[10];
extern const uint8_t gm_suk7nc9hc39vu[13];
extern const uint8_t gm_sumvvvaqn43sl[10];
extern const uint8_t gm_sup2jn5hml593[6];
extern const uint8_t gm_suuphrufuvh4m[12];
extern const uint8_t gm_sv0o0mfb8qq08[83];
extern const uint8_t gm_sv0pi8lprf2k8[55];
extern const uint8_t gm_sv5o4caljvbmj[28];
extern const uint8_t gm_sv5vs7371a92i[18];
extern const uint8_t gm_svaibtfuu0gf2[48];
extern const uint8_t gm_svfa039r0e6te[33];
extern const uint8_t gm_svgn73l5s66lk[7];
extern const uint8_t gm_svhavo13qha5k[4];
extern const uint8_t gm_svkk03nujpn4k[8];
extern const uint8_t gm_svkkls864r4t4[30];
extern const uint8_t gm_svklio0qebpog[9];
extern const uint8_t gm_svkrg5dctlh9m[7];
extern const uint8_t gm_svl4l2v56ma30[8];
extern const uint8_t gm_svln5ui59rjil[6];
extern const uint8_t gm_svmck4pckkpai[41];
extern const uint8_t gm_svmo2cukmep6f[9];
extern const uint8_t gm_svom2l7bom2pm[71];
extern const uint8_t gm_svp6nj851nh2i[29];
extern const uint8_t gm_svsf0pu3t8u6m[7];
extern const uint8_t gm_svsiu4jgdp97c[12];
extern const uint8_t gm_svt8aldhoetro[3];
extern const uint8_t gm_svtut88rj5dr8[15];
extern const uint8_t gm_svu2guaq187mi[9];
extern const uint32_t gm_tpl00cra9g0k45s_i[];
extern const gm_tmpl_piece gm_tpl00nmcu166crh_p[];
extern const gm_tmpl gm_tpl051t251mssfu;
extern const uint32_t gm_tpl051t251mssfu_i[];
extern const gm_tmpl_piece gm_tpl051t251mssfu_p[];
extern const gm_spec *const gm_tpl051t251mssfu_s[];
extern const gm_spec *const gm_tpl05osb04qe3ad_s[];
extern const gm_tmpl gm_tpl083ga8282tol;
extern const uint32_t gm_tpl083ga8282tol_i[];
extern const gm_tmpl_piece gm_tpl083ga8282tol_p[];
extern const gm_spec *const gm_tpl083ga8282tol_s[];
extern const gm_spec *const gm_tpl0aluqdlmv333_s[];
extern const gm_spec *const gm_tpl0cgce8migcnp_s[];
extern const gm_tmpl gm_tpl0e4rd6ag9tqa;
extern const uint32_t gm_tpl0e4rd6ag9tqa_i[];
extern const gm_tmpl_piece gm_tpl0e4rd6ag9tqa_p[];
extern const gm_spec *const gm_tpl0e4rd6ag9tqa_s[];
extern const uint32_t gm_tpl0e782j7cr191_i[];
extern const gm_tmpl gm_tpl0ej2jrvho6u0;
extern const uint32_t gm_tpl0ej2jrvho6u0_i[];
extern const gm_tmpl_piece gm_tpl0ej2jrvho6u0_p[];
extern const gm_spec *const gm_tpl0ej2jrvho6u0_s[];
extern const gm_tmpl gm_tpl0epjr4745p10;
extern const uint32_t gm_tpl0epjr4745p10_i[];
extern const gm_tmpl_piece gm_tpl0epjr4745p10_p[];
extern const gm_spec *const gm_tpl0epjr4745p10_s[];
extern const gm_spec *const gm_tpl0gp8c85b2lf5_s[];
extern const gm_spec *const gm_tpl0j5i33et5u62_s[];
extern const gm_tmpl_piece gm_tpl0lbbjeb2vhti_p[];
extern const uint32_t gm_tpl0lggla435q5r_i[];
extern const gm_tmpl_piece gm_tpl0o9c14r51hfo_p[];
extern const gm_tmpl_piece gm_tpl0qht8d5me9ih_p[];
extern const uint32_t gm_tpl0qlvf3bfgtip_i[];
extern const gm_spec *const gm_tpl0rhldcdavmcg_s[];
extern const uint32_t gm_tpl0sb47rg6hcij_i[];
extern const gm_tmpl_piece gm_tpl0stjtt2gdn50_p[];
extern const gm_tmpl gm_tpl0uott3s3afvp;
extern const uint32_t gm_tpl0uott3s3afvp_i[];
extern const gm_tmpl_piece gm_tpl0uott3s3afvp_p[];
extern const gm_spec *const gm_tpl0uott3s3afvp_s[];
extern const gm_tmpl_piece gm_tpl12e8u6hvkejb_p[];
extern const gm_tmpl_piece gm_tpl13c887f2o944_p[];
extern const uint32_t gm_tpl149k5asebpko_i[];
extern const gm_tmpl gm_tpl1a3usdkusvtq;
extern const uint32_t gm_tpl1a3usdkusvtq_i[];
extern const gm_tmpl_piece gm_tpl1a3usdkusvtq_p[];
extern const gm_spec *const gm_tpl1a3usdkusvtq_s[];
extern const gm_tmpl gm_tpl1c0b1s109fa4;
extern const uint32_t gm_tpl1c0b1s109fa4_i[];
extern const gm_tmpl_piece gm_tpl1c0b1s109fa4_p[];
extern const gm_spec *const gm_tpl1c0b1s109fa4_s[];
extern const uint32_t gm_tpl1dleoebfmvhp_i[];
extern const uint32_t gm_tpl1f5t4gadpj5n_i[];
extern const uint32_t gm_tpl1g1lh0kp24oa_i[];
extern const gm_tmpl_piece gm_tpl1hek9uhvdtmo_p[];
extern const uint32_t gm_tpl1lj3ah7qeq42_i[];
extern const gm_tmpl gm_tpl1q6o13b940dg;
extern const uint32_t gm_tpl1q6o13b940dg_i[];
extern const gm_tmpl_piece gm_tpl1q6o13b940dg_p[];
extern const gm_spec *const gm_tpl1q6o13b940dg_s[];
extern const gm_tmpl gm_tpl20t94jlvdndv;
extern const uint32_t gm_tpl20t94jlvdndv_i[];
extern const gm_tmpl_piece gm_tpl20t94jlvdndv_p[];
extern const gm_spec *const gm_tpl20t94jlvdndv_s[];
extern const gm_tmpl gm_tpl21i1s6vvb3m4;
extern const uint32_t gm_tpl21i1s6vvb3m4_i[];
extern const gm_tmpl_piece gm_tpl21i1s6vvb3m4_p[];
extern const gm_spec *const gm_tpl21i1s6vvb3m4_s[];
extern const uint32_t gm_tpl24fci6dm83eo_i[];
extern const gm_spec *const gm_tpl2aca96qot4p8_s[];
extern const uint32_t gm_tpl2aofg9dsfuds_i[];
extern const gm_tmpl_piece gm_tpl2b904kc90480_p[];
extern const gm_tmpl_piece gm_tpl2cg0ghfdkfbm_p[];
extern const gm_tmpl_piece gm_tpl2egb012obd94_p[];
extern const gm_spec *const gm_tpl2fb8qlpeqlet_s[];
extern const gm_spec *const gm_tpl2fcbm03ka11j_s[];
extern const gm_tmpl gm_tpl2fh423sp4pd0;
extern const uint32_t gm_tpl2fh423sp4pd0_i[];
extern const gm_tmpl_piece gm_tpl2fh423sp4pd0_p[];
extern const gm_spec *const gm_tpl2fh423sp4pd0_s[];
extern const gm_tmpl gm_tpl2kgmgv2ueqo4;
extern const uint32_t gm_tpl2kgmgv2ueqo4_i[];
extern const gm_tmpl_piece gm_tpl2kgmgv2ueqo4_p[];
extern const gm_spec *const gm_tpl2kgmgv2ueqo4_s[];
extern const uint32_t gm_tpl2re1r0fre48m_i[];
extern const gm_spec *const gm_tpl2s7fpgand9gc_s[];
extern const gm_tmpl_piece gm_tpl2u97uspakqd9_p[];
extern const gm_tmpl gm_tpl3052vn0bjbcl;
extern const uint32_t gm_tpl3052vn0bjbcl_i[];
extern const gm_tmpl_piece gm_tpl3052vn0bjbcl_p[];
extern const gm_spec *const gm_tpl3052vn0bjbcl_s[];
extern const gm_spec *const gm_tpl328oas8hh4jg_s[];
extern const uint32_t gm_tpl335dut97l9dl_i[];
extern const uint32_t gm_tpl352s0p85j412_i[];
extern const gm_tmpl_piece gm_tpl36bd3ievounm_p[];
extern const gm_spec *const gm_tpl36bd3ievounm_s[];
extern const gm_tmpl gm_tpl3c9lssf4k186;
extern const uint32_t gm_tpl3c9lssf4k186_i[];
extern const gm_tmpl_piece gm_tpl3c9lssf4k186_p[];
extern const gm_spec *const gm_tpl3c9lssf4k186_s[];
extern const gm_tmpl gm_tpl3dghmndqn708;
extern const uint32_t gm_tpl3dghmndqn708_i[];
extern const gm_tmpl_piece gm_tpl3dghmndqn708_p[];
extern const gm_spec *const gm_tpl3dghmndqn708_s[];
extern const gm_spec *const gm_tpl3kt8q9c3ih5v_s[];
extern const gm_tmpl gm_tpl3l3u367o9nte;
extern const uint32_t gm_tpl3l3u367o9nte_i[];
extern const gm_tmpl_piece gm_tpl3l3u367o9nte_p[];
extern const gm_spec *const gm_tpl3l3u367o9nte_s[];
extern const gm_spec *const gm_tpl3pvc6fn0aqps_s[];
extern const gm_tmpl_piece gm_tpl3skngiao4gkl_p[];
extern const gm_spec *const gm_tpl3skngiao4gkl_s[];
extern const gm_tmpl gm_tpl3sv6aqbksrr5;
extern const uint32_t gm_tpl3sv6aqbksrr5_i[];
extern const gm_tmpl_piece gm_tpl3sv6aqbksrr5_p[];
extern const gm_spec *const gm_tpl3sv6aqbksrr5_s[];
extern const gm_spec *const gm_tpl409di1gnsfit_s[];
extern const uint32_t gm_tpl43224j8old0s_i[];
extern const gm_spec *const gm_tpl44b660pgg1c8_s[];
extern const gm_spec *const gm_tpl46u099ch4aa2_s[];
extern const gm_tmpl_piece gm_tpl48fg75b976dc_p[];
extern const gm_spec *const gm_tpl4bj6lgqq92ev_s[];
extern const uint32_t gm_tpl4clb07vpsm45_i[];
extern const gm_tmpl gm_tpl4g1h0tr3apfi;
extern const uint32_t gm_tpl4g1h0tr3apfi_i[];
extern const gm_tmpl_piece gm_tpl4g1h0tr3apfi_p[];
extern const gm_spec *const gm_tpl4g1h0tr3apfi_s[];
extern const gm_tmpl gm_tpl4ht4dp62es1n;
extern const uint32_t gm_tpl4ht4dp62es1n_i[];
extern const gm_tmpl_piece gm_tpl4ht4dp62es1n_p[];
extern const gm_spec *const gm_tpl4ht4dp62es1n_s[];
extern const gm_tmpl gm_tpl4lt7abhi3ej4;
extern const uint32_t gm_tpl4lt7abhi3ej4_i[];
extern const gm_tmpl_piece gm_tpl4lt7abhi3ej4_p[];
extern const gm_spec *const gm_tpl4lt7abhi3ej4_s[];
extern const gm_tmpl gm_tpl4n3cnfr6vo96;
extern const uint32_t gm_tpl4n3cnfr6vo96_i[];
extern const gm_tmpl_piece gm_tpl4n3cnfr6vo96_p[];
extern const gm_spec *const gm_tpl4n3cnfr6vo96_s[];
extern const uint32_t gm_tpl4phv0vr9as7f_i[];
extern const gm_spec *const gm_tpl4t11fkqh738f_s[];
extern const gm_tmpl_piece gm_tpl570sljg0ivt5_p[];
extern const gm_tmpl gm_tpl578lpoev0s9s;
extern const uint32_t gm_tpl578lpoev0s9s_i[];
extern const gm_tmpl_piece gm_tpl578lpoev0s9s_p[];
extern const gm_spec *const gm_tpl578lpoev0s9s_s[];
extern const uint32_t gm_tpl59lqltdr0h0r_i[];
extern const gm_tmpl_piece gm_tpl5c6n9e2iidjc_p[];
extern const gm_tmpl gm_tpl5i8vi7ph4hfi;
extern const uint32_t gm_tpl5i8vi7ph4hfi_i[];
extern const gm_tmpl_piece gm_tpl5i8vi7ph4hfi_p[];
extern const gm_spec *const gm_tpl5i8vi7ph4hfi_s[];
extern const uint32_t gm_tpl5igv3ckbutrp_i[];
extern const gm_tmpl_piece gm_tpl5igv3ckbutrp_p[];
extern const uint32_t gm_tpl5kl598jq5h12_i[];
extern const gm_tmpl_piece gm_tpl5m0ku1mi16i0_p[];
extern const gm_tmpl gm_tpl5m2t8us2v800;
extern const uint32_t gm_tpl5m2t8us2v800_i[];
extern const gm_tmpl_piece gm_tpl5m2t8us2v800_p[];
extern const gm_spec *const gm_tpl5m2t8us2v800_s[];
extern const gm_tmpl gm_tpl5n5ddjmjlhjb;
extern const uint32_t gm_tpl5n5ddjmjlhjb_i[];
extern const gm_tmpl_piece gm_tpl5n5ddjmjlhjb_p[];
extern const gm_spec *const gm_tpl5n5ddjmjlhjb_s[];
extern const gm_tmpl_piece gm_tpl5q4rr3nd9637_p[];
extern const uint32_t gm_tpl5rd4283boomi_i[];
extern const gm_tmpl gm_tpl5rdj46lif6eq;
extern const uint32_t gm_tpl5rdj46lif6eq_i[];
extern const gm_tmpl_piece gm_tpl5rdj46lif6eq_p[];
extern const gm_spec *const gm_tpl5rdj46lif6eq_s[];
extern const gm_tmpl_piece gm_tpl5s6q12qjusi2_p[];
extern const gm_tmpl_piece gm_tpl5st0uuf0quab_p[];
extern const uint32_t gm_tpl60bi48lvp2qn_i[];
extern const gm_tmpl_piece gm_tpl65nnuqtjd24o_p[];
extern const gm_tmpl_piece gm_tpl6gc9e2atg845_p[];
extern const gm_spec *const gm_tpl6lpvgmmqlalm_s[];
extern const gm_tmpl gm_tpl6m8ee83kb2vt;
extern const uint32_t gm_tpl6m8ee83kb2vt_i[];
extern const gm_tmpl_piece gm_tpl6m8ee83kb2vt_p[];
extern const gm_spec *const gm_tpl6m8ee83kb2vt_s[];
extern const gm_tmpl_piece gm_tpl6md5hu7rmp82_p[];
extern const gm_tmpl_piece gm_tpl6ptbf3l6su1b_p[];
extern const gm_tmpl gm_tpl6q4b2p6tude6;
extern const uint32_t gm_tpl6q4b2p6tude6_i[];
extern const gm_tmpl_piece gm_tpl6q4b2p6tude6_p[];
extern const gm_spec *const gm_tpl6q4b2p6tude6_s[];
extern const gm_tmpl gm_tpl6salfcvi63n2;
extern const uint32_t gm_tpl6salfcvi63n2_i[];
extern const gm_tmpl_piece gm_tpl6salfcvi63n2_p[];
extern const gm_spec *const gm_tpl6salfcvi63n2_s[];
extern const gm_tmpl gm_tpl6skdtj6qlkmd;
extern const uint32_t gm_tpl6skdtj6qlkmd_i[];
extern const gm_tmpl_piece gm_tpl6skdtj6qlkmd_p[];
extern const gm_spec *const gm_tpl6skdtj6qlkmd_s[];
extern const gm_tmpl gm_tpl6tnfk0d9dl8e;
extern const uint32_t gm_tpl6tnfk0d9dl8e_i[];
extern const gm_tmpl_piece gm_tpl6tnfk0d9dl8e_p[];
extern const gm_spec *const gm_tpl6tnfk0d9dl8e_s[];
extern const gm_spec *const gm_tpl70tkged35ahl_s[];
extern const gm_tmpl_piece gm_tpl74mr52kbqst5_p[];
extern const gm_spec *const gm_tpl77gpruiccsl2_s[];
extern const gm_tmpl gm_tpl77hog51tnj6o;
extern const uint32_t gm_tpl77hog51tnj6o_i[];
extern const gm_tmpl_piece gm_tpl77hog51tnj6o_p[];
extern const gm_spec *const gm_tpl77hog51tnj6o_s[];
extern const gm_spec *const gm_tpl7e5tr64a0u6e_s[];
extern const gm_tmpl_piece gm_tpl7kdhh1egc9gc_p[];
extern const gm_spec *const gm_tpl7kmb2inm0gau_s[];
extern const uint32_t gm_tpl7kp6007qjdtb_i[];
extern const gm_tmpl_piece gm_tpl7ll3166bkl17_p[];
extern const gm_tmpl gm_tpl7m3t7r6fqpf7;
extern const uint32_t gm_tpl7m3t7r6fqpf7_i[];
extern const gm_tmpl_piece gm_tpl7m3t7r6fqpf7_p[];
extern const gm_spec *const gm_tpl7m3t7r6fqpf7_s[];
extern const gm_tmpl gm_tpl7meurf74nbc1;
extern const uint32_t gm_tpl7meurf74nbc1_i[];
extern const gm_tmpl_piece gm_tpl7meurf74nbc1_p[];
extern const gm_spec *const gm_tpl7meurf74nbc1_s[];
extern const gm_spec *const gm_tpl7oj2vfshts60_s[];
extern const gm_tmpl gm_tpl81uel28fst2a;
extern const uint32_t gm_tpl81uel28fst2a_i[];
extern const gm_tmpl_piece gm_tpl81uel28fst2a_p[];
extern const gm_spec *const gm_tpl81uel28fst2a_s[];
extern const gm_tmpl gm_tpl84r77qupuujj;
extern const uint32_t gm_tpl84r77qupuujj_i[];
extern const gm_tmpl_piece gm_tpl84r77qupuujj_p[];
extern const gm_spec *const gm_tpl84r77qupuujj_s[];
extern const gm_spec *const gm_tpl86cip41cbf58_s[];
extern const gm_tmpl_piece gm_tpl896mu4np77il_p[];
extern const uint32_t gm_tpl8bar0du85qu4_i[];
extern const gm_tmpl_piece gm_tpl8geisveh2vug_p[];
extern const gm_tmpl gm_tpl8gj5o7rmo2fl;
extern const uint32_t gm_tpl8gj5o7rmo2fl_i[];
extern const gm_tmpl_piece gm_tpl8gj5o7rmo2fl_p[];
extern const gm_spec *const gm_tpl8gj5o7rmo2fl_s[];
extern const uint32_t gm_tpl8in37qe3omcj_i[];
extern const gm_tmpl gm_tpl8lt5kipnmd64;
extern const uint32_t gm_tpl8lt5kipnmd64_i[];
extern const gm_tmpl_piece gm_tpl8lt5kipnmd64_p[];
extern const gm_spec *const gm_tpl8lt5kipnmd64_s[];
extern const gm_spec *const gm_tpl8mdj3nc4lpjv_s[];
extern const gm_tmpl gm_tpl8ptl55ago227;
extern const uint32_t gm_tpl8ptl55ago227_i[];
extern const gm_tmpl_piece gm_tpl8ptl55ago227_p[];
extern const gm_spec *const gm_tpl8ptl55ago227_s[];
extern const gm_tmpl gm_tpl8pvdc7qbgge6;
extern const uint32_t gm_tpl8pvdc7qbgge6_i[];
extern const gm_tmpl_piece gm_tpl8pvdc7qbgge6_p[];
extern const gm_spec *const gm_tpl8pvdc7qbgge6_s[];
extern const gm_tmpl_piece gm_tpl953juq2iaklp_p[];
extern const gm_spec *const gm_tpl953juq2iaklp_s[];
extern const gm_spec *const gm_tpl955dep3t38r6_s[];
extern const gm_tmpl_piece gm_tpl98as72l6ukas_p[];
extern const gm_spec *const gm_tpl9aakii48cq8t_s[];
extern const gm_tmpl_piece gm_tpl9d5e93nnc8t1_p[];
extern const gm_tmpl_piece gm_tpl9dqq7pjdmqnl_p[];
extern const uint32_t gm_tpl9ft3vu1e30uh_i[];
extern const gm_tmpl_piece gm_tpl9gmvk2gm4l5h_p[];
extern const gm_spec *const gm_tpl9h9af6kus46t_s[];
extern const uint32_t gm_tpl9ibjmfjq0iar_i[];
extern const gm_tmpl gm_tpl9j9ok1j4nn18;
extern const uint32_t gm_tpl9j9ok1j4nn18_i[];
extern const gm_tmpl_piece gm_tpl9j9ok1j4nn18_p[];
extern const gm_spec *const gm_tpl9j9ok1j4nn18_s[];
extern const uint32_t gm_tpl9l4fqsojrjsa_i[];
extern const gm_tmpl_piece gm_tpl9ljuo8s7738q_p[];
extern const uint32_t gm_tpl9p5406jtlms7_i[];
extern const gm_tmpl gm_tpl9qchhbq9qlbm;
extern const uint32_t gm_tpl9qchhbq9qlbm_i[];
extern const gm_tmpl_piece gm_tpl9qchhbq9qlbm_p[];
extern const gm_spec *const gm_tpl9qchhbq9qlbm_s[];
extern const gm_tmpl gm_tpl9qg0hlqqke7e;
extern const uint32_t gm_tpl9qg0hlqqke7e_i[];
extern const gm_tmpl_piece gm_tpl9qg0hlqqke7e_p[];
extern const gm_spec *const gm_tpl9qg0hlqqke7e_s[];
extern const gm_tmpl gm_tpl9sh9d989om94;
extern const uint32_t gm_tpl9sh9d989om94_i[];
extern const gm_tmpl_piece gm_tpl9sh9d989om94_p[];
extern const gm_spec *const gm_tpl9sh9d989om94_s[];
extern const gm_spec *const gm_tpl9sifsarv7t06_s[];
extern const uint32_t gm_tpl9v8qcr7jilen_i[];
extern const uint32_t gm_tpl9vu8tqcv1d9s_i[];
extern const gm_tmpl gm_tpla1i8rr6vfju7;
extern const uint32_t gm_tpla1i8rr6vfju7_i[];
extern const gm_tmpl_piece gm_tpla1i8rr6vfju7_p[];
extern const gm_spec *const gm_tpla1i8rr6vfju7_s[];
extern const uint32_t gm_tpla6b7o385jo3o_i[];
extern const gm_tmpl_piece gm_tpla6sajpp07b9l_p[];
extern const gm_tmpl_piece gm_tpla74als5inv2i_p[];
extern const gm_tmpl gm_tpla93ou7df923g;
extern const uint32_t gm_tpla93ou7df923g_i[];
extern const gm_tmpl_piece gm_tpla93ou7df923g_p[];
extern const gm_spec *const gm_tpla93ou7df923g_s[];
extern const gm_tmpl gm_tplad88kt6677ij;
extern const uint32_t gm_tplad88kt6677ij_i[];
extern const gm_tmpl_piece gm_tplad88kt6677ij_p[];
extern const gm_spec *const gm_tplad88kt6677ij_s[];
extern const gm_tmpl_piece gm_tplahe50smef36f_p[];
extern const uint32_t gm_tplalgvrfo85soq_i[];
extern const gm_tmpl gm_tplalu8vhij0mma;
extern const uint32_t gm_tplalu8vhij0mma_i[];
extern const gm_tmpl_piece gm_tplalu8vhij0mma_p[];
extern const gm_spec *const gm_tplalu8vhij0mma_s[];
extern const gm_spec *const gm_tplamm5t71udd8k_s[];
extern const uint32_t gm_tplamv2e4uailq7_i[];
extern const gm_tmpl gm_tplaoj5r4tauejk;
extern const uint32_t gm_tplaoj5r4tauejk_i[];
extern const gm_tmpl_piece gm_tplaoj5r4tauejk_p[];
extern const gm_spec *const gm_tplaoj5r4tauejk_s[];
extern const uint32_t gm_tplaos6tmpdtrie_i[];
extern const gm_spec *const gm_tplaqps986okebj_s[];
extern const uint32_t gm_tplastf5j9ge45b_i[];
extern const gm_tmpl gm_tplau4cbc1piuvn;
extern const uint32_t gm_tplau4cbc1piuvn_i[];
extern const gm_tmpl_piece gm_tplau4cbc1piuvn_p[];
extern const gm_spec *const gm_tplau4cbc1piuvn_s[];
extern const gm_tmpl_piece gm_tplav4294nbj9c7_p[];
extern const gm_tmpl gm_tplav8bdvvt8rem;
extern const uint32_t gm_tplav8bdvvt8rem_i[];
extern const gm_tmpl_piece gm_tplav8bdvvt8rem_p[];
extern const gm_spec *const gm_tplav8bdvvt8rem_s[];
extern const uint32_t gm_tplb52vln6fobrq_i[];
extern const gm_tmpl_piece gm_tplb9i7jfun8rvg_p[];
extern const gm_tmpl gm_tplba0kefu5gu2c;
extern const uint32_t gm_tplba0kefu5gu2c_i[];
extern const gm_tmpl_piece gm_tplba0kefu5gu2c_p[];
extern const gm_spec *const gm_tplba0kefu5gu2c_s[];
extern const gm_tmpl gm_tplbahlus43pmvm;
extern const uint32_t gm_tplbahlus43pmvm_i[];
extern const gm_tmpl_piece gm_tplbahlus43pmvm_p[];
extern const gm_spec *const gm_tplbahlus43pmvm_s[];
extern const gm_tmpl_piece gm_tplbaonrrpbr1uh_p[];
extern const gm_tmpl_piece gm_tplbathm90qd36n_p[];
extern const uint32_t gm_tplbckos3fl05qe_i[];
extern const gm_tmpl gm_tplbfdkthp45ai4;
extern const uint32_t gm_tplbfdkthp45ai4_i[];
extern const gm_tmpl_piece gm_tplbfdkthp45ai4_p[];
extern const gm_spec *const gm_tplbfdkthp45ai4_s[];
extern const uint32_t gm_tplbgu1ah9e6d87_i[];
extern const gm_tmpl gm_tplblrluopde8aq;
extern const uint32_t gm_tplblrluopde8aq_i[];
extern const gm_tmpl_piece gm_tplblrluopde8aq_p[];
extern const gm_spec *const gm_tplblrluopde8aq_s[];
extern const gm_spec *const gm_tplblsk2k5t46dn_s[];
extern const gm_spec *const gm_tplbmf4neusqbo5_s[];
extern const gm_tmpl gm_tplbnt4jnaihcvu;
extern const uint32_t gm_tplbnt4jnaihcvu_i[];
extern const gm_tmpl_piece gm_tplbnt4jnaihcvu_p[];
extern const gm_spec *const gm_tplbnt4jnaihcvu_s[];
extern const gm_tmpl gm_tplbntllbe37f81;
extern const uint32_t gm_tplbntllbe37f81_i[];
extern const gm_tmpl_piece gm_tplbntllbe37f81_p[];
extern const gm_spec *const gm_tplbntllbe37f81_s[];
extern const gm_tmpl gm_tplbo9rmalpf2vj;
extern const uint32_t gm_tplbo9rmalpf2vj_i[];
extern const gm_tmpl_piece gm_tplbo9rmalpf2vj_p[];
extern const gm_spec *const gm_tplbo9rmalpf2vj_s[];
extern const gm_tmpl gm_tplbog2sc0rf3ot;
extern const uint32_t gm_tplbog2sc0rf3ot_i[];
extern const gm_tmpl_piece gm_tplbog2sc0rf3ot_p[];
extern const gm_spec *const gm_tplbog2sc0rf3ot_s[];
extern const gm_spec *const gm_tplbppmjhpd8ol0_s[];
extern const gm_spec *const gm_tplbsca6gthghjf_s[];
extern const uint32_t gm_tplc2ekf83ggtj3_i[];
extern const gm_tmpl gm_tplc6hhdd7vcmvk;
extern const uint32_t gm_tplc6hhdd7vcmvk_i[];
extern const gm_tmpl_piece gm_tplc6hhdd7vcmvk_p[];
extern const gm_spec *const gm_tplc6hhdd7vcmvk_s[];
extern const gm_tmpl gm_tplcddb08maq348;
extern const uint32_t gm_tplcddb08maq348_i[];
extern const gm_tmpl_piece gm_tplcddb08maq348_p[];
extern const gm_spec *const gm_tplcddb08maq348_s[];
extern const gm_tmpl gm_tplcef5l6cvp5lt;
extern const uint32_t gm_tplcef5l6cvp5lt_i[];
extern const gm_tmpl_piece gm_tplcef5l6cvp5lt_p[];
extern const gm_spec *const gm_tplcef5l6cvp5lt_s[];
extern const gm_tmpl gm_tplcein5d5pq2cs;
extern const uint32_t gm_tplcein5d5pq2cs_i[];
extern const gm_tmpl_piece gm_tplcein5d5pq2cs_p[];
extern const gm_spec *const gm_tplcein5d5pq2cs_s[];
extern const gm_tmpl gm_tplceqrbdtcd9p5;
extern const uint32_t gm_tplceqrbdtcd9p5_i[];
extern const gm_tmpl_piece gm_tplceqrbdtcd9p5_p[];
extern const gm_spec *const gm_tplceqrbdtcd9p5_s[];
extern const gm_spec *const gm_tplck20jih4okbk_s[];
extern const uint32_t gm_tplcre592qvlrqu_i[];
extern const gm_tmpl gm_tplctspgtksm35f;
extern const uint32_t gm_tplctspgtksm35f_i[];
extern const gm_tmpl_piece gm_tplctspgtksm35f_p[];
extern const gm_spec *const gm_tplctspgtksm35f_s[];
extern const gm_tmpl_piece gm_tplcttkqubjcaqf_p[];
extern const uint32_t gm_tplcudcaj1iluqd_i[];
extern const gm_tmpl gm_tplcvnbkkkji94u;
extern const uint32_t gm_tplcvnbkkkji94u_i[];
extern const gm_tmpl_piece gm_tplcvnbkkkji94u_p[];
extern const gm_spec *const gm_tplcvnbkkkji94u_s[];
extern const gm_tmpl_piece gm_tpld4u3te7g9c5v_p[];
extern const gm_spec *const gm_tpldbe9bnjeqeqg_s[];
extern const gm_tmpl gm_tpldlrsentitfg3;
extern const uint32_t gm_tpldlrsentitfg3_i[];
extern const gm_tmpl_piece gm_tpldlrsentitfg3_p[];
extern const gm_spec *const gm_tpldlrsentitfg3_s[];
extern const gm_tmpl gm_tpldofkl5m8hqug;
extern const uint32_t gm_tpldofkl5m8hqug_i[];
extern const gm_tmpl_piece gm_tpldofkl5m8hqug_p[];
extern const gm_spec *const gm_tpldofkl5m8hqug_s[];
extern const gm_tmpl gm_tpldrlq7uo0cbrm;
extern const uint32_t gm_tpldrlq7uo0cbrm_i[];
extern const gm_tmpl_piece gm_tpldrlq7uo0cbrm_p[];
extern const gm_spec *const gm_tpldrlq7uo0cbrm_s[];
extern const gm_tmpl gm_tpldti7tgsj3l9q;
extern const uint32_t gm_tpldti7tgsj3l9q_i[];
extern const gm_tmpl_piece gm_tpldti7tgsj3l9q_p[];
extern const gm_spec *const gm_tpldti7tgsj3l9q_s[];
extern const uint32_t gm_tpldueenbh2sgd4_i[];
extern const gm_tmpl_piece gm_tpldunbbu9dpisn_p[];
extern const uint32_t gm_tpldvbj2judb6g5_i[];
extern const gm_tmpl_piece gm_tpldvh3e1jcen1u_p[];
extern const gm_spec *const gm_tple23ed5kepfqi_s[];
extern const gm_tmpl gm_tple4upi9cflnq6;
extern const uint32_t gm_tple4upi9cflnq6_i[];
extern const gm_tmpl_piece gm_tple4upi9cflnq6_p[];
extern const gm_spec *const gm_tple4upi9cflnq6_s[];
extern const gm_tmpl_piece gm_tple7qj0fr2tlub_p[];
extern const gm_spec *const gm_tple7ukme0qchh6_s[];
extern const gm_tmpl gm_tplec2d6tt24j0q;
extern const uint32_t gm_tplec2d6tt24j0q_i[];
extern const gm_tmpl_piece gm_tplec2d6tt24j0q_p[];
extern const gm_spec *const gm_tplec2d6tt24j0q_s[];
extern const gm_tmpl_piece gm_tpled61een8fpfm_p[];
extern const uint32_t gm_tpleeuqgcr11ngn_i[];
extern const gm_tmpl gm_tplegelorakvcn7;
extern const uint32_t gm_tplegelorakvcn7_i[];
extern const gm_tmpl_piece gm_tplegelorakvcn7_p[];
extern const gm_spec *const gm_tplegelorakvcn7_s[];
extern const gm_tmpl_piece gm_tplehc5f52ftol1_p[];
extern const gm_tmpl_piece gm_tplellk9btkq1f2_p[];
extern const gm_tmpl gm_tplemu1bmibr9ve;
extern const uint32_t gm_tplemu1bmibr9ve_i[];
extern const gm_tmpl_piece gm_tplemu1bmibr9ve_p[];
extern const gm_spec *const gm_tplemu1bmibr9ve_s[];
extern const gm_tmpl_piece gm_tplep4rfqi7to9g_p[];
extern const gm_tmpl_piece gm_tplet6obcbkjbht_p[];
extern const gm_tmpl gm_tplevafblg3gmq9;
extern const uint32_t gm_tplevafblg3gmq9_i[];
extern const gm_tmpl_piece gm_tplevafblg3gmq9_p[];
extern const gm_spec *const gm_tplevafblg3gmq9_s[];
extern const gm_spec *const gm_tplf27d39e1vcer_s[];
extern const gm_tmpl gm_tplf3e41f3siie3;
extern const uint32_t gm_tplf3e41f3siie3_i[];
extern const gm_tmpl_piece gm_tplf3e41f3siie3_p[];
extern const gm_spec *const gm_tplf3e41f3siie3_s[];
extern const uint32_t gm_tplf579fe8ol0qp_i[];
extern const gm_tmpl_piece gm_tplf8er2klne616_p[];
extern const gm_tmpl gm_tplf9q60in6m019;
extern const uint32_t gm_tplf9q60in6m019_i[];
extern const gm_tmpl_piece gm_tplf9q60in6m019_p[];
extern const gm_spec *const gm_tplf9q60in6m019_s[];
extern const gm_tmpl_piece gm_tplff85tslj1ph5_p[];
extern const gm_spec *const gm_tplffkq5ue6h7rm_s[];
extern const gm_tmpl_piece gm_tplfoosl93e8k5t_p[];
extern const gm_tmpl gm_tplfsmasr3tgalo;
extern const uint32_t gm_tplfsmasr3tgalo_i[];
extern const gm_tmpl_piece gm_tplfsmasr3tgalo_p[];
extern const gm_spec *const gm_tplfsmasr3tgalo_s[];
extern const gm_tmpl_piece gm_tplftg0h8omro11_p[];
extern const gm_tmpl gm_tplg05c24rh8peq;
extern const uint32_t gm_tplg05c24rh8peq_i[];
extern const gm_tmpl_piece gm_tplg05c24rh8peq_p[];
extern const gm_spec *const gm_tplg05c24rh8peq_s[];
extern const uint32_t gm_tplg0lh6vujmm6o_i[];
extern const uint32_t gm_tplg2qlrjk1sunc_i[];
extern const gm_tmpl_piece gm_tplg3ifbt1a32e1_p[];
extern const uint32_t gm_tplg575kk57c4i8_i[];
extern const gm_tmpl_piece gm_tplg7bh9jeid72i_p[];
extern const gm_tmpl_piece gm_tplgclcdp539fpt_p[];
extern const gm_tmpl_piece gm_tplge1jgdcvfbq1_p[];
extern const gm_tmpl gm_tplggs57b0p2or4;
extern const uint32_t gm_tplggs57b0p2or4_i[];
extern const gm_tmpl_piece gm_tplggs57b0p2or4_p[];
extern const gm_spec *const gm_tplggs57b0p2or4_s[];
extern const gm_tmpl gm_tplgja2nber62dc;
extern const uint32_t gm_tplgja2nber62dc_i[];
extern const gm_tmpl_piece gm_tplgja2nber62dc_p[];
extern const gm_spec *const gm_tplgja2nber62dc_s[];
extern const gm_tmpl gm_tplgjeridtvkml4;
extern const uint32_t gm_tplgjeridtvkml4_i[];
extern const gm_tmpl_piece gm_tplgjeridtvkml4_p[];
extern const gm_spec *const gm_tplgjeridtvkml4_s[];
extern const uint32_t gm_tplgniq1bprrgpo_i[];
extern const gm_tmpl_piece gm_tplgniq1bprrgpo_p[];
extern const uint32_t gm_tplgnp9u7qeb3h6_i[];
extern const gm_spec *const gm_tplgr8ep9alj0ph_s[];
extern const gm_tmpl gm_tplgs2l60s65hm3;
extern const uint32_t gm_tplgs2l60s65hm3_i[];
extern const gm_tmpl_piece gm_tplgs2l60s65hm3_p[];
extern const gm_spec *const gm_tplgs2l60s65hm3_s[];
extern const gm_spec *const gm_tplgursfbit7uc3_s[];
extern const gm_tmpl gm_tplh5ff53j0j0d2;
extern const uint32_t gm_tplh5ff53j0j0d2_i[];
extern const gm_tmpl_piece gm_tplh5ff53j0j0d2_p[];
extern const gm_spec *const gm_tplh5ff53j0j0d2_s[];
extern const gm_spec *const gm_tplhdb29uo8rm6j_s[];
extern const gm_tmpl gm_tplho1kg74nlm64;
extern const uint32_t gm_tplho1kg74nlm64_i[];
extern const gm_tmpl_piece gm_tplho1kg74nlm64_p[];
extern const gm_spec *const gm_tplho1kg74nlm64_s[];
extern const uint32_t gm_tplhp4k6h527j3k_i[];
extern const uint32_t gm_tplhphg08jcb4qr_i[];
extern const gm_tmpl_piece gm_tplhr6emralh8q6_p[];
extern const gm_tmpl_piece gm_tplhv087vo2q53g_p[];
extern const gm_tmpl gm_tpli08vvf1bckj4;
extern const uint32_t gm_tpli08vvf1bckj4_i[];
extern const gm_tmpl_piece gm_tpli08vvf1bckj4_p[];
extern const gm_spec *const gm_tpli08vvf1bckj4_s[];
extern const gm_spec *const gm_tpli0tmonaamd45_s[];
extern const uint32_t gm_tpli3i5m2srtl1a_i[];
extern const gm_spec *const gm_tpli9foo8ib8qrk_s[];
extern const uint32_t gm_tplibfhnj0illo7_i[];
extern const gm_spec *const gm_tplibgopbmor3p6_s[];
extern const gm_tmpl gm_tplicg84n3os024;
extern const uint32_t gm_tplicg84n3os024_i[];
extern const gm_tmpl_piece gm_tplicg84n3os024_p[];
extern const gm_spec *const gm_tplicg84n3os024_s[];
extern const uint32_t gm_tplicuqpjvsak60_i[];
extern const gm_tmpl gm_tplilne6f8eq90i;
extern const uint32_t gm_tplilne6f8eq90i_i[];
extern const gm_tmpl_piece gm_tplilne6f8eq90i_p[];
extern const gm_spec *const gm_tplilne6f8eq90i_s[];
extern const uint32_t gm_tplipqn9urt53l3_i[];
extern const gm_spec *const gm_tplipqn9urt53l3_s[];
extern const gm_tmpl gm_tplisurri6t3aun;
extern const uint32_t gm_tplisurri6t3aun_i[];
extern const gm_tmpl_piece gm_tplisurri6t3aun_p[];
extern const gm_spec *const gm_tplisurri6t3aun_s[];
extern const gm_tmpl gm_tplivckk5jvcluc;
extern const uint32_t gm_tplivckk5jvcluc_i[];
extern const gm_tmpl_piece gm_tplivckk5jvcluc_p[];
extern const gm_spec *const gm_tplivckk5jvcluc_s[];
extern const gm_tmpl_piece gm_tplj6bgc315nev3_p[];
extern const gm_tmpl gm_tplj6imuu9dopvn;
extern const uint32_t gm_tplj6imuu9dopvn_i[];
extern const gm_tmpl_piece gm_tplj6imuu9dopvn_p[];
extern const gm_spec *const gm_tplj6imuu9dopvn_s[];
extern const gm_tmpl_piece gm_tplj92mk62u70db_p[];
extern const gm_tmpl gm_tpljbco1er3agg5;
extern const uint32_t gm_tpljbco1er3agg5_i[];
extern const gm_tmpl_piece gm_tpljbco1er3agg5_p[];
extern const gm_spec *const gm_tpljbco1er3agg5_s[];
extern const gm_tmpl gm_tpljc7kqi31f4d2;
extern const uint32_t gm_tpljc7kqi31f4d2_i[];
extern const gm_tmpl_piece gm_tpljc7kqi31f4d2_p[];
extern const gm_spec *const gm_tpljc7kqi31f4d2_s[];
extern const gm_tmpl_piece gm_tpljdh6phei25vf_p[];
extern const gm_spec *const gm_tpljg9idve5dgca_s[];
extern const gm_tmpl gm_tpljueetjft1ril;
extern const uint32_t gm_tpljueetjft1ril_i[];
extern const gm_tmpl_piece gm_tpljueetjft1ril_p[];
extern const gm_spec *const gm_tpljueetjft1ril_s[];
extern const uint32_t gm_tplk13976gpibvh_i[];
extern const gm_tmpl_piece gm_tplk1a5101vtct2_p[];
extern const gm_tmpl gm_tplk51k04l6756s;
extern const uint32_t gm_tplk51k04l6756s_i[];
extern const gm_tmpl_piece gm_tplk51k04l6756s_p[];
extern const gm_spec *const gm_tplk51k04l6756s_s[];
extern const gm_tmpl gm_tplk66j4h09rih6;
extern const uint32_t gm_tplk66j4h09rih6_i[];
extern const gm_tmpl_piece gm_tplk66j4h09rih6_p[];
extern const gm_spec *const gm_tplk66j4h09rih6_s[];
extern const uint32_t gm_tplk7hjamhd5jcl_i[];
extern const uint32_t gm_tplka8uag807gql_i[];
extern const gm_tmpl gm_tplkd07482m1uv1;
extern const uint32_t gm_tplkd07482m1uv1_i[];
extern const gm_tmpl_piece gm_tplkd07482m1uv1_p[];
extern const gm_spec *const gm_tplkd07482m1uv1_s[];
extern const gm_spec *const gm_tplkhlcq331nni9_s[];
extern const gm_spec *const gm_tplkp06s34ois53_s[];
extern const gm_tmpl_piece gm_tplksrq9kcn68vt_p[];
extern const gm_tmpl gm_tplku5ga9b4h0i9;
extern const uint32_t gm_tplku5ga9b4h0i9_i[];
extern const gm_tmpl_piece gm_tplku5ga9b4h0i9_p[];
extern const gm_spec *const gm_tplku5ga9b4h0i9_s[];
extern const gm_spec *const gm_tplkv0rjh54m5gn_s[];
extern const gm_tmpl gm_tpll1i6ur15ms5i;
extern const uint32_t gm_tpll1i6ur15ms5i_i[];
extern const gm_tmpl_piece gm_tpll1i6ur15ms5i_p[];
extern const gm_spec *const gm_tpll1i6ur15ms5i_s[];
extern const uint32_t gm_tpll1t63hmppo21_i[];
extern const gm_tmpl gm_tpll3bcqsckmobi;
extern const uint32_t gm_tpll3bcqsckmobi_i[];
extern const gm_tmpl_piece gm_tpll3bcqsckmobi_p[];
extern const gm_spec *const gm_tpll3bcqsckmobi_s[];
extern const gm_tmpl_piece gm_tpll4f2ihchoe9c_p[];
extern const uint32_t gm_tpll60ng9eo3ai8_i[];
extern const uint32_t gm_tpll9g0ell7ocpk_i[];
extern const gm_tmpl_piece gm_tpllcp8fo2sqc1r_p[];
extern const gm_tmpl_piece gm_tpllfgb42aufjqd_p[];
extern const gm_tmpl gm_tpllfhth9mee4q8;
extern const uint32_t gm_tpllfhth9mee4q8_i[];
extern const gm_tmpl_piece gm_tpllfhth9mee4q8_p[];
extern const gm_spec *const gm_tpllfhth9mee4q8_s[];
extern const gm_tmpl_piece gm_tpllg65m2i6jvjk_p[];
extern const gm_tmpl gm_tpllihoeldkkkhv;
extern const uint32_t gm_tpllihoeldkkkhv_i[];
extern const gm_tmpl_piece gm_tpllihoeldkkkhv_p[];
extern const gm_spec *const gm_tpllihoeldkkkhv_s[];
extern const gm_tmpl gm_tpllm484u6fuiq3;
extern const uint32_t gm_tpllm484u6fuiq3_i[];
extern const gm_tmpl_piece gm_tpllm484u6fuiq3_p[];
extern const gm_spec *const gm_tpllm484u6fuiq3_s[];
extern const gm_tmpl_piece gm_tplloue1uueuiig_p[];
extern const uint32_t gm_tpllovel7a2ips1_i[];
extern const uint32_t gm_tpllqbinchipsao_i[];
extern const gm_tmpl_piece gm_tplm0bsttjtt8b0_p[];
extern const gm_tmpl gm_tplm3dvc32djhr7;
extern const uint32_t gm_tplm3dvc32djhr7_i[];
extern const gm_tmpl_piece gm_tplm3dvc32djhr7_p[];
extern const gm_spec *const gm_tplm3dvc32djhr7_s[];
extern const gm_tmpl_piece gm_tplm3r2aiholmt8_p[];
extern const gm_tmpl_piece gm_tplm5g4n5l4k6ck_p[];
extern const gm_tmpl_piece gm_tplm61d8mlvmepm_p[];
extern const gm_tmpl gm_tplme3pul5hs3bf;
extern const uint32_t gm_tplme3pul5hs3bf_i[];
extern const gm_tmpl_piece gm_tplme3pul5hs3bf_p[];
extern const gm_spec *const gm_tplme3pul5hs3bf_s[];
extern const gm_tmpl_piece gm_tplmebto0o1gmln_p[];
extern const uint32_t gm_tplmj3d4h2pi7q3_i[];
extern const gm_tmpl gm_tplmk3amvcsk2gu;
extern const uint32_t gm_tplmk3amvcsk2gu_i[];
extern const gm_tmpl_piece gm_tplmk3amvcsk2gu_p[];
extern const gm_spec *const gm_tplmk3amvcsk2gu_s[];
extern const uint32_t gm_tplmn77qas0g7s4_i[];
extern const gm_tmpl_piece gm_tplmo458jpb0n1e_p[];
extern const gm_tmpl_piece gm_tplmpsv5qagthkk_p[];
extern const gm_tmpl gm_tplmsb1klg4n6ap;
extern const uint32_t gm_tplmsb1klg4n6ap_i[];
extern const gm_tmpl_piece gm_tplmsb1klg4n6ap_p[];
extern const gm_spec *const gm_tplmsb1klg4n6ap_s[];
extern const gm_spec *const gm_tplmsd0sfgf2v66_s[];
extern const gm_tmpl_piece gm_tpln4k7ltoriuv9_p[];
extern const gm_tmpl gm_tpln8ctpo9hror7;
extern const uint32_t gm_tpln8ctpo9hror7_i[];
extern const gm_tmpl_piece gm_tpln8ctpo9hror7_p[];
extern const gm_spec *const gm_tpln8ctpo9hror7_s[];
extern const gm_tmpl_piece gm_tplnc4en2s3t0da_p[];
extern const uint32_t gm_tplne0l0lciidvr_i[];
extern const gm_spec *const gm_tplne0l0lciidvr_s[];
extern const uint32_t gm_tplnevoink17fov_i[];
extern const gm_tmpl gm_tplnh359qrng71h;
extern const uint32_t gm_tplnh359qrng71h_i[];
extern const gm_tmpl_piece gm_tplnh359qrng71h_p[];
extern const gm_spec *const gm_tplnh359qrng71h_s[];
extern const uint32_t gm_tplnm0o65j76qg0_i[];
extern const gm_spec *const gm_tplnokap11rv9jr_s[];
extern const gm_tmpl_piece gm_tplnr2qhie4t5ta_p[];
extern const uint32_t gm_tplnud722tu4rrk_i[];
extern const gm_tmpl gm_tplo2l8dmp437h2;
extern const uint32_t gm_tplo2l8dmp437h2_i[];
extern const gm_tmpl_piece gm_tplo2l8dmp437h2_p[];
extern const gm_spec *const gm_tplo2l8dmp437h2_s[];
extern const uint32_t gm_tplo38nq02vgfo5_i[];
extern const gm_tmpl_piece gm_tplo4cs7gli8ts2_p[];
extern const uint32_t gm_tplo66949vs5pfo_i[];
extern const uint32_t gm_tplo6lbijqleti1_i[];
extern const gm_spec *const gm_tplo8bto3ii8iq9_s[];
extern const uint32_t gm_tplob0n62u6gqqu_i[];
extern const gm_tmpl gm_tploc41q7b5gurn;
extern const uint32_t gm_tploc41q7b5gurn_i[];
extern const gm_tmpl_piece gm_tploc41q7b5gurn_p[];
extern const gm_spec *const gm_tploc41q7b5gurn_s[];
extern const gm_tmpl_piece gm_tplod6iso1fh3co_p[];
extern const gm_tmpl gm_tploj0vopi0n43l;
extern const uint32_t gm_tploj0vopi0n43l_i[];
extern const gm_tmpl_piece gm_tploj0vopi0n43l_p[];
extern const gm_spec *const gm_tploj0vopi0n43l_s[];
extern const gm_spec *const gm_tplolonhuuitsj2_s[];
extern const uint32_t gm_tplolsc5tfmth66_i[];
extern const gm_spec *const gm_tplopnhfma5is23_s[];
extern const gm_tmpl_piece gm_tplotas2madniqp_p[];
extern const gm_tmpl gm_tploui9f30uh460;
extern const uint32_t gm_tploui9f30uh460_i[];
extern const gm_tmpl_piece gm_tploui9f30uh460_p[];
extern const gm_spec *const gm_tploui9f30uh460_s[];
extern const gm_tmpl gm_tplp7gdl2lerimh;
extern const uint32_t gm_tplp7gdl2lerimh_i[];
extern const gm_tmpl_piece gm_tplp7gdl2lerimh_p[];
extern const gm_spec *const gm_tplp7gdl2lerimh_s[];
extern const gm_spec *const gm_tplp98fsht4k706_s[];
extern const gm_spec *const gm_tplpaq33rl4cnb8_s[];
extern const uint32_t gm_tplpfavv7btb1m5_i[];
extern const gm_spec *const gm_tplpgeniqejbt43_s[];
extern const uint32_t gm_tplpkq4i4j83tmk_i[];
extern const gm_spec *const gm_tplpkq4i4j83tmk_s[];
extern const uint32_t gm_tplpl5slbegbu93_i[];
extern const gm_tmpl_piece gm_tplpl5slbegbu93_p[];
extern const uint32_t gm_tplpnk24geo6qd8_i[];
extern const gm_spec *const gm_tplpnk24geo6qd8_s[];
extern const gm_tmpl gm_tplpntqck8i8f5b;
extern const uint32_t gm_tplpntqck8i8f5b_i[];
extern const gm_tmpl_piece gm_tplpntqck8i8f5b_p[];
extern const gm_spec *const gm_tplpntqck8i8f5b_s[];
extern const gm_tmpl gm_tplpoe8f3eln4m2;
extern const uint32_t gm_tplpoe8f3eln4m2_i[];
extern const gm_tmpl_piece gm_tplpoe8f3eln4m2_p[];
extern const gm_spec *const gm_tplpoe8f3eln4m2_s[];
extern const gm_tmpl_piece gm_tplpv6nab1nov2l_p[];
extern const gm_spec *const gm_tplpvmvg9ce66cu_s[];
extern const gm_tmpl_piece gm_tplq0mvos8lisnf_p[];
extern const gm_tmpl_piece gm_tplq54tg5m6aueh_p[];
extern const gm_tmpl gm_tplqbbl1qig0e0s;
extern const uint32_t gm_tplqbbl1qig0e0s_i[];
extern const gm_tmpl_piece gm_tplqbbl1qig0e0s_p[];
extern const gm_spec *const gm_tplqbbl1qig0e0s_s[];
extern const uint32_t gm_tplqg9a6t74ffg7_i[];
extern const gm_tmpl gm_tplqj931pe0994k;
extern const uint32_t gm_tplqj931pe0994k_i[];
extern const gm_tmpl_piece gm_tplqj931pe0994k_p[];
extern const gm_spec *const gm_tplqj931pe0994k_s[];
extern const uint32_t gm_tplqjo10upi8bs6_i[];
extern const gm_tmpl gm_tplql7vfevun5ae;
extern const uint32_t gm_tplql7vfevun5ae_i[];
extern const gm_tmpl_piece gm_tplql7vfevun5ae_p[];
extern const gm_spec *const gm_tplql7vfevun5ae_s[];
extern const uint32_t gm_tplqn8qbim70q76_i[];
extern const uint32_t gm_tplqrbhvccemc9k_i[];
extern const gm_tmpl_piece gm_tplqstpsk3jkk1c_p[];
extern const gm_tmpl gm_tplqtueduh1r3n9;
extern const uint32_t gm_tplqtueduh1r3n9_i[];
extern const gm_tmpl_piece gm_tplqtueduh1r3n9_p[];
extern const gm_spec *const gm_tplqtueduh1r3n9_s[];
extern const gm_tmpl gm_tplr2tuff1mvdst;
extern const uint32_t gm_tplr2tuff1mvdst_i[];
extern const gm_tmpl_piece gm_tplr2tuff1mvdst_p[];
extern const gm_spec *const gm_tplr2tuff1mvdst_s[];
extern const gm_tmpl gm_tplr35nagqnpmad;
extern const uint32_t gm_tplr35nagqnpmad_i[];
extern const gm_tmpl_piece gm_tplr35nagqnpmad_p[];
extern const gm_spec *const gm_tplr35nagqnpmad_s[];
extern const gm_tmpl gm_tplr3ni3ai315ji;
extern const uint32_t gm_tplr3ni3ai315ji_i[];
extern const gm_tmpl_piece gm_tplr3ni3ai315ji_p[];
extern const gm_spec *const gm_tplr3ni3ai315ji_s[];
extern const gm_spec *const gm_tplr7vr7aubja28_s[];
extern const gm_tmpl_piece gm_tplr976guo57cat_p[];
extern const gm_spec *const gm_tplr976guo57cat_s[];
extern const gm_spec *const gm_tplre5cfbnr7ltv_s[];
extern const uint32_t gm_tplre7l7cu4fpbv_i[];
extern const gm_tmpl gm_tplrfab517u4o97;
extern const uint32_t gm_tplrfab517u4o97_i[];
extern const gm_tmpl_piece gm_tplrfab517u4o97_p[];
extern const gm_spec *const gm_tplrfab517u4o97_s[];
extern const uint32_t gm_tplrmoc4ag4eqrb_i[];
extern const gm_spec *const gm_tplrp7ukmgscavj_s[];
extern const uint32_t gm_tplrpvalpqjvipe_i[];
extern const gm_tmpl gm_tplrspdrqd1dpc8;
extern const uint32_t gm_tplrspdrqd1dpc8_i[];
extern const gm_tmpl_piece gm_tplrspdrqd1dpc8_p[];
extern const gm_spec *const gm_tplrspdrqd1dpc8_s[];
extern const gm_tmpl_piece gm_tplrtgv8em132rl_p[];
extern const gm_spec *const gm_tplruf3ms5lqgst_s[];
extern const gm_spec *const gm_tpls4bcac20qnq6_s[];
extern const uint32_t gm_tpls5bjfpqmr7e0_i[];
extern const gm_tmpl_piece gm_tpls5bjfpqmr7e0_p[];
extern const gm_spec *const gm_tpls5bjfpqmr7e0_s[];
extern const gm_tmpl gm_tpls6f1h1hgtqgb;
extern const uint32_t gm_tpls6f1h1hgtqgb_i[];
extern const gm_tmpl_piece gm_tpls6f1h1hgtqgb_p[];
extern const gm_spec *const gm_tpls6f1h1hgtqgb_s[];
extern const gm_tmpl_piece gm_tpls80s4u4ujepk_p[];
extern const uint32_t gm_tpls85ptecv5rkj_i[];
extern const gm_tmpl gm_tpls8octb03drbp;
extern const uint32_t gm_tpls8octb03drbp_i[];
extern const gm_tmpl_piece gm_tpls8octb03drbp_p[];
extern const gm_spec *const gm_tpls8octb03drbp_s[];
extern const uint32_t gm_tplsf6r3t44km8l_i[];
extern const gm_spec *const gm_tplsil8jb9ng0ao_s[];
extern const gm_tmpl gm_tplsjs5e78h34ng;
extern const uint32_t gm_tplsjs5e78h34ng_i[];
extern const gm_tmpl_piece gm_tplsjs5e78h34ng_p[];
extern const gm_spec *const gm_tplsjs5e78h34ng_s[];
extern const gm_tmpl_piece gm_tplsk9b8o9jgshf_p[];
extern const gm_tmpl gm_tplska2t1f86rr8;
extern const uint32_t gm_tplska2t1f86rr8_i[];
extern const gm_tmpl_piece gm_tplska2t1f86rr8_p[];
extern const gm_spec *const gm_tplska2t1f86rr8_s[];
extern const gm_tmpl_piece gm_tplslqhr3ac3ilc_p[];
extern const uint32_t gm_tplspgpn4qu6rd4_i[];
extern const gm_tmpl gm_tplsqibbtt8789o;
extern const uint32_t gm_tplsqibbtt8789o_i[];
extern const gm_tmpl_piece gm_tplsqibbtt8789o_p[];
extern const gm_spec *const gm_tplsqibbtt8789o_s[];
extern const uint32_t gm_tplsrr43aau9tgs_i[];
extern const gm_tmpl gm_tplt0qsjg06adbb;
extern const uint32_t gm_tplt0qsjg06adbb_i[];
extern const gm_tmpl_piece gm_tplt0qsjg06adbb_p[];
extern const gm_spec *const gm_tplt0qsjg06adbb_s[];
extern const gm_tmpl_piece gm_tplt1si3io9mdpe_p[];
extern const gm_tmpl_piece gm_tplt3d4sltsd44m_p[];
extern const gm_tmpl_piece gm_tplt5ks894658kl_p[];
extern const gm_tmpl_piece gm_tplt68r4kihke54_p[];
extern const gm_tmpl gm_tplt6nq68u4co32;
extern const uint32_t gm_tplt6nq68u4co32_i[];
extern const gm_tmpl_piece gm_tplt6nq68u4co32_p[];
extern const gm_spec *const gm_tplt6nq68u4co32_s[];
extern const gm_tmpl gm_tplthkeg7jngn6q;
extern const uint32_t gm_tplthkeg7jngn6q_i[];
extern const gm_tmpl_piece gm_tplthkeg7jngn6q_p[];
extern const gm_spec *const gm_tplthkeg7jngn6q_s[];
extern const gm_spec *const gm_tpltnu092iqmfdd_s[];
extern const gm_tmpl gm_tpltt9gd20e1v7c;
extern const uint32_t gm_tpltt9gd20e1v7c_i[];
extern const gm_tmpl_piece gm_tpltt9gd20e1v7c_p[];
extern const gm_spec *const gm_tpltt9gd20e1v7c_s[];
extern const uint32_t gm_tplu0n2ra1s30o8_i[];
extern const uint32_t gm_tplu2g08gg1kb39_i[];
extern const gm_tmpl gm_tplu2tisf3oaee8;
extern const uint32_t gm_tplu2tisf3oaee8_i[];
extern const gm_tmpl_piece gm_tplu2tisf3oaee8_p[];
extern const gm_spec *const gm_tplu2tisf3oaee8_s[];
extern const uint32_t gm_tplu3gnb6pu4f3h_i[];
extern const gm_spec *const gm_tplu76evoc63de2_s[];
extern const gm_tmpl gm_tplu7gq2cm8aanh;
extern const uint32_t gm_tplu7gq2cm8aanh_i[];
extern const gm_tmpl_piece gm_tplu7gq2cm8aanh_p[];
extern const gm_spec *const gm_tplu7gq2cm8aanh_s[];
extern const uint32_t gm_tplua69gir871ue_i[];
extern const gm_tmpl_piece gm_tplubjng587610o_p[];
extern const gm_tmpl_piece gm_tplud75n1k6o39i_p[];
extern const gm_spec *const gm_tpludgprff7jihr_s[];
extern const gm_tmpl gm_tpluduaa40ad285;
extern const uint32_t gm_tpluduaa40ad285_i[];
extern const gm_tmpl_piece gm_tpluduaa40ad285_p[];
extern const gm_spec *const gm_tpluduaa40ad285_s[];
extern const uint32_t gm_tpluetglub2muf2_i[];
extern const gm_tmpl_piece gm_tpluetglub2muf2_p[];
extern const gm_spec *const gm_tpluetglub2muf2_s[];
extern const gm_tmpl_piece gm_tplugmq0iksatjg_p[];
extern const gm_spec *const gm_tpluh0hed6nj59d_s[];
extern const gm_spec *const gm_tplulq1qidn3nog_s[];
extern const gm_tmpl gm_tplumuahjb5m8m8;
extern const uint32_t gm_tplumuahjb5m8m8_i[];
extern const gm_tmpl_piece gm_tplumuahjb5m8m8_p[];
extern const gm_spec *const gm_tplumuahjb5m8m8_s[];
extern const uint32_t gm_tplupuvr4efpn1v_i[];
extern const gm_tmpl gm_tpluue9jh13g9df;
extern const uint32_t gm_tpluue9jh13g9df_i[];
extern const gm_tmpl_piece gm_tpluue9jh13g9df_p[];
extern const gm_spec *const gm_tpluue9jh13g9df_s[];
extern const uint32_t gm_tpluuksm24afvp0_i[];
extern const gm_tmpl_piece gm_tpluuksm24afvp0_p[];
extern const gm_spec *const gm_tpluvp47iqcj4uv_s[];
extern const gm_spec *const gm_tplv2usruqglqv9_s[];
extern const gm_tmpl gm_tplv8s8fgl2lggu;
extern const uint32_t gm_tplv8s8fgl2lggu_i[];
extern const gm_tmpl_piece gm_tplv8s8fgl2lggu_p[];
extern const gm_spec *const gm_tplv8s8fgl2lggu_s[];
extern const gm_tmpl_piece gm_tplv9sqjp27m76e_p[];
extern const gm_tmpl_piece gm_tplvdf3kbq8rjbi_p[];
extern const gm_spec *const gm_tplvdft4iatcp17_s[];
extern const gm_spec *const gm_tplvdkp4jmav7jr_s[];
extern const gm_tmpl_piece gm_tplvf2bf6hftf4i_p[];
extern const gm_tmpl gm_tplvhp4k8a2f5k6;
extern const uint32_t gm_tplvhp4k8a2f5k6_i[];
extern const gm_tmpl_piece gm_tplvhp4k8a2f5k6_p[];
extern const gm_spec *const gm_tplvhp4k8a2f5k6_s[];
extern const uint32_t gm_tplvi7pmohm1ihu_i[];
extern const gm_spec *const gm_tplvi7pmohm1ihu_s[];
extern const gm_spec *const gm_tplvk1pmcp9tr4p_s[];
extern const gm_spec *const gm_tplvkpfj3qq0aig_s[];
extern const gm_tmpl gm_tplvle6f1rmlmjh;
extern const uint32_t gm_tplvle6f1rmlmjh_i[];
extern const gm_tmpl_piece gm_tplvle6f1rmlmjh_p[];
extern const gm_spec *const gm_tplvle6f1rmlmjh_s[];
extern const gm_tmpl_piece gm_tplvm3qupt9dved_p[];
extern const gm_tmpl gm_tplvm52cg2hs7v4;
extern const uint32_t gm_tplvm52cg2hs7v4_i[];
extern const gm_tmpl_piece gm_tplvm52cg2hs7v4_p[];
extern const gm_spec *const gm_tplvm52cg2hs7v4_s[];
extern const gm_tmpl_piece gm_tplvm7e7lfhcheu_p[];
extern const gm_spec *const gm_tplvq03d5skib28_s[];
extern const gm_tmpl_piece gm_tplvv610d042tt6_p[];
extern const gm_tmpl gm_tplvvtt1jk959m0;
extern const uint32_t gm_tplvvtt1jk959m0_i[];
extern const gm_tmpl_piece gm_tplvvtt1jk959m0_p[];
extern const gm_spec *const gm_tplvvtt1jk959m0_s[];
extern const gm_type gm_tyiiln09e647;
const char gm_src8kcterbt0p7b[] = "compiler/fmt/classify.gm";
const char gm_src9gqs06mvt4n2[] = "compiler/pkg/build.gm";
const char gm_srce9tqfqu9u3el[] = "compiler/fmt/fmt.gm";
const char gm_srcg0ns322676oc[] = "compiler/load/manifest.gm";
const char gm_srcgs96ai7rj93j[] = "compiler/main.gm";
const char gm_srclv0dcbbfhves[] = "compiler/sema/sema.gm";
const char gm_srcoj9bno4fi1q4[] = "compiler/pkg/sync.gm";
const char gm_srcp2vt6rk8827h[] = "compiler/driver/paths.gm";
const char gm_srcq0uibesf3n7k[] = "lib/std/fiber/waiters/waiters.gm";
const char gm_srcrvqeupiu1766[] = "compiler/interp/place.gm";
const gm_spec *const gm_tpl05osb04qe3ad_s[] = {NULL};
const gm_spec *const gm_tpl0aluqdlmv333_s[] = {NULL, NULL};
const gm_spec *const gm_tpl0cgce8migcnp_s[] = {NULL};
const gm_spec *const gm_tpl0gp8c85b2lf5_s[] = {NULL};
const gm_spec *const gm_tpl0j5i33et5u62_s[] = {NULL};
const gm_spec *const gm_tpl0rhldcdavmcg_s[] = {NULL, NULL};
const gm_spec *const gm_tpl2aca96qot4p8_s[] = {NULL};
const gm_spec *const gm_tpl2fb8qlpeqlet_s[] = {NULL, NULL};
const gm_spec *const gm_tpl2fcbm03ka11j_s[] = {NULL, NULL};
const gm_spec *const gm_tpl2fh423sp4pd0_s[] = {NULL};
const gm_spec *const gm_tpl2s7fpgand9gc_s[] = {NULL};
const gm_spec *const gm_tpl328oas8hh4jg_s[] = {NULL, NULL};
const gm_spec *const gm_tpl36bd3ievounm_s[] = {NULL};
const gm_spec *const gm_tpl3c9lssf4k186_s[] = {NULL};
const gm_spec *const gm_tpl3kt8q9c3ih5v_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl3pvc6fn0aqps_s[] = {NULL};
const gm_spec *const gm_tpl3skngiao4gkl_s[] = {NULL};
const gm_spec *const gm_tpl409di1gnsfit_s[] = {NULL, NULL};
const gm_spec *const gm_tpl44b660pgg1c8_s[] = {NULL, NULL};
const gm_spec *const gm_tpl46u099ch4aa2_s[] = {NULL};
const gm_spec *const gm_tpl4bj6lgqq92ev_s[] = {NULL, NULL};
const gm_spec *const gm_tpl4t11fkqh738f_s[] = {NULL, NULL};
const gm_spec *const gm_tpl578lpoev0s9s_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl6lpvgmmqlalm_s[] = {NULL};
const gm_spec *const gm_tpl70tkged35ahl_s[] = {NULL, NULL};
const gm_spec *const gm_tpl77gpruiccsl2_s[] = {NULL};
const gm_spec *const gm_tpl7e5tr64a0u6e_s[] = {NULL};
const gm_spec *const gm_tpl7kmb2inm0gau_s[] = {NULL};
const gm_spec *const gm_tpl7oj2vfshts60_s[] = {NULL};
const gm_spec *const gm_tpl81uel28fst2a_s[] = {NULL, NULL};
const gm_spec *const gm_tpl86cip41cbf58_s[] = {NULL};
const gm_spec *const gm_tpl8mdj3nc4lpjv_s[] = {NULL};
const gm_spec *const gm_tpl953juq2iaklp_s[] = {NULL, NULL};
const gm_spec *const gm_tpl955dep3t38r6_s[] = {NULL};
const gm_spec *const gm_tpl9aakii48cq8t_s[] = {NULL, NULL};
const gm_spec *const gm_tpl9h9af6kus46t_s[] = {NULL, NULL};
const gm_spec *const gm_tpl9sifsarv7t06_s[] = {NULL};
const gm_spec *const gm_tplamm5t71udd8k_s[] = {NULL, NULL};
const gm_spec *const gm_tplaqps986okebj_s[] = {NULL, NULL};
const gm_spec *const gm_tplau4cbc1piuvn_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplblsk2k5t46dn_s[] = {NULL};
const gm_spec *const gm_tplbmf4neusqbo5_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplbppmjhpd8ol0_s[] = {NULL};
const gm_spec *const gm_tplbsca6gthghjf_s[] = {NULL};
const gm_spec *const gm_tplck20jih4okbk_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplcvnbkkkji94u_s[] = {NULL, NULL};
const gm_spec *const gm_tpldbe9bnjeqeqg_s[] = {NULL};
const gm_spec *const gm_tpldofkl5m8hqug_s[] = {NULL};
const gm_spec *const gm_tple23ed5kepfqi_s[] = {NULL};
const gm_spec *const gm_tple7ukme0qchh6_s[] = {&(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1}};
const gm_spec *const gm_tplf27d39e1vcer_s[] = {NULL, NULL};
const gm_spec *const gm_tplffkq5ue6h7rm_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplgr8ep9alj0ph_s[] = {NULL, NULL};
const gm_spec *const gm_tplgursfbit7uc3_s[] = {NULL, NULL};
const gm_spec *const gm_tplhdb29uo8rm6j_s[] = {NULL, NULL};
const gm_spec *const gm_tpli0tmonaamd45_s[] = {NULL};
const gm_spec *const gm_tpli9foo8ib8qrk_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplibgopbmor3p6_s[] = {NULL, NULL};
const gm_spec *const gm_tplipqn9urt53l3_s[] = {NULL};
const gm_spec *const gm_tpljg9idve5dgca_s[] = {NULL, NULL};
const gm_spec *const gm_tplk51k04l6756s_s[] = {NULL};
const gm_spec *const gm_tplkhlcq331nni9_s[] = {NULL, NULL};
const gm_spec *const gm_tplkp06s34ois53_s[] = {NULL, NULL};
const gm_spec *const gm_tplkv0rjh54m5gn_s[] = {NULL};
const gm_spec *const gm_tplmsd0sfgf2v66_s[] = {NULL, NULL};
const gm_spec *const gm_tplne0l0lciidvr_s[] = {NULL, NULL};
const gm_spec *const gm_tplnokap11rv9jr_s[] = {NULL};
const gm_spec *const gm_tplo8bto3ii8iq9_s[] = {NULL, NULL};
const gm_spec *const gm_tplolonhuuitsj2_s[] = {NULL};
const gm_spec *const gm_tplopnhfma5is23_s[] = {NULL, NULL};
const gm_spec *const gm_tplp98fsht4k706_s[] = {NULL};
const gm_spec *const gm_tplpaq33rl4cnb8_s[] = {NULL, NULL};
const gm_spec *const gm_tplpgeniqejbt43_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplpkq4i4j83tmk_s[] = {NULL};
const gm_spec *const gm_tplpnk24geo6qd8_s[] = {NULL, NULL};
const gm_spec *const gm_tplpvmvg9ce66cu_s[] = {NULL};
const gm_spec *const gm_tplr3ni3ai315ji_s[] = {NULL};
const gm_spec *const gm_tplr7vr7aubja28_s[] = {NULL};
const gm_spec *const gm_tplr976guo57cat_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplre5cfbnr7ltv_s[] = {NULL, NULL};
const gm_spec *const gm_tplrp7ukmgscavj_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplruf3ms5lqgst_s[] = {NULL};
const gm_spec *const gm_tpls4bcac20qnq6_s[] = {NULL};
const gm_spec *const gm_tpls5bjfpqmr7e0_s[] = {NULL};
const gm_spec *const gm_tplsil8jb9ng0ao_s[] = {NULL};
const gm_spec *const gm_tpltnu092iqmfdd_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplu2tisf3oaee8_s[] = {NULL};
const gm_spec *const gm_tplu76evoc63de2_s[] = {NULL, NULL};
const gm_spec *const gm_tpludgprff7jihr_s[] = {NULL};
const gm_spec *const gm_tpluetglub2muf2_s[] = {NULL};
const gm_spec *const gm_tpluh0hed6nj59d_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplulq1qidn3nog_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpluvp47iqcj4uv_s[] = {NULL};
const gm_spec *const gm_tplv2usruqglqv9_s[] = {NULL, NULL};
const gm_spec *const gm_tplvdft4iatcp17_s[] = {NULL};
const gm_spec *const gm_tplvdkp4jmav7jr_s[] = {NULL, NULL};
const gm_spec *const gm_tplvi7pmohm1ihu_s[] = {NULL};
const gm_spec *const gm_tplvk1pmcp9tr4p_s[] = {NULL};
const gm_spec *const gm_tplvkpfj3qq0aig_s[] = {NULL};
const gm_spec *const gm_tplvq03d5skib28_s[] = {NULL};
const gm_tmpl gm_tpl051t251mssfu = {1u, gm_tpl051t251mssfu_p, gm_tpl051t251mssfu_i, gm_tpl051t251mssfu_s};
const gm_tmpl gm_tpl083ga8282tol = {3u, gm_tpl083ga8282tol_p, gm_tpl083ga8282tol_i, gm_tpl083ga8282tol_s};
const gm_tmpl gm_tpl0e4rd6ag9tqa = {3u, gm_tpl0e4rd6ag9tqa_p, gm_tpl0e4rd6ag9tqa_i, gm_tpl0e4rd6ag9tqa_s};
const gm_tmpl gm_tpl0ej2jrvho6u0 = {1u, gm_tpl0ej2jrvho6u0_p, gm_tpl0ej2jrvho6u0_i, gm_tpl0ej2jrvho6u0_s};
const gm_tmpl gm_tpl0epjr4745p10 = {6u, gm_tpl0epjr4745p10_p, gm_tpl0epjr4745p10_i, gm_tpl0epjr4745p10_s};
const gm_tmpl gm_tpl0uott3s3afvp = {5u, gm_tpl0uott3s3afvp_p, gm_tpl0uott3s3afvp_i, gm_tpl0uott3s3afvp_s};
const gm_tmpl gm_tpl1a3usdkusvtq = {2u, gm_tpl1a3usdkusvtq_p, gm_tpl1a3usdkusvtq_i, gm_tpl1a3usdkusvtq_s};
const gm_tmpl gm_tpl1c0b1s109fa4 = {2u, gm_tpl1c0b1s109fa4_p, gm_tpl1c0b1s109fa4_i, gm_tpl1c0b1s109fa4_s};
const gm_tmpl gm_tpl1q6o13b940dg = {1u, gm_tpl1q6o13b940dg_p, gm_tpl1q6o13b940dg_i, gm_tpl1q6o13b940dg_s};
const gm_tmpl gm_tpl20t94jlvdndv = {1u, gm_tpl20t94jlvdndv_p, gm_tpl20t94jlvdndv_i, gm_tpl20t94jlvdndv_s};
const gm_tmpl gm_tpl21i1s6vvb3m4 = {1u, gm_tpl21i1s6vvb3m4_p, gm_tpl21i1s6vvb3m4_i, gm_tpl21i1s6vvb3m4_s};
const gm_tmpl gm_tpl2fh423sp4pd0 = {1u, gm_tpl2fh423sp4pd0_p, gm_tpl2fh423sp4pd0_i, gm_tpl2fh423sp4pd0_s};
const gm_tmpl gm_tpl2kgmgv2ueqo4 = {2u, gm_tpl2kgmgv2ueqo4_p, gm_tpl2kgmgv2ueqo4_i, gm_tpl2kgmgv2ueqo4_s};
const gm_tmpl gm_tpl3052vn0bjbcl = {2u, gm_tpl3052vn0bjbcl_p, gm_tpl3052vn0bjbcl_i, gm_tpl3052vn0bjbcl_s};
const gm_tmpl gm_tpl3c9lssf4k186 = {1u, gm_tpl3c9lssf4k186_p, gm_tpl3c9lssf4k186_i, gm_tpl3c9lssf4k186_s};
const gm_tmpl gm_tpl3dghmndqn708 = {1u, gm_tpl3dghmndqn708_p, gm_tpl3dghmndqn708_i, gm_tpl3dghmndqn708_s};
const gm_tmpl gm_tpl3l3u367o9nte = {1u, gm_tpl3l3u367o9nte_p, gm_tpl3l3u367o9nte_i, gm_tpl3l3u367o9nte_s};
const gm_tmpl gm_tpl3sv6aqbksrr5 = {1u, gm_tpl3sv6aqbksrr5_p, gm_tpl3sv6aqbksrr5_i, gm_tpl3sv6aqbksrr5_s};
const gm_tmpl gm_tpl4g1h0tr3apfi = {1u, gm_tpl4g1h0tr3apfi_p, gm_tpl4g1h0tr3apfi_i, gm_tpl4g1h0tr3apfi_s};
const gm_tmpl gm_tpl4ht4dp62es1n = {1u, gm_tpl4ht4dp62es1n_p, gm_tpl4ht4dp62es1n_i, gm_tpl4ht4dp62es1n_s};
const gm_tmpl gm_tpl4lt7abhi3ej4 = {1u, gm_tpl4lt7abhi3ej4_p, gm_tpl4lt7abhi3ej4_i, gm_tpl4lt7abhi3ej4_s};
const gm_tmpl gm_tpl4n3cnfr6vo96 = {1u, gm_tpl4n3cnfr6vo96_p, gm_tpl4n3cnfr6vo96_i, gm_tpl4n3cnfr6vo96_s};
const gm_tmpl gm_tpl578lpoev0s9s = {7u, gm_tpl578lpoev0s9s_p, gm_tpl578lpoev0s9s_i, gm_tpl578lpoev0s9s_s};
const gm_tmpl gm_tpl5i8vi7ph4hfi = {2u, gm_tpl5i8vi7ph4hfi_p, gm_tpl5i8vi7ph4hfi_i, gm_tpl5i8vi7ph4hfi_s};
const gm_tmpl gm_tpl5m2t8us2v800 = {2u, gm_tpl5m2t8us2v800_p, gm_tpl5m2t8us2v800_i, gm_tpl5m2t8us2v800_s};
const gm_tmpl gm_tpl5n5ddjmjlhjb = {4u, gm_tpl5n5ddjmjlhjb_p, gm_tpl5n5ddjmjlhjb_i, gm_tpl5n5ddjmjlhjb_s};
const gm_tmpl gm_tpl5rdj46lif6eq = {2u, gm_tpl5rdj46lif6eq_p, gm_tpl5rdj46lif6eq_i, gm_tpl5rdj46lif6eq_s};
const gm_tmpl gm_tpl6m8ee83kb2vt = {2u, gm_tpl6m8ee83kb2vt_p, gm_tpl6m8ee83kb2vt_i, gm_tpl6m8ee83kb2vt_s};
const gm_tmpl gm_tpl6q4b2p6tude6 = {1u, gm_tpl6q4b2p6tude6_p, gm_tpl6q4b2p6tude6_i, gm_tpl6q4b2p6tude6_s};
const gm_tmpl gm_tpl6salfcvi63n2 = {2u, gm_tpl6salfcvi63n2_p, gm_tpl6salfcvi63n2_i, gm_tpl6salfcvi63n2_s};
const gm_tmpl gm_tpl6skdtj6qlkmd = {1u, gm_tpl6skdtj6qlkmd_p, gm_tpl6skdtj6qlkmd_i, gm_tpl6skdtj6qlkmd_s};
const gm_tmpl gm_tpl6tnfk0d9dl8e = {1u, gm_tpl6tnfk0d9dl8e_p, gm_tpl6tnfk0d9dl8e_i, gm_tpl6tnfk0d9dl8e_s};
const gm_tmpl gm_tpl77hog51tnj6o = {1u, gm_tpl77hog51tnj6o_p, gm_tpl77hog51tnj6o_i, gm_tpl77hog51tnj6o_s};
const gm_tmpl gm_tpl7m3t7r6fqpf7 = {1u, gm_tpl7m3t7r6fqpf7_p, gm_tpl7m3t7r6fqpf7_i, gm_tpl7m3t7r6fqpf7_s};
const gm_tmpl gm_tpl7meurf74nbc1 = {1u, gm_tpl7meurf74nbc1_p, gm_tpl7meurf74nbc1_i, gm_tpl7meurf74nbc1_s};
const gm_tmpl gm_tpl81uel28fst2a = {2u, gm_tpl81uel28fst2a_p, gm_tpl81uel28fst2a_i, gm_tpl81uel28fst2a_s};
const gm_tmpl gm_tpl84r77qupuujj = {1u, gm_tpl84r77qupuujj_p, gm_tpl84r77qupuujj_i, gm_tpl84r77qupuujj_s};
const gm_tmpl gm_tpl8gj5o7rmo2fl = {4u, gm_tpl8gj5o7rmo2fl_p, gm_tpl8gj5o7rmo2fl_i, gm_tpl8gj5o7rmo2fl_s};
const gm_tmpl gm_tpl8lt5kipnmd64 = {3u, gm_tpl8lt5kipnmd64_p, gm_tpl8lt5kipnmd64_i, gm_tpl8lt5kipnmd64_s};
const gm_tmpl gm_tpl8ptl55ago227 = {1u, gm_tpl8ptl55ago227_p, gm_tpl8ptl55ago227_i, gm_tpl8ptl55ago227_s};
const gm_tmpl gm_tpl8pvdc7qbgge6 = {1u, gm_tpl8pvdc7qbgge6_p, gm_tpl8pvdc7qbgge6_i, gm_tpl8pvdc7qbgge6_s};
const gm_tmpl gm_tpl9j9ok1j4nn18 = {2u, gm_tpl9j9ok1j4nn18_p, gm_tpl9j9ok1j4nn18_i, gm_tpl9j9ok1j4nn18_s};
const gm_tmpl gm_tpl9qchhbq9qlbm = {2u, gm_tpl9qchhbq9qlbm_p, gm_tpl9qchhbq9qlbm_i, gm_tpl9qchhbq9qlbm_s};
const gm_tmpl gm_tpl9qg0hlqqke7e = {1u, gm_tpl9qg0hlqqke7e_p, gm_tpl9qg0hlqqke7e_i, gm_tpl9qg0hlqqke7e_s};
const gm_tmpl gm_tpl9sh9d989om94 = {1u, gm_tpl9sh9d989om94_p, gm_tpl9sh9d989om94_i, gm_tpl9sh9d989om94_s};
const gm_tmpl gm_tpla1i8rr6vfju7 = {1u, gm_tpla1i8rr6vfju7_p, gm_tpla1i8rr6vfju7_i, gm_tpla1i8rr6vfju7_s};
const gm_tmpl gm_tpla93ou7df923g = {3u, gm_tpla93ou7df923g_p, gm_tpla93ou7df923g_i, gm_tpla93ou7df923g_s};
const gm_tmpl gm_tplad88kt6677ij = {1u, gm_tplad88kt6677ij_p, gm_tplad88kt6677ij_i, gm_tplad88kt6677ij_s};
const gm_tmpl gm_tplalu8vhij0mma = {1u, gm_tplalu8vhij0mma_p, gm_tplalu8vhij0mma_i, gm_tplalu8vhij0mma_s};
const gm_tmpl gm_tplaoj5r4tauejk = {1u, gm_tplaoj5r4tauejk_p, gm_tplaoj5r4tauejk_i, gm_tplaoj5r4tauejk_s};
const gm_tmpl gm_tplau4cbc1piuvn = {3u, gm_tplau4cbc1piuvn_p, gm_tplau4cbc1piuvn_i, gm_tplau4cbc1piuvn_s};
const gm_tmpl gm_tplav8bdvvt8rem = {1u, gm_tplav8bdvvt8rem_p, gm_tplav8bdvvt8rem_i, gm_tplav8bdvvt8rem_s};
const gm_tmpl gm_tplba0kefu5gu2c = {1u, gm_tplba0kefu5gu2c_p, gm_tplba0kefu5gu2c_i, gm_tplba0kefu5gu2c_s};
const gm_tmpl gm_tplbahlus43pmvm = {2u, gm_tplbahlus43pmvm_p, gm_tplbahlus43pmvm_i, gm_tplbahlus43pmvm_s};
const gm_tmpl gm_tplbfdkthp45ai4 = {8u, gm_tplbfdkthp45ai4_p, gm_tplbfdkthp45ai4_i, gm_tplbfdkthp45ai4_s};
const gm_tmpl gm_tplblrluopde8aq = {3u, gm_tplblrluopde8aq_p, gm_tplblrluopde8aq_i, gm_tplblrluopde8aq_s};
const gm_tmpl gm_tplbnt4jnaihcvu = {1u, gm_tplbnt4jnaihcvu_p, gm_tplbnt4jnaihcvu_i, gm_tplbnt4jnaihcvu_s};
const gm_tmpl gm_tplbntllbe37f81 = {1u, gm_tplbntllbe37f81_p, gm_tplbntllbe37f81_i, gm_tplbntllbe37f81_s};
const gm_tmpl gm_tplbo9rmalpf2vj = {4u, gm_tplbo9rmalpf2vj_p, gm_tplbo9rmalpf2vj_i, gm_tplbo9rmalpf2vj_s};
const gm_tmpl gm_tplbog2sc0rf3ot = {1u, gm_tplbog2sc0rf3ot_p, gm_tplbog2sc0rf3ot_i, gm_tplbog2sc0rf3ot_s};
const gm_tmpl gm_tplc6hhdd7vcmvk = {1u, gm_tplc6hhdd7vcmvk_p, gm_tplc6hhdd7vcmvk_i, gm_tplc6hhdd7vcmvk_s};
const gm_tmpl gm_tplcddb08maq348 = {1u, gm_tplcddb08maq348_p, gm_tplcddb08maq348_i, gm_tplcddb08maq348_s};
const gm_tmpl gm_tplcef5l6cvp5lt = {2u, gm_tplcef5l6cvp5lt_p, gm_tplcef5l6cvp5lt_i, gm_tplcef5l6cvp5lt_s};
const gm_tmpl gm_tplcein5d5pq2cs = {2u, gm_tplcein5d5pq2cs_p, gm_tplcein5d5pq2cs_i, gm_tplcein5d5pq2cs_s};
const gm_tmpl gm_tplceqrbdtcd9p5 = {1u, gm_tplceqrbdtcd9p5_p, gm_tplceqrbdtcd9p5_i, gm_tplceqrbdtcd9p5_s};
const gm_tmpl gm_tplctspgtksm35f = {1u, gm_tplctspgtksm35f_p, gm_tplctspgtksm35f_i, gm_tplctspgtksm35f_s};
const gm_tmpl gm_tplcvnbkkkji94u = {2u, gm_tplcvnbkkkji94u_p, gm_tplcvnbkkkji94u_i, gm_tplcvnbkkkji94u_s};
const gm_tmpl gm_tpldlrsentitfg3 = {2u, gm_tpldlrsentitfg3_p, gm_tpldlrsentitfg3_i, gm_tpldlrsentitfg3_s};
const gm_tmpl gm_tpldofkl5m8hqug = {1u, gm_tpldofkl5m8hqug_p, gm_tpldofkl5m8hqug_i, gm_tpldofkl5m8hqug_s};
const gm_tmpl gm_tpldrlq7uo0cbrm = {1u, gm_tpldrlq7uo0cbrm_p, gm_tpldrlq7uo0cbrm_i, gm_tpldrlq7uo0cbrm_s};
const gm_tmpl gm_tpldti7tgsj3l9q = {2u, gm_tpldti7tgsj3l9q_p, gm_tpldti7tgsj3l9q_i, gm_tpldti7tgsj3l9q_s};
const gm_tmpl gm_tple4upi9cflnq6 = {4u, gm_tple4upi9cflnq6_p, gm_tple4upi9cflnq6_i, gm_tple4upi9cflnq6_s};
const gm_tmpl gm_tplec2d6tt24j0q = {1u, gm_tplec2d6tt24j0q_p, gm_tplec2d6tt24j0q_i, gm_tplec2d6tt24j0q_s};
const gm_tmpl gm_tplegelorakvcn7 = {1u, gm_tplegelorakvcn7_p, gm_tplegelorakvcn7_i, gm_tplegelorakvcn7_s};
const gm_tmpl gm_tplemu1bmibr9ve = {1u, gm_tplemu1bmibr9ve_p, gm_tplemu1bmibr9ve_i, gm_tplemu1bmibr9ve_s};
const gm_tmpl gm_tplevafblg3gmq9 = {3u, gm_tplevafblg3gmq9_p, gm_tplevafblg3gmq9_i, gm_tplevafblg3gmq9_s};
const gm_tmpl gm_tplf3e41f3siie3 = {3u, gm_tplf3e41f3siie3_p, gm_tplf3e41f3siie3_i, gm_tplf3e41f3siie3_s};
const gm_tmpl gm_tplf9q60in6m019 = {2u, gm_tplf9q60in6m019_p, gm_tplf9q60in6m019_i, gm_tplf9q60in6m019_s};
const gm_tmpl gm_tplfsmasr3tgalo = {2u, gm_tplfsmasr3tgalo_p, gm_tplfsmasr3tgalo_i, gm_tplfsmasr3tgalo_s};
const gm_tmpl gm_tplg05c24rh8peq = {1u, gm_tplg05c24rh8peq_p, gm_tplg05c24rh8peq_i, gm_tplg05c24rh8peq_s};
const gm_tmpl gm_tplggs57b0p2or4 = {1u, gm_tplggs57b0p2or4_p, gm_tplggs57b0p2or4_i, gm_tplggs57b0p2or4_s};
const gm_tmpl gm_tplgja2nber62dc = {1u, gm_tplgja2nber62dc_p, gm_tplgja2nber62dc_i, gm_tplgja2nber62dc_s};
const gm_tmpl gm_tplgjeridtvkml4 = {1u, gm_tplgjeridtvkml4_p, gm_tplgjeridtvkml4_i, gm_tplgjeridtvkml4_s};
const gm_tmpl gm_tplgs2l60s65hm3 = {1u, gm_tplgs2l60s65hm3_p, gm_tplgs2l60s65hm3_i, gm_tplgs2l60s65hm3_s};
const gm_tmpl gm_tplh5ff53j0j0d2 = {1u, gm_tplh5ff53j0j0d2_p, gm_tplh5ff53j0j0d2_i, gm_tplh5ff53j0j0d2_s};
const gm_tmpl gm_tplho1kg74nlm64 = {1u, gm_tplho1kg74nlm64_p, gm_tplho1kg74nlm64_i, gm_tplho1kg74nlm64_s};
const gm_tmpl gm_tpli08vvf1bckj4 = {3u, gm_tpli08vvf1bckj4_p, gm_tpli08vvf1bckj4_i, gm_tpli08vvf1bckj4_s};
const gm_tmpl gm_tplicg84n3os024 = {2u, gm_tplicg84n3os024_p, gm_tplicg84n3os024_i, gm_tplicg84n3os024_s};
const gm_tmpl gm_tplilne6f8eq90i = {1u, gm_tplilne6f8eq90i_p, gm_tplilne6f8eq90i_i, gm_tplilne6f8eq90i_s};
const gm_tmpl gm_tplisurri6t3aun = {2u, gm_tplisurri6t3aun_p, gm_tplisurri6t3aun_i, gm_tplisurri6t3aun_s};
const gm_tmpl gm_tplivckk5jvcluc = {1u, gm_tplivckk5jvcluc_p, gm_tplivckk5jvcluc_i, gm_tplivckk5jvcluc_s};
const gm_tmpl gm_tplj6imuu9dopvn = {5u, gm_tplj6imuu9dopvn_p, gm_tplj6imuu9dopvn_i, gm_tplj6imuu9dopvn_s};
const gm_tmpl gm_tpljbco1er3agg5 = {3u, gm_tpljbco1er3agg5_p, gm_tpljbco1er3agg5_i, gm_tpljbco1er3agg5_s};
const gm_tmpl gm_tpljc7kqi31f4d2 = {1u, gm_tpljc7kqi31f4d2_p, gm_tpljc7kqi31f4d2_i, gm_tpljc7kqi31f4d2_s};
const gm_tmpl gm_tpljueetjft1ril = {2u, gm_tpljueetjft1ril_p, gm_tpljueetjft1ril_i, gm_tpljueetjft1ril_s};
const gm_tmpl gm_tplk51k04l6756s = {1u, gm_tplk51k04l6756s_p, gm_tplk51k04l6756s_i, gm_tplk51k04l6756s_s};
const gm_tmpl gm_tplk66j4h09rih6 = {1u, gm_tplk66j4h09rih6_p, gm_tplk66j4h09rih6_i, gm_tplk66j4h09rih6_s};
const gm_tmpl gm_tplkd07482m1uv1 = {2u, gm_tplkd07482m1uv1_p, gm_tplkd07482m1uv1_i, gm_tplkd07482m1uv1_s};
const gm_tmpl gm_tplku5ga9b4h0i9 = {1u, gm_tplku5ga9b4h0i9_p, gm_tplku5ga9b4h0i9_i, gm_tplku5ga9b4h0i9_s};
const gm_tmpl gm_tpll1i6ur15ms5i = {2u, gm_tpll1i6ur15ms5i_p, gm_tpll1i6ur15ms5i_i, gm_tpll1i6ur15ms5i_s};
const gm_tmpl gm_tpll3bcqsckmobi = {1u, gm_tpll3bcqsckmobi_p, gm_tpll3bcqsckmobi_i, gm_tpll3bcqsckmobi_s};
const gm_tmpl gm_tpllfhth9mee4q8 = {2u, gm_tpllfhth9mee4q8_p, gm_tpllfhth9mee4q8_i, gm_tpllfhth9mee4q8_s};
const gm_tmpl gm_tpllihoeldkkkhv = {1u, gm_tpllihoeldkkkhv_p, gm_tpllihoeldkkkhv_i, gm_tpllihoeldkkkhv_s};
const gm_tmpl gm_tpllm484u6fuiq3 = {1u, gm_tpllm484u6fuiq3_p, gm_tpllm484u6fuiq3_i, gm_tpllm484u6fuiq3_s};
const gm_tmpl gm_tplm3dvc32djhr7 = {1u, gm_tplm3dvc32djhr7_p, gm_tplm3dvc32djhr7_i, gm_tplm3dvc32djhr7_s};
const gm_tmpl gm_tplme3pul5hs3bf = {1u, gm_tplme3pul5hs3bf_p, gm_tplme3pul5hs3bf_i, gm_tplme3pul5hs3bf_s};
const gm_tmpl gm_tplmk3amvcsk2gu = {2u, gm_tplmk3amvcsk2gu_p, gm_tplmk3amvcsk2gu_i, gm_tplmk3amvcsk2gu_s};
const gm_tmpl gm_tplmsb1klg4n6ap = {1u, gm_tplmsb1klg4n6ap_p, gm_tplmsb1klg4n6ap_i, gm_tplmsb1klg4n6ap_s};
const gm_tmpl gm_tpln8ctpo9hror7 = {1u, gm_tpln8ctpo9hror7_p, gm_tpln8ctpo9hror7_i, gm_tpln8ctpo9hror7_s};
const gm_tmpl gm_tplnh359qrng71h = {2u, gm_tplnh359qrng71h_p, gm_tplnh359qrng71h_i, gm_tplnh359qrng71h_s};
const gm_tmpl gm_tplo2l8dmp437h2 = {1u, gm_tplo2l8dmp437h2_p, gm_tplo2l8dmp437h2_i, gm_tplo2l8dmp437h2_s};
const gm_tmpl gm_tploc41q7b5gurn = {3u, gm_tploc41q7b5gurn_p, gm_tploc41q7b5gurn_i, gm_tploc41q7b5gurn_s};
const gm_tmpl gm_tploj0vopi0n43l = {1u, gm_tploj0vopi0n43l_p, gm_tploj0vopi0n43l_i, gm_tploj0vopi0n43l_s};
const gm_tmpl gm_tploui9f30uh460 = {1u, gm_tploui9f30uh460_p, gm_tploui9f30uh460_i, gm_tploui9f30uh460_s};
const gm_tmpl gm_tplp7gdl2lerimh = {1u, gm_tplp7gdl2lerimh_p, gm_tplp7gdl2lerimh_i, gm_tplp7gdl2lerimh_s};
const gm_tmpl gm_tplpntqck8i8f5b = {1u, gm_tplpntqck8i8f5b_p, gm_tplpntqck8i8f5b_i, gm_tplpntqck8i8f5b_s};
const gm_tmpl gm_tplpoe8f3eln4m2 = {1u, gm_tplpoe8f3eln4m2_p, gm_tplpoe8f3eln4m2_i, gm_tplpoe8f3eln4m2_s};
const gm_tmpl gm_tplqbbl1qig0e0s = {2u, gm_tplqbbl1qig0e0s_p, gm_tplqbbl1qig0e0s_i, gm_tplqbbl1qig0e0s_s};
const gm_tmpl gm_tplqj931pe0994k = {4u, gm_tplqj931pe0994k_p, gm_tplqj931pe0994k_i, gm_tplqj931pe0994k_s};
const gm_tmpl gm_tplql7vfevun5ae = {1u, gm_tplql7vfevun5ae_p, gm_tplql7vfevun5ae_i, gm_tplql7vfevun5ae_s};
const gm_tmpl gm_tplqtueduh1r3n9 = {4u, gm_tplqtueduh1r3n9_p, gm_tplqtueduh1r3n9_i, gm_tplqtueduh1r3n9_s};
const gm_tmpl gm_tplr2tuff1mvdst = {1u, gm_tplr2tuff1mvdst_p, gm_tplr2tuff1mvdst_i, gm_tplr2tuff1mvdst_s};
const gm_tmpl gm_tplr35nagqnpmad = {1u, gm_tplr35nagqnpmad_p, gm_tplr35nagqnpmad_i, gm_tplr35nagqnpmad_s};
const gm_tmpl gm_tplr3ni3ai315ji = {1u, gm_tplr3ni3ai315ji_p, gm_tplr3ni3ai315ji_i, gm_tplr3ni3ai315ji_s};
const gm_tmpl gm_tplrfab517u4o97 = {1u, gm_tplrfab517u4o97_p, gm_tplrfab517u4o97_i, gm_tplrfab517u4o97_s};
const gm_tmpl gm_tplrspdrqd1dpc8 = {2u, gm_tplrspdrqd1dpc8_p, gm_tplrspdrqd1dpc8_i, gm_tplrspdrqd1dpc8_s};
const gm_tmpl gm_tpls6f1h1hgtqgb = {1u, gm_tpls6f1h1hgtqgb_p, gm_tpls6f1h1hgtqgb_i, gm_tpls6f1h1hgtqgb_s};
const gm_tmpl gm_tpls8octb03drbp = {1u, gm_tpls8octb03drbp_p, gm_tpls8octb03drbp_i, gm_tpls8octb03drbp_s};
const gm_tmpl gm_tplsjs5e78h34ng = {1u, gm_tplsjs5e78h34ng_p, gm_tplsjs5e78h34ng_i, gm_tplsjs5e78h34ng_s};
const gm_tmpl gm_tplska2t1f86rr8 = {2u, gm_tplska2t1f86rr8_p, gm_tplska2t1f86rr8_i, gm_tplska2t1f86rr8_s};
const gm_tmpl gm_tplsqibbtt8789o = {1u, gm_tplsqibbtt8789o_p, gm_tplsqibbtt8789o_i, gm_tplsqibbtt8789o_s};
const gm_tmpl gm_tplt0qsjg06adbb = {2u, gm_tplt0qsjg06adbb_p, gm_tplt0qsjg06adbb_i, gm_tplt0qsjg06adbb_s};
const gm_tmpl gm_tplt6nq68u4co32 = {3u, gm_tplt6nq68u4co32_p, gm_tplt6nq68u4co32_i, gm_tplt6nq68u4co32_s};
const gm_tmpl gm_tplthkeg7jngn6q = {1u, gm_tplthkeg7jngn6q_p, gm_tplthkeg7jngn6q_i, gm_tplthkeg7jngn6q_s};
const gm_tmpl gm_tpltt9gd20e1v7c = {1u, gm_tpltt9gd20e1v7c_p, gm_tpltt9gd20e1v7c_i, gm_tpltt9gd20e1v7c_s};
const gm_tmpl gm_tplu2tisf3oaee8 = {1u, gm_tplu2tisf3oaee8_p, gm_tplu2tisf3oaee8_i, gm_tplu2tisf3oaee8_s};
const gm_tmpl gm_tplu7gq2cm8aanh = {1u, gm_tplu7gq2cm8aanh_p, gm_tplu7gq2cm8aanh_i, gm_tplu7gq2cm8aanh_s};
const gm_tmpl gm_tpluduaa40ad285 = {1u, gm_tpluduaa40ad285_p, gm_tpluduaa40ad285_i, gm_tpluduaa40ad285_s};
const gm_tmpl gm_tplumuahjb5m8m8 = {2u, gm_tplumuahjb5m8m8_p, gm_tplumuahjb5m8m8_i, gm_tplumuahjb5m8m8_s};
const gm_tmpl gm_tpluue9jh13g9df = {3u, gm_tpluue9jh13g9df_p, gm_tpluue9jh13g9df_i, gm_tpluue9jh13g9df_s};
const gm_tmpl gm_tplv8s8fgl2lggu = {1u, gm_tplv8s8fgl2lggu_p, gm_tplv8s8fgl2lggu_i, gm_tplv8s8fgl2lggu_s};
const gm_tmpl gm_tplvhp4k8a2f5k6 = {2u, gm_tplvhp4k8a2f5k6_p, gm_tplvhp4k8a2f5k6_i, gm_tplvhp4k8a2f5k6_s};
const gm_tmpl gm_tplvle6f1rmlmjh = {1u, gm_tplvle6f1rmlmjh_p, gm_tplvle6f1rmlmjh_i, gm_tplvle6f1rmlmjh_s};
const gm_tmpl gm_tplvm52cg2hs7v4 = {3u, gm_tplvm52cg2hs7v4_p, gm_tplvm52cg2hs7v4_i, gm_tplvm52cg2hs7v4_s};
const gm_tmpl gm_tplvvtt1jk959m0 = {1u, gm_tplvvtt1jk959m0_p, gm_tplvvtt1jk959m0_i, gm_tplvvtt1jk959m0_s};
const gm_tmpl_piece gm_tpl00nmcu166crh_p[] = {{gm_slpekm89q5lb3, 28}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl0lbbjeb2vhti_p[] = {{gm_sr4utq83odb88, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl0o9c14r51hfo_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sfdbfrv46lihd, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl0qht8d5me9ih_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_smooaouqmm8dj, 1}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpl0stjtt2gdn50_p[] = {{gm_s74ohph6l9htf, 20}, {gm_s0jm0vg43i745, 10}};
const gm_tmpl_piece gm_tpl12e8u6hvkejb_p[] = {{gm_stmsadvf54k73, 2}, {gm_ssnbkpbck3vbo, 21}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl13c887f2o944_p[] = {{gm_sgb7sja94ppju, 25}, {gm_sj1gcl0qo4i9p, 6}};
const gm_tmpl_piece gm_tpl1hek9uhvdtmo_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_s3p0rbs9hc3lg, 1}, {gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl2b904kc90480_p[] = {{gm_sc9p2e0c29ni7, 28}, {gm_s959ni36b80a4, 3}};
const gm_tmpl_piece gm_tpl2cg0ghfdkfbm_p[] = {{gm_siiqktevj2s2h, 24}, {gm_s7t1ot7rgnuch, 10}, {gm_s7c48l6el9tbd, 11}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl2egb012obd94_p[] = {{gm_s6k5k2kcfu4ll, 9}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl2u97uspakqd9_p[] = {{gm_sh40na9qbga3l, 1}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s2cuflhqkkuaq, 1}};
const gm_tmpl_piece gm_tpl36bd3ievounm_p[] = {{gm_s5s1m7053mh3n, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl3skngiao4gkl_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s5ppm345dr9s1, 14}};
const gm_tmpl_piece gm_tpl48fg75b976dc_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_ss9fj8tanu5jn, 50}};
const gm_tmpl_piece gm_tpl570sljg0ivt5_p[] = {{gm_smooaouqmm8dj, 1}, {gm_srv4dcfccuset, 2}, {gm_sujpen8u5kmon, 9}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl5c6n9e2iidjc_p[] = {{gm_sns1hl83hdlcn, 4}, {gm_s5ea2jdgum6o9, 32}, {gm_scq06n33arhgv, 4}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpl5igv3ckbutrp_p[] = {{gm_sqfe8a5cd6b60, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl5m0ku1mi16i0_p[] = {{gm_sa525m041ir3n, 16}, {gm_sumvvvaqn43sl, 9}, {gm_sb6c4qgs6rpfi, 7}};
const gm_tmpl_piece gm_tpl5q4rr3nd9637_p[] = {{gm_sua4hqf3te7mn, 14}, {gm_s2vmmrs8i9iqs, 3}};
const gm_tmpl_piece gm_tpl5s6q12qjusi2_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sl3q52ep6aa53, 1}, {gm_s2urait735ofq, 4}, {gm_smooaouqmm8dj, 1}, {gm_s1asl1e1tvb0k, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl5st0uuf0quab_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sglu5epjv38rl, 91}};
const gm_tmpl_piece gm_tpl65nnuqtjd24o_p[] = {{gm_s88lqrfvhc8i3, 45}, {gm_sq02q1bjdgrej, 122}};
const gm_tmpl_piece gm_tpl6gc9e2atg845_p[] = {{gm_smooaouqmm8dj, 1}, {gm_svhavo13qha5k, 3}, {gm_s1asl1e1tvb0k, 1}, {gm_s3p0rbs9hc3lg, 1}, {gm_sqahda31evi4f, 8}, {gm_stc3e0j0moc2h, 4}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tpl6md5hu7rmp82_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_son8q7pl3pv8g, 71}};
const gm_tmpl_piece gm_tpl6ptbf3l6su1b_p[] = {{gm_sm3sqa0e3amks, 8}, {gm_siscc5ella8ma, 3}};
const gm_tmpl_piece gm_tpl74mr52kbqst5_p[] = {{gm_srnn7edb2gm48, 41}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl7kdhh1egc9gc_p[] = {{gm_sfrrtib86oiig, 22}, {gm_snset5tb694as, 14}, {gm_spajg3q27hr58, 14}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl7ll3166bkl17_p[] = {{gm_s5lqhiu2bn6of, 7}, {gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl896mu4np77il_p[] = {{gm_s16mf4cvajh59, 12}, {gm_stkqau5o4elnq, 14}};
const gm_tmpl_piece gm_tpl8geisveh2vug_p[] = {{gm_smovt8m5cpa13, 6}, {gm_sosa5l0k6d34c, 14}, {gm_s6qvm08vrap69, 3}, {gm_snbihmvq8hfip, 28}, {gm_sg5gdnrith2i6, 2}, {gm_s5epronuu4280, 6}, {gm_s84hftqv0533g, 4}, {gm_s6ab2s9s2fif9, 8}, {gm_stc3e0j0moc2h, 4}, {gm_sb398tg9q30m5, 5}, {gm_suidg89j7tg2d, 4}};
const gm_tmpl_piece gm_tpl953juq2iaklp_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sem2htac3j4np, 65}, {gm_sgcporqt2gqqg, 15}};
const gm_tmpl_piece gm_tpl98as72l6ukas_p[] = {{gm_s1ishc3mvan41, 30}, {gm_s64s5a4ht3148, 68}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl9d5e93nnc8t1_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sqjomq37io03j, 150}};
const gm_tmpl_piece gm_tpl9dqq7pjdmqnl_p[] = {{gm_sjpvc4ek9dber, 38}, {gm_s5hr2c7nkokp9, 69}};
const gm_tmpl_piece gm_tpl9gmvk2gm4l5h_p[] = {{gm_so3bl4q7kp6gl, 20}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl9ljuo8s7738q_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_stmdki8kna56a, 23}};
const gm_tmpl_piece gm_tpla6sajpp07b9l_p[] = {{gm_s5pra70i8o734, 13}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpla74als5inv2i_p[] = {{gm_s8eg138piktj1, 11}, {gm_sfm8ps2e26dh1, 2}};
const gm_tmpl_piece gm_tplahe50smef36f_p[] = {{gm_shrtuk3jka1au, 15}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_snr5o5tak6405, 3}};
const gm_tmpl_piece gm_tplav4294nbj9c7_p[] = {{gm_sicq1bvp13t5d, 22}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplb9i7jfun8rvg_p[] = {{gm_s654e61ni0n2t, 66}, {gm_spbhhmdbp335u, 3}};
const gm_tmpl_piece gm_tplbahlus43pmvm_p[] = {{gm_s4m36jv5rcfat, 12}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplbaonrrpbr1uh_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sjbtfuea8qauf, 22}};
const gm_tmpl_piece gm_tplbathm90qd36n_p[] = {{gm_siamquc19uikr, 1}, {gm_s69k18vq2gid8, 11}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplcttkqubjcaqf_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sdkah956ljhi9, 18}};
const gm_tmpl_piece gm_tpld4u3te7g9c5v_p[] = {{gm_sk2jb5rv3689k, 33}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpldunbbu9dpisn_p[] = {{gm_s9tjtf1373ti5, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpldvh3e1jcen1u_p[] = {{gm_so9fi6r28770a, 2}, {gm_s1asl1e1tvb0k, 1}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tple4upi9cflnq6_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_smrba58613c3b, 16}, {gm_sg71mqv9mtu78, 32}, {gm_s0m0qaokk51g0, 3}, {gm_s959ni36b80a4, 3}};
const gm_tmpl_piece gm_tple7qj0fr2tlub_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_si82ti7ep94be, 10}};
const gm_tmpl_piece gm_tpled61een8fpfm_p[] = {{gm_sqjh8kseu4lti, 5}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplehc5f52ftol1_p[] = {{gm_sl3q52ep6aa53, 1}, {gm_smooaouqmm8dj, 1}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplellk9btkq1f2_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sas87vff32eaf, 42}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplep4rfqi7to9g_p[] = {{gm_sns1hl83hdlcn, 4}, {gm_s66b184memqnd, 3}, {gm_sql0in48r61nd, 4}, {gm_s3kfdt7s6c0hi, 14}, {gm_sf0f3lvsd76jf, 16}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplet6obcbkjbht_p[] = {{gm_s1g69u68g0ra5, 2}, {gm_s4ifh5kc7nc0d, 61}};
const gm_tmpl_piece gm_tplf8er2klne616_p[] = {{gm_s2umangg4hn1g, 14}, {gm_sg5h7btla5ksa, 28}};
const gm_tmpl_piece gm_tplff85tslj1ph5_p[] = {{gm_ss5c184rv03vi, 17}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplfoosl93e8k5t_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplftg0h8omro11_p[] = {{gm_sa70lf0p32gn5, 27}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplg3ifbt1a32e1_p[] = {{gm_sm55k23067j17, 15}, {gm_s24s9g8r9gc0v, 6}, {gm_s23t712hkqlmp, 4}, {gm_sp598n426d61b, 39}};
const gm_tmpl_piece gm_tplg7bh9jeid72i_p[] = {{gm_sj9t4d2ku5m56, 46}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplgclcdp539fpt_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s6f83og3gefc1, 7}, {gm_s2053hhvf16fm, 1}};
const gm_tmpl_piece gm_tplge1jgdcvfbq1_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_su635cbqj0rpd, 2}};
const gm_tmpl_piece gm_tplgniq1bprrgpo_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sboedjgihdapp, 80}, {gm_scv8i3jrgh2rk, 64}, {gm_sq05ai6ts8qlk, 16}};
const gm_tmpl_piece gm_tplhr6emralh8q6_p[] = {{gm_sbg28ee7l5gjs, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplhv087vo2q53g_p[] = {{gm_s7gn2sla7drif, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplicg84n3os024_p[] = {{gm_sd3a17sd7gp5u, 8}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplj6bgc315nev3_p[] = {{gm_sclhssivsh27a, 36}, {gm_s09jc0n36v9gm, 96}};
const gm_tmpl_piece gm_tplj92mk62u70db_p[] = {{gm_sk175hsmu7b99, 21}, {gm_sjehit11lds9h, 33}};
const gm_tmpl_piece gm_tpljdh6phei25vf_p[] = {{gm_sl3acq1nje8kk, 38}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplk1a5101vtct2_p[] = {{gm_s3p0rbs9hc3lg, 1}, {gm_svt8aldhoetro, 2}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tplksrq9kcn68vt_p[] = {{gm_sc7bkbe71it8c, 9}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpll4f2ihchoe9c_p[] = {{gm_sv5vs7371a92i, 17}, {gm_sefm4vafv1a3l, 2}, {gm_sq0bes3qstkub, 14}};
const gm_tmpl_piece gm_tpllcp8fo2sqc1r_p[] = {{gm_sl7doakbaoimv, 6}, {gm_s7f8crhgefl4u, 27}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpllfgb42aufjqd_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_snsu3jrv1dbtj, 31}};
const gm_tmpl_piece gm_tpllg65m2i6jvjk_p[] = {{gm_s2u0i5o1ej1mg, 9}, {gm_sd2a8ckkfb013, 25}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplloue1uueuiig_p[] = {{gm_s9uc7or8diipg, 34}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplm0bsttjtt8b0_p[] = {{gm_sfs5rrntfu3mi, 24}, {gm_slmas8vbhthkn, 19}, {gm_smbgfcmcr0r8n, 10}};
const gm_tmpl_piece gm_tplm3r2aiholmt8_p[] = {{gm_sds1asc07m30k, 15}, {gm_scfppf8naer38, 20}, {gm_sand7bpicpp6t, 25}};
const gm_tmpl_piece gm_tplm5g4n5l4k6ck_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s29jv7rttsnpq, 51}};
const gm_tmpl_piece gm_tplm61d8mlvmepm_p[] = {{gm_smrahl8ppkcu9, 33}, {gm_sni9iu6vnvsjk, 43}};
const gm_tmpl_piece gm_tplmebto0o1gmln_p[] = {{gm_slmeqnp27emg7, 8}, {gm_slnqcu8l15gll, 11}, {gm_st35tcurhvabs, 30}, {gm_sgdrhdkbtd5gr, 18}, {gm_scold1pf1iq8n, 10}, {gm_sj4nhb5mobdan, 36}, {gm_sa16a135q4am6, 12}, {gm_sl7pe4skltkbu, 8}, {gm_s31g9ftlb6lgj, 10}, {gm_saempncqkf91r, 2}};
const gm_tmpl_piece gm_tplmo458jpb0n1e_p[] = {{gm_sim03rp8pjg4e, 43}, {gm_s967vts9f19o3, 10}, {gm_stbjrsoqc456r, 11}, {gm_sqjlosotosjfp, 17}};
const gm_tmpl_piece gm_tplmpsv5qagthkk_p[] = {{gm_s8hv5sfsqarrt, 24}, {gm_s9a8dkouqsfgp, 8}};
const gm_tmpl_piece gm_tpln4k7ltoriuv9_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_slrd70lo3b6iq, 14}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplnc4en2s3t0da_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s2d9sjnr88ud8, 15}, {gm_stuutd7nfq21h, 2}, {gm_sao9h06bhkdja, 25}};
const gm_tmpl_piece gm_tplnr2qhie4t5ta_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sgh0dvu15b9ta, 9}, {gm_sg5gdnrith2i6, 2}, {gm_shrelr880pj0m, 6}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplo4cs7gli8ts2_p[] = {{gm_s03c24qg7i06q, 12}, {gm_s7c8ep4trp7bk, 6}, {gm_sg4nlara1m052, 13}, {gm_suevj69t71tjc, 34}};
const gm_tmpl_piece gm_tplod6iso1fh3co_p[] = {{gm_sauf6ibu7mnba, 4}, {gm_s7c9tkfjfc169, 25}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplotas2madniqp_p[] = {{gm_ssuuud2f0uns5, 8}, {gm_s5m9qimbv7e8k, 2}};
const gm_tmpl_piece gm_tplpl5slbegbu93_p[] = {{gm_se8v3b7tfvt0e, 22}, {gm_sh9ju7u678u0l, 27}};
const gm_tmpl_piece gm_tplpv6nab1nov2l_p[] = {{gm_smooaouqmm8dj, 1}, {gm_s87e746eia9j4, 5}};
const gm_tmpl_piece gm_tplq0mvos8lisnf_p[] = {{gm_sg5o5gf2u71g7, 8}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplq54tg5m6aueh_p[] = {{gm_sst2khrvr4nus, 9}, {gm_s3sb6rrf98gpi, 19}};
const gm_tmpl_piece gm_tplqbbl1qig0e0s_p[] = {{gm_sa8neoommnpov, 23}, {gm_sf0p9o7g7fr6l, 6}, {gm_saempncqkf91r, 2}};
const gm_tmpl_piece gm_tplql7vfevun5ae_p[] = {{gm_s4lj2bemc87jp, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplqstpsk3jkk1c_p[] = {{gm_sdjk9c005ec9t, 12}, {gm_s7nnecs6i9eki, 40}};
const gm_tmpl_piece gm_tplr976guo57cat_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s7hc5dru68qbt, 14}, {gm_se0plntg0il3g, 13}, {gm_s985n04e0mov8, 15}, {gm_s8hhhdlan8pqr, 7}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tplrtgv8em132rl_p[] = {{gm_s34hhp1ucrags, 11}, {gm_shi7nidbc64qf, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpls5bjfpqmr7e0_p[] = {{gm_sjmhgc2b6rnks, 9}, {gm_sg5gdnrith2i6, 2}};
const gm_tmpl_piece gm_tpls80s4u4ujepk_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_spm33surstj1l, 7}, {gm_sf9regh1oaao3, 13}, {gm_s81trlqpvp2do, 10}, {gm_sg5gdnrith2i6, 2}, {gm_sjcm18p35o9rq, 3}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplsk9b8o9jgshf_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sb11lrfd85gub, 13}, {gm_sdfa87va44k3m, 23}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplslqhr3ac3ilc_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_stkc6efbvodm1, 13}};
const gm_tmpl_piece gm_tplt1si3io9mdpe_p[] = {{gm_stmsadvf54k73, 2}, {gm_sjnv3p8t49clu, 8}, {gm_sse5id8rmv069, 7}, {gm_sm4ujt9jgj9au, 7}, {gm_sjfqm9gvu1om9, 7}};
const gm_tmpl_piece gm_tplt3d4sltsd44m_p[] = {{gm_sf5brl3u7mr5h, 6}, {gm_sg5gdnrith2i6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplt5ks894658kl_p[] = {{gm_sahd19pngn4vn, 13}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplt68r4kihke54_p[] = {{gm_sbhq27eoeitmb, 23}, {gm_sabhgcltm0thi, 7}, {gm_sfg8mkf2826qa, 4}, {gm_sgb12qbh3gevi, 4}};
const gm_tmpl_piece gm_tplthkeg7jngn6q_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s8r83g99aqvfj, 51}};
const gm_tmpl_piece gm_tplubjng587610o_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplud75n1k6o39i_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sovn1qud9dmh0, 69}};
const gm_tmpl_piece gm_tpluetglub2muf2_p[] = {{gm_svsf0pu3t8u6m, 6}, {gm_sqcjuf1r2epl2, 6}};
const gm_tmpl_piece gm_tplugmq0iksatjg_p[] = {{gm_s7s5thcg1q6e2, 25}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpluuksm24afvp0_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sbbucsktpih4c, 43}};
const gm_tmpl_piece gm_tplv9sqjp27m76e_p[] = {{gm_s5kue8bfcvqfr, 26}, {gm_sbpf3hgo18oq5, 51}, {gm_sjnkguo0atpm5, 2}};
const gm_tmpl_piece gm_tplvdf3kbq8rjbi_p[] = {{gm_svkk03nujpn4k, 7}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplvf2bf6hftf4i_p[] = {{gm_se8cpd8s09lkc, 12}, {gm_s221m8qkvkf71, 22}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplvm3qupt9dved_p[] = {{gm_s62k5o20indcj, 11}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplvm7e7lfhcheu_p[] = {{gm_stpdu6olnph3l, 32}, {gm_sr04t1tcf6jsd, 5}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplvv610d042tt6_p[] = {{gm_sauf6ibu7mnba, 4}, {gm_s3e9133b3pasn, 2}, {gm_sj6enltl2qi3c, 20}, {gm_sr4qbrk6tlgaa, 0}};
const gm_type gm_tyiiln09e647 = {.size = 16, .align = 16, .kind = GM_K_INT, .bits = 128, .is_signed = true};
const gm_type gm_tyiiln09e647;
const uint32_t gm_tpl00cra9g0k45s_i[] = {0u, 1u};
const uint32_t gm_tpl0e782j7cr191_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl0lggla435q5r_i[] = {0u};
const uint32_t gm_tpl0qlvf3bfgtip_i[] = {0u, 1u};
const uint32_t gm_tpl0sb47rg6hcij_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpl149k5asebpko_i[] = {0u};
const uint32_t gm_tpl1dleoebfmvhp_i[] = {0u, 1u};
const uint32_t gm_tpl1f5t4gadpj5n_i[] = {0u};
const uint32_t gm_tpl1g1lh0kp24oa_i[] = {0u, 1u};
const uint32_t gm_tpl1lj3ah7qeq42_i[] = {0u};
const uint32_t gm_tpl24fci6dm83eo_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl2aofg9dsfuds_i[] = {0u, 1u};
const uint32_t gm_tpl2re1r0fre48m_i[] = {0u};
const uint32_t gm_tpl335dut97l9dl_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpl352s0p85j412_i[] = {0u};
const uint32_t gm_tpl43224j8old0s_i[] = {0u};
const uint32_t gm_tpl4clb07vpsm45_i[] = {0u, 1u};
const uint32_t gm_tpl4phv0vr9as7f_i[] = {0u, 1u};
const uint32_t gm_tpl578lpoev0s9s_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
const uint32_t gm_tpl59lqltdr0h0r_i[] = {0u};
const uint32_t gm_tpl5igv3ckbutrp_i[] = {0u};
const uint32_t gm_tpl5kl598jq5h12_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
const uint32_t gm_tpl5rd4283boomi_i[] = {0u};
const uint32_t gm_tpl60bi48lvp2qn_i[] = {0u};
const uint32_t gm_tpl6q4b2p6tude6_i[] = {0u};
const uint32_t gm_tpl7kp6007qjdtb_i[] = {0u};
const uint32_t gm_tpl8bar0du85qu4_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpl8in37qe3omcj_i[] = {0u};
const uint32_t gm_tpl8lt5kipnmd64_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl9ft3vu1e30uh_i[] = {0u};
const uint32_t gm_tpl9ibjmfjq0iar_i[] = {0u};
const uint32_t gm_tpl9l4fqsojrjsa_i[] = {0u};
const uint32_t gm_tpl9p5406jtlms7_i[] = {0u};
const uint32_t gm_tpl9v8qcr7jilen_i[] = {0u, 1u};
const uint32_t gm_tpl9vu8tqcv1d9s_i[] = {0u, 1u};
const uint32_t gm_tpla6b7o385jo3o_i[] = {0u};
const uint32_t gm_tplalgvrfo85soq_i[] = {0u};
const uint32_t gm_tplamv2e4uailq7_i[] = {0u, 1u};
const uint32_t gm_tplaoj5r4tauejk_i[] = {0u};
const uint32_t gm_tplaos6tmpdtrie_i[] = {0u, 1u};
const uint32_t gm_tplastf5j9ge45b_i[] = {0u};
const uint32_t gm_tplb52vln6fobrq_i[] = {0u};
const uint32_t gm_tplbckos3fl05qe_i[] = {0u, 1u};
const uint32_t gm_tplbgu1ah9e6d87_i[] = {0u};
const uint32_t gm_tplc2ekf83ggtj3_i[] = {0u, 1u, 2u};
const uint32_t gm_tplcre592qvlrqu_i[] = {0u, 1u};
const uint32_t gm_tplcudcaj1iluqd_i[] = {0u, 1u};
const uint32_t gm_tpldueenbh2sgd4_i[] = {0u, 1u};
const uint32_t gm_tpldvbj2judb6g5_i[] = {0u};
const uint32_t gm_tpleeuqgcr11ngn_i[] = {0u, 1u, 2u};
const uint32_t gm_tplegelorakvcn7_i[] = {0u};
const uint32_t gm_tplf579fe8ol0qp_i[] = {0u, 1u, 2u};
const uint32_t gm_tplg0lh6vujmm6o_i[] = {0u};
const uint32_t gm_tplg2qlrjk1sunc_i[] = {0u};
const uint32_t gm_tplg575kk57c4i8_i[] = {0u, 1u};
const uint32_t gm_tplgniq1bprrgpo_i[] = {0u, 1u, 2u};
const uint32_t gm_tplgnp9u7qeb3h6_i[] = {0u, 1u};
const uint32_t gm_tplhp4k6h527j3k_i[] = {0u, 1u, 2u};
const uint32_t gm_tplhphg08jcb4qr_i[] = {0u};
const uint32_t gm_tpli3i5m2srtl1a_i[] = {0u};
const uint32_t gm_tplibfhnj0illo7_i[] = {0u, 1u};
const uint32_t gm_tplicuqpjvsak60_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplipqn9urt53l3_i[] = {0u};
const uint32_t gm_tpljc7kqi31f4d2_i[] = {0u};
const uint32_t gm_tplk13976gpibvh_i[] = {0u};
const uint32_t gm_tplk7hjamhd5jcl_i[] = {0u, 1u, 2u};
const uint32_t gm_tplka8uag807gql_i[] = {0u, 1u, 2u, 3u, 4u, 5u};
const uint32_t gm_tplkd07482m1uv1_i[] = {0u, 1u};
const uint32_t gm_tpll1t63hmppo21_i[] = {0u};
const uint32_t gm_tpll60ng9eo3ai8_i[] = {0u};
const uint32_t gm_tpll9g0ell7ocpk_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
const uint32_t gm_tpllovel7a2ips1_i[] = {0u, 1u};
const uint32_t gm_tpllqbinchipsao_i[] = {0u, 1u};
const uint32_t gm_tplmj3d4h2pi7q3_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplmn77qas0g7s4_i[] = {0u};
const uint32_t gm_tplne0l0lciidvr_i[] = {0u, 1u};
const uint32_t gm_tplnevoink17fov_i[] = {0u, 1u};
const uint32_t gm_tplnm0o65j76qg0_i[] = {0u, 1u};
const uint32_t gm_tplnud722tu4rrk_i[] = {0u};
const uint32_t gm_tplo38nq02vgfo5_i[] = {0u};
const uint32_t gm_tplo66949vs5pfo_i[] = {0u, 1u};
const uint32_t gm_tplo6lbijqleti1_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
const uint32_t gm_tplob0n62u6gqqu_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
const uint32_t gm_tplolsc5tfmth66_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplpfavv7btb1m5_i[] = {0u, 1u};
const uint32_t gm_tplpkq4i4j83tmk_i[] = {0u};
const uint32_t gm_tplpl5slbegbu93_i[] = {0u};
const uint32_t gm_tplpnk24geo6qd8_i[] = {0u, 1u};
const uint32_t gm_tplqg9a6t74ffg7_i[] = {0u, 1u};
const uint32_t gm_tplqjo10upi8bs6_i[] = {0u};
const uint32_t gm_tplqn8qbim70q76_i[] = {0u, 1u};
const uint32_t gm_tplqrbhvccemc9k_i[] = {0u};
const uint32_t gm_tplre7l7cu4fpbv_i[] = {0u, 1u};
const uint32_t gm_tplrmoc4ag4eqrb_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplrpvalpqjvipe_i[] = {0u, 1u};
const uint32_t gm_tpls5bjfpqmr7e0_i[] = {0u};
const uint32_t gm_tpls85ptecv5rkj_i[] = {0u};
const uint32_t gm_tplsf6r3t44km8l_i[] = {0u, 1u};
const uint32_t gm_tplspgpn4qu6rd4_i[] = {0u};
const uint32_t gm_tplsrr43aau9tgs_i[] = {0u};
const uint32_t gm_tplt6nq68u4co32_i[] = {0u, 1u, 2u};
const uint32_t gm_tplthkeg7jngn6q_i[] = {0u};
const uint32_t gm_tplu0n2ra1s30o8_i[] = {0u, 1u};
const uint32_t gm_tplu2g08gg1kb39_i[] = {0u};
const uint32_t gm_tplu3gnb6pu4f3h_i[] = {0u, 1u};
const uint32_t gm_tplua69gir871ue_i[] = {0u};
const uint32_t gm_tpluduaa40ad285_i[] = {0u};
const uint32_t gm_tpluetglub2muf2_i[] = {0u};
const uint32_t gm_tplupuvr4efpn1v_i[] = {0u};
const uint32_t gm_tpluuksm24afvp0_i[] = {0u};
const uint32_t gm_tplvi7pmohm1ihu_i[] = {0u};
const uint8_t gm_s00odc15r3q8f[16] = "invalid value `";
const uint8_t gm_s00pihipe0jga[6] = "ToPtr";
const uint8_t gm_s0147vgtvilnq[19] = "` does not embed `";
const uint8_t gm_s07uh61s060o9[25] = "expected a constant here";
const uint8_t gm_s09tkid4cqjmm[62] = "` is an effect set, not a type; use it after `needs` (\302\24716.6)";
const uint8_t gm_s0doambt0iqhe[4] = "\\u{";
const uint8_t gm_s0ek1hibl01t2[47] = "`^` dereferences a pointer `^T`, but this is `";
const uint8_t gm_s0f7epcithn0r[18] = "kind = GM_K_ARRAY";
const uint8_t gm_s0h9r2boh81d2[65] = "(void *code, void *env, gm_node *cx, const void *d, uintptr_t n)";
const uint8_t gm_s0hfsnjdrghcm[9] = "\000@\000\000\000\000\000\000";
const uint8_t gm_s0johk446liuj[28] = "` does not apply to a field";
const uint8_t gm_s0kajv5j53ub7[38] = "not supported by the C back end yet: ";
const uint8_t gm_s0l40gug6qpuq[17] = " && memcmp(mp, \"";
const uint8_t gm_s0nad8bs1qlvl[76] = "the field `{}` of `{}` is visible only in the package of its module (\302\2473.3)";
const uint8_t gm_s0oocf03eil44[6] = " len ";
const uint8_t gm_s0u9ru4tiqbe7[9] = "register";
const uint8_t gm_s0unahdn5j99v[14] = "complex {} {}";
const uint8_t gm_s12khqa42i680[29] = "a literal is too large for `";
const uint8_t gm_s13b13uieof5u[12] = "gm_proc_put";
const uint8_t gm_s13b5om17s13f[8] = "{}: {}\012";
const uint8_t gm_s187rb18i1kgk[4] = "Min";
const uint8_t gm_s1cn5ab5opg9f[131] = "fibers (`with \342\200\246 on`) need a context switch, which the runtime has only for x86-64, AArch64 and s390x; this freestanding target (";
const uint8_t gm_s1crsce68cmnd[20] = "GM_RUN_EXPORT(gm_xt";
const uint8_t gm_s1h3hvq26g2dl[47] = "`{}` is a module, which is not a value (\302\2473.2)";
const uint8_t gm_s1idibqkta392[7] = "value ";
const uint8_t gm_s1lfsmjv8evdo[9] = "a vector";
const uint8_t gm_s1n4l05blor5r[40] = "`_` cannot be the name of a declaration";
const uint8_t gm_s1r58jmps9mcc[7] = "coerce";
const uint8_t gm_s1spbnihet3ou[25] = "-Wl,--thinlto-cache-dir=";
const uint8_t gm_s1tdqlhdsnu5l[14] = "this returns ";
const uint8_t gm_s1tkkenmpa3aa[13] = "source = {}\012";
const uint8_t gm_s1ul47vja8nnc[16] = "gm_cover_counts";
const uint8_t gm_s1vmtsmtbio1j[15] = "cannot create ";
const uint8_t gm_s20t5qopq8n9u[5] = "args";
const uint8_t gm_s27am6v0m91fv[87] = "`--target` cannot be combined with `--mode=safe`, which builds for the host with Fil-C";
const uint8_t gm_s2ma5cp41jk1a[7] = "Center";
const uint8_t gm_s2pblv08uof55[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\016\001\000\000\216\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_s2q81sartj882[42] = "gm_alloc_tag({}, {}, {}, {}, {}, {}, {});";
const uint8_t gm_s2svafu2na27p[17] = "`~` on this type";
const uint8_t gm_s2u0i5o1ej1mg[10] = "the URL `";
const uint8_t gm_s2ud1phv1fk5e[15] = "gm_fs_open_dir";
const uint8_t gm_s2uilckebopb3[12] = "gm_f16_from";
const uint8_t gm_s2v0mmbl3fojp[55] = "` is visible only in the package of its module (\302\2473.3)";
const uint8_t gm_s31vj4bjbj0gj[36] = "static void gm_init_consts(void) {\012";
const uint8_t gm_s34flpapqdf4s[14] = "--run-test={}";
const uint8_t gm_s3behnbdv0iv4[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000<\001\000\000.\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_s3hali1afgel3[28] = "a fiber's function returned";
const uint8_t gm_s3hrsavigl7la[19] = "to begin the block";
const uint8_t gm_s3k4homl43m7q[74] = "    GM_ST(uint32_t, loc + {}, line);\012    GM_ST(uint32_t, loc + {}, col);\012";
const uint8_t gm_s3ngn7me40qa4[5] = "arch";
const uint8_t gm_s3svhagp75tqj[38] = "static const uint8_t vs{}[] = {{{}}};";
const uint8_t gm_s41sp7ht9l505[67] = "unknown lint rule `{}` in [lint] (`gamma lint --rules` lists them)";
const uint8_t gm_s42b9o00en8pl[34] = "`{}` is not an operator; use `{}`";
const uint8_t gm_s449uaav7f0l0[11] = "--asyncify";
const uint8_t gm_s460n81rl2c9c[5] = "'\\n'";
const uint8_t gm_s467d5cm1d5us[4] = "u32";
const uint8_t gm_s480gcu6od53p[61] = "invalid memory access: write to read-only memory at address ";
const uint8_t gm_s48o8olsbqnph[12] = ".body_cx = ";
const uint8_t gm_s495moo6tjeh0[20] = "the slice method `.";
const uint8_t gm_s4ccl84nrk27k[55] = "cannot read {}: No such file or directory (os error 2)";
const uint8_t gm_s4cdme6b7eogn[56] = "dependency `{}` has both `{}` and `{}`; use one of them";
const uint8_t gm_s4e3q3n6msi6m[6] = ")));\012";
const uint8_t gm_s4hv5fsmnuq3r[15] = "; return 0; }\012";
const uint8_t gm_s4inmcuim4088[46] = "an expression left over from generic checking";
const uint8_t gm_s4j4d3l9da8i6[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000I\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_s4m2m0c0t74ak[46] = "apply the fixes of the warnings that have one";
const uint8_t gm_s4puodol85qqo[55] = "an arithmetic operand of a shift must be parenthesized";
const uint8_t gm_s4qg6lddtahb8[4] = "\302\265s";
const uint8_t gm_s4qqlf0cno16q[45] = "{} {} = __builtin_shufflevector({}, {}, {});";
const uint8_t gm_s4t89div5i959[80] = "an array indexed by a type needs a concrete enum type, not the type parameter `";
const uint8_t gm_s4v6k193oo0cr[14] = "gm_proc_write";
const uint8_t gm_s50fbjkgs3ote[11] = "); return ";
const uint8_t gm_s54emlnl8lmoe[38] = ": stream did not contain valid UTF-8\012";
const uint8_t gm_s54jmbv61vfjv[12] = "placeholder";
const uint8_t gm_s557bv2sfqcom[6] = "enum ";
const uint8_t gm_s579fb7l5l6df[3] = "fn";
const uint8_t gm_s589bsqckia3s[8] = "Sys({})";
const uint8_t gm_s5958kaiuh54g[84] = "the members of a tuple compared by `in` must have one type, but found `{}` and `{}`";
const uint8_t gm_s5cmg64f6av8k[70] = "internal error: an integer operator on a value that is not an integer";
const uint8_t gm_s5cpmumv7vkcr[7] = "error\012";
const uint8_t gm_s5dhdddu73608[25] = "unwrap of error value {}";
const uint8_t gm_s5ea2jdgum6o9[33] = ") gm_trap(\"integer overflow in `";
const uint8_t gm_s5glpb7fbtk4c[27] = "GM_ST(void *, (uint8_t *)&";
const uint8_t gm_s5hr2c7nkokp9[70] = "`, whose operations can exit or suspend, unless it handles it (\302\2478.8)";
const uint8_t gm_s5k6emuk51u47[27] = "` is not defined on floats";
const uint8_t gm_s5kh0f7b7mijc[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000%\002\000\000\025\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_s5kvaetfehsvi[13] = "is_signed = ";
const uint8_t gm_s5n7j87m9k09e[36] = "`{}` requires {} operands, not `{}`";
const uint8_t gm_s5ndb61nne1fg[58] = "`: `subdir` must be a relative path inside the repository";
const uint8_t gm_s5qukklsuqpi4[17] = "unused-parameter";
const uint8_t gm_s5rv6umk4akl0[6] = "while";
const uint8_t gm_s5s758m9u7gle[26] = "gm_cancel_cont(inst->fw, ";
const uint8_t gm_s5v7o1bkddp5s[7] = "unpack";
const uint8_t gm_s616b6iju29ec[5] = "push";
const uint8_t gm_s62an8gcfpfbj[53] = "a union's tag type must be an integer type, not `{}`";
const uint8_t gm_s64vef3if6vik[23] = "to close `pub(package`";
const uint8_t gm_s65k7rb52dupd[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\\\004\000\000\026\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_s65qt8qo64cje[3] = "-o";
const uint8_t gm_s66b184memqnd[4] = " > ";
const uint8_t gm_s66fbu3uhl900[20] = "unknown lint rule `";
const uint8_t gm_s6ab2s9s2fif9[9] = ")) | (((";
const uint8_t gm_s6aiq6b6gsv1o[18] = "character literal";
const uint8_t gm_s6cchf3tcs9e4[21] = "` has no operation `";
const uint8_t gm_s6ecg17hs903q[9] = ".next = ";
const uint8_t gm_s6iln1emufss5[20] = "unmatched closing `";
const uint8_t gm_s6r46262afe2h[7] = ")) - (";
const uint8_t gm_s6sv4vhunlp6l[9] = " became ";
const uint8_t gm_s6ths5rchgo8g[6] = "fn {}";
const uint8_t gm_s6u49d04cqlg1[17] = "the clause for `";
const uint8_t gm_s6uegtmhpu1qn[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\021\000\000\000X\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s6uo8vuok1rk4[10] = "name = {}";
const uint8_t gm_s70h6okja64qq[25] = "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000";
const uint8_t gm_s70ks96e05b2g[39] = "static int gm_entry{}(gm_node *cx) {{\012";
const uint8_t gm_s73vt0nuu7890[28] = "` (supplied by this spread)";
const uint8_t gm_s76hci78s6ds4[6] = "{:09}";
const uint8_t gm_s77fggjckb5ln[41] = "the directory {} exists and is not empty";
const uint8_t gm_s77n5hibks91d[30] = "gm_format(&{}, {}, {}, true);";
const uint8_t gm_s79fsf3j19oj8[39] = "if ({} != 0) memmove({}, {}, {} * {});";
const uint8_t gm_s7cu0m5qc7arr[43] = " repl; :help for commands, :quit to leave\012";
const uint8_t gm_s7fl8t4aje2e7[24] = "{} {} = ({})({} {} {});";
const uint8_t gm_s7fto4aj9q7su[5] = "Sub(";
const uint8_t gm_s7gn2sla7drif[3] = "nf";
const uint8_t gm_s7i80akiq0d95[5] = "Hash";
const uint8_t gm_s7j165k7b6shl[32] = "double free of heap block {:#x}";
const uint8_t gm_s7pkv0u1oe9rv[102] = "Safe builds compile with the Fil-C compiler (https://fil-c.org), but the C compiler `{}` is not Fil-C";
const uint8_t gm_s7qhcf46f682d[21] = "kind = GM_K_OPTIONAL";
const uint8_t gm_s7t1ot7rgnuch[11] = ", (double)";
const uint8_t gm_s7umjdfqes00m[52] = "`{}` needs an element for every variant: missing {}";
const uint8_t gm_s7uovnbe5u8va[13] = "gm_fs_remove";
const uint8_t gm_s829ksoojqn6l[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\212\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_s84p47580vnhr[12] = "proc_launch";
const uint8_t gm_s8gkio2134kd6[70] = "this array lives until the end of the block of the statement it is in";
const uint8_t gm_s8krlr0c92m1s[15] = ",\"end_offset\":";
const uint8_t gm_s8o3vqolbfl8t[11] = "clock_wall";
const uint8_t gm_s8ok287s0vtoq[38] = "the `url` of dependency `{}` is empty";
const uint8_t gm_s8pk5tcnrk6p2[8] = "LaneMin";
const uint8_t gm_s8q575q0mjt4k[6] = " the ";
const uint8_t gm_s8r83g99aqvfj[52] = "` does not exist on a freestanding target (\302\2473.7.1)";
const uint8_t gm_s8sv6qdepk5ba[4] = "std";
const uint8_t gm_s8sv99c8q8gbc[71] = "`, since a field has a type with invalid bit patterns; build it with `";
const uint8_t gm_s91mn5q6gq6th[66] = "if ({} > {} || {} > (uintptr_t){}) gm_trap_slice({}, {}, {}, {});";
const uint8_t gm_s9hdvuc1np1st[10] = "trap: {}\012";
const uint8_t gm_s9lds9mpotrru[15] = "gm_posix_spawn";
const uint8_t gm_s9qu470anjl4i[8] = "warning";
const uint8_t gm_s9r2a03bnhj06[58] = "internal error: a shift by a value that is not an integer";
const uint8_t gm_s9r64t1166q1f[5] = "test";
const uint8_t gm_s9rujqsrrjcho[110] = "` of a `#bits` struct must be `bool`, an integer, an enum with an explicit tag type or another `#bits` struct";
const uint8_t gm_s9sakn2pk9s4e[19] = "l{} is not a local";
const uint8_t gm_s9tjtf1373ti5[5] = "_ = ";
const uint8_t gm_s9tr38mqso4n7[93] = "`needs not` ends the signature: it means the empty effect set, not \"everything but\" (\302\24719.2)";
const uint8_t gm_s9uhkcs7rcfn8[14] = "gm_cmd_unsafe";
const uint8_t gm_sa0l93d5jbk7k[69] = "destructuring is not allowed at top level; bind each name separately";
const uint8_t gm_sa525m041ir3n[17] = "does not target ";
const uint8_t gm_saja8vc684oud[41] = "invalid `git` URL of dependency `{}`: {}";
const uint8_t gm_salin070nim24[30] = "signed integer or float lanes";
const uint8_t gm_san7he7q20qtj[7] = "__size";
const uint8_t gm_saoprtd6rfhf7[4] = ")->";
const uint8_t gm_sapidpi7me2ee[95] = "gamma: cannot run this program here: foreign code and assembly are not available on this host\012";
const uint8_t gm_saq0ier99o235[35] = "` cannot take a receiver of type `";
const uint8_t gm_sasgv00j2p5nu[18] = "--connect-timeout";
const uint8_t gm_sb2nfo787ropg[6] = "the `";
const uint8_t gm_sb35te3dpkhv1[13] = " = bits-get ";
const uint8_t gm_sb3sdaa9odag6[13] = "capabilities";
const uint8_t gm_sb6654l540rsv[921] = "\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000=\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\0002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000.\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000b\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000e\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000:\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000F\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000(\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\034\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000f\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000c\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000p\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000I\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000C\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\077\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000E\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000)\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000<\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000'\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000M\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000a\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000";
const uint8_t gm_sbe9uv194cpmc[16] = "` (known keys: ";
const uint8_t gm_sbf1f7fu8bvpt[5] = " or ";
const uint8_t gm_sbfc2b8kquvq8[9] = "float 0x";
const uint8_t gm_sbgcsq0m3astj[43] = "this `if` gives the value of its condition";
const uint8_t gm_sbhui4f7fb5q5[9] = "Variants";
const uint8_t gm_sbjj41cb5lph3[60] = "internal error: unresolved coercion reached the interpreter";
const uint8_t gm_sblo01ng5f1c2[1308] = ":root { color-scheme: light dark; --fg: #1f2328; --bg: #ffffff; --muted: #59636e; --code-bg: #f3f4f6; --rule: #d1d9e0; --link: #0550ae; }\012@media (prefers-color-scheme: dark) { :root { --fg: #e6edf3; --bg: #0d1117; --muted: #9198a1; --code-bg: #161b22; --rule: #30363d; --link: #58a6ff; } }\012body { margin: 0 auto; max-width: 52rem; padding: 1.5rem 1rem 4rem; background: var(--bg); color: var(--fg); font: 16px/1.55 system-ui, sans-serif; }\012a { color: var(--link); }\012h1, h2, h3 { line-height: 1.25; }\012h2 { margin-top: 2.5rem; padding-bottom: .3rem; border-bottom: 1px solid var(--rule); }\012h3 { margin-top: 2rem; }\012code, pre { font: 14px/1.45 ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }\012code { background: var(--code-bg); padding: .1em .3em; border-radius: 4px; }\012pre { background: var(--code-bg); padding: .75rem 1rem; border-radius: 6px; overflow-x: auto; }\012pre code { background: none; padding: 0; }\012table { border-collapse: collapse; }\012th, td { border: 1px solid var(--rule); padding: .3rem .6rem; text-align: left; vertical-align: top; }\012blockquote { margin-left: 0; padding-left: 1rem; border-left: 3px solid var(--rule); color: var(--muted); }\012.contents { columns: 14rem; column-gap: 2rem; padding-left: 1.2rem; }\012.members dt { margin-top: .5rem; }\012.members dd { margin-left: 1.5rem; }\012";
const uint8_t gm_sblsu018nl4o3[3] = "cc";
const uint8_t gm_sbq9k9kvdokbf[20] = "compiler/pkg/git.gm";
const uint8_t gm_sbsb3nmbfgbv2[4] = "Hex";
const uint8_t gm_sbsu8rcurukea[25] = "this operator on `bool`s";
const uint8_t gm_sbui4p06qp6ck[29] = "((((unsigned __int128)1) << ";
const uint8_t gm_sbupitjaj376b[34] = " m;\012    memset(&m, 0, sizeof m);\012";
const uint8_t gm_sc0gdocdik6sv[7] = "<item>";
const uint8_t gm_sc1ac3a85htl8[12] = "    if ((a[";
const uint8_t gm_sc3ht2jgt5ee4[689] = "\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sc61gdfjf1pvd[38] = " names cannot destructure a tuple of ";
const uint8_t gm_sccadmt684uri[23] = "invalid library name `";
const uint8_t gm_sccm0heds95r6[60] = "`@variant_name` takes a value of a tagged union type, not `";
const uint8_t gm_scd599tsljaee[5] = "Cast";
const uint8_t gm_scdj734mhh9f0[17] = "__atomic_load_n(";
const uint8_t gm_scj1kuqp0242l[6] = "gm_hd";
const uint8_t gm_scnj362636897[25] = "a temporary `#soa` array";
const uint8_t gm_scold1pf1iq8n[11] = ", .hctx = ";
const uint8_t gm_scq7qb5df34b0[14] = "a declaration";
const uint8_t gm_sct07pl02n02h[27] = "invalid leading zero in {}";
const uint8_t gm_scueeuhqhtotc[30] = "cannot unpack the archive: {}";
const uint8_t gm_sd11ghdf4ciqp[6] = "rules";
const uint8_t gm_sd1d50j1ea4vf[46] = "this `if` gives the negation of its condition";
const uint8_t gm_sd4ibvv64si9n[45] = "internal error: float widening of non-floats";
const uint8_t gm_sd4qrpv3lpilp[4] = "u{}";
const uint8_t gm_sd5fg79k33phf[37] = "static void {}(void *req, gm_cont k)";
const uint8_t gm_sd5iqorsg0rc4[73] = "`@variants({})` needs a union without payloads, but variant `{}` has one";
const uint8_t gm_sd6d9fvngpchj[14] = "gm_cover_init";
const uint8_t gm_sddtbfibb719p[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\034\001\000\000\005\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_sdeq555foclko[3] = "+=";
const uint8_t gm_sdg7bdfivjinb[3] = "Ge";
const uint8_t gm_sdi44r3kug6n6[14] = "gm_node *inst";
const uint8_t gm_sdjbnp04akbbb[50] = "`[_]T(fill: x)` cannot infer the length; write it";
const uint8_t gm_sdlumbvroiufa[3] = "Fs";
const uint8_t gm_sdnbbauegomd2[7] = "sizeof";
const uint8_t gm_sdnjmvrkc1r7r[12] = "gm_with_go(";
const uint8_t gm_sdslahffv7339[17] = "    uint8_t loc[";
const uint8_t gm_se19nf1421e37[4] = "Neg";
const uint8_t gm_se64as7gfqfsc[36] = ")-1) gm_trap(\"integer overflow in `";
const uint8_t gm_se7gs48u9l3d7[10] = "gen_fiber";
const uint8_t gm_seak3hcpfjfp2[70] = "an array length is a constant `usize` or an enum type (`[E]T`), and `";
const uint8_t gm_sefrps06g7jfu[11] = "a slice of";
const uint8_t gm_seg9l1usqbs3j[4] = "Bin";
const uint8_t gm_segcvuj6bu7e4[17] = "positionEncoding";
const uint8_t gm_seje0bncv3tch[17] = "\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_seor1k5gntiuc[52] = "` needs a package, but no gamma.toml was found for ";
const uint8_t gm_sepo1h9bofnod[30] = "expected {} digits after `{}`";
const uint8_t gm_setv7ejg35cg9[43] = "gamma: the program has no `main` function\012";
const uint8_t gm_sevfgu4cub43a[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000\350\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sf4qi8ut6u3p1[5] = "\015\012\015\012";
const uint8_t gm_sf7l7ca9coehi[85] = "a type declared inside a function cannot be generic; declare it at top level (\302\2473.5)";
const uint8_t gm_sf9regh1oaao3[14] = " = __builtin_";
const uint8_t gm_sfd9hctnph9ef[12] = "fs_set_mode";
const uint8_t gm_sfdq8ss0dnpe2[8] = "--rules";
const uint8_t gm_sfj8otp2pq7qq[55] = "without the marker it is private to its module (\302\2473.3)";
const uint8_t gm_sfku76mf5fn5d[9] = "fs_close";
const uint8_t gm_sfm6vc4bhnr4s[44] = ", the limit of this implementation (\302\24723.5)";
const uint8_t gm_sfm739ta4hkl2[6] = "_s};\012";
const uint8_t gm_sfm9elkq6bn2e[2] = "`";
const uint8_t gm_sfpfdag7t7v0f[16] = "gm_fs_read_link";
const uint8_t gm_sfpopfqbubj89[35] = "`@type_of(e)` takes one expression";
const uint8_t gm_sfrku7jk647q0[83] = "targets a machine without `__int128`, which the C back end needs (a 64-bit target)";
const uint8_t gm_sfv61btcutqd4[21] = "-Wl,-z,stack-size={}";
const uint8_t gm_sg0997vvtiund[65] = "this calls a function value whose target is not known statically";
const uint8_t gm_sg4u1qvcufkum[5] = " yet";
const uint8_t gm_sg5uvbf14q54s[21] = " __attribute__(({}))";
const uint8_t gm_sg7920c5gghpv[13] = "gm_heap_free";
const uint8_t gm_sg9102k54psou[16] = "expr_to_pattern";
const uint8_t gm_sg9g5p2e4brnh[62] = "{}:{}: dependency `{}` is a url dependency; run `gamma fetch`";
const uint8_t gm_sgbumttiou4de[8] = "effects";
const uint8_t gm_sgdvb6gobbii2[8] = "    {}\012";
const uint8_t gm_sgh0dvu15b9ta[10] = "((void *)";
const uint8_t gm_sghk06svnbp7m[40] = "too many arguments: the function takes ";
const uint8_t gm_sgls2f653rbea[17] = "parse_effect_lit";
const uint8_t gm_sgqcrkl2nh339[4] = "Dir";
const uint8_t gm_sgr5hvs6tocci[12] = "from_worker";
const uint8_t gm_sgs4qi36v3o5v[8] = "bit-not";
const uint8_t gm_sgs96ai7rj93j[17] = "compiler/main.gm";
const uint8_t gm_sgvvkov5kvprt[10] = "copy_from";
const uint8_t gm_sh08f8dq9lenu[41] = "if the function must take it, name it `_";
const uint8_t gm_sh5k7nstldaeg[7] = " [N]T`";
const uint8_t gm_sh8t790g5lerb[12] = "sort_by_key";
const uint8_t gm_shcu73rsj445k[13] = ",\"end_line\":";
const uint8_t gm_shes0dtqt9sqq[28] = "{} is not an array or slice";
const uint8_t gm_shh35vtq9t5v9[17] = "{}.body_cx = {};";
const uint8_t gm_shido3ioit5oe[10] = "(a + i * ";
const uint8_t gm_shjfpng3ei3du[82] = "`{}` does not apply to complex numbers; they have `+`, `-`, `*` and `/` (\302\2477.4.7)";
const uint8_t gm_shl0ucmfrvk77[54] = "`--timeout` needs a positive number of seconds, not `";
const uint8_t gm_shofa8pdml9fb[13] = "pub(package)";
const uint8_t gm_shrelr880pj0m[7] = ", NULL";
const uint8_t gm_shsqur3mdh4ln[41] = "GM_ST(uint{}_t, (uint8_t *)&{} + {}, 0);";
const uint8_t gm_shtiokhdiebr1[3] = "-=";
const uint8_t gm_shtvnkmjp804q[23] = ";\012static void gm_ginit";
const uint8_t gm_shudlfi0ri4tb[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000h\001\000\000\034\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_si00d7fs7b5aq[10] = "--backend";
const uint8_t gm_si0567rndd8dl[17] = "\000\000\000\000\000\000\000\000P\000\000\000\000\000\000\000";
const uint8_t gm_si2s3gai2c29i[16] = "else capture {}";
const uint8_t gm_si33msf4t0sbn[8] = "--force";
const uint8_t gm_si3f75dnj7pb9[26] = "more than one input given";
const uint8_t gm_si430gujtnl9u[11] = "GM_ST(uint";
const uint8_t gm_sifq8gd3d748j[36] = "the element for `{}` is given twice";
const uint8_t gm_sigr245qbgr4o[14] = "gm_trap_slice";
const uint8_t gm_sihsae37m31pr[15] = "h = {}({}, h);";
const uint8_t gm_sijd693t5ccnb[4] = "box";
const uint8_t gm_sim03rp8pjg4e[44] = "    for (uintptr_t i = 0; i < n; i++) if (!";
const uint8_t gm_sinmpfqtbo9in[5] = "trap";
const uint8_t gm_sinogveu00g7m[36] = ": the process cannot be waited for\012";
const uint8_t gm_sipf690cm2c6f[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\361\000\000\000%\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_siqa65grpcsk9[99] = "an exported function's effects must be empty or `{Sys}`: C callers cannot supply handlers (\302\24719.4)";
const uint8_t gm_siqsngf0mfo86[9] = "\014\000\000\000\000\000\000\000";
const uint8_t gm_sitbu884cbr46[96] = "the `else` of a pattern binding cannot capture an error; match the error with `!e` in a `match`";
const uint8_t gm_siun1n8hqooca[9] = "an error";
const uint8_t gm_sj2ngf6ro6oja[65] = "a generic function cannot have a calling convention other than C";
const uint8_t gm_sj62oio2kg58e[20] = ", &p, sizeof p); }\012";
const uint8_t gm_sj7gfkb5eknah[76] = "gm_unw.kind = {}; gm_unw.frame = {}; gm_unw.label = {}; gm_unw.is_err = {};";
const uint8_t gm_sj7kfef17i9q2[9] = "lir_body";
const uint8_t gm_sj85nechopaet[54] = "internal error: the address of a local without memory";
const uint8_t gm_sj8b32e846r90[11] = "pat-range ";
const uint8_t gm_sjdbk1ada1ddm[8] = ", gm_r)";
const uint8_t gm_sjdv8icqpfhc5[6] = "MacOS";
const uint8_t gm_sjeh66tmmrhop[3] = "kb";
const uint8_t gm_sjfg2291kjugh[4] = "---";
const uint8_t gm_sjfrmur8rvp24[34] = "` (in an instantiation used here)";
const uint8_t gm_sjiaitbr3bp01[10] = "reference";
const uint8_t gm_sjibktbbpgpq2[17] = "untyped-complex ";
const uint8_t gm_sjkqdj35ksd9p[3] = "wn";
const uint8_t gm_sjn3oh87mil2m[333] = "pub Os :: union {{ Linux, MacOS, Windows, FreeBSD, Wasi, Freestanding }}\012pub Arch :: union {{ X86_64, Aarch64, Riscv64, S390x, Wasm32, Other }}\012pub Endian :: union {{ Little, Big }}\012pub Mode :: union {{ Debug, Checked, Safe, Fast }}\012pub os :: Os.{}\012pub arch :: Arch.{}\012pub endian :: Endian.{}\012pub ptr_bits :: {}\012pub mode :: Mode.{}\012";
const uint8_t gm_sjngmi12bbcu5[30] = "a `match` that no arm matches";
const uint8_t gm_sjq0dshsov83m[20] = "first declared here";
const uint8_t gm_sk04qg20jmac0[4] = "{}\012";
const uint8_t gm_sk0bnuggr6sri[14] = "proc_write_in";
const uint8_t gm_sk1ojn82dahdn[13] = "gm_rt_switch";
const uint8_t gm_sk5asgkllekl2[14] = "gm_filc_entry";
const uint8_t gm_sk8fkllqjst4j[13] = "builtin type";
const uint8_t gm_ska23dbjvthl3[15] = "renameProvider";
const uint8_t gm_ske882p1cpf5s[10] = "site_fill";
const uint8_t gm_skfdo9qggtrbp[6] = "(a + ";
const uint8_t gm_skg1c046vhqs5[45] = "with completed while {} fiber{} {} suspended";
const uint8_t gm_skj1a463v050q[79] = "if (!gm_unw.kind) gm_trap(\"internal error: a `ctl` clause finished normally\", ";
const uint8_t gm_skjhai33494u2[9] = "\370\377\377\377\377\377\377\377";
const uint8_t gm_sko4cgkdijve3[57] = "`: `archive` must be `sha256:` and 64 hexadecimal digits";
const uint8_t gm_sks4g7rc6a1af[16] = "(GM_LD(void *, ";
const uint8_t gm_sl2lo3hki3t4d[36] = "unclosed inline table, expected `}`";
const uint8_t gm_sl34pggfk1a23[38] = "for (size_t {} = 1; {} < {}; {}++) {{";
const uint8_t gm_sl3q52ep6aa53[2] = ".";
const uint8_t gm_sl4ns6hobicht[6] = "range";
const uint8_t gm_sl55obh2ok05j[4] = "{}=";
const uint8_t gm_sl7p05f6l6cga[10] = "lex parse";
const uint8_t gm_sl8fmedd9kk64[34] = "a declaration after the attribute";
const uint8_t gm_slasd9g26mfnt[12] = "attribute #";
const uint8_t gm_slbmfdf2lr7u3[15] = "gm_ctl_perform";
const uint8_t gm_slekeb8da8in6[5] = " + 1";
const uint8_t gm_sletp8nk609ee[17] = "\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000";
const uint8_t gm_slf2kl43ogurh[17] = "division by zero";
const uint8_t gm_slh6d6uosdbkg[35] = "expected a closing run of exactly ";
const uint8_t gm_slikoo8r5oupm[18] = "this trap message";
const uint8_t gm_sllgtfi45c44a[8] = "archive";
const uint8_t gm_sllrmadmbrcvv[54] = "this condition is always false: the branch never runs";
const uint8_t gm_slmqf5nrs9f6b[13] = "((uint8_t)0)";
const uint8_t gm_slphk0n858mv9[13] = " type, not `";
const uint8_t gm_slqtt99r493q9[102] = "`, which points outside the package through other links; links must stay inside the package directory";
const uint8_t gm_slskjtl6alfrd[6] = "clean";
const uint8_t gm_sm06nsg0msdgd[9] = "markdown";
const uint8_t gm_sm3fgo8acha61[13] = "__riscv_xlen";
const uint8_t gm_sm81b0bm9nrdl[30] = "gm_heap_free({}, {}, {}, {});";
const uint8_t gm_sm9iv4f7cjs9t[62] = "a `while` loop has no value, so `break` takes no operand here";
const uint8_t gm_smdggivmb7eg1[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000V\000\000\000(\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_smeu8adf6pdko[38] = ")));\012    memset(loc, 0, sizeof loc);\012";
const uint8_t gm_smfprcpea3ibe[10] = "proc_wait";
const uint8_t gm_smgdg1m3kad32[74] = "    if (gm_unw.kind == GM_CONTINUE) {{ gm_unw.kind = GM_NONE; goto {}; }}";
const uint8_t gm_smglbbtmdmip2[6] = "where";
const uint8_t gm_smhli04hme60d[30] = "unknown format spec `{{:{}}}`";
const uint8_t gm_smhs805pum0dn[26] = "top-level intrinsic calls";
const uint8_t gm_smi7k9v6u7cmo[11] = "`@type_of`";
const uint8_t gm_smk33hl0qg9hs[4] = "h{}";
const uint8_t gm_smq842qg8d7u6[119] = "to pass its result, call it: `{}(\342\200\246)`; to pass its address, give it a function pointer type first: `p: ^fn(\342\200\246) = {}`";
const uint8_t gm_smr2fm6cfetj8[5] = "pr{}";
const uint8_t gm_smraeij5q6ubc[11] = "parameter ";
const uint8_t gm_smsrao7h22muv[61] = "internal error: a conversion to a value that is not a vector";
const uint8_t gm_smuti066jdthl[24] = "{}/{}/{}/{}/{}/{}/{}/{}";
const uint8_t gm_smvgcg1j21t6v[27] = "use `}}` for a literal `}`";
const uint8_t gm_sn449k8j7bj3v[51] = "`@stack_size` takes one function: `@stack_size(f)`";
const uint8_t gm_sn88c2jg5b9gv[4] = "`--";
const uint8_t gm_snb8htg7a7p5u[16] = "identifier `{}`";
const uint8_t gm_snbn61h56c5fj[52] = "__atomic_compare_exchange_n({}, &{}, {}, 0, {}, {})";
const uint8_t gm_snc8fbp1a7m98[31] = "static void {}(gm_fiber *fb);\012";
const uint8_t gm_snf4l98ljss2u[89] = "to write its address, give it a function pointer type first: `p: ^fn(\342\200\246) = fn(\342\200\246) \342\200\246`";
const uint8_t gm_snftqbg249ljm[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\014\002\000\000:\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_sngdu4l8vh3l8[5] = "-not";
const uint8_t gm_snh86nvrv0us3[19] = "`not not x` is `x`";
const uint8_t gm_snhmu20ccir10[16] = "missing field `";
const uint8_t gm_snlpcspgan3ri[16] = "(({})[{}] != 0)";
const uint8_t gm_sno2v4rqn9iav[61] = "`#{}` makes {} of an array type with a constant length: `{}`";
const uint8_t gm_snokcvhtidt97[6] = "_Bool";
const uint8_t gm_so3t10be8g68d[10] = "gm_ga{}()";
const uint8_t gm_so4dfspvuibt1[11] = "--run-test";
const uint8_t gm_so77ks9ivg559[9] = " = addr ";
const uint8_t gm_so78814tnt5g5[2] = "a";
const uint8_t gm_so7em1q8clnib[8] = "a label";
const uint8_t gm_sod9r7r024h9q[6] = " bits";
const uint8_t gm_soefp7bosmvt1[12] = "remove `()`";
const uint8_t gm_soo0b43e4t7sn[12] = "layout of `";
const uint8_t gm_sopacnadlsnva[99] = "`@asm` takes a template, outputs, inputs and clobbers: `@asm(template, outputs, inputs, clobbers)`";
const uint8_t gm_sou5f0qr4vdgr[8] = "core/{}";
const uint8_t gm_sovqp4emsh0q8[5] = "_.+-";
const uint8_t gm_sp02qqgucmpt7[3] = " (";
const uint8_t gm_sp0kv61jd1btm[5] = "-l{}";
const uint8_t gm_sp6l5tr58ip76[23] = "this stops the program";
const uint8_t gm_sp9i9tubgmd3s[21] = "gm_with_go(&{}, {});";
const uint8_t gm_spchsf3taendr[3] = "\\f";
const uint8_t gm_spdkbvjffkg4b[11] = ") < GM_LD(";
const uint8_t gm_speaekk0qc740[9] = "gm_cv_u_";
const uint8_t gm_spfe58u39phsl[21] = "cannot infer which `";
const uint8_t gm_sph476jbvrpt5[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000w\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_spnr4ftjbm9m9[61] = "internal error: a conversion of a value that is not a vector";
const uint8_t gm_sppi971hm2k0k[15] = "static {} {};\012";
const uint8_t gm_sps1dthki4jpu[50] = "cannot ask the C compiler `{}` for its target:\012{}";
const uint8_t gm_spsjkc9sqdvl6[61] = "dependency `{}` of `{}`: the path `{}` leaves the repository";
const uint8_t gm_spts1f8sg22km[18] = ")) gm_trap_index(";
const uint8_t gm_spu32ed945op3[49] = "gm_bt B = { gm_bt_top \077 gm_bt_top->prev : NULL, ";
const uint8_t gm_sq0573h7j28rq[23] = "invalid profile name `";
const uint8_t gm_sq12h87ngj5nl[9] = ", true);";
const uint8_t gm_sq9f472jpmh59[64] = "`else` needs an optional or a result on its left, but this is `";
const uint8_t gm_sq9f8k00ocil6[35] = "` is private to its module (\302\2473.3)";
const uint8_t gm_sq9pusqm0kq2f[7] = "gm_out";
const uint8_t gm_sqa5rf17c57e4[5] = "`{}`";
const uint8_t gm_sqaji2d91cvct[19] = "; const void *argp";
const uint8_t gm_sqc0827akcmi8[40] = "this loop never ends: it has no `break`";
const uint8_t gm_sqhghq789nvm0[8] = "decimal";
const uint8_t gm_sqj9769nr8pm0[48] = "numeric field names may not have a leading zero";
const uint8_t gm_sqlorrkumh3an[23] = "` is not an identifier";
const uint8_t gm_sqmqlaupa64jq[7] = "cursor";
const uint8_t gm_sqpitork4qoam[21] = "%{} = aggregate ({})";
const uint8_t gm_sqqaphhmqv39s[5] = "&a{}";
const uint8_t gm_sqslcbfraugjg[8] = " type `";
const uint8_t gm_squv1ktk9abst[41] = "a bounds check against a length of type ";
const uint8_t gm_sqvgecd0v5l54[208] = "GM_NOINLINE {} {{\012    {} a;\012    GM_ST(void *, (uint8_t *)&a, (void *)(d));\012    GM_ST(uintptr_t, (uint8_t *)&a + {}, (uintptr_t)(n));\012    if (GM_PTR_TAGGED(code)) {}; else {};\012    return gm_unw.kind != 0;\012}}\012";
const uint8_t gm_sr00p3fdg5qn3[19] = " does not exist: `";
const uint8_t gm_sr04t1tcf6jsd[6] = " in `";
const uint8_t gm_sr1bm2v312fv6[15] = "UpperCamelCase";
const uint8_t gm_sr43s52rmecnf[33] = "`cache clean` takes no arguments";
const uint8_t gm_sr462lclo52fq[8] = ": \342\200\246)`";
const uint8_t gm_sr87tgv3ltq65[13] = "type-name {}";
const uint8_t gm_srav7444q331a[37] = "` cannot be constructed outside its ";
const uint8_t gm_srb1158jlk9u7[75] = "`#callconv` applies only to a function, a foreign function or a `^fn` type";
const uint8_t gm_sreop6h89ulb0[18] = " must be a string";
const uint8_t gm_srfagufvviad7[6] = "bench";
const uint8_t gm_srhms7dhd79bq[15] = "gm_clock_sleep";
const uint8_t gm_srjdrn9cldsu5[57] = "cannot read `{}`: No such file or directory (os error 2)";
const uint8_t gm_srq72vm6d96b6[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000I\001\000\000\021\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sru9l3qtrdjko[84] = "constant contains a pointer to memory that does not outlive compile-time evaluation";
const uint8_t gm_srv05plrkjhf8[7] = "  std\012";
const uint8_t gm_srv0b9jht81lt[18] = "_p, NULL, NULL};\012";
const uint8_t gm_ss1ntuh8relgg[19] = "{} = {} \077 {} : {};";
const uint8_t gm_ss3kah49g222u[33] = "(void)env; (void)cx; (void)inst;";
const uint8_t gm_ss3op0i298nem[69] = "void *env = fb->env; gm_node *cx = fb->cx; gm_node *inst = fb->inst;";
const uint8_t gm_ss7moisvq3l8h[6] = " is `";
const uint8_t gm_ssd7h3tuipgq5[55] = "` is not available on vectors: a lane cannot be `null`";
const uint8_t gm_sse5id8rmv069[8] = ", and `";
const uint8_t gm_ssi8bk0pvfr56[22] = "`{}` cannot be called";
const uint8_t gm_ssj1a8k9nspq4[82] = "attributes here must precede a `struct`, a `union`, an array type or a `^fn` type";
const uint8_t gm_ssmeaumlrdi7h[7] = "{} {};";
const uint8_t gm_ssnb4or831s5l[78] = "`#soa` precedes an array or slice type of a struct: `#soa [N]S` or `#soa []S`";
const uint8_t gm_ssob5j6sas4h7[58] = "The runtime of programs compiled by the Gamma C back end.";
const uint8_t gm_ssu25mmrd1p46[40] = "TOML parse error at line {}, column {}\012";
const uint8_t gm_st2s0ov9emddj[12] = "stable_sort";
const uint8_t gm_st2vlvkf674c2[250] = "`; they have `reduce_sum`, `reduce_min`, `reduce_max`, `min`, `max`, `abs`, `to`, `shuffle`, `store` and the comparisons `lanes_eq`, `lanes_ne`, `lanes_lt`, `lanes_le`, `lanes_gt` and `lanes_ge`, and masks `all`, `any`, `count` and `select` (\302\24721.3)";
const uint8_t gm_st518h9bgt9n7[22] = ", &ml, sizeof ml); }\012";
const uint8_t gm_st7d4oeklkoma[11] = "gm_rt_lock";
const uint8_t gm_stco696vp9tto[14] = "gm_buf_free(&";
const uint8_t gm_std2slsc0ssrl[11] = "gm_hb_slot";
const uint8_t gm_std59a9b6lpnn[10] = "ReduceSum";
const uint8_t gm_stdefoi5sk91c[12] = "ProcReadOut";
const uint8_t gm_stfhv28imtfkb[6] = "goto ";
const uint8_t gm_stjlu12rqiie7[13] = "--speed-time";
const uint8_t gm_stk678hojgu6q[24] = "from_tag on a non-union";
const uint8_t gm_stkh9r1ip5q2v[35] = "indexing this type at compile time";
const uint8_t gm_stmdki8kna56a[24] = "` is a foreign function";
const uint8_t gm_storp3otq7orh[59] = "static {}uint8_t gm_g{}[{}] __attribute__((aligned({})));\012";
const uint8_t gm_stpnhgp46l3tu[9] = "wasm-opt";
const uint8_t gm_strsld72u30ds[9] = "(double)";
const uint8_t gm_su01gdacg3ppp[9] = "_f[] = {";
const uint8_t gm_su1mil1q3eqgp[16] = "((uint8_t *){})";
const uint8_t gm_su61qr04nq335[13] = "gm_atag_used";
const uint8_t gm_su635cbqj0rpd[3] = " \077";
const uint8_t gm_su6s568b7b4kr[5] = "wn{}";
const uint8_t gm_sub1g4k5l4c07[11] = "into_slice";
const uint8_t gm_subcm6amchfvv[5] = "ULL)";
const uint8_t gm_sucs0qmdapqe8[41] = "expected a value, found the effect set `";
const uint8_t gm_sud4o49sgtb17[28] = "static inline {} {}({}) {{\012";
const uint8_t gm_suk7nc9hc39vu[13] = "parse_string";
const uint8_t gm_sup2jn5hml593[6] = "ident";
const uint8_t gm_suuphrufuvh4m[12] = "GM_LONG_FN ";
const uint8_t gm_sv0o0mfb8qq08[83] = "    uint8_t loc[{}] __attribute__((aligned({})));\012    memset(loc, 0, sizeof loc);\012";
const uint8_t gm_sv0pi8lprf2k8[55] = "` is not a target triple (such as `aarch64-linux-gnu`)";
const uint8_t gm_sv5o4caljvbmj[28] = "extra `=`, expected nothing";
const uint8_t gm_svaibtfuu0gf2[48] = "gamma: cannot create a temporary directory in `";
const uint8_t gm_svfa039r0e6te[33] = "invalid {} string, expected `{}`";
const uint8_t gm_svgn73l5s66lk[7] = "wasm32";
const uint8_t gm_svhavo13qha5k[4] = ")((";
const uint8_t gm_svkkls864r4t4[30] = "` does not fit the tag type `";
const uint8_t gm_svklio0qebpog[9] = "constant";
const uint8_t gm_svkrg5dctlh9m[7] = "{}[{}]";
const uint8_t gm_svl4l2v56ma30[8] = "effect ";
const uint8_t gm_svln5ui59rjil[6] = "Other";
const uint8_t gm_svmck4pckkpai[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\0002\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_svmo2cukmep6f[9] = "{}__size";
const uint8_t gm_svom2l7bom2pm[71] = "a variable declared with `:=` that is never assigned, so `::` would do";
const uint8_t gm_svp6nj851nh2i[29] = "const void *vp{}[] = {{{}}};";
const uint8_t gm_svsiu4jgdp97c[12] = " = variant ";
const uint8_t gm_svtut88rj5dr8[15] = "gamma: `gamma ";
const uint8_t gm_svu2guaq187mi[9] = "asyncify";
uint8_t gm_c0fhj0gi05s[2] __attribute__((aligned(1)));
uint8_t gm_c0m0l8q79po[17] __attribute__((aligned(1)));
uint8_t gm_c0u09tvsmdv[1] __attribute__((aligned(1)));
uint8_t gm_c2keo1qaake[4] __attribute__((aligned(1)));
uint8_t gm_c2t4q8gsqsl[3] __attribute__((aligned(1)));
uint8_t gm_c2vlljohddh[10] __attribute__((aligned(1)));
uint8_t gm_c32pk0vs0kc[5] __attribute__((aligned(1)));
uint8_t gm_c3i8biff6gm[11] __attribute__((aligned(1)));
uint8_t gm_c4crumb5mc9[1] __attribute__((aligned(1)));
uint8_t gm_c4f2cgckmo6[7] __attribute__((aligned(1)));
uint8_t gm_c5urm6ehcs8[8] __attribute__((aligned(1)));
uint8_t gm_c5v8jddvmh5[13] __attribute__((aligned(1)));
uint8_t gm_c6iib8ocmqh[11] __attribute__((aligned(1)));
uint8_t gm_c6iptqebh84[6] __attribute__((aligned(1)));
uint8_t gm_c708i8j8tcn[6] __attribute__((aligned(1)));
uint8_t gm_c71idar1l1k[1] __attribute__((aligned(1)));
uint8_t gm_c7379qlpin9[12] __attribute__((aligned(1)));
uint8_t gm_c781dt962ao[16] __attribute__((aligned(1)));
uint8_t gm_c8ps00qu8ur[9] __attribute__((aligned(1)));
uint8_t gm_cas4sbdf6ub[12] __attribute__((aligned(1)));
uint8_t gm_cbe5loot5ou[9] __attribute__((aligned(1)));
uint8_t gm_cbhtvjhhm2v[39] __attribute__((aligned(1)));
uint8_t gm_cc8hnl5rm6d[9] __attribute__((aligned(1)));
uint8_t gm_cc8mmf5go3e[4] __attribute__((aligned(1)));
uint8_t gm_cch1hm6o00s[13] __attribute__((aligned(1)));
uint8_t gm_ccimdvgv2uo[5] __attribute__((aligned(1)));
uint8_t gm_cdb2briosl4[12] __attribute__((aligned(1)));
uint8_t gm_cdg9l95tj3v[10] __attribute__((aligned(1)));
uint8_t gm_cdll3irir7s[10] __attribute__((aligned(1)));
uint8_t gm_cdvl4a09e32[18] __attribute__((aligned(1)));
uint8_t gm_cenbs8unhg6[4] __attribute__((aligned(1)));
uint8_t gm_ceperl1a3fj[5] __attribute__((aligned(1)));
uint8_t gm_cfd1ujo4cs9[4] __attribute__((aligned(1)));
uint8_t gm_cfj1ctfn4pv[16] __attribute__((aligned(8)));
uint8_t gm_cfosr5mhlig[11] __attribute__((aligned(1)));
uint8_t gm_cfsqere63u7[146] __attribute__((aligned(1)));
uint8_t gm_cftdgoeqqrn[12] __attribute__((aligned(1)));
uint8_t gm_cgmt38nbmrp[17] __attribute__((aligned(1)));
uint8_t gm_ch06gde2041[13] __attribute__((aligned(1)));
uint8_t gm_ch70o9r96kg[16] __attribute__((aligned(1)));
uint8_t gm_ci7lmabjiao[2] __attribute__((aligned(1)));
uint8_t gm_cijmd8makat[10] __attribute__((aligned(1)));
uint8_t gm_cilec57f4ei[8] __attribute__((aligned(8)));
uint8_t gm_ck2s18je8ki[4] __attribute__((aligned(1)));
uint8_t gm_ck6o0oplinp[1] __attribute__((aligned(1)));
uint8_t gm_ckus03er382_2[4] __attribute__((aligned(4)));
uint8_t gm_clj49qvgrpn[8] __attribute__((aligned(1)));
uint8_t gm_cm9aodfhip7[5] __attribute__((aligned(1)));
uint8_t gm_cmbh1midkkg[11] __attribute__((aligned(1)));
uint8_t gm_cmd0i1ng0uh[16] __attribute__((aligned(1)));
uint8_t gm_cml63tkso1b[6] __attribute__((aligned(1)));
uint8_t gm_cnb3jfedmgn[3] __attribute__((aligned(1)));
uint8_t gm_cnlt8qov7cb[16] __attribute__((aligned(1)));
uint8_t gm_cp1bmt1lpnn[15] __attribute__((aligned(1)));
uint8_t gm_cp1l69rfj3m[11] __attribute__((aligned(1)));
uint8_t gm_cq9lk6ce28a[4] __attribute__((aligned(1)));
uint8_t gm_cr9tkd1skqs[4] __attribute__((aligned(1)));
uint8_t gm_crfnqbinhm6[8] __attribute__((aligned(1)));
uint8_t gm_crthrtmpk9i[2] __attribute__((aligned(1)));
uint8_t gm_ctd4c52p0f8[60] __attribute__((aligned(1)));
uint8_t gm_ctkboukdglo[8] __attribute__((aligned(8)));
uint8_t gm_cv8a63bhkjv[7] __attribute__((aligned(1)));
uint8_t gm_k28ocradvls[40] __attribute__((aligned(8)));
uint8_t gm_k3h72k4hfps[40] __attribute__((aligned(8)));
uint8_t gm_k5u3oseesd7[40] __attribute__((aligned(8)));
uint8_t gm_k5upcce8mpd[40] __attribute__((aligned(8)));
uint8_t gm_k6pllmcjbks[40] __attribute__((aligned(8)));
uint8_t gm_k6t8ggbfl65[40] __attribute__((aligned(8)));
uint8_t gm_k9o17uvn1g2[40] __attribute__((aligned(8)));
uint8_t gm_kbamu2ordri[40] __attribute__((aligned(8)));
uint8_t gm_ke9p7o91039[40] __attribute__((aligned(8)));
uint8_t gm_kebr2u4173v[40] __attribute__((aligned(8)));
uint8_t gm_kh0pci1a0en[40] __attribute__((aligned(8)));
uint8_t gm_km0lis6h9ua[40] __attribute__((aligned(8)));
uint8_t gm_ku35m9tsdej[40] __attribute__((aligned(8)));
uint8_t gm_ku6cmvc6apj[40] __attribute__((aligned(8)));
void *const gm_hdvslis4o40u[] = {(void *)gm_dtvslis4o40u_0};
void *const gm_hl6s5gct9vkh[] = {(void *)gm_f_for_body_terksaa};
