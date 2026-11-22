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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; int32_t f1; uint8_t pe[4]; } gm_t43v1gep754th;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint8_t a[40]; } gm_t6kvj62qe5fp2;
typedef struct { uint8_t a[32]; } gm_t7mlrlibdq1ja;
typedef struct { int64_t a[3]; } gm_t7vno1ieejig6;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { float f0; } gm_tdqbns26udv5e;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t a[131]; } gm_tfbi275tn5ccf;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { int64_t f0; int32_t f1; uint8_t pe[4]; } gm_tnur0m5k1ttis;
typedef struct { __int128 f0; } gm_tpl3q6e8s9qo1;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t43v1gep754th) == 16 && _Alignof(gm_t43v1gep754th) == 8 && offsetof(gm_t43v1gep754th, f0) == 0 && offsetof(gm_t43v1gep754th, f1) == 8, "layout of `(n: usize, exp: i32)`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t6kvj62qe5fp2) == 40 && _Alignof(gm_t6kvj62qe5fp2) == 1 && offsetof(gm_t6kvj62qe5fp2, a) == 0, "layout of `[40]u8`");
_Static_assert(sizeof(gm_t7mlrlibdq1ja) == 32 && _Alignof(gm_t7mlrlibdq1ja) == 1 && offsetof(gm_t7mlrlibdq1ja, a) == 0, "layout of `[32]u8`");
_Static_assert(sizeof(gm_t7vno1ieejig6) == 24 && _Alignof(gm_t7vno1ieejig6) == 8 && offsetof(gm_t7vno1ieejig6, a) == 0, "layout of `[3]i64`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tdqbns26udv5e) == 4 && _Alignof(gm_tdqbns26udv5e) == 4 && offsetof(gm_tdqbns26udv5e, f0) == 0, "layout of `(f32,)`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
_Static_assert(sizeof(gm_tfbi275tn5ccf) == 131 && _Alignof(gm_tfbi275tn5ccf) == 1 && offsetof(gm_tfbi275tn5ccf, a) == 0, "layout of `[131]u8`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tnur0m5k1ttis) == 16 && _Alignof(gm_tnur0m5k1ttis) == 8 && offsetof(gm_tnur0m5k1ttis, f0) == 0 && offsetof(gm_tnur0m5k1ttis, f1) == 8, "layout of `(i64, i32)`");
_Static_assert(sizeof(gm_tpl3q6e8s9qo1) == 16 && _Alignof(gm_tpl3q6e8s9qo1) == 16 && offsetof(gm_tpl3q6e8s9qo1, f0) == 0, "layout of `(i128,)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; double v1; } f1; } gm_tspt3vn9ljcnd;
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tspt3vn9ljcnd) == 24 && _Alignof(gm_tspt3vn9ljcnd) == 8 && offsetof(gm_tspt3vn9ljcnd, f0) == 0 && offsetof(gm_tspt3vn9ljcnd, f1) == 8, "layout of `ParseError!f64`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_dec_395otau {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_deinit_r70pji5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fixed_buf_len_gor7a4d {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_fmt_adt_p6448k2 {
    gm_frame h;
    gm_tpautr4br7h3l l5; /* def */
    gm_t0mglksrkfa5e l10; /* f */
    gm_tl572d7579hns ret;
};
struct gm_fr_fmt_float_1ivmudg {
    gm_frame h;
    gm_t6kvj62qe5fp2 l3; /* dig */
    gm_b0_1 ret;
};
struct gm_fr_fmt_float_bits_9d6f5r5 {
    gm_frame h;
    double ret;
};
struct gm_fr_fmt_float_spec_glt99ob {
    gm_frame h;
    gm_t6kvj62qe5fp2 l9; /* dig */
    gm_b0_1 ret;
};
struct gm_fr_fmt_int_svhqra7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fmt_pad_ev4lnnu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fmt_pow2_1p38akk {
    gm_frame h;
    double ret;
};
struct gm_fr_fmt_quote_f7sotri {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fmt_seq_c3i8abi {
    gm_frame h;
    gm_tl572d7579hns ret;
};
struct gm_fr_fmt_shortest16_cln2heq {
    gm_frame h;
    gm_t43v1gep754th ret;
};
struct gm_fr_fmt_shortest_2l54as9 {
    gm_frame h;
    gm_t43v1gep754th ret;
};
struct gm_fr_fmt_split_exp_0kapfov {
    gm_frame h;
    gm_t43v1gep754th ret;
};
struct gm_fr_fmt_valid_utf8_9mug7al {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_format_13oum9l {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eucvbnh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h8e7ps1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_odb8fco {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_spec_7aph60t {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* body */
    gm_tfbi275tn5ccf l12; /* buf */
    gm_trqfvmjtt4sij l17; /* head */
    gm_tl572d7579hns ret;
};
struct gm_fr_format_value_vmle7qr {
    gm_frame h;
    gm_tpekhfs32op0f l11; /* e */
    gm_tl572d7579hns ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_payload_offset_la0c8ft {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sci_buf_len_tedn1r4 {
    gm_frame h;
    uint64_t ret;
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
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_write_1599ben {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_cnos3th {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_fsqql49 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_jkg06fo {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_jvie1fn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_kvqtui9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_adt_of_8k5vqud(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dec_395otau(int64_t a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_field_bytes_bhivr4s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_fixed_buf_len_gor7a4d(uint32_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fixed_rppim5r(double a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_fmt_adt_p6448k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_float_1ivmudg(void *env, gm_node *cx, gm_node *inst, void * a0, double a1, uint16_t a2);
__attribute__((aligned(4))) double gm_f_fmt_float_bits_9d6f5r5(void * a0, uint16_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_float_spec_glt99ob(void *env, gm_node *cx, gm_node *inst, void * a0, double a1, uint16_t a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_int_svhqra7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_pad_ev4lnnu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint32_t a3, uint8_t a4, gm_t9qkh0rsooc8d a5, uint8_t a6, uint8_t a7);
inline __attribute__((aligned(4))) double gm_f_fmt_pow2_1p38akk(int32_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_quote_f7sotri(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_fmt_seq_c3i8abi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, gm_b16_8 a4, uint64_t a5);
__attribute__((aligned(4))) gm_t43v1gep754th gm_f_fmt_shortest16_cln2heq(void *env, gm_node *cx, gm_node *inst, double a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t43v1gep754th gm_f_fmt_shortest_2l54as9(void *env, gm_node *cx, gm_node *inst, double a0, uint16_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t43v1gep754th gm_f_fmt_split_exp_0kapfov(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_fmt_valid_utf8_9mug7al(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_format_13oum9l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnur0m5k1ttis a0);
__attribute__((aligned(4))) void gm_f_format_3fbeiu9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnur0m5k1ttis a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_4mujj14(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tdqbns26udv5e a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_6dvr333(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_eucvbnh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
inline __attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_odb8fco(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tdqbns26udv5e a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_spec_7aph60t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) void gm_f_format_value_vmle7qr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_invalid_tag_text_f3l5hi7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_fill_jpvm0kc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint8_t a1, const void * a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_parse_float_17hgksa(void *gm_out, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_radix_with_e6f5j99(int64_t a0, uint32_t a1, uint8_t a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_radix_with_qaamfpk(unsigned __int128 a0, uint32_t a1, uint8_t a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_read_field_ua0742c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_sci_buf_len_tedn1r4(uint32_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_sci_oikoiqs(double a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_variant_index_s2p6fs4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_1599ben(void * a0, gm_tsn2omssg6cpf a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cnos3th(void * a0, gm_tpl3q6e8s9qo1 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dnr2f82(void * a0, gm_tpl3q6e8s9qo1 a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fsqql49(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hqo1u7h(void * a0, gm_t4ce0jd3tdro4 a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jkg06fo(void * a0, gm_t19enj90vg8bd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jvie1fn(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_k2pnevr(void * a0, gm_t19enj90vg8bd a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kvqtui9(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_mmjltmi(void * a0, gm_tsn2omssg6cpf a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uqn5uk3(void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_b16_8 a3);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_k30q9glr4ue[40] __attribute__((aligned(8)));
extern uint8_t gm_k3pvi109nh9[40] __attribute__((aligned(8)));
extern uint8_t gm_ka73u23j5nf[40] __attribute__((aligned(8)));
extern uint8_t gm_kbfuu8gel4m[40] __attribute__((aligned(8)));
extern uint8_t gm_kkpcsaqkj4k[40] __attribute__((aligned(8)));
extern uint8_t gm_km7a24is5mf[40] __attribute__((aligned(8)));
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s2v5amvfkfoll[3];
extern const uint8_t gm_s3erfe82rop7u[5];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s70bck1t85qpv[9];
extern const uint8_t gm_s7cvig9qav6l5[3];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_s9lclteeapkg2[3];
extern const uint8_t gm_sc6st5mdtp49u[16];
extern const uint8_t gm_scf7jg05jni41[5];
extern const uint8_t gm_schmp66oj2del[6];
extern const uint8_t gm_scs3asa1395s7[6];
extern const uint8_t gm_sdc2n4tsqs8nn[4];
extern const uint8_t gm_sdk5n6a42f7nl[3];
extern const uint8_t gm_segch6a5dme9u[5];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_seng063nd6hio[4];
extern const uint8_t gm_sfa86erpod86s[4];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sjq55q61p743j[3];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_sns75reqq32d2[4];
extern const uint8_t gm_so4mj8r7k5aek[3];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_sqp26euqk4rkd[16];
extern const uint8_t gm_squedv51820an[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcfde9lsgge1ld[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcrsgf2gf2b1dq[];
extern const gm_tmpl gm_tpl1gtefnja08ps;
extern const gm_tmpl gm_tpl2dia2gbnfh9l;
extern const gm_tmpl gm_tplfoosl93e8k5t;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplk83cf3bpuiqb;
extern const gm_tmpl gm_tplmrpf5m9qkka0;
extern const gm_tmpl gm_tplodl7i2apj0bs;
extern const gm_tmpl gm_tplvl1jqbb33ag2;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq9oqmmiu8qc(a + 2, b + 2)) return false;
    return true;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    uint8_t t3 = (t2 <= ((uint64_t)1ULL));
    if (t3) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    uint64_t t4 = gm_l1;
    uint64_t t5 = gm_l1;
    uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
    if (o7) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
    uint64_t t8 = r6;
    uint64_t t9 = (uint64_t)(t4 & t8);
    uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
    if (t10) {
        uint64_t t11 = gm_l0;
        uint64_t t12 = gm_l1;
        uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
        if (o14) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
        uint64_t t15 = r13;
        uint64_t t16 = (uint64_t)(t11 & t15);
        uint8_t t17 = ((t16 == ((uint64_t)0ULL)));
        F.ret = t17;
        goto gm_fret;
    }
    uint64_t t18 = gm_l0;
    uint64_t t19 = gm_l1;
    if (t19 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
    uint64_t t20 = (uint64_t)(t18 % t19);
    uint8_t t21 = ((t20 == ((uint64_t)0ULL)));
    F.ret = t21;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dec_395otau(int64_t a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_dec_395otau F;
    int64_t gm_l0; /* x */
    gm_b16_8 gm_l1; /* buf */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int64_t t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_radix_with_e6f5j99(t2, ((uint32_t)10ULL), ((uint8_t)0), ((uint8_t)0), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_r70pji5 F;
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
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)1ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)1ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_ijtp9lo();
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
inline __attribute__((aligned(4))) uint64_t gm_f_fixed_buf_len_gor7a4d(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fixed_buf_len_gor7a4d F;
    uint32_t gm_l0; /* decimals */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t2 = gm_l0;
    uint64_t t3 = (uint64_t)t2;
    uint64_t r4; bool o5 = __builtin_add_overflow(t3, ((uint64_t)312ULL), &r4);
    if (o5) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 447, 52);
    uint64_t t6 = r4;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_fmt_pow2_1p38akk(int32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fmt_pow2_1p38akk F;
    int32_t gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    int32_t t2 = gm_l0;
    int32_t r3; bool o4 = __builtin_add_overflow(t2, ((int32_t)1023LL), &r3);
    if (o4) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 194, 36);
    int32_t t5 = r3;
    uint64_t t6 = (uint64_t)t5;
    unsigned __int128 n7 = (unsigned __int128)((uint64_t)52ULL);
    if (n7 >= 64) gm_trap_shift((uint64_t)n7, "u64", gm_srcfde9lsgge1ld, 194, 35);
    uint64_t r8 = (uint64_t)((uint64_t)t6 << (int)n7);
    bool o9 = (uint64_t)(r8 >> (int)n7) != t6;
    if (o9) gm_trap("integer overflow in `<<`", gm_srcfde9lsgge1ld, 194, 35);
    uint64_t t10 = r8;
    double t11;
    memset(&t11, 0, sizeof t11); memcpy(&t11, ((uint8_t *)&t10), sizeof t11 < 8 ? sizeof t11 : 8);
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_13oum9l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnur0m5k1ttis a0) {
    struct gm_fr_format_13oum9l F;
    gm_tnur0m5k1ttis gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tnur0m5k1ttis gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnur0m5k1ttis t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scs3asa1395s7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_f_format_3fbeiu9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmrpf5m9qkka0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_eucvbnh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0) {
    struct gm_fr_format_eucvbnh F;
    gm_tidu2er0d7h95 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tidu2er0d7h95 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tidu2er0d7h95 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scf7jg05jni41));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_6dvr333((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl2dia2gbnfh9l), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h8e7ps1 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc6st5mdtp49u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvl1jqbb33ag2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_odb8fco(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tdqbns26udv5e a0) {
    struct gm_fr_format_odb8fco F;
    gm_tdqbns26udv5e gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tdqbns26udv5e gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 29u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tdqbns26udv5e t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scf7jg05jni41));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_4mujj14((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl2dia2gbnfh9l), t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t3 = (t2 <= ((uint64_t)8ULL));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        uint64_t t6 = gm_f_rd_word_8flcq6o(t4, t5);
        gm_l2 = t6;
    } else {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_l1;
        unsigned __int128 t9 = gm_f_get_uint_bq8c4b7(t7, t8);
        uint64_t t10 = (uint64_t)t9;
        gm_l2 = t10;
    }
    uint64_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
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
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t3, t4);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_kpppm1j F;
    void * gm_l0; /* m */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 360) + 40));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 360) + 48));
    gm_t2lqu8jsj511d t6;
    GM_ST(uint64_t, ((uint8_t *)&t6 + 0), t3);
    GM_ST(uint64_t, ((uint8_t *)&t6 + 8), t5);
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
gm_fprop:
    return GM_CONV(gm_csc7lc6jlkqkuq, F.ret);
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_qpt7c30 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 16);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_of_e6hfj1r((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 16));
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
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_offset_la0c8ft F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l3;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t4;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t6 = (t5 > ((uint64_t)1ULL));
    if (t6) {
        void * t7 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + (uintptr_t)((uint64_t)1ULL) * 8));
        gm_l3 = t9;
    } else {
        gm_l3 = ((uint64_t)0ULL);
    }
    uint64_t t10 = gm_l3;
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 176));
    uint64_t t7 = gm_f_us_cuv01fp(t6);
    uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t9 = (uintptr_t)t7;
    if (t8 > t9 || t9 > (uintptr_t)t4) gm_trap_slice(t8, t9, t4, gm_srcnqtpfma6qe4a, 104, 68);
    uint64_t t10 = ((uintptr_t)t7) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t3 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
    uint64_t t12 = gm_f_get_u64_tgpb2vh(t2, t11);
    F.ret = t12;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 89));
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t5 = ((t4 == ((uint64_t)8ULL)));
        if (t5) {
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t7, gm_srcnqtpfma6qe4a, 316, 36);
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t9 = (uint64_t)t8;
            void * t10 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t11, gm_srcnqtpfma6qe4a, 316, 49);
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t13 = (uint64_t)t12;
            unsigned __int128 n14 = (unsigned __int128)((uint64_t)8ULL);
            if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t r15 = (uint64_t)((uint64_t)t13 << (int)n14);
            bool o16 = (uint64_t)(r15 >> (int)n14) != t13;
            if (o16) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t t17 = r15;
            uint64_t t18 = (uint64_t)(t9 | t17);
            void * t19 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t20, gm_srcnqtpfma6qe4a, 316, 68);
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t19) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t22 = (uint64_t)t21;
            unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
            if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t r24 = (uint64_t)((uint64_t)t22 << (int)n23);
            bool o25 = (uint64_t)(r24 >> (int)n23) != t22;
            if (o25) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t t26 = r24;
            uint64_t t27 = (uint64_t)(t18 | t26);
            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t29, gm_srcnqtpfma6qe4a, 316, 88);
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t31 = (uint64_t)t30;
            unsigned __int128 n32 = (unsigned __int128)((uint64_t)24ULL);
            if (n32 >= 64) gm_trap_shift((uint64_t)n32, "u64", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t r33 = (uint64_t)((uint64_t)t31 << (int)n32);
            bool o34 = (uint64_t)(r33 >> (int)n32) != t31;
            if (o34) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t t35 = r33;
            uint64_t t36 = (uint64_t)(t27 | t35);
            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t38, gm_srcnqtpfma6qe4a, 316, 108);
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)((uint64_t)4ULL) * 1));
            uint64_t t40 = (uint64_t)t39;
            unsigned __int128 n41 = (unsigned __int128)((uint64_t)32ULL);
            if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t r42 = (uint64_t)((uint64_t)t40 << (int)n41);
            bool o43 = (uint64_t)(r42 >> (int)n41) != t40;
            if (o43) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t t44 = r42;
            uint64_t t45 = (uint64_t)(t36 | t44);
            void * t46 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t47, gm_srcnqtpfma6qe4a, 316, 128);
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t46) + (uintptr_t)((uint64_t)5ULL) * 1));
            uint64_t t49 = (uint64_t)t48;
            unsigned __int128 n50 = (unsigned __int128)((uint64_t)40ULL);
            if (n50 >= 64) gm_trap_shift((uint64_t)n50, "u64", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t r51 = (uint64_t)((uint64_t)t49 << (int)n50);
            bool o52 = (uint64_t)(r51 >> (int)n50) != t49;
            if (o52) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t t53 = r51;
            uint64_t t54 = (uint64_t)(t45 | t53);
            void * t55 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)6ULL) >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)((uint64_t)6ULL), t56, gm_srcnqtpfma6qe4a, 316, 148);
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)((uint64_t)6ULL) * 1));
            uint64_t t58 = (uint64_t)t57;
            unsigned __int128 n59 = (unsigned __int128)((uint64_t)48ULL);
            if (n59 >= 64) gm_trap_shift((uint64_t)n59, "u64", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t r60 = (uint64_t)((uint64_t)t58 << (int)n59);
            bool o61 = (uint64_t)(r60 >> (int)n59) != t58;
            if (o61) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t t62 = r60;
            uint64_t t63 = (uint64_t)(t54 | t62);
            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)7ULL) >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)((uint64_t)7ULL), t65, gm_srcnqtpfma6qe4a, 316, 168);
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t64) + (uintptr_t)((uint64_t)7ULL) * 1));
            uint64_t t67 = (uint64_t)t66;
            unsigned __int128 n68 = (unsigned __int128)((uint64_t)56ULL);
            if (n68 >= 64) gm_trap_shift((uint64_t)n68, "u64", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t r69 = (uint64_t)((uint64_t)t67 << (int)n68);
            bool o70 = (uint64_t)(r69 >> (int)n68) != t67;
            if (o70) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t t71 = r69;
            uint64_t t72 = (uint64_t)(t63 | t71);
            F.ret = t72;
            goto gm_fret;
        }
        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t74 = ((t73 == ((uint64_t)4ULL)));
        if (t74) {
            void * t75 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t76, gm_srcnqtpfma6qe4a, 317, 36);
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)t75) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t78 = (uint64_t)t77;
            void * t79 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t80, gm_srcnqtpfma6qe4a, 317, 49);
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t79) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t82 = (uint64_t)t81;
            unsigned __int128 n83 = (unsigned __int128)((uint64_t)8ULL);
            if (n83 >= 64) gm_trap_shift((uint64_t)n83, "u64", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t r84 = (uint64_t)((uint64_t)t82 << (int)n83);
            bool o85 = (uint64_t)(r84 >> (int)n83) != t82;
            if (o85) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t t86 = r84;
            uint64_t t87 = (uint64_t)(t78 | t86);
            void * t88 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t89, gm_srcnqtpfma6qe4a, 317, 68);
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)t88) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t91 = (uint64_t)t90;
            unsigned __int128 n92 = (unsigned __int128)((uint64_t)16ULL);
            if (n92 >= 64) gm_trap_shift((uint64_t)n92, "u64", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t r93 = (uint64_t)((uint64_t)t91 << (int)n92);
            bool o94 = (uint64_t)(r93 >> (int)n92) != t91;
            if (o94) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t t95 = r93;
            uint64_t t96 = (uint64_t)(t87 | t95);
            void * t97 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t98)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t98, gm_srcnqtpfma6qe4a, 317, 88);
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)t97) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t100 = (uint64_t)t99;
            unsigned __int128 n101 = (unsigned __int128)((uint64_t)24ULL);
            if (n101 >= 64) gm_trap_shift((uint64_t)n101, "u64", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t r102 = (uint64_t)((uint64_t)t100 << (int)n101);
            bool o103 = (uint64_t)(r102 >> (int)n101) != t100;
            if (o103) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t t104 = r102;
            uint64_t t105 = (uint64_t)(t96 | t104);
            F.ret = t105;
            goto gm_fret;
        }
        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t107 = ((t106 == ((uint64_t)1ULL)));
        if (t107) {
            void * t108 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t109, gm_srcnqtpfma6qe4a, 318, 36);
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)t108) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t111 = (uint64_t)t110;
            F.ret = t111;
            goto gm_fret;
        }
        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t113 = ((t112 == ((uint64_t)2ULL)));
        if (t113) {
            void * t114 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t115, gm_srcnqtpfma6qe4a, 319, 36);
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)t114) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t117 = (uint64_t)t116;
            void * t118 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t119)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t119, gm_srcnqtpfma6qe4a, 319, 49);
            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)t118) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t121 = (uint64_t)t120;
            unsigned __int128 n122 = (unsigned __int128)((uint64_t)8ULL);
            if (n122 >= 64) gm_trap_shift((uint64_t)n122, "u64", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t r123 = (uint64_t)((uint64_t)t121 << (int)n122);
            bool o124 = (uint64_t)(r123 >> (int)n122) != t121;
            if (o124) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t t125 = r123;
            uint64_t t126 = (uint64_t)(t117 | t125);
            F.ret = t126;
            goto gm_fret;
        }
    }
    void * t127 = gm_l0;
    gm_b16_8 t128 = gm_l1;
    unsigned __int128 t129 = gm_f_get_uint_bq8c4b7(t127, t128);
    uint64_t t130 = (uint64_t)t129;
    F.ret = t130;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l5;
    {
        {
            uint64_t t6 = gm_l2;
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t8 <= ((uint64_t)1ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = gm_f_empty_slice_ijtp9lo();
            gm_t934k1j7u70ct t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t11 + 40, 0);
            ((uint8_t *)&t11)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 8), t10);
            memmove(gm_out, ((uint8_t *)&t11), 48);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    uint64_t t12 = gm_l1;
    uint64_t t13 = gm_l3;
    uint8_t t14 = gm_f_aligned_q6sor4h(t12, t13);
    if (t14) {
        {
            {
                void * t15 = gm_l0;
                uint64_t t16 = gm_l1;
                uint64_t t17 = gm_l2;
                void * t18 = gm_f_view_qalkbv1(t15, t16, t17, ((uint8_t)0));
                gm_l5 = t18;
                uint8_t t19 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                if (!t19) {
                    goto gm_lb4;
                }
                void * t20 = GM_LD(void *, ((uint8_t *)&gm_l5));
                gm_l4 = t20;
                void * t21 = gm_l4;
                uint64_t t22 = gm_l2;
                uint64_t t23 = gm_f_us_cuv01fp(t22);
                uint64_t t24 = ((uintptr_t)t23) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t25;
                GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
                gm_t934k1j7u70ct t26;
                GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 40, 0);
                ((uint8_t *)&t26)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t26 + 8), t25);
                memmove(gm_out, ((uint8_t *)&t26), 48);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    void * t27 = gm_l0;
    uint64_t t28 = gm_l1;
    uint64_t t29 = gm_l2;
    uint64_t t30 = gm_l3;
    gm_tpekhfs32op0f t31;
    gm_f_fault_u31b92l((void *)&t31, t27, t28, t29, t30, ((uint8_t)0));
    gm_t934k1j7u70ct t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
    ((uint8_t *)&t32)[0] = 1;
    GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t32 + 8), t31);
    memmove(gm_out, ((uint8_t *)&t32), 48);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_release_jn5kqdi F;
    void * gm_l0; /* m */
    gm_t2lqu8jsj511d gm_l1; /* mk */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
    GM_ST(uint64_t, ((((uint8_t *)t2) + 360) + 40), t3);
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    GM_ST(uint64_t, ((((uint8_t *)t4) + 360) + 48), t5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint64_t gm_f_sci_buf_len_tedn1r4(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sci_buf_len_tedn1r4 F;
    uint32_t gm_l0; /* digits */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t t2 = gm_l0;
    uint64_t t3 = (uint64_t)t2;
    uint64_t r4; bool o5 = __builtin_add_overflow(t3, ((uint64_t)8ULL), &r4);
    if (o5) gm_trap("integer overflow in `+`", gm_srcrsgf2gf2b1dq, 451, 48);
    uint64_t t6 = r4;
    F.ret = t6;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 524u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l8;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 360);
    gm_l2 = t3;
    void * t4 = gm_l2;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
    void * t7 = gm_l2;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 40));
    if ((uintptr_t)t8 >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)t8, t6, gm_srcpit8mdn0781q, 387, 14);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t5) + (uintptr_t)t8 * 16));
    gm_l3 = t9;
    void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t11 = (uintptr_t)t10;
    gm_l4 = t11;
    uint64_t t12 = gm_l4;
    void * t13 = gm_l2;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
    uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t17 = r15;
    uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
    if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t20 = r18;
    uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
    uint64_t t22 = (uint64_t)~t21;
    uint64_t t23 = (uint64_t)(t20 & t22);
    uint64_t t24 = gm_l4;
    uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
    if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 391, 12);
    uint64_t t27 = r25;
    gm_l5 = t27;
    uint64_t t28 = gm_l1;
    uint64_t t29 = gm_f_us_cuv01fp(t28);
    gm_l6 = t29;
    uint64_t t30 = gm_l1;
    uint8_t t31 = ((t30 == ((uint64_t)0ULL)));
    if (t31) {
        gm_l8 = ((uint8_t)1);
    } else {
        uint64_t t32 = gm_l5;
        uint64_t t33 = gm_l6;
        uint64_t r34; bool o35 = __builtin_add_overflow(t32, t33, &r34);
        if (o35) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 18);
        uint64_t t36 = r34;
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t38 = (t36 > t37);
        gm_l8 = t38;
    }
    uint8_t t39 = gm_l8;
    if (t39) {
        void * t40 = gm_l0;
        uint64_t t41 = gm_l1;
        gm_b16_8 t42 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t42;
        goto gm_fret;
    }
    void * t43 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t45 = gm_l5;
    uint64_t t46 = gm_l5;
    uint64_t t47 = gm_l6;
    uint64_t r48; bool o49 = __builtin_add_overflow(t46, t47, &r48);
    if (o49) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 21);
    uint64_t t50 = r48;
    uint64_t t51 = (uintptr_t)t45;
    uint64_t t52 = (uintptr_t)t50;
    if (t51 > t52 || t52 > (uintptr_t)t44) gm_trap_slice(t51, t52, t44, gm_srcpit8mdn0781q, 394, 10);
    uint64_t t53 = ((uintptr_t)t50) - (uintptr_t)t45;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t43 + (uintptr_t)t45 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
    gm_l7 = t54;
    void * t55 = gm_l2;
    uint64_t t56 = gm_l5;
    uint64_t t57 = gm_l6;
    uint64_t r58; bool o59 = __builtin_add_overflow(t56, t57, &r58);
    if (o59) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 395, 14);
    uint64_t t60 = r58;
    GM_ST(uint64_t, (((uint8_t *)t55) + 48), t60);
    gm_b16_8 t61 = gm_l7;
    gm_f_fill_abmf5g5(t61, ((uint8_t)0ULL));
    gm_b16_8 t62 = gm_l7;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t2 = gm_l0;
    uint64_t t3 = (uint64_t)t2;
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_qalkbv1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint8_t gm_l3; /* write */
    uint64_t gm_l4; /* p */
    void * gm_l5; /* pg */
    void * gm_l6; /* ss */
    uint64_t gm_l7; /* rel */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* s */
    uint64_t gm_l10; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 538u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
    uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
    uint64_t t7 = r5;
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)gm_c6p4chcutca));
    unsigned __int128 n9 = (unsigned __int128)t8;
    if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 206, 10);
    uint64_t t10 = (uint64_t)(t7 >> (int)n9);
    gm_l4 = t10;
    uint64_t t11 = gm_l4;
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
    uint64_t t14 = (uint64_t)t13;
    uint8_t t15 = (t11 >= t14);
    if (t15) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = gm_l4;
    uint64_t t19 = gm_f_us_cuv01fp(t18);
    void * t20 = (void *)(((uint8_t *)t17) + (uintptr_t)t19 * 32);
    gm_l5 = t20;
    void * t21 = gm_l5;
    uint8_t t22 = (uint8_t)(GM_LD(void *, ((uint8_t *)t21)) != 0);
    if (t22) {
        void * t23 = GM_LD(void *, ((uint8_t *)t21));
        gm_l11 = t23;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t24 = gm_l11;
    gm_l6 = t24;
    uint64_t t25 = gm_l1;
    void * t26 = gm_l5;
    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 8));
    uint64_t r28; bool o29 = __builtin_sub_overflow(t25, t27, &r28);
    if (o29) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
    uint64_t t30 = r28;
    gm_l7 = t30;
    uint64_t t31 = gm_l7;
    void * t32 = gm_l5;
    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 16));
    unsigned __int128 n34 = (unsigned __int128)t33;
    if (n34 >= 64) gm_trap_shift((uint64_t)n34, "u64", gm_srcq7ohh8h77bn7, 211, 10);
    uint64_t t35 = (uint64_t)(t31 >> (int)n34);
    gm_l8 = t35;
    void * t36 = gm_l6;
    uint64_t t37 = gm_l8;
    uint64_t t38 = gm_f_us_cuv01fp(t37);
    void * t39 = (void *)(((uint8_t *)t36) + (uintptr_t)t38 * 24);
    gm_l9 = t39;
    uint64_t t40 = gm_l7;
    uint64_t t41 = gm_l8;
    void * t42 = gm_l5;
    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)t42) + 16));
    unsigned __int128 n44 = (unsigned __int128)t43;
    if (n44 >= 64) gm_trap_shift((uint64_t)n44, "u64", gm_srcq7ohh8h77bn7, 213, 19);
    uint64_t r45 = (uint64_t)((uint64_t)t41 << (int)n44);
    bool o46 = (uint64_t)(r45 >> (int)n44) != t41;
    if (o46) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
    uint64_t t47 = r45;
    uint64_t r48; bool o49 = __builtin_sub_overflow(t40, t47, &r48);
    if (o49) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
    uint64_t t50 = r48;
    gm_l10 = t50;
    void * t51 = gm_l9;
    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)t51) + 17));
    uint8_t t53 = (uint8_t)!t52;
    if (t53) {
        gm_l14 = ((uint8_t)1);
    } else {
        uint64_t t54 = gm_l2;
        void * t55 = gm_l9;
        uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 8));
        uint8_t t57 = (t54 > t56);
        gm_l14 = t57;
    }
    uint8_t t58 = gm_l14;
    if (t58) {
        gm_l13 = ((uint8_t)1);
    } else {
        uint64_t t59 = gm_l10;
        void * t60 = gm_l9;
        uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)t60) + 8));
        uint64_t t62 = gm_l2;
        uint64_t r63; bool o64 = __builtin_sub_overflow(t61, t62, &r63);
        if (o64) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
        uint64_t t65 = r63;
        uint8_t t66 = (t59 > t65);
        gm_l13 = t66;
    }
    uint8_t t67 = gm_l13;
    if (t67) {
        gm_l12 = ((uint8_t)1);
    } else {
        uint8_t t68 = gm_l3;
        if (t68) {
            void * t69 = gm_l9;
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)t69) + 18));
            uint8_t t71 = (uint8_t)!t70;
            gm_l15 = t71;
        } else {
            gm_l15 = ((uint8_t)0);
        }
        uint8_t t72 = gm_l15;
        gm_l12 = t72;
    }
    uint8_t t73 = gm_l12;
    if (t73) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t74 = gm_l9;
    void * t75 = GM_LD(void *, ((uint8_t *)t74));
    uint64_t t76 = gm_l10;
    uint64_t t77 = gm_f_us_cuv01fp(t76);
    void * t78 = (void *)(((uint8_t *)t75) + (uintptr_t)t77 * 1);
    void * t79 = t78;
    F.ret = t79;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_1599ben(void * a0, gm_tsn2omssg6cpf a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_1599ben F;
    void * gm_l0; /* l */
    gm_tsn2omssg6cpf gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_tsn2omssg6cpf gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tsn2omssg6cpf t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(2));
    gm_tftb1odc739t5 t5 = gm_f_write_mmjltmi(t2, t3, ((void *)&gm_tplk34ojqiaho84), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cnos3th(void * a0, gm_tpl3q6e8s9qo1 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_cnos3th F;
    void * gm_l0; /* l */
    gm_tpl3q6e8s9qo1 gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_tpl3q6e8s9qo1 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tpl3q6e8s9qo1 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(2));
    gm_tftb1odc739t5 t5 = gm_f_write_dnr2f82(t2, t3, ((void *)&gm_tplk34ojqiaho84), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fsqql49(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_fsqql49 F;
    void * gm_l0; /* l */
    gm_t9qkh0rsooc8d gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t9qkh0rsooc8d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s70bck1t85qpv));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(8));
    gm_tftb1odc739t5 t5 = gm_f_write_uqn5uk3(t2, t3, ((void *)&gm_tpl1gtefnja08ps), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jkg06fo(void * a0, gm_t19enj90vg8bd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_jkg06fo F;
    void * gm_l0; /* l */
    gm_t19enj90vg8bd gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t19enj90vg8bd gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t19enj90vg8bd t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_seng063nd6hio));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_tftb1odc739t5 t5 = gm_f_write_k2pnevr(t2, t3, ((void *)&gm_tplk83cf3bpuiqb), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jvie1fn(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_jvie1fn F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s3erfe82rop7u));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(4));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplodl7i2apj0bs), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kvqtui9(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_kvqtui9 F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 63u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_schmp66oj2del));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tplfoosl93e8k5t), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_fmt_adt_p6448k2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_fmt_adt_p6448k2 F;
    void * gm_l0; /* m */
    void * gm_l1; /* out */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    void * gm_l4; /* a */
    uint8_t gm_l6; /* first */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_t2lqu8jsj511d gm_l11; /* mk */
    gm_b16_8 gm_l12; /* fb */
    gm_tl572d7579hns gm_l13; /* r */
    gm_t88rcucg1b7uk gm_l14; /* tag */
    gm_b16_8 gm_l15; /* variants */
    uint32_t gm_l16; /* v */
    gm_t88rcucg1b7uk gm_l17; /* pt */
    uint64_t gm_l18; /* off */
    gm_b16_8 gm_l19; /* pb */
    gm_b16_8 gm_l20; /* elems */
    gm_b16_8 gm_l21; /* names */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_t88rcucg1b7uk gm_l25; /* et */
    GM_STACK_CHECK(&F, 2348u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l26;
    void * gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_t8ta482bl6be0 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    uint8_t gm_l35;
    gm_tkn42u7na3t81 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_t9805qtonbsag gm_l38;
    uint32_t gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_t53at80pfdus3 gm_l43;
    gm_tlcm4lvu3g1a8 gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_tftb1odc739t5 gm_l46;
    gm_tftb1odc739t5 gm_l47;
    gm_tl572d7579hns gm_l48;
    gm_tl572d7579hns gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    {
        void * t23 = gm_l0;
        gm_t88rcucg1b7uk t24 = gm_l2;
        void * t25 = gm_f_adt_of_8k5vqud(t23, GM_CONV(gm_csguankko4lmhd, t24));
        gm_l26 = t25;
        uint8_t t26 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l26)) != 0);
        if (t26) {
            void * t27 = GM_LD(void *, ((uint8_t *)&gm_l26));
            gm_l27 = t27;
        } else {
            void * t28 = gm_l0;
            void * t29 = GM_LD(void *, (((uint8_t *)t28) + 8));
            gm_t88rcucg1b7uk t30 = gm_l2;
            gm_b16_8 t31 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t32;
            GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
            gm_f_format_h8e7ps1((void *)&gm_l28, (void *)NULL, cx, NULL, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
            if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srcfde9lsgge1ld, 63, 35);
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
            gm_tl572d7579hns t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
            ((uint8_t *)&t36)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t36 + 8), t35);
            memmove(gm_out, ((uint8_t *)&t36), 24);
            goto gm_fdone;
        }
        void * t37 = gm_l27;
        gm_l4 = t37;
        void * t38 = gm_l4;
        {
            {
                {
                    uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t38) + 24) + 0));
                    uint8_t t40 = (uint8_t)(t39 == ((uint8_t)0ULL));
                    if (!t40) {
                        goto gm_lb2;
                    }
                    memmove(((uint8_t *)&F.l5), ((((uint8_t *)t38) + 24) + 8), 48);
                    void * t41 = gm_l1;
                    void * t42 = gm_l4;
                    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)t42));
                    gm_tftb1odc739t5 t44 = gm_f_extend_0cs85gs(t41, t43);
                    gm_l29 = t44;
                    uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                    if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcfde9lsgge1ld, 66, 13);
                    void * t46 = gm_l1;
                    gm_tftb1odc739t5 t47 = gm_f_push_t83rg2m(t46, ((uint8_t)40ULL));
                    gm_l30 = t47;
                    uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                    if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srcfde9lsgge1ld, 67, 13);
                    gm_l6 = ((uint8_t)1);
                    gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                    gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                    gm_l7 = t50;
                    gm_l8 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t51 = gm_l8;
                            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            uint8_t t53 = (t51 < t52);
                            if (!t53) {
                                goto gm_lb3;
                            }
                            {
                                uint64_t t54 = gm_l8;
                                void * t55 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                uint64_t t57 = gm_l8;
                                gm_t0mglksrkfa5e t58 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t55) + (uintptr_t)t57 * 40));
                                gm_t8ta482bl6be0 t59;
                                GM_ST(uint64_t, ((uint8_t *)&t59 + 0), t54);
                                GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&t59 + 8), t58);
                                gm_l31 = t59;
                                {
                                    {
                                        uint64_t t60 = GM_LD(uint64_t, ((uint8_t *)&gm_l31));
                                        gm_l9 = t60;
                                        memmove(((uint8_t *)&F.l10), (((uint8_t *)&gm_l31) + 8), 40);
                                        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                        gm_b16_8 t62;
                                        GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sfdbfrv46lihd));
                                        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(1));
                                        uint8_t t63 = (gm_eq0iqiubfm7e(((uint8_t *)&t61), ((uint8_t *)&t62)));
                                        if (t63) {
                                            goto gm_lb5;
                                        }
                                        uint8_t t64 = gm_l6;
                                        uint8_t t65 = (uint8_t)!t64;
                                        if (t65) {
                                            void * t66 = gm_l1;
                                            gm_b16_8 t67;
                                            GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sg5gdnrith2i6));
                                            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(2));
                                            gm_tftb1odc739t5 t68 = gm_f_extend_0cs85gs(t66, t67);
                                            gm_l32 = t68;
                                            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcfde9lsgge1ld, 71, 32);
                                        }
                                        gm_l6 = ((uint8_t)0);
                                        void * t70 = gm_l1;
                                        gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                        gm_tftb1odc739t5 t72 = gm_f_extend_0cs85gs(t70, t71);
                                        gm_l33 = t72;
                                        uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                        if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcfde9lsgge1ld, 73, 17);
                                        void * t74 = gm_l1;
                                        gm_b16_8 t75;
                                        GM_ST(void *, (uint8_t *)&t75, (void *)(gm_slgmqgkioa4u6));
                                        GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(2));
                                        gm_tftb1odc739t5 t76 = gm_f_extend_0cs85gs(t74, t75);
                                        gm_l34 = t76;
                                        uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                        if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_srcfde9lsgge1ld, 74, 17);
                                        void * t78 = gm_l0;
                                        gm_t2lqu8jsj511d t79 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t78));
                                        gm_l11 = t79;
                                        gm_b16_8 t80 = gm_f_empty_slice_ijtp9lo();
                                        gm_l12 = t80;
                                        void * t81 = gm_l0;
                                        gm_t88rcucg1b7uk t82 = gm_l2;
                                        gm_tkn42u7na3t81 t83 = gm_f_bits_struct_ty_22ue20s(t81, GM_CONV(gm_csguankko4lmhd, t82));
                                        gm_l36 = t83;
                                        {
                                            {
                                                uint8_t t84 = (uint8_t)((((uint8_t *)&gm_l36))[4] != 0);
                                                if (t84) {
                                                    goto gm_lb9;
                                                }
                                                gm_l35 = ((uint8_t)0);
                                                goto gm_lb8;
                                            }
gm_lb9:;
                                            {
                                                gm_l35 = ((uint8_t)1);
                                                goto gm_lb8;
                                            }
gm_lb10:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 77, 20);
                                        }
gm_lb8:;
                                        uint8_t t85 = gm_l35;
                                        if (t85) {
                                            void * t86 = gm_l0;
                                            void * t87 = gm_l0;
                                            gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                                            uint64_t t89 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t87, t88);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_b16_8 t90 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t86, t89);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l12 = t90;
                                            void * t91 = gm_l0;
                                            gm_t88rcucg1b7uk t92 = gm_l2;
                                            gm_b16_8 t93 = gm_l3;
                                            uint64_t t94 = gm_l9;
                                            uint32_t t95 = (uint32_t)t94;
                                            gm_b16_8 t96 = gm_l12;
                                            gm_b0_1 t97 = gm_f_read_field_ua0742c((void *)NULL, cx, NULL, t91, t92, t93, t95, t96);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        } else {
                                            void * t98 = gm_l0;
                                            gm_t88rcucg1b7uk t99 = gm_l2;
                                            gm_b16_8 t100 = gm_l3;
                                            uint64_t t101 = gm_l9;
                                            uint32_t t102 = (uint32_t)t101;
                                            gm_b16_8 t103 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t98, t99, t100, t102);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l12 = t103;
                                        }
                                        void * t104 = gm_l0;
                                        void * t105 = gm_l1;
                                        gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                                        gm_b16_8 t107 = gm_l12;
                                        gm_f_format_value_vmle7qr((void *)&gm_l13, (void *)NULL, cx, NULL, t104, t105, t106, t107, ((uint8_t)0));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t108 = gm_l0;
                                        gm_t2lqu8jsj511d t109 = gm_l11;
                                        gm_f_release_jn5kqdi(t108, GM_CONV(gm_csc7lc6jlkqkuq, t109));
                                        uint8_t t110 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                        if (t110) {
                                        } else {
                                            gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                                            gm_tl572d7579hns t112;
                                            GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
                                            ((uint8_t *)&t112)[0] = 1;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t112 + 8), t111);
                                            memmove(gm_out, ((uint8_t *)&t112), 24);
                                            goto gm_fdone;
                                        }
                                        goto gm_lb6;
                                    }
gm_lb7:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 69, 13);
                                }
gm_lb6:;
                            }
gm_lb5:;
                            uint64_t t113 = gm_l8;
                            uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                            uint64_t t116 = r114;
                            gm_l8 = t116;
                        }
                    }
gm_lb3:;
                    void * t117 = gm_l1;
                    gm_tftb1odc739t5 t118 = gm_f_push_t83rg2m(t117, ((uint8_t)41ULL));
                    gm_l37 = t118;
                    uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                    if (!t119) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srcfde9lsgge1ld, 87, 13);
                    goto gm_lb1;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t38) + 24) + 0));
                    uint8_t t121 = (uint8_t)(t120 == ((uint8_t)1ULL));
                    if (!t121) {
                        goto gm_lb11;
                    }
                    gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t38) + 24) + 8));
                    gm_l14 = t122;
                    gm_b16_8 t123 = GM_LD(gm_b16_8, (((((uint8_t *)t38) + 24) + 8) + 8));
                    gm_l15 = t123;
                    void * t124 = gm_l0;
                    gm_t88rcucg1b7uk t125 = gm_l2;
                    gm_b16_8 t126 = gm_l3;
                    gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
                    gm_t9805qtonbsag t128 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t124, t125, t127);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l38 = t128;
                    uint8_t t129 = (uint8_t)((((uint8_t *)&gm_l38))[4] != 0);
                    if (t129) {
                        uint32_t t130 = GM_LD(uint32_t, ((uint8_t *)&gm_l38));
                        gm_l39 = t130;
                    } else {
                        void * t131 = gm_l0;
                        gm_t88rcucg1b7uk t132 = gm_l2;
                        gm_t88rcucg1b7uk t133 = gm_l14;
                        gm_b16_8 t134 = gm_l3;
                        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                        gm_b16_8 t136 = gm_f_invalid_tag_text_f3l5hi7((void *)NULL, cx, NULL, t131, t132, t133, t135);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tl572d7579hns t137;
                        GM_ST(uint8_t, (uint8_t *)&t137 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t137 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t137 + 4, 0);
                        ((uint8_t *)&t137)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t137 + 8), t136);
                        memmove(gm_out, ((uint8_t *)&t137), 24);
                        goto gm_fdone;
                    }
                    uint32_t t138 = gm_l39;
                    gm_l16 = t138;
                    void * t139 = gm_l1;
                    gm_tftb1odc739t5 t140 = gm_f_push_t83rg2m(t139, ((uint8_t)46ULL));
                    gm_l40 = t140;
                    uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                    if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srcfde9lsgge1ld, 91, 13);
                    void * t142 = gm_l1;
                    void * t143 = GM_LD(void *, ((uint8_t *)&gm_l15));
                    uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    uint32_t t145 = gm_l16;
                    uint64_t t146 = (uint64_t)t145;
                    uint64_t t147 = gm_f_us_cuv01fp(t146);
                    if ((uintptr_t)t147 >= (uintptr_t)(t144)) gm_trap_index((uintptr_t)t147, t144, gm_srcfde9lsgge1ld, 92, 24);
                    gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)t143) + (uintptr_t)t147 * 72));
                    gm_tftb1odc739t5 t149 = gm_f_extend_0cs85gs(t142, t148);
                    gm_l41 = t149;
                    uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                    if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srcfde9lsgge1ld, 92, 13);
                    {
                        {
                            void * t151 = GM_LD(void *, ((uint8_t *)&gm_l15));
                            uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint32_t t153 = gm_l16;
                            uint64_t t154 = (uint64_t)t153;
                            uint64_t t155 = gm_f_us_cuv01fp(t154);
                            if ((uintptr_t)t155 >= (uintptr_t)(t152)) gm_trap_index((uintptr_t)t155, t152, gm_srcfde9lsgge1ld, 93, 22);
                            uint8_t t156 = (uint8_t)((((((uint8_t *)t151) + (uintptr_t)t155 * 72) + 16))[4] != 0);
                            if (!t156) {
                                goto gm_lb13;
                            }
                            gm_t88rcucg1b7uk t157 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t151) + (uintptr_t)t155 * 72) + 16));
                            gm_l17 = t157;
                            void * t158 = gm_l0;
                            gm_t88rcucg1b7uk t159 = gm_l2;
                            uint64_t t160 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t158, t159);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t161 = gm_f_us_cuv01fp(t160);
                            gm_l18 = t161;
                            void * t162 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t164 = gm_l18;
                            uint64_t t165 = gm_l18;
                            void * t166 = gm_l0;
                            gm_t88rcucg1b7uk t167 = gm_l17;
                            uint64_t t168 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t166, t167);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t169 = gm_f_us_cuv01fp(t168);
                            uint64_t r170; bool o171 = __builtin_add_overflow(t165, t169, &r170);
                            if (o171) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 95, 30);
                            uint64_t t172 = r170;
                            uint64_t t173 = (uintptr_t)t164;
                            uint64_t t174 = (uintptr_t)t172;
                            if (t173 > t174 || t174 > (uintptr_t)t163) gm_trap_slice(t173, t174, t163, gm_srcfde9lsgge1ld, 95, 23);
                            uint64_t t175 = ((uintptr_t)t172) - (uintptr_t)t164;
                            gm_b16_8 t176;
                            GM_ST(void *, (uint8_t *)&t176, (void *)(((uint8_t *)t162 + (uintptr_t)t164 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(t175));
                            gm_l19 = t176;
                            void * t177 = gm_l1;
                            gm_tftb1odc739t5 t178 = gm_f_push_t83rg2m(t177, ((uint8_t)40ULL));
                            gm_l42 = t178;
                            uint8_t t179 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                            if (!t179) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srcfde9lsgge1ld, 96, 17);
                            {
                                {
                                    void * t180 = gm_l0;
                                    gm_t88rcucg1b7uk t181 = gm_l17;
                                    gm_f_kind_htv6093((void *)&gm_l43, t180, GM_CONV(gm_csguankko4lmhd, t181));
                                    uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)&gm_l43) + 0));
                                    uint8_t t183 = (uint8_t)(t182 == ((uint8_t)12ULL));
                                    if (!t183) {
                                        goto gm_lb15;
                                    }
                                    gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
                                    gm_l20 = t184;
                                    gm_b16_8 t185 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l43) + 8) + 16));
                                    gm_l21 = t185;
                                    gm_b16_8 t186 = gm_l20;
                                    gm_l22 = t186;
                                    gm_l23 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t187 = gm_l23;
                                            uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                            uint8_t t189 = (t187 < t188);
                                            if (!t189) {
                                                goto gm_lb16;
                                            }
                                            {
                                                uint64_t t190 = gm_l23;
                                                void * t191 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                                uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                                uint64_t t193 = gm_l23;
                                                gm_t88rcucg1b7uk t194 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t191) + (uintptr_t)t193 * 4));
                                                gm_tlcm4lvu3g1a8 t195;
                                                GM_ST(uint32_t, (uint8_t *)&t195 + 12, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t195 + 0), t190);
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t195 + 8), t194);
                                                gm_l44 = t195;
                                                {
                                                    {
                                                        uint64_t t196 = GM_LD(uint64_t, ((uint8_t *)&gm_l44));
                                                        gm_l24 = t196;
                                                        gm_t88rcucg1b7uk t197 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l44) + 8));
                                                        gm_l25 = t197;
                                                        uint64_t t198 = gm_l24;
                                                        uint8_t t199 = (t198 > ((uint64_t)0ULL));
                                                        if (t199) {
                                                            void * t200 = gm_l1;
                                                            gm_b16_8 t201;
                                                            GM_ST(void *, (uint8_t *)&t201, (void *)(gm_sg5gdnrith2i6));
                                                            GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(2));
                                                            gm_tftb1odc739t5 t202 = gm_f_extend_0cs85gs(t200, t201);
                                                            gm_l45 = t202;
                                                            uint8_t t203 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                                            if (!t203) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srcfde9lsgge1ld, 99, 36);
                                                        }
                                                        uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                                        uint8_t t205 = (t204 > ((uint64_t)0ULL));
                                                        if (t205) {
                                                            void * t206 = gm_l1;
                                                            void * t207 = GM_LD(void *, ((uint8_t *)&gm_l21));
                                                            uint64_t t208 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                                            uint64_t t209 = gm_l24;
                                                            if ((uintptr_t)t209 >= (uintptr_t)(t208)) gm_trap_index((uintptr_t)t209, t208, gm_srcfde9lsgge1ld, 101, 40);
                                                            gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)t207) + (uintptr_t)t209 * 16));
                                                            gm_tftb1odc739t5 t211 = gm_f_extend_0cs85gs(t206, t210);
                                                            gm_l46 = t211;
                                                            uint8_t t212 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                                            if (!t212) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_srcfde9lsgge1ld, 101, 29);
                                                            void * t213 = gm_l1;
                                                            gm_b16_8 t214;
                                                            GM_ST(void *, (uint8_t *)&t214, (void *)(gm_slgmqgkioa4u6));
                                                            GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(2));
                                                            gm_tftb1odc739t5 t215 = gm_f_extend_0cs85gs(t213, t214);
                                                            gm_l47 = t215;
                                                            uint8_t t216 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                                                            if (!t216) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_srcfde9lsgge1ld, 102, 29);
                                                        }
                                                        void * t217 = gm_l0;
                                                        void * t218 = gm_l1;
                                                        gm_t88rcucg1b7uk t219 = gm_l25;
                                                        void * t220 = gm_l0;
                                                        gm_t88rcucg1b7uk t221 = gm_l17;
                                                        gm_b16_8 t222 = gm_l19;
                                                        uint64_t t223 = gm_l24;
                                                        uint32_t t224 = (uint32_t)t223;
                                                        gm_b16_8 t225 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t220, t221, t222, t224);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_f_format_value_vmle7qr((void *)&gm_l48, (void *)NULL, cx, NULL, t217, t218, t219, t225, ((uint8_t)0));
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t226 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                                        if (t226) {
                                                        } else {
                                                            gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                                                            gm_tl572d7579hns t228;
                                                            GM_ST(uint8_t, (uint8_t *)&t228 + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t228 + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t228 + 4, 0);
                                                            ((uint8_t *)&t228)[0] = 1;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t228 + 8), t227);
                                                            memmove(gm_out, ((uint8_t *)&t228), 24);
                                                            goto gm_fdone;
                                                        }
                                                        goto gm_lb19;
                                                    }
gm_lb20:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 98, 21);
                                                }
gm_lb19:;
                                            }
gm_lb18:;
                                            uint64_t t229 = gm_l23;
                                            uint64_t r230; bool o231 = __builtin_add_overflow(t229, ((uint64_t)1ULL), &r230);
                                            uint64_t t232 = r230;
                                            gm_l23 = t232;
                                        }
                                    }
gm_lb16:;
                                    goto gm_lb14;
                                }
gm_lb15:;
                                void * t233 = gm_l0;
                                void * t234 = gm_l1;
                                gm_t88rcucg1b7uk t235 = gm_l17;
                                gm_b16_8 t236 = gm_l19;
                                gm_f_format_value_vmle7qr((void *)&gm_l49, (void *)NULL, cx, NULL, t233, t234, t235, t236, ((uint8_t)0));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t237 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                if (t237) {
                                } else {
                                    gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l49) + 8));
                                    gm_tl572d7579hns t239;
                                    GM_ST(uint8_t, (uint8_t *)&t239 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t239 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t239 + 4, 0);
                                    ((uint8_t *)&t239)[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t239 + 8), t238);
                                    memmove(gm_out, ((uint8_t *)&t239), 24);
                                    goto gm_fdone;
                                }
                            }
gm_lb14:;
                            void * t240 = gm_l1;
                            gm_tftb1odc739t5 t241 = gm_f_push_t83rg2m(t240, ((uint8_t)41ULL));
                            gm_l50 = t241;
                            uint8_t t242 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                            if (!t242) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srcfde9lsgge1ld, 109, 17);
                            goto gm_lb12;
                        }
gm_lb13:;
                    }
gm_lb12:;
                    goto gm_lb1;
                }
gm_lb11:;
            }
            {
                {
                    uint8_t t243 = GM_LD(uint8_t, ((((uint8_t *)t38) + 24) + 0));
                    uint8_t t244 = (uint8_t)(t243 == ((uint8_t)2ULL));
                    if (!t244) {
                        goto gm_lb21;
                    }
                    void * t245 = gm_l1;
                    void * t246 = gm_l4;
                    gm_b16_8 t247 = GM_LD(gm_b16_8, ((uint8_t *)t246));
                    gm_tnpifv20irnph t248;
                    GM_ST(gm_b16_8, ((uint8_t *)&t248 + 0), t247);
                    gm_tftb1odc739t5 t249 = gm_f_write_jvie1fn(t245, t248);
                    gm_l51 = t249;
                    uint8_t t250 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                    if (!t250) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srcfde9lsgge1ld, 112, 23);
                    goto gm_lb1;
                }
gm_lb21:;
                gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 64, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_tl572d7579hns t251;
        GM_ST(uint8_t, (uint8_t *)&t251 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t251 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t251 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t251 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t251 + 16, 0);
        ((uint8_t *)&t251)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t251 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t251), 24);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_float_1ivmudg(void *env, gm_node *cx, gm_node *inst, void * a0, double a1, uint16_t a2) {
    struct gm_fr_fmt_float_1ivmudg F;
    void * gm_l0; /* out */
    double gm_l1; /* x */
    uint16_t gm_l2; /* bits */
    gm_t43v1gep754th gm_l4; /* sh */
    uint64_t gm_l5; /* n */
    int32_t gm_l6; /* exp */
    uint64_t gm_l7; /* int_len */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    GM_STACK_CHECK(&F, 1190u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l12;
    gm_b16_8 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_b16_8 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    double t14 = gm_l1;
    uint8_t t15 = (uint8_t)__builtin_isnan(t14);
    if (t15) {
        void * t16 = gm_l0;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sfa86erpod86s));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(3));
        gm_tftb1odc739t5 t18 = gm_f_extend_0cs85gs(t16, t17);
        gm_l12 = t18;
        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcfde9lsgge1ld, 292, 9);
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    double t20 = gm_l1;
    uint8_t t21 = (uint8_t)__builtin_isinf(t20);
    if (t21) {
        void * t22 = gm_l0;
        double t23 = gm_l1;
        uint8_t t24 = (t23 < ((double)0e0));
        if (t24) {
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_segch6a5dme9u));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(4));
            gm_l13 = t25;
        } else {
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sns75reqq32d2));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(3));
            gm_l13 = t26;
        }
        gm_b16_8 t27 = gm_l13;
        gm_tftb1odc739t5 t28 = gm_f_extend_0cs85gs(t22, t27);
        gm_l14 = t28;
        uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcfde9lsgge1ld, 296, 9);
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    double t30 = gm_l1;
    uint8_t t31 = ((t30 == ((double)0e0)));
    if (t31) {
        void * t32 = gm_l0;
        double t33 = gm_l1;
        uint8_t t34 = (uint8_t)(__builtin_signbit(t33) != 0);
        if (t34) {
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s9lclteeapkg2));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(2));
            gm_l15 = t35;
        } else {
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(1));
            gm_l15 = t36;
        }
        gm_b16_8 t37 = gm_l15;
        gm_tftb1odc739t5 t38 = gm_f_extend_0cs85gs(t32, t37);
        gm_l16 = t38;
        uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcfde9lsgge1ld, 300, 9);
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    double t40 = gm_l1;
    uint8_t t41 = (t40 < ((double)0e0));
    if (t41) {
        void * t42 = gm_l0;
        gm_tftb1odc739t5 t43 = gm_f_push_t83rg2m(t42, ((uint8_t)45ULL));
        gm_l17 = t43;
        uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
        if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcfde9lsgge1ld, 303, 18);
    }
    gm_t6kvj62qe5fp2 t45;
    for (size_t i46 = 0; i46 < 40; i46++) GM_ST(uint8_t, (uint8_t *)&t45 + i46 * 1, ((uint8_t)0ULL));
    F.l3 = t45;
    double t47 = gm_l1;
    double t48 = (t47 < 0 || ((t47) == 0 && __builtin_signbit(t47))) ? -t47 : t47;
    uint16_t t49 = gm_l2;
    void * t50 = (void *)((uint8_t *)&F.l3);
    uint64_t t51 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t52 = (uintptr_t)((uint64_t)40ULL);
    if (t51 > t52 || t52 > (uintptr_t)((uint64_t)40ULL)) gm_trap_slice(t51, t52, ((uint64_t)40ULL), gm_srcfde9lsgge1ld, 305, 39);
    uint64_t t53 = ((uintptr_t)((uint64_t)40ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t50 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
    gm_t43v1gep754th t55 = gm_f_fmt_shortest_2l54as9((void *)NULL, cx, NULL, t48, t49, t54);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t55;
    uint64_t t56 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
    gm_l5 = t56;
    int32_t t57 = GM_LD(int32_t, (((uint8_t *)&gm_l4) + 8));
    gm_l6 = t57;
    {
        {
            int32_t t58 = gm_l6;
            uint8_t t59 = (t58 >= ((int32_t)-6LL));
            if (!t59) {
                goto gm_lb2;
            }
            int32_t t60 = gm_l6;
            uint8_t t61 = (t60 <= ((int32_t)20LL));
            if (!t61) {
                goto gm_lb2;
            }
            int32_t t62 = gm_l6;
            uint8_t t63 = (t62 >= ((int32_t)0LL));
            if (t63) {
                int32_t t64 = gm_l6;
                uint64_t t65 = (uint64_t)t64;
                uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                if (o67) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 310, 24);
                uint64_t t68 = r66;
                gm_l7 = t68;
                uint64_t t69 = gm_l5;
                uint64_t t70 = gm_l7;
                uint8_t t71 = (t69 <= t70);
                if (t71) {
                    void * t72 = gm_l0;
                    void * t73 = (void *)((uint8_t *)&F.l3);
                    uint64_t t74 = gm_l5;
                    uint64_t t75 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t76 = (uintptr_t)t74;
                    if (t75 > t76 || t76 > (uintptr_t)((uint64_t)40ULL)) gm_trap_slice(t75, t76, ((uint64_t)40ULL), gm_srcfde9lsgge1ld, 312, 28);
                    uint64_t t77 = ((uintptr_t)t74) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t78;
                    GM_ST(void *, (uint8_t *)&t78, (void *)(((uint8_t *)t73 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(t77));
                    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                    gm_tftb1odc739t5 t80 = gm_f_extend_0cs85gs(t72, t79);
                    gm_l18 = t80;
                    uint8_t t81 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                    if (!t81) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcfde9lsgge1ld, 312, 17);
                    gm_l9 = ((uint64_t)0ULL);
                    uint64_t t82 = gm_l7;
                    uint64_t t83 = gm_l5;
                    uint64_t r84; bool o85 = __builtin_sub_overflow(t82, t83, &r84);
                    if (o85) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 313, 29);
                    uint64_t t86 = r84;
                    gm_l8 = t86;
                    {
                        for (;;) {
                            uint64_t t87 = gm_l9;
                            uint64_t t88 = gm_l8;
                            uint8_t t89 = (t87 < t88);
                            if (!t89) {
                                goto gm_lb3;
                            }
                            {
                                {
                                    {
                                        void * t90 = gm_l0;
                                        gm_tftb1odc739t5 t91 = gm_f_push_t83rg2m(t90, ((uint8_t)48ULL));
                                        gm_l19 = t91;
                                        uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                        if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcfde9lsgge1ld, 313, 43);
                                        goto gm_lb6;
                                    }
gm_lb7:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 313, 17);
                                }
gm_lb6:;
                            }
gm_lb5:;
                            uint64_t t93 = gm_l9;
                            uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                            uint64_t t96 = r94;
                            gm_l9 = t96;
                        }
                    }
gm_lb3:;
                } else {
                    void * t97 = gm_l0;
                    void * t98 = (void *)((uint8_t *)&F.l3);
                    uint64_t t99 = gm_l7;
                    uint64_t t100 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t101 = (uintptr_t)t99;
                    if (t100 > t101 || t101 > (uintptr_t)((uint64_t)40ULL)) gm_trap_slice(t100, t101, ((uint64_t)40ULL), gm_srcfde9lsgge1ld, 315, 28);
                    uint64_t t102 = ((uintptr_t)t99) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t103;
                    GM_ST(void *, (uint8_t *)&t103, (void *)(((uint8_t *)t98 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(t102));
                    gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
                    gm_tftb1odc739t5 t105 = gm_f_extend_0cs85gs(t97, t104);
                    gm_l20 = t105;
                    uint8_t t106 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                    if (!t106) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcfde9lsgge1ld, 315, 17);
                    void * t107 = gm_l0;
                    gm_tftb1odc739t5 t108 = gm_f_push_t83rg2m(t107, ((uint8_t)46ULL));
                    gm_l21 = t108;
                    uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                    if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcfde9lsgge1ld, 316, 17);
                    void * t110 = gm_l0;
                    void * t111 = (void *)((uint8_t *)&F.l3);
                    uint64_t t112 = gm_l7;
                    uint64_t t113 = gm_l5;
                    uint64_t t114 = (uintptr_t)t112;
                    uint64_t t115 = (uintptr_t)t113;
                    if (t114 > t115 || t115 > (uintptr_t)((uint64_t)40ULL)) gm_trap_slice(t114, t115, ((uint64_t)40ULL), gm_srcfde9lsgge1ld, 317, 28);
                    uint64_t t116 = ((uintptr_t)t113) - (uintptr_t)t112;
                    gm_b16_8 t117;
                    GM_ST(void *, (uint8_t *)&t117, (void *)(((uint8_t *)t111 + (uintptr_t)t112 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(t116));
                    gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                    gm_tftb1odc739t5 t119 = gm_f_extend_0cs85gs(t110, t118);
                    gm_l22 = t119;
                    uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                    if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcfde9lsgge1ld, 317, 17);
                }
            } else {
                void * t121 = gm_l0;
                gm_b16_8 t122;
                GM_ST(void *, (uint8_t *)&t122, (void *)(gm_sjq55q61p743j));
                GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(2));
                gm_tftb1odc739t5 t123 = gm_f_extend_0cs85gs(t121, t122);
                gm_l23 = t123;
                uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcfde9lsgge1ld, 320, 13);
                gm_l11 = ((uint64_t)0ULL);
                int32_t t125 = gm_l6;
                int32_t r126; bool o127 = __builtin_sub_overflow(((int32_t)0LL), t125, &r126);
                if (o127) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 321, 26);
                int32_t t128 = r126;
                int32_t r129; bool o130 = __builtin_sub_overflow(t128, ((int32_t)1LL), &r129);
                if (o130) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 321, 26);
                int32_t t131 = r129;
                uint64_t t132 = (uint64_t)t131;
                gm_l10 = t132;
                {
                    for (;;) {
                        uint64_t t133 = gm_l11;
                        uint64_t t134 = gm_l10;
                        uint8_t t135 = (t133 < t134);
                        if (!t135) {
                            goto gm_lb8;
                        }
                        {
                            {
                                {
                                    void * t136 = gm_l0;
                                    gm_tftb1odc739t5 t137 = gm_f_push_t83rg2m(t136, ((uint8_t)48ULL));
                                    gm_l24 = t137;
                                    uint8_t t138 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                    if (!t138) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcfde9lsgge1ld, 321, 54);
                                    goto gm_lb11;
                                }
gm_lb12:;
                                gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 321, 13);
                            }
gm_lb11:;
                        }
gm_lb10:;
                        uint64_t t139 = gm_l11;
                        uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                        uint64_t t142 = r140;
                        gm_l11 = t142;
                    }
                }
gm_lb8:;
                void * t143 = gm_l0;
                void * t144 = (void *)((uint8_t *)&F.l3);
                uint64_t t145 = gm_l5;
                uint64_t t146 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t147 = (uintptr_t)t145;
                if (t146 > t147 || t147 > (uintptr_t)((uint64_t)40ULL)) gm_trap_slice(t146, t147, ((uint64_t)40ULL), gm_srcfde9lsgge1ld, 322, 24);
                uint64_t t148 = ((uintptr_t)t145) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t149;
                GM_ST(void *, (uint8_t *)&t149, (void *)(((uint8_t *)t144 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(t148));
                gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                gm_tftb1odc739t5 t151 = gm_f_extend_0cs85gs(t143, t150);
                gm_l25 = t151;
                uint8_t t152 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                if (!t152) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcfde9lsgge1ld, 322, 13);
            }
            goto gm_lb1;
        }
gm_lb2:;
        void * t153 = gm_l0;
        uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + (uintptr_t)((uint64_t)0ULL) * 1));
        gm_tftb1odc739t5 t155 = gm_f_push_t83rg2m(t153, t154);
        gm_l26 = t155;
        uint8_t t156 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
        if (!t156) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcfde9lsgge1ld, 325, 9);
        uint64_t t157 = gm_l5;
        uint8_t t158 = (t157 > ((uint64_t)1ULL));
        if (t158) {
            void * t159 = gm_l0;
            gm_tftb1odc739t5 t160 = gm_f_push_t83rg2m(t159, ((uint8_t)46ULL));
            gm_l27 = t160;
            uint8_t t161 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
            if (!t161) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcfde9lsgge1ld, 327, 13);
            void * t162 = gm_l0;
            void * t163 = (void *)((uint8_t *)&F.l3);
            uint64_t t164 = gm_l5;
            uint64_t t165 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t166 = (uintptr_t)t164;
            if (t165 > t166 || t166 > (uintptr_t)((uint64_t)40ULL)) gm_trap_slice(t165, t166, ((uint64_t)40ULL), gm_srcfde9lsgge1ld, 328, 24);
            uint64_t t167 = ((uintptr_t)t164) - (uintptr_t)((uint64_t)1ULL);
            gm_b16_8 t168;
            GM_ST(void *, (uint8_t *)&t168, (void *)(((uint8_t *)t163 + (uintptr_t)((uint64_t)1ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(t167));
            gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
            gm_tftb1odc739t5 t170 = gm_f_extend_0cs85gs(t162, t169);
            gm_l28 = t170;
            uint8_t t171 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
            if (!t171) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcfde9lsgge1ld, 328, 13);
        }
        void * t172 = gm_l0;
        int32_t t173 = gm_l6;
        gm_t19enj90vg8bd t174;
        GM_ST(int32_t, ((uint8_t *)&t174 + 0), t173);
        gm_tftb1odc739t5 t175 = gm_f_write_jkg06fo(t172, t174);
        gm_l29 = t175;
        uint8_t t176 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
        if (!t176) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcfde9lsgge1ld, 330, 9);
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
__attribute__((aligned(4))) double gm_f_fmt_float_bits_9d6f5r5(void * a0, uint16_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fmt_float_bits_9d6f5r5 F;
    void * gm_l0; /* m */
    uint16_t gm_l1; /* bits */
    gm_b16_8 gm_l2; /* b */
    unsigned __int128 gm_l3; /* raw */
    uint16_t gm_l4; /* h */
    uint8_t gm_l5; /* neg */
    int32_t gm_l6; /* e */
    uint16_t gm_l7; /* man */
    double gm_l8; /* mag */
    GM_STACK_CHECK_SHALLOW(&F, 462u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t gm_l9;
    double gm_l10;
    double gm_l11;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l2;
    unsigned __int128 t4 = gm_f_get_uint_bq8c4b7(t2, t3);
    gm_l3 = t4;
    uint16_t t5 = gm_l1;
    uint8_t t6 = ((t5 == ((uint16_t)64ULL)));
    if (t6) {
        unsigned __int128 t7 = gm_l3;
        uint64_t t8 = (uint64_t)t7;
        double t9;
        memset(&t9, 0, sizeof t9); memcpy(&t9, ((uint8_t *)&t8), sizeof t9 < 8 ? sizeof t9 : 8);
        F.ret = t9;
        goto gm_fret;
    }
    uint16_t t10 = gm_l1;
    uint8_t t11 = ((t10 == ((uint16_t)32ULL)));
    if (t11) {
        unsigned __int128 t12 = gm_l3;
        uint32_t t13 = (uint32_t)t12;
        float t14;
        memset(&t14, 0, sizeof t14); memcpy(&t14, ((uint8_t *)&t13), sizeof t14 < 4 ? sizeof t14 : 4);
        double t15 = (double)((double)t14);
        F.ret = t15;
        goto gm_fret;
    }
    unsigned __int128 t16 = gm_l3;
    uint16_t t17 = (uint16_t)t16;
    gm_l4 = t17;
    uint16_t t18 = gm_l4;
    uint16_t t19 = (uint16_t)(t18 & ((uint16_t)32768ULL));
    uint8_t t20 = (!(t19 == ((uint16_t)0ULL)));
    gm_l5 = t20;
    uint16_t t21 = gm_l4;
    unsigned __int128 n22 = (unsigned __int128)((uint64_t)10ULL);
    if (n22 >= 16) gm_trap_shift((uint64_t)n22, "u16", gm_srcfde9lsgge1ld, 203, 15);
    uint16_t t23 = (uint16_t)(t21 >> (int)n22);
    uint16_t t24 = (uint16_t)(t23 & ((uint16_t)31ULL));
    int32_t t25 = (int32_t)t24;
    gm_l6 = t25;
    uint16_t t26 = gm_l4;
    uint16_t t27 = (uint16_t)(t26 & ((uint16_t)1023ULL));
    gm_l7 = t27;
    int32_t t28 = gm_l6;
    uint8_t t29 = ((t28 == ((int32_t)31LL)));
    if (t29) {
        uint16_t t30 = gm_l4;
        uint64_t t31 = (uint64_t)t30;
        uint64_t t32 = (uint64_t)(t31 & ((uint64_t)32768ULL));
        unsigned __int128 n33 = (unsigned __int128)((uint64_t)48ULL);
        if (n33 >= 64) gm_trap_shift((uint64_t)n33, "u64", gm_srcfde9lsgge1ld, 207, 18);
        uint64_t r34 = (uint64_t)((uint64_t)t32 << (int)n33);
        bool o35 = (uint64_t)(r34 >> (int)n33) != t32;
        if (o35) gm_trap("integer overflow in `<<`", gm_srcfde9lsgge1ld, 207, 18);
        uint64_t t36 = r34;
        uint64_t t37 = (uint64_t)(t36 | ((uint64_t)9218868437227405312ULL));
        uint16_t t38 = gm_l7;
        uint8_t t39 = ((t38 == ((uint16_t)0ULL)));
        if (t39) {
            gm_l9 = ((uint64_t)0ULL);
        } else {
            uint16_t t40 = gm_l7;
            uint64_t t41 = (uint64_t)t40;
            unsigned __int128 n42 = (unsigned __int128)((uint64_t)42ULL);
            if (n42 >= 64) gm_trap_shift((uint64_t)n42, "u64", gm_srcfde9lsgge1ld, 207, 125);
            uint64_t r43 = (uint64_t)((uint64_t)t41 << (int)n42);
            bool o44 = (uint64_t)(r43 >> (int)n42) != t41;
            if (o44) gm_trap("integer overflow in `<<`", gm_srcfde9lsgge1ld, 207, 125);
            uint64_t t45 = r43;
            uint64_t t46 = (uint64_t)(((uint64_t)2251799813685248ULL) | t45);
            gm_l9 = t46;
        }
        uint64_t t47 = gm_l9;
        uint64_t t48 = (uint64_t)(t37 | t47);
        double t49;
        memset(&t49, 0, sizeof t49); memcpy(&t49, ((uint8_t *)&t48), sizeof t49 < 8 ? sizeof t49 : 8);
        F.ret = t49;
        goto gm_fret;
    }
    int32_t t50 = gm_l6;
    uint8_t t51 = ((t50 == ((int32_t)0LL)));
    if (t51) {
        uint16_t t52 = gm_l7;
        double t53 = (double)(t52);
        double t54 = gm_f_fmt_pow2_1p38akk(((int32_t)-24LL));
        double t55 = (t53 * t54);
        gm_l10 = t55;
    } else {
        uint16_t t56 = gm_l7;
        uint16_t r57; bool o58 = __builtin_add_overflow(((uint16_t)1024ULL), t56, &r57);
        if (o58) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 209, 66);
        uint16_t t59 = r57;
        double t60 = (double)(t59);
        int32_t t61 = gm_l6;
        int32_t r62; bool o63 = __builtin_sub_overflow(t61, ((int32_t)25LL), &r62);
        if (o63) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 209, 89);
        int32_t t64 = r62;
        double t65 = gm_f_fmt_pow2_1p38akk(t64);
        double t66 = (t60 * t65);
        gm_l10 = t66;
    }
    double t67 = gm_l10;
    gm_l8 = t67;
    uint8_t t68 = gm_l5;
    if (t68) {
        double t69 = gm_l8;
        uint64_t t70;
        memset(&t70, 0, sizeof t70); memcpy(&t70, ((uint8_t *)&t69), sizeof t70 < 8 ? sizeof t70 : 8);
        uint64_t t71 = (uint64_t)(t70 | ((uint64_t)9223372036854775808ULL));
        double t72;
        memset(&t72, 0, sizeof t72); memcpy(&t72, ((uint8_t *)&t71), sizeof t72 < 8 ? sizeof t72 : 8);
        gm_l11 = t72;
    } else {
        double t73 = gm_l8;
        gm_l11 = t73;
    }
    double t74 = gm_l11;
    F.ret = t74;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_float_spec_glt99ob(void *env, gm_node *cx, gm_node *inst, void * a0, double a1, uint16_t a2, void * a3) {
    struct gm_fr_fmt_float_spec_glt99ob F;
    void * gm_l0; /* body */
    double gm_l1; /* a */
    uint16_t gm_l2; /* bits */
    void * gm_l3; /* spec */
    uint32_t gm_l4; /* p */
    gm_b16_8 gm_l5; /* buf */
    gm_b16_8 gm_l6; /* text */
    uint64_t gm_l7; /* e */
    int32_t gm_l8; /* ex */
    gm_t43v1gep754th gm_l10; /* sh */
    uint32_t gm_l11; /* p */
    gm_b16_8 gm_l12; /* buf */
    GM_STACK_CHECK(&F, 1027u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l13;
    gm_t43v1gep754th gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tftb1odc739t5 gm_l23;
    void * t9 = gm_l3;
    gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t9) + 20));
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
    uint8_t t12 = (uint8_t)((t11 == ((uint8_t)5ULL)));
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)5ULL));
    if (t12) {
        {
            {
                void * t14 = gm_l3;
                uint8_t t15 = (uint8_t)(((((uint8_t *)t14) + 12))[4] != 0);
                if (!t15) {
                    goto gm_lb2;
                }
                uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)t14) + 12));
                gm_l4 = t16;
                uint32_t t17 = gm_l4;
                uint64_t t18 = gm_f_sci_buf_len_tedn1r4(t17);
                gm_f_make_fill_jpvm0kc((void *)&gm_l13, (void *)NULL, cx, NULL, t18, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3pvi109nh9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcfde9lsgge1ld, 408, 20);
                gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                gm_l5 = t20;
                double t21 = gm_l1;
                uint32_t t22 = gm_l4;
                gm_b16_8 t23 = gm_l5;
                gm_b16_8 t24 = gm_f_sci_oikoiqs(t21, t22, t23);
                gm_l6 = t24;
                gm_l7 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        void * t25 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t27 = gm_l7;
                        if ((uintptr_t)t27 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t27, t26, gm_srcfde9lsgge1ld, 411, 19);
                        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)t25) + (uintptr_t)t27 * 1));
                        uint8_t t29 = (!(t28 == ((uint8_t)101ULL)));
                        if (!t29) {
                            goto gm_lb3;
                        }
                        {
                            uint64_t t30 = gm_l7;
                            uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                            if (o32) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 411, 40);
                            uint64_t t33 = r31;
                            gm_l7 = t33;
                        }
gm_lb5:;
                    }
                }
gm_lb3:;
                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t36 = gm_l7;
                uint64_t t37 = (uintptr_t)t36;
                uint64_t t38 = (uintptr_t)t35;
                if (t37 > t38 || t38 > (uintptr_t)t35) gm_trap_slice(t37, t38, t35, gm_srcfde9lsgge1ld, 412, 33);
                uint64_t t39 = ((uintptr_t)t35) - (uintptr_t)t36;
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t34 + (uintptr_t)t36 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(t39));
                void * t41 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t43 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t44 = (uintptr_t)((uint64_t)0ULL);
                if (t43 > t44 || t44 > (uintptr_t)t42) gm_trap_slice(t43, t44, t42, gm_srcfde9lsgge1ld, 412, 44);
                uint64_t t45 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t46;
                GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t41 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45));
                gm_t43v1gep754th t47 = gm_f_fmt_split_exp_0kapfov(t40, t46);
                gm_l14 = t47;
                int32_t t48 = GM_LD(int32_t, (((uint8_t *)&gm_l14) + 8));
                gm_l8 = t48;
                void * t49 = gm_l0;
                void * t50 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t52 = gm_l7;
                uint64_t t53 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t54 = (uintptr_t)t52;
                if (t53 > t54 || t54 > (uintptr_t)t51) gm_trap_slice(t53, t54, t51, gm_srcfde9lsgge1ld, 413, 25);
                uint64_t t55 = ((uintptr_t)t52) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t56;
                GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t50 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55));
                gm_tftb1odc739t5 t57 = gm_f_extend_0cs85gs(t49, t56);
                gm_l15 = t57;
                uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcfde9lsgge1ld, 413, 13);
                void * t59 = gm_l0;
                int32_t t60 = gm_l8;
                gm_t19enj90vg8bd t61;
                GM_ST(int32_t, ((uint8_t *)&t61 + 0), t60);
                gm_tftb1odc739t5 t62 = gm_f_write_jkg06fo(t59, t61);
                gm_l16 = t62;
                uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcfde9lsgge1ld, 414, 13);
                gm_b16_8 t64 = gm_l5;
                gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                gm_b0_1 t66 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t65, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k30q9glr4ue)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb1;
            }
gm_lb2:;
            double t67 = gm_l1;
            uint8_t t68 = ((t67 == ((double)0e0)));
            if (t68) {
                void * t69 = gm_l0;
                gm_b16_8 t70;
                GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sdc2n4tsqs8nn));
                GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(3));
                gm_tftb1odc739t5 t71 = gm_f_extend_0cs85gs(t69, t70);
                gm_l17 = t71;
                uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcfde9lsgge1ld, 417, 13);
            } else {
                gm_t6kvj62qe5fp2 t73;
                for (size_t i74 = 0; i74 < 40; i74++) GM_ST(uint8_t, (uint8_t *)&t73 + i74 * 1, ((uint8_t)0ULL));
                F.l9 = t73;
                double t75 = gm_l1;
                uint16_t t76 = gm_l2;
                void * t77 = (void *)((uint8_t *)&F.l9);
                uint64_t t78 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t79 = (uintptr_t)((uint64_t)40ULL);
                if (t78 > t79 || t79 > (uintptr_t)((uint64_t)40ULL)) gm_trap_slice(t78, t79, ((uint64_t)40ULL), gm_srcfde9lsgge1ld, 420, 41);
                uint64_t t80 = ((uintptr_t)((uint64_t)40ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t81;
                GM_ST(void *, (uint8_t *)&t81, (void *)(((uint8_t *)t77 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(t80));
                gm_t43v1gep754th t82 = gm_f_fmt_shortest_2l54as9((void *)NULL, cx, NULL, t75, t76, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t82;
                void * t83 = gm_l0;
                uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + (uintptr_t)((uint64_t)0ULL) * 1));
                gm_tftb1odc739t5 t85 = gm_f_push_t83rg2m(t83, t84);
                gm_l18 = t85;
                uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcfde9lsgge1ld, 421, 13);
                uint64_t t87 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                uint8_t t88 = (t87 > ((uint64_t)1ULL));
                if (t88) {
                    void * t89 = gm_l0;
                    gm_tftb1odc739t5 t90 = gm_f_push_t83rg2m(t89, ((uint8_t)46ULL));
                    gm_l19 = t90;
                    uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                    if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcfde9lsgge1ld, 423, 17);
                    void * t92 = gm_l0;
                    void * t93 = (void *)((uint8_t *)&F.l9);
                    uint64_t t94 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                    uint64_t t95 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t96 = (uintptr_t)t94;
                    if (t95 > t96 || t96 > (uintptr_t)((uint64_t)40ULL)) gm_trap_slice(t95, t96, ((uint64_t)40ULL), gm_srcfde9lsgge1ld, 424, 29);
                    uint64_t t97 = ((uintptr_t)t94) - (uintptr_t)((uint64_t)1ULL);
                    gm_b16_8 t98;
                    GM_ST(void *, (uint8_t *)&t98, (void *)(((uint8_t *)t93 + (uintptr_t)((uint64_t)1ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(t97));
                    gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                    gm_tftb1odc739t5 t100 = gm_f_extend_0cs85gs(t92, t99);
                    gm_l20 = t100;
                    uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                    if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcfde9lsgge1ld, 424, 17);
                }
                void * t102 = gm_l0;
                int32_t t103 = GM_LD(int32_t, (((uint8_t *)&gm_l10) + 8));
                gm_t19enj90vg8bd t104;
                GM_ST(int32_t, ((uint8_t *)&t104 + 0), t103);
                gm_tftb1odc739t5 t105 = gm_f_write_jkg06fo(t102, t104);
                gm_l21 = t105;
                uint8_t t106 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                if (!t106) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcfde9lsgge1ld, 426, 13);
            }
        }
gm_lb1:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            void * t107 = gm_l3;
            uint8_t t108 = (uint8_t)(((((uint8_t *)t107) + 12))[4] != 0);
            if (!t108) {
                goto gm_lb7;
            }
            uint32_t t109 = GM_LD(uint32_t, (((uint8_t *)t107) + 12));
            gm_l11 = t109;
            uint32_t t110 = gm_l11;
            uint64_t t111 = gm_f_fixed_buf_len_gor7a4d(t110);
            gm_f_make_fill_jpvm0kc((void *)&gm_l22, (void *)NULL, cx, NULL, t111, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kbfuu8gel4m)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
            if (!t112) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srcfde9lsgge1ld, 431, 16);
            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
            gm_l12 = t113;
            void * t114 = gm_l0;
            double t115 = gm_l1;
            uint32_t t116 = gm_l11;
            gm_b16_8 t117 = gm_l12;
            gm_b16_8 t118 = gm_f_fixed_rppim5r(t115, t116, t117);
            gm_tftb1odc739t5 t119 = gm_f_extend_0cs85gs(t114, t118);
            gm_l23 = t119;
            uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
            if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcfde9lsgge1ld, 432, 9);
            gm_b16_8 t121 = gm_l12;
            gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
            gm_b0_1 t123 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t122, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kkpcsaqkj4k)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb7:;
    }
gm_lb6:;
    void * t124 = gm_l0;
    double t125 = gm_l1;
    uint16_t t126 = gm_l2;
    gm_b0_1 t127 = gm_f_fmt_float_1ivmudg((void *)NULL, cx, NULL, t124, t125, t126);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fmt_int_svhqra7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_fmt_int_svhqra7 F;
    void * gm_l0; /* m */
    void * gm_l1; /* out */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    unsigned __int128 gm_l4; /* v */
    gm_ttovhfee8mf28 gm_l5; /* info */
    GM_STACK_CHECK(&F, 288u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tg5lql6itufk7 gm_l6;
    gm_ttovhfee8mf28 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_b16_8 t4 = gm_l3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    unsigned __int128 t6 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t5);
    gm_l4 = t6;
    void * t7 = gm_l0;
    gm_t88rcucg1b7uk t8 = gm_l2;
    gm_tg5lql6itufk7 t9 = gm_f_int_info_ulcevdv(t7, GM_CONV(gm_csguankko4lmhd, t8));
    gm_l6 = t9;
    uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l6))[8] != 0);
    if (t10) {
        gm_ttovhfee8mf28 t11 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l6));
        gm_l7 = t11;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_ttovhfee8mf28 t12 = gm_l7;
    gm_l5 = t12;
    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)&gm_l5));
    if (t13) {
        void * t14 = gm_l1;
        unsigned __int128 t15 = gm_l4;
        __int128 t16;
        memset(&t16, 0, sizeof t16); memcpy(&t16, ((uint8_t *)&t15), sizeof t16 < 16 ? sizeof t16 : 16);
        gm_tpl3q6e8s9qo1 t17;
        GM_ST(__int128, ((uint8_t *)&t17 + 0), t16);
        gm_tftb1odc739t5 t18 = gm_f_write_cnos3th(t14, t17);
        gm_l8 = t18;
        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcfde9lsgge1ld, 183, 9);
    } else {
        void * t20 = gm_l1;
        unsigned __int128 t21 = gm_l4;
        gm_tsn2omssg6cpf t22;
        GM_ST(unsigned __int128, ((uint8_t *)&t22 + 0), t21);
        gm_tftb1odc739t5 t23 = gm_f_write_1599ben(t20, t22);
        gm_l9 = t23;
        uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcfde9lsgge1ld, 185, 9);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_pad_ev4lnnu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint32_t a3, uint8_t a4, gm_t9qkh0rsooc8d a5, uint8_t a6, uint8_t a7) {
    struct gm_fr_fmt_pad_ev4lnnu F;
    void * gm_l0; /* out */
    gm_b16_8 gm_l1; /* head */
    gm_b16_8 gm_l2; /* body */
    uint32_t gm_l3; /* width */
    uint8_t gm_l4; /* fill */
    gm_t9qkh0rsooc8d gm_l5; /* align */
    uint8_t gm_l6; /* zero */
    uint8_t gm_l7; /* number */
    uint64_t gm_l8; /* len */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint8_t gm_l11; /* c */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint8_t gm_l14; /* c */
    uint64_t gm_l15; /* gap */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* before */
    uint64_t gm_l19; /* after */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    GM_STACK_CHECK(&F, 799u);
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
    uint64_t gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_l8 = ((uint64_t)0ULL);
    gm_b16_8 t32 = gm_l1;
    gm_l9 = t32;
    gm_l10 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t33 = gm_l10;
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            uint8_t t35 = (t33 < t34);
            if (!t35) {
                goto gm_lb1;
            }
            {
                void * t36 = GM_LD(void *, ((uint8_t *)&gm_l9));
                uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint64_t t38 = gm_l10;
                {
                    {
                        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t36) + (uintptr_t)t38 * 1));
                        gm_l11 = t39;
                        uint8_t t40 = gm_l11;
                        uint8_t t41 = (uint8_t)(t40 & ((uint8_t)192ULL));
                        uint8_t t42 = (!(t41 == ((uint8_t)128ULL)));
                        if (t42) {
                            uint64_t t43 = gm_l8;
                            uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                            if (o45) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 443, 47);
                            uint64_t t46 = r44;
                            gm_l8 = t46;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 443, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t47 = gm_l10;
            uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
            uint64_t t50 = r48;
            gm_l10 = t50;
        }
    }
gm_lb1:;
    gm_b16_8 t51 = gm_l2;
    gm_l12 = t51;
    gm_l13 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t52 = gm_l13;
            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
            uint8_t t54 = (t52 < t53);
            if (!t54) {
                goto gm_lb6;
            }
            {
                void * t55 = GM_LD(void *, ((uint8_t *)&gm_l12));
                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint64_t t57 = gm_l13;
                {
                    {
                        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)t57 * 1));
                        gm_l14 = t58;
                        uint8_t t59 = gm_l14;
                        uint8_t t60 = (uint8_t)(t59 & ((uint8_t)192ULL));
                        uint8_t t61 = (!(t60 == ((uint8_t)128ULL)));
                        if (t61) {
                            uint64_t t62 = gm_l8;
                            uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                            if (o64) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 444, 47);
                            uint64_t t65 = r63;
                            gm_l8 = t65;
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 444, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t66 = gm_l13;
            uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
            uint64_t t69 = r67;
            gm_l13 = t69;
        }
    }
gm_lb6:;
    uint32_t t70 = gm_l3;
    uint64_t t71 = (uint64_t)t70;
    uint64_t t72 = gm_l8;
    uint8_t t73 = (t71 > t72);
    if (t73) {
        uint32_t t74 = gm_l3;
        uint64_t t75 = (uint64_t)t74;
        uint64_t t76 = gm_l8;
        uint64_t r77; bool o78 = __builtin_sub_overflow(t75, t76, &r77);
        if (o78) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 445, 36);
        uint64_t t79 = r77;
        gm_l24 = t79;
    } else {
        gm_l24 = ((uint64_t)0ULL);
    }
    uint64_t t80 = gm_l24;
    gm_l15 = t80;
    uint8_t t81 = gm_l6;
    if (t81) {
        void * t82 = gm_l0;
        gm_b16_8 t83 = gm_l1;
        gm_tftb1odc739t5 t84 = gm_f_extend_0cs85gs(t82, t83);
        gm_l25 = t84;
        uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
        if (!t85) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcfde9lsgge1ld, 447, 9);
        gm_l17 = ((uint64_t)0ULL);
        uint64_t t86 = gm_l15;
        gm_l16 = t86;
        {
            for (;;) {
                uint64_t t87 = gm_l17;
                uint64_t t88 = gm_l16;
                uint8_t t89 = (t87 < t88);
                if (!t89) {
                    goto gm_lb11;
                }
                {
                    {
                        {
                            void * t90 = gm_l0;
                            gm_tftb1odc739t5 t91 = gm_f_push_t83rg2m(t90, ((uint8_t)48ULL));
                            gm_l26 = t91;
                            uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                            if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcfde9lsgge1ld, 448, 27);
                            goto gm_lb14;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 448, 9);
                    }
gm_lb14:;
                }
gm_lb13:;
                uint64_t t93 = gm_l17;
                uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                uint64_t t96 = r94;
                gm_l17 = t96;
            }
        }
gm_lb11:;
        void * t97 = gm_l0;
        gm_b16_8 t98 = gm_l2;
        gm_tftb1odc739t5 t99 = gm_f_extend_0cs85gs(t97, t98);
        gm_l27 = t99;
        uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
        if (!t100) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcfde9lsgge1ld, 449, 9);
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_l18 = ((uint64_t)0ULL);
    gm_l19 = ((uint64_t)0ULL);
    {
        {
            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t102 = (uint8_t)(t101 == ((uint8_t)1ULL));
            if (!t102) {
                goto gm_lb17;
            }
            uint64_t t103 = gm_l15;
            gm_l19 = t103;
            goto gm_lb16;
        }
gm_lb17:;
        {
            uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t105 = (uint8_t)(t104 == ((uint8_t)2ULL));
            if (!t105) {
                goto gm_lb18;
            }
            uint64_t t106 = gm_l15;
            gm_l18 = t106;
            goto gm_lb16;
        }
gm_lb18:;
        {
            uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t108 = (uint8_t)(t107 == ((uint8_t)3ULL));
            if (!t108) {
                goto gm_lb19;
            }
            uint64_t t109 = gm_l15;
            if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcfde9lsgge1ld, 458, 22);
            uint64_t t110 = (uint64_t)(t109 / ((uint64_t)2ULL));
            gm_l18 = t110;
            uint64_t t111 = gm_l15;
            uint64_t t112 = gm_l15;
            if (((uint64_t)2ULL) == 0) gm_trap("division by zero", gm_srcfde9lsgge1ld, 459, 27);
            uint64_t t113 = (uint64_t)(t112 / ((uint64_t)2ULL));
            uint64_t r114; bool o115 = __builtin_sub_overflow(t111, t113, &r114);
            if (o115) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 459, 21);
            uint64_t t116 = r114;
            gm_l19 = t116;
            goto gm_lb16;
        }
gm_lb19:;
        {
            uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t118 = (uint8_t)(t117 == ((uint8_t)0ULL));
            if (!t118) {
                goto gm_lb20;
            }
            uint8_t t119 = gm_l7;
            if (t119) {
                uint64_t t120 = gm_l15;
                gm_l18 = t120;
            } else {
                uint64_t t121 = gm_l15;
                gm_l19 = t121;
            }
            goto gm_lb16;
        }
gm_lb20:;
        gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 454, 5);
    }
gm_lb16:;
    gm_l21 = ((uint64_t)0ULL);
    uint64_t t122 = gm_l18;
    gm_l20 = t122;
    {
        for (;;) {
            uint64_t t123 = gm_l21;
            uint64_t t124 = gm_l20;
            uint8_t t125 = (t123 < t124);
            if (!t125) {
                goto gm_lb21;
            }
            {
                {
                    {
                        void * t126 = gm_l0;
                        uint8_t t127 = gm_l4;
                        gm_tftb1odc739t5 t128 = gm_f_push_t83rg2m(t126, t127);
                        gm_l28 = t128;
                        uint8_t t129 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                        if (!t129) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcfde9lsgge1ld, 465, 26);
                        goto gm_lb24;
                    }
gm_lb25:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 465, 5);
                }
gm_lb24:;
            }
gm_lb23:;
            uint64_t t130 = gm_l21;
            uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
            uint64_t t133 = r131;
            gm_l21 = t133;
        }
    }
gm_lb21:;
    void * t134 = gm_l0;
    gm_b16_8 t135 = gm_l1;
    gm_tftb1odc739t5 t136 = gm_f_extend_0cs85gs(t134, t135);
    gm_l29 = t136;
    uint8_t t137 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
    if (!t137) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcfde9lsgge1ld, 466, 5);
    void * t138 = gm_l0;
    gm_b16_8 t139 = gm_l2;
    gm_tftb1odc739t5 t140 = gm_f_extend_0cs85gs(t138, t139);
    gm_l30 = t140;
    uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
    if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srcfde9lsgge1ld, 467, 5);
    gm_l23 = ((uint64_t)0ULL);
    uint64_t t142 = gm_l19;
    gm_l22 = t142;
    {
        for (;;) {
            uint64_t t143 = gm_l23;
            uint64_t t144 = gm_l22;
            uint8_t t145 = (t143 < t144);
            if (!t145) {
                goto gm_lb26;
            }
            {
                {
                    {
                        void * t146 = gm_l0;
                        uint8_t t147 = gm_l4;
                        gm_tftb1odc739t5 t148 = gm_f_push_t83rg2m(t146, t147);
                        gm_l31 = t148;
                        uint8_t t149 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                        if (!t149) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcfde9lsgge1ld, 468, 25);
                        goto gm_lb29;
                    }
gm_lb30:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 468, 5);
                }
gm_lb29:;
            }
gm_lb28:;
            uint64_t t150 = gm_l23;
            uint64_t r151; bool o152 = __builtin_add_overflow(t150, ((uint64_t)1ULL), &r151);
            uint64_t t153 = r151;
            gm_l23 = t153;
        }
    }
gm_lb26:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) double gm_f_fmt_pow2_1p38akk(int32_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fmt_quote_f7sotri(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fmt_quote_f7sotri F;
    void * gm_l0; /* out */
    gm_b16_8 gm_l1; /* bytes */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* c */
    uint64_t gm_l4; /* n */
    GM_STACK_CHECK(&F, 708u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    uint64_t gm_l13;
    uint64_t gm_l14;
    uint64_t gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    void * t15 = gm_l0;
    gm_tftb1odc739t5 t16 = gm_f_push_t83rg2m(t15, ((uint8_t)34ULL));
    gm_l5 = t16;
    uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcfde9lsgge1ld, 128, 5);
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t18 = gm_l2;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t20 = (t18 < t19);
            if (!t20) {
                goto gm_lb1;
            }
            {
                void * t21 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t23 = gm_l2;
                uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)t23 * 1));
                gm_l3 = t24;
                uint64_t t25 = gm_l2;
                uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                if (o27) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 132, 9);
                uint64_t t28 = r26;
                gm_l2 = t28;
                uint8_t t29 = gm_l3;
                uint8_t t30 = ((t29 == ((uint8_t)34ULL)));
                if (t30) {
                    void * t31 = gm_l0;
                    gm_b16_8 t32;
                    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_smee1janbl2t9));
                    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(2));
                    gm_tftb1odc739t5 t33 = gm_f_extend_0cs85gs(t31, t32);
                    gm_l6 = t33;
                    uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                    if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcfde9lsgge1ld, 134, 13);
                } else {
                    uint8_t t35 = gm_l3;
                    uint8_t t36 = ((t35 == ((uint8_t)92ULL)));
                    if (t36) {
                        void * t37 = gm_l0;
                        gm_b16_8 t38;
                        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s3p8gug7bvl1p));
                        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t39 = gm_f_extend_0cs85gs(t37, t38);
                        gm_l7 = t39;
                        uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcfde9lsgge1ld, 136, 13);
                    } else {
                        uint8_t t41 = gm_l3;
                        uint8_t t42 = ((t41 == ((uint8_t)10ULL)));
                        if (t42) {
                            void * t43 = gm_l0;
                            gm_b16_8 t44;
                            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s6gt8b7hsisgu));
                            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t45 = gm_f_extend_0cs85gs(t43, t44);
                            gm_l8 = t45;
                            uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                            if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcfde9lsgge1ld, 138, 13);
                        } else {
                            uint8_t t47 = gm_l3;
                            uint8_t t48 = ((t47 == ((uint8_t)13ULL)));
                            if (t48) {
                                void * t49 = gm_l0;
                                gm_b16_8 t50;
                                GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s9cb6j5ok7bdk));
                                GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t51 = gm_f_extend_0cs85gs(t49, t50);
                                gm_l9 = t51;
                                uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcfde9lsgge1ld, 140, 13);
                            } else {
                                uint8_t t53 = gm_l3;
                                uint8_t t54 = ((t53 == ((uint8_t)9ULL)));
                                if (t54) {
                                    void * t55 = gm_l0;
                                    gm_b16_8 t56;
                                    GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s22g482qk7a40));
                                    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(2));
                                    gm_tftb1odc739t5 t57 = gm_f_extend_0cs85gs(t55, t56);
                                    gm_l10 = t57;
                                    uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                    if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcfde9lsgge1ld, 142, 13);
                                } else {
                                    uint8_t t59 = gm_l3;
                                    uint8_t t60 = ((t59 == ((uint8_t)0ULL)));
                                    if (t60) {
                                        void * t61 = gm_l0;
                                        gm_b16_8 t62;
                                        GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s2v5amvfkfoll));
                                        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(2));
                                        gm_tftb1odc739t5 t63 = gm_f_extend_0cs85gs(t61, t62);
                                        gm_l11 = t63;
                                        uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                        if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcfde9lsgge1ld, 144, 13);
                                    } else {
                                        {
                                            {
                                                uint8_t t65 = gm_l3;
                                                uint8_t t66 = (t65 >= ((uint8_t)32ULL));
                                                if (!t66) {
                                                    goto gm_lb5;
                                                }
                                                uint8_t t67 = gm_l3;
                                                uint8_t t68 = (t67 <= ((uint8_t)126ULL));
                                                if (!t68) {
                                                    goto gm_lb5;
                                                }
                                                void * t69 = gm_l0;
                                                uint8_t t70 = gm_l3;
                                                gm_tftb1odc739t5 t71 = gm_f_push_t83rg2m(t69, t70);
                                                gm_l12 = t71;
                                                uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                                if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcfde9lsgge1ld, 146, 13);
                                                goto gm_lb4;
                                            }
gm_lb5:;
                                            {
                                                {
                                                    uint8_t t73 = gm_l3;
                                                    uint8_t t74 = (t73 >= ((uint8_t)194ULL));
                                                    if (!t74) {
                                                        goto gm_lb7;
                                                    }
                                                    uint8_t t75 = gm_l3;
                                                    uint8_t t76 = (t75 <= ((uint8_t)223ULL));
                                                    if (!t76) {
                                                        goto gm_lb7;
                                                    }
                                                    gm_l13 = ((uint64_t)2ULL);
                                                    goto gm_lb6;
                                                }
gm_lb7:;
                                                {
                                                    {
                                                        uint8_t t77 = gm_l3;
                                                        uint8_t t78 = (t77 >= ((uint8_t)224ULL));
                                                        if (!t78) {
                                                            goto gm_lb9;
                                                        }
                                                        uint8_t t79 = gm_l3;
                                                        uint8_t t80 = (t79 <= ((uint8_t)239ULL));
                                                        if (!t80) {
                                                            goto gm_lb9;
                                                        }
                                                        gm_l14 = ((uint64_t)3ULL);
                                                        goto gm_lb8;
                                                    }
gm_lb9:;
                                                    {
                                                        {
                                                            uint8_t t81 = gm_l3;
                                                            uint8_t t82 = (t81 >= ((uint8_t)240ULL));
                                                            if (!t82) {
                                                                goto gm_lb11;
                                                            }
                                                            uint8_t t83 = gm_l3;
                                                            uint8_t t84 = (t83 <= ((uint8_t)244ULL));
                                                            if (!t84) {
                                                                goto gm_lb11;
                                                            }
                                                            gm_l15 = ((uint64_t)4ULL);
                                                            goto gm_lb10;
                                                        }
gm_lb11:;
                                                        gm_l15 = ((uint64_t)0ULL);
                                                    }
gm_lb10:;
                                                    uint64_t t85 = gm_l15;
                                                    gm_l14 = t85;
                                                }
gm_lb8:;
                                                uint64_t t86 = gm_l14;
                                                gm_l13 = t86;
                                            }
gm_lb6:;
                                            uint64_t t87 = gm_l13;
                                            gm_l4 = t87;
                                            {
                                                {
                                                    uint64_t t88 = gm_l4;
                                                    uint8_t t89 = (t88 > ((uint64_t)0ULL));
                                                    if (!t89) {
                                                        goto gm_lb13;
                                                    }
                                                    uint64_t t90 = gm_l2;
                                                    uint64_t r91; bool o92 = __builtin_sub_overflow(t90, ((uint64_t)1ULL), &r91);
                                                    if (o92) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 149, 26);
                                                    uint64_t t93 = r91;
                                                    uint64_t t94 = gm_l4;
                                                    uint64_t r95; bool o96 = __builtin_add_overflow(t93, t94, &r95);
                                                    if (o96) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 149, 26);
                                                    uint64_t t97 = r95;
                                                    uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                                    uint8_t t99 = (t97 <= t98);
                                                    if (!t99) {
                                                        goto gm_lb13;
                                                    }
                                                    void * t100 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                                    uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                                    uint64_t t102 = gm_l2;
                                                    uint64_t r103; bool o104 = __builtin_sub_overflow(t102, ((uint64_t)1ULL), &r103);
                                                    if (o104) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 149, 74);
                                                    uint64_t t105 = r103;
                                                    uint64_t t106 = gm_l2;
                                                    uint64_t r107; bool o108 = __builtin_sub_overflow(t106, ((uint64_t)1ULL), &r107);
                                                    if (o108) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 149, 81);
                                                    uint64_t t109 = r107;
                                                    uint64_t t110 = gm_l4;
                                                    uint64_t r111; bool o112 = __builtin_add_overflow(t109, t110, &r111);
                                                    if (o112) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 149, 81);
                                                    uint64_t t113 = r111;
                                                    uint64_t t114 = (uintptr_t)t105;
                                                    uint64_t t115 = (uintptr_t)t113;
                                                    if (t114 > t115 || t115 > (uintptr_t)t101) gm_trap_slice(t114, t115, t101, gm_srcfde9lsgge1ld, 149, 68);
                                                    uint64_t t116 = ((uintptr_t)t113) - (uintptr_t)t105;
                                                    gm_b16_8 t117;
                                                    GM_ST(void *, (uint8_t *)&t117, (void *)(((uint8_t *)t100 + (uintptr_t)t105 * 1)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(t116));
                                                    uint8_t t118 = gm_f_fmt_valid_utf8_9mug7al(t117);
                                                    if (!t118) {
                                                        goto gm_lb13;
                                                    }
                                                    void * t119 = gm_l0;
                                                    void * t120 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                                    uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                                    uint64_t t122 = gm_l2;
                                                    uint64_t r123; bool o124 = __builtin_sub_overflow(t122, ((uint64_t)1ULL), &r123);
                                                    if (o124) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 150, 34);
                                                    uint64_t t125 = r123;
                                                    uint64_t t126 = gm_l2;
                                                    uint64_t r127; bool o128 = __builtin_sub_overflow(t126, ((uint64_t)1ULL), &r127);
                                                    if (o128) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 150, 41);
                                                    uint64_t t129 = r127;
                                                    uint64_t t130 = gm_l4;
                                                    uint64_t r131; bool o132 = __builtin_add_overflow(t129, t130, &r131);
                                                    if (o132) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 150, 41);
                                                    uint64_t t133 = r131;
                                                    uint64_t t134 = (uintptr_t)t125;
                                                    uint64_t t135 = (uintptr_t)t133;
                                                    if (t134 > t135 || t135 > (uintptr_t)t121) gm_trap_slice(t134, t135, t121, gm_srcfde9lsgge1ld, 150, 28);
                                                    uint64_t t136 = ((uintptr_t)t133) - (uintptr_t)t125;
                                                    gm_b16_8 t137;
                                                    GM_ST(void *, (uint8_t *)&t137, (void *)(((uint8_t *)t120 + (uintptr_t)t125 * 1)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(t136));
                                                    gm_tftb1odc739t5 t138 = gm_f_extend_0cs85gs(t119, t137);
                                                    gm_l16 = t138;
                                                    uint8_t t139 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                                    if (!t139) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcfde9lsgge1ld, 150, 17);
                                                    uint64_t t140 = gm_l2;
                                                    uint64_t t141 = gm_l4;
                                                    uint64_t r142; bool o143 = __builtin_sub_overflow(t141, ((uint64_t)1ULL), &r142);
                                                    if (o143) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 151, 22);
                                                    uint64_t t144 = r142;
                                                    uint64_t r145; bool o146 = __builtin_add_overflow(t140, t144, &r145);
                                                    if (o146) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 151, 17);
                                                    uint64_t t147 = r145;
                                                    gm_l2 = t147;
                                                    goto gm_lb12;
                                                }
gm_lb13:;
                                                void * t148 = gm_l0;
                                                uint8_t t149 = gm_l3;
                                                gm_t9qkh0rsooc8d t150;
                                                GM_ST(uint8_t, ((uint8_t *)&t150 + 0), t149);
                                                gm_tftb1odc739t5 t151 = gm_f_write_fsqql49(t148, t150);
                                                gm_l17 = t151;
                                                uint8_t t152 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                                if (!t152) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcfde9lsgge1ld, 153, 17);
                                            }
gm_lb12:;
                                        }
gm_lb4:;
                                    }
                                }
                            }
                        }
                    }
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t153 = gm_l0;
    gm_tftb1odc739t5 t154 = gm_f_push_t83rg2m(t153, ((uint8_t)34ULL));
    gm_l18 = t154;
    uint8_t t155 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (!t155) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcfde9lsgge1ld, 157, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_fmt_seq_c3i8abi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, gm_b16_8 a4, uint64_t a5) {
    struct gm_fr_fmt_seq_c3i8abi F;
    void * gm_l0; /* m */
    void * gm_l1; /* out */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    uint64_t gm_l3; /* esz */
    gm_b16_8 gm_l4; /* data */
    uint64_t gm_l5; /* n */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    GM_STACK_CHECK(&F, 410u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tl572d7579hns gm_l11;
    gm_tftb1odc739t5 gm_l12;
    void * t7 = gm_l1;
    gm_tftb1odc739t5 t8 = gm_f_push_t83rg2m(t7, ((uint8_t)91ULL));
    gm_l9 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcfde9lsgge1ld, 118, 5);
    gm_l7 = ((uint64_t)0ULL);
    uint64_t t10 = gm_l5;
    gm_l6 = t10;
    {
        for (;;) {
            uint64_t t11 = gm_l7;
            uint64_t t12 = gm_l6;
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t14 = gm_l7;
                        gm_l8 = t14;
                        uint64_t t15 = gm_l8;
                        uint8_t t16 = (t15 > ((uint64_t)0ULL));
                        if (t16) {
                            void * t17 = gm_l1;
                            gm_b16_8 t18;
                            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
                            gm_l10 = t19;
                            uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                            if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcfde9lsgge1ld, 120, 20);
                        }
                        void * t21 = gm_l0;
                        void * t22 = gm_l1;
                        gm_t88rcucg1b7uk t23 = gm_l2;
                        void * t24 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t26 = gm_l8;
                        uint64_t t27 = gm_l3;
                        uint64_t r28; bool o29 = __builtin_mul_overflow(t26, t27, &r28);
                        if (o29) gm_trap("integer overflow in `*`", gm_srcfde9lsgge1ld, 121, 45);
                        uint64_t t30 = r28;
                        uint64_t t31 = gm_l8;
                        uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                        if (o33) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 121, 55);
                        uint64_t t34 = r32;
                        uint64_t t35 = gm_l3;
                        uint64_t r36; bool o37 = __builtin_mul_overflow(t34, t35, &r36);
                        if (o37) gm_trap("integer overflow in `*`", gm_srcfde9lsgge1ld, 121, 54);
                        uint64_t t38 = r36;
                        uint64_t t39 = (uintptr_t)t30;
                        uint64_t t40 = (uintptr_t)t38;
                        if (t39 > t40 || t40 > (uintptr_t)t25) gm_trap_slice(t39, t40, t25, gm_srcfde9lsgge1ld, 121, 40);
                        uint64_t t41 = ((uintptr_t)t38) - (uintptr_t)t30;
                        gm_b16_8 t42;
                        GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t24 + (uintptr_t)t30 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(t41));
                        gm_f_format_value_vmle7qr((void *)&gm_l11, (void *)NULL, cx, NULL, t21, t22, t23, t42, ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (t43) {
                        } else {
                            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                            gm_tl572d7579hns t45;
                            GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                            ((uint8_t *)&t45)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t45 + 8), t44);
                            memmove(gm_out, ((uint8_t *)&t45), 24);
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 119, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t46 = gm_l7;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
            uint64_t t49 = r47;
            gm_l7 = t49;
        }
    }
gm_lb1:;
    void * t50 = gm_l1;
    gm_tftb1odc739t5 t51 = gm_f_push_t83rg2m(t50, ((uint8_t)93ULL));
    gm_l12 = t51;
    uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcfde9lsgge1ld, 123, 5);
    gm_tl572d7579hns t53;
    GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t53 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t53 + 16, 0);
    ((uint8_t *)&t53)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t53 + 8), GM_ZERO(gm_b0_1));
    memmove(gm_out, ((uint8_t *)&t53), 24);
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
__attribute__((aligned(4))) gm_t43v1gep754th gm_f_fmt_shortest16_cln2heq(void *env, gm_node *cx, gm_node *inst, double a0, gm_b16_8 a1) {
    struct gm_fr_fmt_shortest16_cln2heq F;
    double gm_l0; /* x */
    gm_b16_8 gm_l1; /* dig */
    uint64_t gm_l2; /* h */
    int32_t gm_l3; /* be */
    uint8_t gm_l4; /* low */
    int32_t gm_l5; /* q */
    double gm_l6; /* c */
    uint32_t gm_l7; /* cu */
    double gm_l8; /* ulp */
    double gm_l9; /* hi */
    double gm_l10; /* lo */
    uint8_t gm_l11; /* incl */
    gm_t7mlrlibdq1ja gm_l12; /* buf */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint8_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* digits */
    gm_b16_8 gm_l17; /* text */
    gm_t43v1gep754th gm_l18; /* sp */
    int64_t gm_l19; /* d */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* k */
    int32_t gm_l23; /* e10 */
    gm_t7vno1ieejig6 gm_l24; /* <tmp> */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    int64_t gm_l27; /* delta */
    int64_t gm_l28; /* cand */
    gm_b16_8 gm_l29; /* s */
    gm_ttiiv5vhiuvvr gm_l30; /* e */
    double gm_l31; /* v */
    uint8_t gm_l32; /* ok */
    gm_b16_8 gm_l33; /* nb */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* k */
    GM_STACK_CHECK(&F, 1701u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int32_t gm_l37;
    double gm_l38;
    gm_tabb710d9mq2e gm_l39;
    gm_tspt3vn9ljcnd gm_l40;
    double gm_l41;
    uint8_t gm_l42;
    uint8_t gm_l43;
    uint8_t gm_l44;
    {
        double t24 = gm_l0;
        uint64_t t25;
        memset(&t25, 0, sizeof t25); memcpy(&t25, ((uint8_t *)&t24), sizeof t25 < 8 ? sizeof t25 : 8);
        gm_l2 = t25;
        uint64_t t26 = gm_l2;
        unsigned __int128 n27 = (unsigned __int128)((uint64_t)52ULL);
        if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_srcfde9lsgge1ld, 253, 13);
        uint64_t t28 = (uint64_t)(t26 >> (int)n27);
        uint64_t t29 = (uint64_t)(t28 & ((uint64_t)2047ULL));
        int32_t t30 = (int32_t)t29;
        int32_t r31; bool o32 = __builtin_sub_overflow(t30, ((int32_t)1023LL), &r31);
        if (o32) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 253, 11);
        int32_t t33 = r31;
        gm_l3 = t33;
        int32_t t34 = gm_l3;
        uint8_t t35 = (t34 < ((int32_t)-14LL));
        gm_l4 = t35;
        uint8_t t36 = gm_l4;
        if (t36) {
            gm_l37 = ((int32_t)-24LL);
        } else {
            int32_t t37 = gm_l3;
            int32_t r38; bool o39 = __builtin_sub_overflow(t37, ((int32_t)10LL), &r38);
            if (o39) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 255, 36);
            int32_t t40 = r38;
            gm_l37 = t40;
        }
        int32_t t41 = gm_l37;
        gm_l5 = t41;
        double t42 = gm_l0;
        int32_t t43 = gm_l5;
        double t44 = gm_f_fmt_pow2_1p38akk(t43);
        double t45 = (t42 / t44);
        gm_l6 = t45;
        double t46 = gm_l6;
        double t47 = (double)t46;
        double t48 = __builtin_trunc(t47);
        if (!(__builtin_isfinite(t48) && t48 >= 0e0 && t48 < 4.294967296e9)) gm_trap("float to integer conversion out of range", gm_srcfde9lsgge1ld, 257, 11);
        uint32_t t49 = (uint32_t)t48;
        gm_l7 = t49;
        int32_t t50 = gm_l5;
        double t51 = gm_f_fmt_pow2_1p38akk(t50);
        gm_l8 = t51;
        double t52 = gm_l0;
        double t53 = gm_l8;
        double t54 = (t53 / ((double)2e0));
        double t55 = (t52 + t54);
        gm_l9 = t55;
        {
            {
                uint32_t t56 = gm_l7;
                uint8_t t57 = ((t56 == ((uint32_t)1024ULL)));
                if (!t57) {
                    goto gm_lb2;
                }
                int32_t t58 = gm_l5;
                uint8_t t59 = (t58 > ((int32_t)-24LL));
                if (!t59) {
                    goto gm_lb2;
                }
                double t60 = gm_l0;
                double t61 = gm_l8;
                double t62 = (t61 / ((double)4e0));
                double t63 = (t60 - t62);
                gm_l38 = t63;
                goto gm_lb1;
            }
gm_lb2:;
            double t64 = gm_l0;
            double t65 = gm_l8;
            double t66 = (t65 / ((double)2e0));
            double t67 = (t64 - t66);
            gm_l38 = t67;
        }
gm_lb1:;
        double t68 = gm_l38;
        gm_l10 = t68;
        uint32_t t69 = gm_l7;
        uint32_t t70 = (uint32_t)(t69 & ((uint32_t)1ULL));
        uint8_t t71 = ((t70 == ((uint32_t)0ULL)));
        gm_l11 = t71;
        gm_t7mlrlibdq1ja t72;
        for (size_t i73 = 0; i73 < 32; i73++) GM_ST(uint8_t, (uint8_t *)&t72 + i73 * 1, ((uint8_t)0ULL));
        gm_l12 = t72;
        gm_l14 = ((uint64_t)1ULL);
    }
    {
        gm_l13 = ((uint64_t)17ULL);
        uint64_t t74 = gm_l14;
        uint64_t t75 = gm_l13;
        uint8_t t76 = (t74 <= t75);
        gm_l15 = t76;
        {
            for (;;) {
                uint8_t t77 = gm_l15;
                if (!t77) {
                    goto gm_lb3;
                }
                {
                    {
                        {
                            uint64_t t78 = gm_l14;
                            gm_l16 = t78;
                            double t79 = gm_l0;
                            uint64_t t80 = gm_l16;
                            uint64_t r81; bool o82 = __builtin_sub_overflow(t80, ((uint64_t)1ULL), &r81);
                            if (o82) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 264, 32);
                            uint64_t t83 = r81;
                            uint32_t t84 = (uint32_t)t83;
                            void * t85 = (void *)((uint8_t *)&gm_l12);
                            uint64_t t86 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t87 = (uintptr_t)((uint64_t)32ULL);
                            if (t86 > t87 || t87 > (uintptr_t)((uint64_t)32ULL)) gm_trap_slice(t86, t87, ((uint64_t)32ULL), gm_srcfde9lsgge1ld, 264, 56);
                            uint64_t t88 = ((uintptr_t)((uint64_t)32ULL)) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t89;
                            GM_ST(void *, (uint8_t *)&t89, (void *)(((uint8_t *)t85 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(t88));
                            gm_b16_8 t90 = gm_f_sci_oikoiqs(t79, t84, t89);
                            gm_l17 = t90;
                            gm_b16_8 t91 = gm_l17;
                            gm_b16_8 t92 = gm_l1;
                            gm_t43v1gep754th t93 = gm_f_fmt_split_exp_0kapfov(t91, t92);
                            gm_l18 = t93;
                            gm_l19 = ((int64_t)0LL);
                            gm_l21 = ((uint64_t)0ULL);
                            uint64_t t94 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
                            gm_l20 = t94;
                            {
                                for (;;) {
                                    uint64_t t95 = gm_l21;
                                    uint64_t t96 = gm_l20;
                                    uint8_t t97 = (t95 < t96);
                                    if (!t97) {
                                        goto gm_lb8;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t98 = gm_l21;
                                                gm_l22 = t98;
                                                int64_t t99 = gm_l19;
                                                int64_t r100; bool o101 = __builtin_mul_overflow(t99, ((int64_t)10LL), &r100);
                                                if (o101) gm_trap("integer overflow in `*`", gm_srcfde9lsgge1ld, 267, 32);
                                                int64_t t102 = r100;
                                                void * t103 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                                uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                                uint64_t t105 = gm_l22;
                                                if ((uintptr_t)t105 >= (uintptr_t)(t104)) gm_trap_index((uintptr_t)t105, t104, gm_srcfde9lsgge1ld, 267, 45);
                                                uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)t103) + (uintptr_t)t105 * 1));
                                                uint8_t r107; bool o108 = __builtin_sub_overflow(t106, ((uint8_t)48ULL), &r107);
                                                if (o108) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 267, 45);
                                                uint8_t t109 = r107;
                                                int64_t t110 = (int64_t)t109;
                                                int64_t r111; bool o112 = __builtin_add_overflow(t102, t110, &r111);
                                                if (o112) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 267, 32);
                                                int64_t t113 = r111;
                                                gm_l19 = t113;
                                                goto gm_lb11;
                                            }
gm_lb12:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 267, 9);
                                        }
gm_lb11:;
                                    }
gm_lb10:;
                                    uint64_t t114 = gm_l21;
                                    uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                                    uint64_t t117 = r115;
                                    gm_l21 = t117;
                                }
                            }
gm_lb8:;
                            int32_t t118 = GM_LD(int32_t, (((uint8_t *)&gm_l18) + 8));
                            uint64_t t119 = gm_l16;
                            uint64_t r120; bool o121 = __builtin_sub_overflow(t119, ((uint64_t)1ULL), &r120);
                            if (o121) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 268, 26);
                            uint64_t t122 = r120;
                            int32_t t123 = (int32_t)t122;
                            int32_t r124; bool o125 = __builtin_sub_overflow(t118, t123, &r124);
                            if (o125) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 268, 16);
                            int32_t t126 = r124;
                            gm_l23 = t126;
                            gm_t7vno1ieejig6 t127;
                            GM_ST(int64_t, ((uint8_t *)&t127 + 0), ((int64_t)0LL));
                            GM_ST(int64_t, ((uint8_t *)&t127 + 8), ((int64_t)-1LL));
                            GM_ST(int64_t, ((uint8_t *)&t127 + 16), ((int64_t)1LL));
                            gm_l24 = t127;
                            void * t128 = (void *)((uint8_t *)&gm_l24);
                            uint64_t t129 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t130 = (uintptr_t)((uint64_t)3ULL);
                            if (t129 > t130 || t130 > (uintptr_t)((uint64_t)3ULL)) gm_trap_slice(t129, t130, ((uint64_t)3ULL), gm_srcfde9lsgge1ld, 271, 9);
                            uint64_t t131 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t132;
                            GM_ST(void *, (uint8_t *)&t132, (void *)(((uint8_t *)t128 + (uintptr_t)((uint64_t)0ULL) * 8)));
                            GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(t131));
                            gm_l25 = t132;
                            gm_l26 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t133 = gm_l26;
                                    uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                    uint8_t t135 = (t133 < t134);
                                    if (!t135) {
                                        goto gm_lb13;
                                    }
                                    {
                                        void * t136 = GM_LD(void *, ((uint8_t *)&gm_l25));
                                        uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                        uint64_t t138 = gm_l26;
                                        {
                                            {
                                                int64_t t139 = GM_LD(int64_t, (((uint8_t *)t136) + (uintptr_t)t138 * 8));
                                                gm_l27 = t139;
                                                int64_t t140 = gm_l19;
                                                int64_t t141 = gm_l27;
                                                int64_t r142; bool o143 = __builtin_add_overflow(t140, t141, &r142);
                                                if (o143) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 272, 21);
                                                int64_t t144 = r142;
                                                gm_l28 = t144;
                                                int64_t t145 = gm_l28;
                                                uint8_t t146 = (t145 <= ((int64_t)0LL));
                                                if (t146) {
                                                    goto gm_lb15;
                                                }
                                                int64_t t147 = gm_l28;
                                                int32_t t148 = gm_l23;
                                                gm_tnur0m5k1ttis t149;
                                                GM_ST(uint32_t, (uint8_t *)&t149 + 12, 0);
                                                GM_ST(int64_t, ((uint8_t *)&t149 + 0), t147);
                                                GM_ST(int32_t, ((uint8_t *)&t149 + 8), t148);
                                                gm_f_format_13oum9l((void *)&gm_l39, (void *)NULL, cx, NULL, t149);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                                if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_srcfde9lsgge1ld, 274, 18);
                                                gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                                                gm_l29 = t151;
                                                gm_b16_8 t152 = gm_l29;
                                                gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
                                                gm_f_parse_float_17hgksa((void *)&gm_l40, t153);
                                                uint8_t t154 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                                if (t154) {
                                                    double t155 = GM_LD(double, (((uint8_t *)&gm_l40) + 8));
                                                    gm_l41 = t155;
                                                } else {
                                                    gm_ttiiv5vhiuvvr t156 = GM_LD(gm_ttiiv5vhiuvvr, (((uint8_t *)&gm_l40) + 8));
                                                    gm_l30 = t156;
                                                    double t157 = gm_l0;
                                                    gm_l41 = t157;
                                                }
                                                double t158 = gm_l41;
                                                gm_l31 = t158;
                                                gm_b16_8 t159 = gm_l29;
                                                gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
                                                gm_b0_1 t161 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t160, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ka73u23j5nf)));
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t162 = gm_l11;
                                                if (t162) {
                                                    double t163 = gm_l31;
                                                    double t164 = gm_l10;
                                                    uint8_t t165 = (t163 >= t164);
                                                    if (t165) {
                                                        double t166 = gm_l31;
                                                        double t167 = gm_l9;
                                                        uint8_t t168 = (t166 <= t167);
                                                        gm_l43 = t168;
                                                    } else {
                                                        gm_l43 = ((uint8_t)0);
                                                    }
                                                    uint8_t t169 = gm_l43;
                                                    gm_l42 = t169;
                                                } else {
                                                    double t170 = gm_l31;
                                                    double t171 = gm_l10;
                                                    uint8_t t172 = (t170 > t171);
                                                    if (t172) {
                                                        double t173 = gm_l31;
                                                        double t174 = gm_l9;
                                                        uint8_t t175 = (t173 < t174);
                                                        gm_l44 = t175;
                                                    } else {
                                                        gm_l44 = ((uint8_t)0);
                                                    }
                                                    uint8_t t176 = gm_l44;
                                                    gm_l42 = t176;
                                                }
                                                uint8_t t177 = gm_l42;
                                                gm_l32 = t177;
                                                uint8_t t178 = gm_l32;
                                                if (t178) {
                                                    int64_t t179 = gm_l28;
                                                    void * t180 = (void *)((uint8_t *)&gm_l12);
                                                    uint64_t t181 = (uintptr_t)((uint64_t)0ULL);
                                                    uint64_t t182 = (uintptr_t)((uint64_t)32ULL);
                                                    if (t181 > t182 || t182 > (uintptr_t)((uint64_t)32ULL)) gm_trap_slice(t181, t182, ((uint64_t)32ULL), gm_srcfde9lsgge1ld, 279, 40);
                                                    uint64_t t183 = ((uintptr_t)((uint64_t)32ULL)) - (uintptr_t)((uint64_t)0ULL);
                                                    gm_b16_8 t184;
                                                    GM_ST(void *, (uint8_t *)&t184, (void *)(((uint8_t *)t180 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                                    GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(t183));
                                                    gm_b16_8 t185 = gm_f_dec_395otau(t179, t184);
                                                    gm_l33 = t185;
                                                    gm_l35 = ((uint64_t)0ULL);
                                                    uint64_t t186 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                                    gm_l34 = t186;
                                                    {
                                                        for (;;) {
                                                            uint64_t t187 = gm_l35;
                                                            uint64_t t188 = gm_l34;
                                                            uint8_t t189 = (t187 < t188);
                                                            if (!t189) {
                                                                goto gm_lb18;
                                                            }
                                                            {
                                                                {
                                                                    {
                                                                        uint64_t t190 = gm_l35;
                                                                        gm_l36 = t190;
                                                                        void * t191 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                                                        uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                                                        uint64_t t193 = gm_l36;
                                                                        if ((uintptr_t)t193 >= (uintptr_t)(t192)) gm_trap_index((uintptr_t)t193, t192, gm_srcfde9lsgge1ld, 280, 38);
                                                                        void * t194 = GM_LD(void *, ((uint8_t *)&gm_l33));
                                                                        uint64_t t195 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                                                        uint64_t t196 = gm_l36;
                                                                        uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)t194) + (uintptr_t)t196 * 1));
                                                                        GM_ST(uint8_t, (((uint8_t *)t191) + (uintptr_t)t193 * 1), t197);
                                                                        goto gm_lb21;
                                                                    }
gm_lb22:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 280, 17);
                                                                }
gm_lb21:;
                                                            }
gm_lb20:;
                                                            uint64_t t198 = gm_l35;
                                                            uint64_t r199; bool o200 = __builtin_add_overflow(t198, ((uint64_t)1ULL), &r199);
                                                            uint64_t t201 = r199;
                                                            gm_l35 = t201;
                                                        }
                                                    }
gm_lb18:;
                                                    uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                                    int32_t t203 = gm_l23;
                                                    uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                                                    int32_t t205 = (int32_t)t204;
                                                    int32_t r206; bool o207 = __builtin_add_overflow(t203, t205, &r206);
                                                    if (o207) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 281, 41);
                                                    int32_t t208 = r206;
                                                    int32_t r209; bool o210 = __builtin_sub_overflow(t208, ((int32_t)1LL), &r209);
                                                    if (o210) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 281, 41);
                                                    int32_t t211 = r209;
                                                    gm_t43v1gep754th t212;
                                                    GM_ST(uint32_t, (uint8_t *)&t212 + 12, 0);
                                                    GM_ST(uint64_t, ((uint8_t *)&t212 + 0), t202);
                                                    GM_ST(int32_t, ((uint8_t *)&t212 + 8), t211);
                                                    F.ret = t212;
                                                    goto gm_fret;
                                                }
                                                goto gm_lb16;
                                            }
gm_lb17:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 271, 9);
                                        }
gm_lb16:;
                                    }
gm_lb15:;
                                    uint64_t t213 = gm_l26;
                                    uint64_t r214; bool o215 = __builtin_add_overflow(t213, ((uint64_t)1ULL), &r214);
                                    uint64_t t216 = r214;
                                    gm_l26 = t216;
                                }
                            }
gm_lb13:;
                            goto gm_lb6;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 263, 5);
                    }
gm_lb6:;
                }
gm_lb5:;
                uint64_t t217 = gm_l14;
                uint64_t t218 = gm_l13;
                uint8_t t219 = ((t217 == t218));
                if (t219) {
                    gm_l15 = ((uint8_t)0);
                } else {
                    uint64_t t220 = gm_l14;
                    uint64_t r221; bool o222 = __builtin_add_overflow(t220, ((uint64_t)1ULL), &r221);
                    uint64_t t223 = r221;
                    gm_l14 = t223;
                }
            }
        }
gm_lb3:;
    }
    {
        gm_t43v1gep754th t224;
        GM_ST(uint32_t, (uint8_t *)&t224 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t224 + 0), ((uint64_t)0ULL));
        GM_ST(int32_t, ((uint8_t *)&t224 + 8), ((int32_t)0LL));
        F.ret = t224;
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
inline __attribute__((aligned(4))) gm_t43v1gep754th gm_f_fmt_shortest_2l54as9(void *env, gm_node *cx, gm_node *inst, double a0, uint16_t a1, gm_b16_8 a2) {
    struct gm_fr_fmt_shortest_2l54as9 F;
    double gm_l0; /* x */
    uint16_t gm_l1; /* bits */
    gm_b16_8 gm_l2; /* dig */
    gm_b16_8 gm_l3; /* text */
    gm_t43v1gep754th gm_l4; /* r */
    GM_STACK_CHECK(&F, 394u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_tabb710d9mq2e gm_l7;
    uint16_t t2 = gm_l1;
    uint8_t t3 = ((t2 == ((uint16_t)16ULL)));
    if (t3) {
        double t4 = gm_l0;
        gm_b16_8 t5 = gm_l2;
        gm_t43v1gep754th t6 = gm_f_fmt_shortest16_cln2heq((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t6;
        goto gm_fret;
    }
    uint16_t t7 = gm_l1;
    uint8_t t8 = ((t7 == ((uint16_t)32ULL)));
    if (t8) {
        double t9 = gm_l0;
        float t10 = (float)((double)t9);
        gm_tdqbns26udv5e t11;
        GM_ST(float, ((uint8_t *)&t11 + 0), t10);
        gm_f_format_odb8fco((void *)&gm_l6, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcfde9lsgge1ld, 242, 29);
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_l5 = t13;
    } else {
        double t14 = gm_l0;
        gm_tidu2er0d7h95 t15;
        GM_ST(double, ((uint8_t *)&t15 + 0), t14);
        gm_f_format_eucvbnh((void *)&gm_l7, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcfde9lsgge1ld, 242, 69);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
        gm_l5 = t17;
    }
    gm_b16_8 t18 = gm_l5;
    gm_l3 = t18;
    gm_b16_8 t19 = gm_l3;
    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
    gm_b16_8 t21 = gm_l2;
    gm_t43v1gep754th t22 = gm_f_fmt_split_exp_0kapfov(t20, t21);
    gm_l4 = t22;
    gm_b16_8 t23 = gm_l3;
    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
    gm_b0_1 t25 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t24, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_km7a24is5mf)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t43v1gep754th t26 = gm_l4;
    F.ret = t26;
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
__attribute__((aligned(4))) gm_t43v1gep754th gm_f_fmt_split_exp_0kapfov(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fmt_split_exp_0kapfov F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* dig */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* i */
    uint8_t gm_l4; /* neg */
    int32_t gm_l5; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 503u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l6;
    uint8_t gm_l7;
    int32_t gm_l8;
    gm_l2 = ((uint64_t)0ULL);
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l3;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            void * t13 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t15 = gm_l3;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t13) + (uintptr_t)t15 * 1));
            uint8_t t17 = (!(t16 == ((uint8_t)101ULL)));
            if (!t17) {
                goto gm_lb1;
            }
            {
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t20 = gm_l3;
                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t18) + (uintptr_t)t20 * 1));
                uint8_t t22 = (!(t21 == ((uint8_t)46ULL)));
                if (t22) {
                    void * t23 = GM_LD(void *, ((uint8_t *)&gm_l1));
                    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                    uint64_t t25 = gm_l2;
                    if ((uintptr_t)t25 >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)t25, t24, gm_srcfde9lsgge1ld, 221, 13);
                    void * t26 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t28 = gm_l3;
                    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)t26) + (uintptr_t)t28 * 1));
                    GM_ST(uint8_t, (((uint8_t *)t23) + (uintptr_t)t25 * 1), t29);
                    uint64_t t30 = gm_l2;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    if (o32) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 222, 13);
                    uint64_t t33 = r31;
                    gm_l2 = t33;
                }
                uint64_t t34 = gm_l3;
                uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                if (o36) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 224, 9);
                uint64_t t37 = r35;
                gm_l3 = t37;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t38 = gm_l3;
    uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
    if (o40) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 226, 5);
    uint64_t t41 = r39;
    gm_l3 = t41;
    uint64_t t42 = gm_l3;
    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t44 = (t42 < t43);
    if (t44) {
        void * t45 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t47 = gm_l3;
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t45) + (uintptr_t)t47 * 1));
        uint8_t t49 = ((t48 == ((uint8_t)45ULL)));
        gm_l6 = t49;
    } else {
        gm_l6 = ((uint8_t)0);
    }
    uint8_t t50 = gm_l6;
    gm_l4 = t50;
    {
        {
            uint64_t t51 = gm_l3;
            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t53 = (t51 < t52);
            if (!t53) {
                goto gm_lb5;
            }
            void * t54 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t56 = gm_l3;
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t54) + (uintptr_t)t56 * 1));
            uint8_t t58 = ((t57 == ((uint8_t)45ULL)));
            if (t58) {
                gm_l7 = ((uint8_t)1);
            } else {
                void * t59 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t61 = gm_l3;
                uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)t59) + (uintptr_t)t61 * 1));
                uint8_t t63 = ((t62 == ((uint8_t)43ULL)));
                gm_l7 = t63;
            }
            uint8_t t64 = gm_l7;
            if (!t64) {
                goto gm_lb5;
            }
            uint64_t t65 = gm_l3;
            uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
            if (o67) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 228, 62);
            uint64_t t68 = r66;
            gm_l3 = t68;
            goto gm_lb4;
        }
gm_lb5:;
    }
gm_lb4:;
    gm_l5 = ((int32_t)0LL);
    {
        for (;;) {
            uint64_t t69 = gm_l3;
            uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t71 = (t69 < t70);
            if (!t71) {
                goto gm_lb6;
            }
            {
                int32_t t72 = gm_l5;
                int32_t r73; bool o74 = __builtin_mul_overflow(t72, ((int32_t)10LL), &r73);
                if (o74) gm_trap("integer overflow in `*`", gm_srcfde9lsgge1ld, 231, 13);
                int32_t t75 = r73;
                void * t76 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t78 = gm_l3;
                uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)t76) + (uintptr_t)t78 * 1));
                uint8_t r80; bool o81 = __builtin_sub_overflow(t79, ((uint8_t)48ULL), &r80);
                if (o81) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 231, 26);
                uint8_t t82 = r80;
                int32_t t83 = (int32_t)t82;
                int32_t r84; bool o85 = __builtin_add_overflow(t75, t83, &r84);
                if (o85) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 231, 13);
                int32_t t86 = r84;
                gm_l5 = t86;
                uint64_t t87 = gm_l3;
                uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                if (o89) gm_trap("integer overflow in `+`", gm_srcfde9lsgge1ld, 232, 9);
                uint64_t t90 = r88;
                gm_l3 = t90;
            }
gm_lb8:;
        }
    }
gm_lb6:;
    uint64_t t91 = gm_l2;
    uint8_t t92 = gm_l4;
    if (t92) {
        int32_t t93 = gm_l5;
        int32_t r94; bool o95 = __builtin_sub_overflow(((int32_t)0LL), t93, &r94);
        if (o95) gm_trap("integer overflow in `-`", gm_srcfde9lsgge1ld, 234, 33);
        int32_t t96 = r94;
        gm_l8 = t96;
    } else {
        int32_t t97 = gm_l5;
        gm_l8 = t97;
    }
    int32_t t98 = gm_l8;
    gm_t43v1gep754th t99;
    GM_ST(uint32_t, (uint8_t *)&t99 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t99 + 0), t91);
    GM_ST(int32_t, ((uint8_t *)&t99 + 8), t98);
    F.ret = t99;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fmt_valid_utf8_9mug7al(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fmt_valid_utf8_9mug7al F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* k */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = ((uint64_t)1ULL);
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l1 = t7;
    {
        for (;;) {
            uint64_t t8 = gm_l2;
            uint64_t t9 = gm_l1;
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t11 = gm_l2;
                        gm_l3 = t11;
                        void * t12 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t14 = gm_l3;
                        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t12) + (uintptr_t)t14 * 1));
                        uint8_t t16 = (uint8_t)(t15 & ((uint8_t)192ULL));
                        uint8_t t17 = (!(t16 == ((uint8_t)128ULL)));
                        if (t17) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 163, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t18 = gm_l2;
            uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
            uint64_t t21 = r19;
            gm_l2 = t21;
        }
    }
gm_lb1:;
    void * t22 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t23, gm_srcfde9lsgge1ld, 166, 10);
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t22) + (uintptr_t)((uint64_t)0ULL) * 1));
    gm_l4 = t24;
    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t26 = ((t25 == ((uint64_t)3ULL)));
    if (t26) {
        uint8_t t27 = gm_l4;
        uint8_t t28 = ((t27 == ((uint8_t)224ULL)));
        if (t28) {
            void * t29 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t30, gm_srcfde9lsgge1ld, 168, 31);
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t29) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint8_t t32 = (t31 >= ((uint8_t)160ULL));
            F.ret = t32;
            goto gm_fret;
        }
        uint8_t t33 = gm_l4;
        uint8_t t34 = ((t33 == ((uint8_t)237ULL)));
        if (t34) {
            void * t35 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t36)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t36, gm_srcfde9lsgge1ld, 169, 31);
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t35) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint8_t t38 = (t37 < ((uint8_t)160ULL));
            F.ret = t38;
            goto gm_fret;
        }
    }
    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t40 = ((t39 == ((uint64_t)4ULL)));
    if (t40) {
        uint8_t t41 = gm_l4;
        uint8_t t42 = ((t41 == ((uint8_t)240ULL)));
        if (t42) {
            void * t43 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t44)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t44, gm_srcfde9lsgge1ld, 172, 31);
            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)t43) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint8_t t46 = (t45 >= ((uint8_t)144ULL));
            F.ret = t46;
            goto gm_fret;
        }
        uint8_t t47 = gm_l4;
        uint8_t t48 = ((t47 == ((uint8_t)244ULL)));
        if (t48) {
            void * t49 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t50)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t50, gm_srcfde9lsgge1ld, 173, 31);
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t49) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint8_t t52 = (t51 < ((uint8_t)144ULL));
            F.ret = t52;
            goto gm_fret;
        }
    }
    F.ret = ((uint8_t)1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_format_spec_7aph60t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_format_spec_7aph60t F;
    void * gm_l0; /* m */
    void * gm_l1; /* out */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    void * gm_l4; /* spec */
    uint8_t gm_l6; /* neg */
    uint8_t gm_l7; /* finite */
    uint8_t gm_l8; /* number */
    unsigned __int128 gm_l9; /* v */
    gm_ttovhfee8mf28 gm_l10; /* info */
    unsigned __int128 gm_l11; /* mag */
    gm_b16_8 gm_l13; /* s */
    uint16_t gm_l14; /* bits */
    double gm_l15; /* x */
    double gm_l16; /* a */
    GM_STACK_CHECK(&F, 2048u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t53at80pfdus3 gm_l18;
    gm_tg5lql6itufk7 gm_l19;
    gm_ttovhfee8mf28 gm_l20;
    uint8_t gm_l21;
    unsigned __int128 gm_l22;
    gm_b16_8 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    uint8_t gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tl572d7579hns gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_b16_8 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    {
        {
            {
                gm_trqfvmjtt4sij t29;
                gm_f_list_uvfu1k1((void *)&t29, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l5 = t29;
                gm_l6 = ((uint8_t)0);
                gm_l7 = ((uint8_t)1);
                gm_l8 = ((uint8_t)1);
                void * t30 = gm_l0;
                gm_t88rcucg1b7uk t31 = gm_l2;
                gm_f_kind_htv6093((void *)&gm_l18, t30, GM_CONV(gm_csguankko4lmhd, t31));
                {
                    {
                        {
                            {
                                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                                uint8_t t33 = (uint8_t)(t32 == ((uint8_t)0ULL));
                                if (!t33) {
                                    goto gm_lb5;
                                }
                                goto gm_lb4;
                            }
gm_lb5:;
                            {
                                uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                                uint8_t t35 = (uint8_t)(t34 == ((uint8_t)1ULL));
                                if (!t35) {
                                    goto gm_lb6;
                                }
                                goto gm_lb4;
                            }
gm_lb6:;
                            {
                                uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                                uint8_t t37 = (uint8_t)(t36 == ((uint8_t)2ULL));
                                if (!t37) {
                                    goto gm_lb7;
                                }
                                goto gm_lb4;
                            }
gm_lb7:;
                            goto gm_lb3;
                        }
gm_lb4:;
                        void * t38 = gm_l0;
                        gm_t88rcucg1b7uk t39 = gm_l2;
                        gm_b16_8 t40 = gm_l3;
                        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                        unsigned __int128 t42 = gm_f_get_int_pg7p4gc(t38, GM_CONV(gm_csguankko4lmhd, t39), t41);
                        gm_l9 = t42;
                        void * t43 = gm_l0;
                        gm_t88rcucg1b7uk t44 = gm_l2;
                        gm_tg5lql6itufk7 t45 = gm_f_int_info_ulcevdv(t43, GM_CONV(gm_csguankko4lmhd, t44));
                        gm_l19 = t45;
                        uint8_t t46 = (uint8_t)((((uint8_t *)&gm_l19))[8] != 0);
                        if (t46) {
                            gm_ttovhfee8mf28 t47 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l19));
                            gm_l20 = t47;
                        } else {
                            gm_tl572d7579hns t48;
                            GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t48 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t48 + 16, 0);
                            ((uint8_t *)&t48)[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&t48 + 8), GM_ZERO(gm_b0_1));
                            memmove(&F.ret, ((uint8_t *)&t48), 24);
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                            goto gm_ld28_1;
                        }
                        gm_ttovhfee8mf28 t49 = gm_l20;
                        gm_l10 = t49;
                        uint8_t t50 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
                        if (t50) {
                            unsigned __int128 t51 = gm_l9;
                            __int128 t52;
                            memset(&t52, 0, sizeof t52); memcpy(&t52, ((uint8_t *)&t51), sizeof t52 < 16 ? sizeof t52 : 16);
                            uint8_t t53 = (t52 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
                            gm_l21 = t53;
                        } else {
                            gm_l21 = ((uint8_t)0);
                        }
                        uint8_t t54 = gm_l21;
                        gm_l6 = t54;
                        uint8_t t55 = gm_l6;
                        if (t55) {
                            unsigned __int128 t56 = gm_l9;
                            unsigned __int128 r57; bool o58 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), t56, &r57);
                            unsigned __int128 t59 = r57;
                            gm_l22 = t59;
                        } else {
                            unsigned __int128 t60 = gm_l9;
                            gm_l22 = t60;
                        }
                        unsigned __int128 t61 = gm_l22;
                        gm_l11 = t61;
                        gm_tfbi275tn5ccf t62;
                        for (size_t i63 = 0; i63 < 131; i63++) GM_ST(uint8_t, (uint8_t *)&t62 + i63 * 1, ((uint8_t)0ULL));
                        F.l12 = t62;
                        void * t64 = gm_l4;
                        {
                            {
                                uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t64) + 20) + 0));
                                uint8_t t66 = (uint8_t)(t65 == ((uint8_t)1ULL));
                                if (!t66) {
                                    goto gm_lb9;
                                }
                                unsigned __int128 t67 = gm_l11;
                                void * t68 = (void *)((uint8_t *)&F.l12);
                                uint64_t t69 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t70 = (uintptr_t)((uint64_t)131ULL);
                                if (t69 > t70 || t70 > (uintptr_t)((uint64_t)131ULL)) gm_trap_slice(t69, t70, ((uint64_t)131ULL), gm_srcfde9lsgge1ld, 354, 66);
                                uint64_t t71 = ((uintptr_t)((uint64_t)131ULL)) - (uintptr_t)((uint64_t)0ULL);
                                gm_b16_8 t72;
                                GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t68 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(t71));
                                gm_b16_8 t73 = gm_f_radix_with_qaamfpk(t67, ((uint32_t)16ULL), ((uint8_t)0), ((uint8_t)0), t72);
                                gm_l23 = t73;
                                goto gm_lb8;
                            }
gm_lb9:;
                            {
                                uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t64) + 20) + 0));
                                uint8_t t75 = (uint8_t)(t74 == ((uint8_t)2ULL));
                                if (!t75) {
                                    goto gm_lb10;
                                }
                                unsigned __int128 t76 = gm_l11;
                                void * t77 = (void *)((uint8_t *)&F.l12);
                                uint64_t t78 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t79 = (uintptr_t)((uint64_t)131ULL);
                                if (t78 > t79 || t79 > (uintptr_t)((uint64_t)131ULL)) gm_trap_slice(t78, t79, ((uint64_t)131ULL), gm_srcfde9lsgge1ld, 355, 70);
                                uint64_t t80 = ((uintptr_t)((uint64_t)131ULL)) - (uintptr_t)((uint64_t)0ULL);
                                gm_b16_8 t81;
                                GM_ST(void *, (uint8_t *)&t81, (void *)(((uint8_t *)t77 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(t80));
                                gm_b16_8 t82 = gm_f_radix_with_qaamfpk(t76, ((uint32_t)16ULL), ((uint8_t)1), ((uint8_t)0), t81);
                                gm_l23 = t82;
                                goto gm_lb8;
                            }
gm_lb10:;
                            {
                                uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t64) + 20) + 0));
                                uint8_t t84 = (uint8_t)(t83 == ((uint8_t)3ULL));
                                if (!t84) {
                                    goto gm_lb11;
                                }
                                unsigned __int128 t85 = gm_l11;
                                void * t86 = (void *)((uint8_t *)&F.l12);
                                uint64_t t87 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t88 = (uintptr_t)((uint64_t)131ULL);
                                if (t87 > t88 || t88 > (uintptr_t)((uint64_t)131ULL)) gm_trap_slice(t87, t88, ((uint64_t)131ULL), gm_srcfde9lsgge1ld, 356, 65);
                                uint64_t t89 = ((uintptr_t)((uint64_t)131ULL)) - (uintptr_t)((uint64_t)0ULL);
                                gm_b16_8 t90;
                                GM_ST(void *, (uint8_t *)&t90, (void *)(((uint8_t *)t86 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(t89));
                                gm_b16_8 t91 = gm_f_radix_with_qaamfpk(t85, ((uint32_t)8ULL), ((uint8_t)0), ((uint8_t)0), t90);
                                gm_l23 = t91;
                                goto gm_lb8;
                            }
gm_lb11:;
                            {
                                uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t64) + 20) + 0));
                                uint8_t t93 = (uint8_t)(t92 == ((uint8_t)4ULL));
                                if (!t93) {
                                    goto gm_lb12;
                                }
                                unsigned __int128 t94 = gm_l11;
                                void * t95 = (void *)((uint8_t *)&F.l12);
                                uint64_t t96 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t97 = (uintptr_t)((uint64_t)131ULL);
                                if (t96 > t97 || t97 > (uintptr_t)((uint64_t)131ULL)) gm_trap_slice(t96, t97, ((uint64_t)131ULL), gm_srcfde9lsgge1ld, 357, 65);
                                uint64_t t98 = ((uintptr_t)((uint64_t)131ULL)) - (uintptr_t)((uint64_t)0ULL);
                                gm_b16_8 t99;
                                GM_ST(void *, (uint8_t *)&t99, (void *)(((uint8_t *)t95 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(t98));
                                gm_b16_8 t100 = gm_f_radix_with_qaamfpk(t94, ((uint32_t)2ULL), ((uint8_t)0), ((uint8_t)0), t99);
                                gm_l23 = t100;
                                goto gm_lb8;
                            }
gm_lb12:;
                            {
                                unsigned __int128 t101 = gm_l11;
                                void * t102 = (void *)((uint8_t *)&F.l12);
                                uint64_t t103 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t104 = (uintptr_t)((uint64_t)131ULL);
                                if (t103 > t104 || t104 > (uintptr_t)((uint64_t)131ULL)) gm_trap_slice(t103, t104, ((uint64_t)131ULL), gm_srcfde9lsgge1ld, 358, 63);
                                uint64_t t105 = ((uintptr_t)((uint64_t)131ULL)) - (uintptr_t)((uint64_t)0ULL);
                                gm_b16_8 t106;
                                GM_ST(void *, (uint8_t *)&t106, (void *)(((uint8_t *)t102 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(t105));
                                gm_b16_8 t107 = gm_f_radix_with_qaamfpk(t101, ((uint32_t)10ULL), ((uint8_t)0), ((uint8_t)0), t106);
                                gm_l23 = t107;
                                goto gm_lb8;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 353, 18);
                        }
gm_lb8:;
                        gm_b16_8 t108 = gm_l23;
                        gm_l13 = t108;
                        void * t109 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t110 = gm_l13;
                        gm_tftb1odc739t5 t111 = gm_f_extend_0cs85gs(t109, t110);
                        gm_l24 = t111;
                        uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                        if (!t112) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcfde9lsgge1ld, 360, 13);
                        goto gm_lb2;
                    }
gm_lb3:;
                    {
                        uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                        uint8_t t114 = (uint8_t)(t113 == ((uint8_t)3ULL));
                        if (!t114) {
                            goto gm_lb14;
                        }
                        uint16_t t115 = GM_LD(uint16_t, (((uint8_t *)&gm_l18) + 8));
                        gm_l14 = t115;
                        void * t116 = gm_l0;
                        uint16_t t117 = gm_l14;
                        gm_b16_8 t118 = gm_l3;
                        gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
                        double t120 = gm_f_fmt_float_bits_9d6f5r5(t116, t117, t119);
                        gm_l15 = t120;
                        double t121 = gm_l15;
                        uint8_t t122 = (uint8_t)__builtin_isfinite(t121);
                        gm_l7 = t122;
                        double t123 = gm_l15;
                        uint8_t t124 = (uint8_t)(__builtin_signbit(t123) != 0);
                        if (t124) {
                            double t125 = gm_l15;
                            uint8_t t126 = (uint8_t)__builtin_isnan(t125);
                            uint8_t t127 = (uint8_t)!t126;
                            gm_l25 = t127;
                        } else {
                            gm_l25 = ((uint8_t)0);
                        }
                        uint8_t t128 = gm_l25;
                        gm_l6 = t128;
                        double t129 = gm_l15;
                        double t130 = (t129 < 0 || ((t129) == 0 && __builtin_signbit(t129))) ? -t129 : t129;
                        gm_l16 = t130;
                        double t131 = gm_l15;
                        uint8_t t132 = (uint8_t)__builtin_isnan(t131);
                        if (t132) {
                            void * t133 = (void *)((uint8_t *)&F.l5);
                            gm_b16_8 t134;
                            GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sfa86erpod86s));
                            GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(3));
                            gm_tftb1odc739t5 t135 = gm_f_extend_0cs85gs(t133, t134);
                            gm_l26 = t135;
                            uint8_t t136 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                            if (!t136) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcfde9lsgge1ld, 368, 17);
                        } else {
                            uint8_t t137 = gm_l7;
                            uint8_t t138 = (uint8_t)!t137;
                            if (t138) {
                                void * t139 = (void *)((uint8_t *)&F.l5);
                                gm_b16_8 t140;
                                GM_ST(void *, (uint8_t *)&t140, (void *)(gm_sns75reqq32d2));
                                GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(3));
                                gm_tftb1odc739t5 t141 = gm_f_extend_0cs85gs(t139, t140);
                                gm_l27 = t141;
                                uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcfde9lsgge1ld, 370, 17);
                            } else {
                                void * t143 = (void *)((uint8_t *)&F.l5);
                                double t144 = gm_l16;
                                uint16_t t145 = gm_l14;
                                void * t146 = gm_l4;
                                gm_b0_1 t147 = gm_f_fmt_float_spec_glt99ob((void *)NULL, cx, NULL, t143, t144, t145, t146);
                                if (gm_unw.kind) {
                                    goto gm_ld28_1;
                                }
                            }
                        }
                        goto gm_lb2;
                    }
gm_lb14:;
                    {
                        gm_l8 = ((uint8_t)0);
                        void * t148 = gm_l0;
                        void * t149 = (void *)((uint8_t *)&F.l5);
                        gm_t88rcucg1b7uk t150 = gm_l2;
                        gm_b16_8 t151 = gm_l3;
                        void * t152 = gm_l4;
                        gm_t9qkh0rsooc8d t153 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t152) + 20));
                        uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&t153) + 0));
                        uint8_t t155 = (uint8_t)(!(t154 == ((uint8_t)6ULL)));
                        gm_t9qkh0rsooc8d t156;
                        GM_ST(uint8_t, ((uint8_t *)&t156 + 0), ((uint8_t)6ULL));
                        gm_f_format_value_vmle7qr((void *)&gm_l28, (void *)NULL, cx, NULL, t148, t149, t150, t151, t155);
                        if (gm_unw.kind) {
                            goto gm_ld28_1;
                        }
                        uint8_t t157 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                        if (t157) {
                        } else {
                            gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                            gm_tl572d7579hns t159;
                            GM_ST(uint8_t, (uint8_t *)&t159 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t159 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t159 + 4, 0);
                            ((uint8_t *)&t159)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t159 + 8), t158);
                            memmove(&F.ret, ((uint8_t *)&t159), 24);
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                            goto gm_ld28_1;
                        }
                        void * t160 = gm_l1;
                        gm_b16_8 t161;
                        GM_ST(void *, (uint8_t *)&t161, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(0));
                        gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                        gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
                        void * t164 = gm_l4;
                        uint32_t t165 = GM_LD(uint32_t, (((uint8_t *)t164) + 8));
                        void * t166 = gm_l4;
                        uint8_t t167 = GM_LD(uint8_t, ((uint8_t *)t166));
                        void * t168 = gm_l4;
                        gm_t9qkh0rsooc8d t169 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t168) + 1));
                        void * t170 = gm_l4;
                        uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)t170) + 4));
                        gm_b0_1 t172 = gm_f_fmt_pad_ev4lnnu((void *)NULL, cx, NULL, t160, t161, t163, t165, t167, t169, t171, ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_ld28_1;
                        }
                        gm_tl572d7579hns t173;
                        GM_ST(uint8_t, (uint8_t *)&t173 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t173 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t173 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t173 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t173 + 16, 0);
                        ((uint8_t *)&t173)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t173 + 8), GM_ZERO(gm_b0_1));
                        memmove(&F.ret, ((uint8_t *)&t173), 24);
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                        goto gm_ld28_1;
                    }
gm_lb15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 346, 5);
                }
gm_lb2:;
            }
            {
                gm_trqfvmjtt4sij t174;
                gm_f_list_uvfu1k1((void *)&t174, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ld28_1;
                }
                F.l17 = t174;
                uint8_t t175 = gm_l6;
                if (t175) {
                    void * t176 = (void *)((uint8_t *)&F.l17);
                    gm_tftb1odc739t5 t177 = gm_f_push_t83rg2m(t176, ((uint8_t)45ULL));
                    gm_l29 = t177;
                    uint8_t t178 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                    if (!t178) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcfde9lsgge1ld, 385, 9);
                } else {
                    {
                        {
                            void * t179 = gm_l4;
                            uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)t179) + 2));
                            if (!t180) {
                                goto gm_lb17;
                            }
                            gm_b16_8 t181 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                            gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
                            gm_b16_8 t183;
                            GM_ST(void *, (uint8_t *)&t183, (void *)(gm_sfa86erpod86s));
                            GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(3));
                            uint8_t t184 = (gm_eq0iqiubfm7e(((uint8_t *)&t182), ((uint8_t *)&t183)));
                            uint8_t t185 = (uint8_t)!t184;
                            if (!t185) {
                                goto gm_lb17;
                            }
                            void * t186 = (void *)((uint8_t *)&F.l17);
                            gm_tftb1odc739t5 t187 = gm_f_push_t83rg2m(t186, ((uint8_t)43ULL));
                            gm_l30 = t187;
                            uint8_t t188 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                            if (!t188) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srcfde9lsgge1ld, 387, 9);
                            goto gm_lb16;
                        }
gm_lb17:;
                    }
gm_lb16:;
                }
                void * t189 = gm_l4;
                uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)t189) + 3));
                if (t190) {
                    void * t191 = (void *)((uint8_t *)&F.l17);
                    void * t192 = gm_l4;
                    {
                        {
                            {
                                {
                                    uint8_t t193 = GM_LD(uint8_t, ((((uint8_t *)t192) + 20) + 0));
                                    uint8_t t194 = (uint8_t)(t193 == ((uint8_t)1ULL));
                                    if (!t194) {
                                        goto gm_lb21;
                                    }
                                    goto gm_lb20;
                                }
gm_lb21:;
                                {
                                    uint8_t t195 = GM_LD(uint8_t, ((((uint8_t *)t192) + 20) + 0));
                                    uint8_t t196 = (uint8_t)(t195 == ((uint8_t)2ULL));
                                    if (!t196) {
                                        goto gm_lb22;
                                    }
                                    goto gm_lb20;
                                }
gm_lb22:;
                                goto gm_lb19;
                            }
gm_lb20:;
                            gm_b16_8 t197;
                            GM_ST(void *, (uint8_t *)&t197, (void *)(gm_sdk5n6a42f7nl));
                            GM_ST(uintptr_t, (uint8_t *)&t197 + 8, (uintptr_t)(2));
                            gm_l31 = t197;
                            goto gm_lb18;
                        }
gm_lb19:;
                        {
                            uint8_t t198 = GM_LD(uint8_t, ((((uint8_t *)t192) + 20) + 0));
                            uint8_t t199 = (uint8_t)(t198 == ((uint8_t)3ULL));
                            if (!t199) {
                                goto gm_lb23;
                            }
                            gm_b16_8 t200;
                            GM_ST(void *, (uint8_t *)&t200, (void *)(gm_so4mj8r7k5aek));
                            GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(2));
                            gm_l31 = t200;
                            goto gm_lb18;
                        }
gm_lb23:;
                        {
                            gm_b16_8 t201;
                            GM_ST(void *, (uint8_t *)&t201, (void *)(gm_s7cvig9qav6l5));
                            GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(2));
                            gm_l31 = t201;
                            goto gm_lb18;
                        }
gm_lb24:;
                        gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 390, 21);
                    }
gm_lb18:;
                    gm_b16_8 t202 = gm_l31;
                    gm_tftb1odc739t5 t203 = gm_f_extend_0cs85gs(t191, t202);
                    gm_l32 = t203;
                    uint8_t t204 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                    if (!t204) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcfde9lsgge1ld, 390, 9);
                }
            }
            {
                {
                    {
                        void * t205 = gm_l4;
                        uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)t205) + 4));
                        if (!t206) {
                            goto gm_lb26;
                        }
                        uint8_t t207 = gm_l7;
                        uint8_t t208 = (uint8_t)!t207;
                        if (!t208) {
                            goto gm_lb26;
                        }
                        void * t209 = gm_l1;
                        gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                        gm_b16_8 t211 = GM_LD(gm_b16_8, ((uint8_t *)&t210));
                        gm_b16_8 t212 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                        gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&t212));
                        void * t214 = gm_l4;
                        uint32_t t215 = GM_LD(uint32_t, (((uint8_t *)t214) + 8));
                        void * t216 = gm_l4;
                        uint8_t t217 = GM_LD(uint8_t, ((uint8_t *)t216));
                        uint8_t t218 = gm_l8;
                        gm_t9qkh0rsooc8d t219;
                        GM_ST(uint8_t, ((uint8_t *)&t219 + 0), ((uint8_t)2ULL));
                        gm_b0_1 t220 = gm_f_fmt_pad_ev4lnnu((void *)NULL, cx, NULL, t209, t211, t213, t215, t217, t219, ((uint8_t)0), t218);
                        if (gm_unw.kind) {
                            goto gm_ld28_2;
                        }
                        goto gm_lb25;
                    }
gm_lb26:;
                    void * t221 = gm_l1;
                    gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                    gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
                    gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                    gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&t224));
                    void * t226 = gm_l4;
                    uint32_t t227 = GM_LD(uint32_t, (((uint8_t *)t226) + 8));
                    void * t228 = gm_l4;
                    uint8_t t229 = GM_LD(uint8_t, ((uint8_t *)t228));
                    void * t230 = gm_l4;
                    gm_t9qkh0rsooc8d t231 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t230) + 1));
                    void * t232 = gm_l4;
                    uint8_t t233 = GM_LD(uint8_t, (((uint8_t *)t232) + 4));
                    uint8_t t234 = gm_l8;
                    gm_b0_1 t235 = gm_f_fmt_pad_ev4lnnu((void *)NULL, cx, NULL, t221, t223, t225, t227, t229, t231, t233, t234);
                    if (gm_unw.kind) {
                        goto gm_ld28_2;
                    }
                }
gm_lb25:;
            }
        }
        {
            void * t236 = (void *)((uint8_t *)&F.l17);
            gm_f_deinit_r70pji5(t236);
        }
        {
            void * t237 = (void *)((uint8_t *)&F.l5);
            gm_f_deinit_r70pji5(t237);
        }
        goto gm_ls238;
gm_ld28_2:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t239 = (void *)((uint8_t *)&F.l17);
            gm_f_deinit_r70pji5(t239);
            gm_unw = gm_saved;
        }
gm_ld28_1:;
        {
            gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
            void * t240 = (void *)((uint8_t *)&F.l5);
            gm_f_deinit_r70pji5(t240);
            gm_unw = gm_saved;
        }
        goto gm_lad;
gm_ls238:;
gm_lb1:;
    }
    {
        gm_tl572d7579hns t241;
        GM_ST(uint8_t, (uint8_t *)&t241 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t241 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t241 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t241 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t241 + 16, 0);
        ((uint8_t *)&t241)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t241 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t241), 24);
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
__attribute__((aligned(4))) void gm_f_format_value_vmle7qr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint8_t a4) {
    struct gm_fr_format_value_vmle7qr F;
    void * gm_l0; /* m */
    void * gm_l1; /* out */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    uint8_t gm_l4; /* top */
    uint16_t gm_l5; /* bits */
    gm_t88rcucg1b7uk gm_l6; /* elem */
    gm_t2lqu8jsj511d gm_l7; /* sp */
    uint64_t gm_l8; /* esz */
    uint64_t gm_l9; /* total */
    gm_b16_8 gm_l10; /* data */
    uint64_t gm_l12; /* len */
    gm_t88rcucg1b7uk gm_l13; /* elem */
    gm_b16_8 gm_l14; /* elems */
    gm_b16_8 gm_l15; /* names */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_t88rcucg1b7uk gm_l19; /* et */
    gm_t88rcucg1b7uk gm_l20; /* inner */
    gm_t88rcucg1b7uk gm_l21; /* err */
    gm_t88rcucg1b7uk gm_l22; /* ok */
    gm_b16_8 gm_l23; /* payload */
    GM_STACK_CHECK(&F, 2694u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t53at80pfdus3 gm_l24;
    gm_b16_8 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tf5srh4mp4b3i gm_l28;
    uint64_t gm_l29;
    gm_t934k1j7u70ct gm_l30;
    gm_b16_8 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tl572d7579hns gm_l33;
    gm_tl572d7579hns gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tlcm4lvu3g1a8 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    gm_tl572d7579hns gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tl572d7579hns gm_l43;
    gm_tftb1odc739t5 gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_tl572d7579hns gm_l46;
    gm_tl572d7579hns gm_l47;
    gm_tl572d7579hns gm_l48;
    gm_tftb1odc739t5 gm_l49;
    {
        void * t30 = gm_l0;
        gm_t88rcucg1b7uk t31 = gm_l2;
        gm_f_kind_htv6093((void *)&gm_l24, t30, GM_CONV(gm_csguankko4lmhd, t31));
        {
            {
                {
                    {
                        {
                            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)0ULL));
                            if (!t33) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)1ULL));
                            if (!t35) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)2ULL));
                            if (!t37) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t38 = gm_l0;
                    void * t39 = gm_l1;
                    gm_t88rcucg1b7uk t40 = gm_l2;
                    gm_b16_8 t41 = gm_l3;
                    gm_b0_1 t42 = gm_f_fmt_int_svhqra7((void *)NULL, cx, NULL, t38, t39, t40, t41);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                    uint8_t t44 = (uint8_t)(t43 == ((uint8_t)3ULL));
                    if (!t44) {
                        goto gm_lb7;
                    }
                    uint16_t t45 = GM_LD(uint16_t, (((uint8_t *)&gm_l24) + 8));
                    gm_l5 = t45;
                    void * t46 = gm_l1;
                    void * t47 = gm_l0;
                    uint16_t t48 = gm_l5;
                    gm_b16_8 t49 = gm_l3;
                    gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                    double t51 = gm_f_fmt_float_bits_9d6f5r5(t47, t48, t50);
                    uint16_t t52 = gm_l5;
                    gm_b0_1 t53 = gm_f_fmt_float_1ivmudg((void *)NULL, cx, NULL, t46, t51, t52);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                    uint8_t t55 = (uint8_t)(t54 == ((uint8_t)4ULL));
                    if (!t55) {
                        goto gm_lb8;
                    }
                    void * t56 = gm_l1;
                    void * t57 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t58)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t58, gm_srcfde9lsgge1ld, 12, 32);
                    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)t57) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t60 = (!(t59 == ((uint8_t)0ULL)));
                    if (t60) {
                        gm_b16_8 t61;
                        GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sehko5bb96s6p));
                        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(4));
                        gm_l25 = t61;
                    } else {
                        gm_b16_8 t62;
                        GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sogehh8t1m7tn));
                        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(5));
                        gm_l25 = t62;
                    }
                    gm_b16_8 t63 = gm_l25;
                    gm_tftb1odc739t5 t64 = gm_f_extend_0cs85gs(t56, t63);
                    gm_l26 = t64;
                    uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                    if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcfde9lsgge1ld, 12, 18);
                    goto gm_lb1;
                }
gm_lb8:;
                {
                    {
                        {
                            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                            uint8_t t67 = (uint8_t)(t66 == ((uint8_t)8ULL));
                            if (!t67) {
                                goto gm_lb11;
                            }
                            goto gm_lb10;
                        }
gm_lb11:;
                        {
                            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                            uint8_t t69 = (uint8_t)(t68 == ((uint8_t)9ULL));
                            if (!t69) {
                                goto gm_lb12;
                            }
                            goto gm_lb10;
                        }
gm_lb12:;
                        {
                            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                            uint8_t t71 = (uint8_t)(t70 == ((uint8_t)7ULL));
                            if (!t71) {
                                goto gm_lb13;
                            }
                            goto gm_lb10;
                        }
gm_lb13:;
                        {
                            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                            uint8_t t73 = (uint8_t)(t72 == ((uint8_t)16ULL));
                            if (!t73) {
                                goto gm_lb14;
                            }
                            goto gm_lb10;
                        }
gm_lb14:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    void * t74 = gm_l1;
                    void * t75 = gm_l0;
                    gm_b16_8 t76 = gm_l3;
                    gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                    uint64_t t78 = gm_f_ptr_of_b77b4c9(t75, t77);
                    gm_t4ce0jd3tdro4 t79;
                    GM_ST(uint64_t, ((uint8_t *)&t79 + 0), t78);
                    gm_tftb1odc739t5 t80 = gm_f_write_kvqtui9(t74, t79);
                    gm_l27 = t80;
                    uint8_t t81 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                    if (!t81) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcfde9lsgge1ld, 13, 59);
                    goto gm_lb1;
                }
gm_lb9:;
            }
            {
                {
                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)10ULL));
                    if (!t83) {
                        goto gm_lb15;
                    }
                    gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l24) + 8) + 4));
                    gm_l6 = t84;
                    void * t85 = gm_l0;
                    gm_b16_8 t86 = gm_l3;
                    gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                    gm_t2lqu8jsj511d t88 = gm_f_slice_parts_1se5hu4(t85, t87);
                    gm_l7 = t88;
                    void * t89 = gm_l0;
                    gm_t88rcucg1b7uk t90 = gm_l6;
                    uint64_t t91 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t89, t90);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l8 = t91;
                    uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t93 = gm_l8;
                    uint64_t r94; bool o95 = __builtin_mul_overflow(t92, t93, &r94);
                    gm_tf5srh4mp4b3i t96;
                    if (o95) {
                    gm_tf5srh4mp4b3i t97;
                    memset(&t97, 0, sizeof t97);
                        t96 = t97;
                    } else {
                    uint64_t t98 = r94;
                    gm_tf5srh4mp4b3i t99;
                    GM_ST(uint8_t, (uint8_t *)&t99 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t99 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t99 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t99), t98);
                    ((uint8_t *)&t99)[8] = 1;
                        t96 = t99;
                    }
                    gm_l28 = t96;
                    uint8_t t100 = (uint8_t)((((uint8_t *)&gm_l28))[8] != 0);
                    if (t100) {
                        uint64_t t101 = GM_LD(uint64_t, ((uint8_t *)&gm_l28));
                        gm_l29 = t101;
                    } else {
                        gm_b16_8 t102;
                        GM_ST(void *, (uint8_t *)&t102, (void *)(gm_sqp26euqk4rkd));
                        GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(15));
                        gm_tl572d7579hns t103;
                        GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
                        ((uint8_t *)&t103)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t103 + 8), t102);
                        memmove(gm_out, ((uint8_t *)&t103), 24);
                        goto gm_fdone;
                    }
                    uint64_t t104 = gm_l29;
                    gm_l9 = t104;
                    gm_b16_8 t105 = gm_f_empty_slice_ijtp9lo();
                    gm_l10 = t105;
                    uint64_t t106 = gm_l9;
                    uint8_t t107 = (t106 > ((uint64_t)0ULL));
                    if (t107) {
                        void * t108 = gm_l0;
                        void * t109 = (void *)(((uint8_t *)t108) + 184);
                        uint64_t t110 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                        uint64_t t111 = gm_l9;
                        gm_f_read_u6r4vns((void *)&gm_l30, t109, t110, t111, ((uint64_t)1ULL));
                        uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                        if (t112) {
                            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                            gm_l31 = t113;
                        } else {
                            memmove(((uint8_t *)&F.l11), (((uint8_t *)&gm_l30) + 8), 40);
                            gm_b16_8 t114 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l11)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tl572d7579hns t115;
                            GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                            ((uint8_t *)&t115)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t115 + 8), t114);
                            memmove(gm_out, ((uint8_t *)&t115), 24);
                            goto gm_fdone;
                        }
                        gm_b16_8 t116 = gm_l31;
                        gm_l10 = t116;
                    }
                    void * t117 = gm_l0;
                    gm_t88rcucg1b7uk t118 = gm_l6;
                    gm_t53at80pfdus3 t119;
                    gm_f_kind_htv6093((void *)&t119, t117, GM_CONV(gm_csguankko4lmhd, t118));
                    gm_tjcj4j2gbh6c8 t120;
                    GM_ST(uint8_t, (uint8_t *)&t120 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t120 + 0), ((uint8_t)0));
                    GM_ST(uint16_t, ((uint8_t *)&t120 + 2), ((uint16_t)8ULL));
                    uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&t119) + 0));
                    uint8_t t122 = (uint8_t)(t121 == ((uint8_t)0ULL));
                    if (t122) {
                        gm_tjcj4j2gbh6c8 t123 = GM_LD(gm_tjcj4j2gbh6c8, (((uint8_t *)&t119) + 8));
                        t122 = (uint8_t)gm_eqo3v9599nus(((uint8_t *)&t123), ((uint8_t *)&t120));
                    }
                    uint8_t t124 = (uint8_t)(t122);
                    gm_t53at80pfdus3 t125;
                    memset(&t125, 0, sizeof t125);
                    GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t125 + 8), t120);
                    if (t124) {
                        uint8_t t126 = gm_l4;
                        if (t126) {
                            void * t127 = gm_l1;
                            gm_b16_8 t128 = gm_l10;
                            gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                            gm_tftb1odc739t5 t130 = gm_f_extend_0cs85gs(t127, t129);
                            gm_l32 = t130;
                            uint8_t t131 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                            if (!t131) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcfde9lsgge1ld, 21, 26);
                        } else {
                            void * t132 = gm_l1;
                            gm_b16_8 t133 = gm_l10;
                            gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                            gm_b0_1 t135 = gm_f_fmt_quote_f7sotri((void *)NULL, cx, NULL, t132, t134);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                    } else {
                        void * t136 = gm_l0;
                        void * t137 = gm_l1;
                        gm_t88rcucg1b7uk t138 = gm_l6;
                        uint64_t t139 = gm_l8;
                        uint64_t t140 = gm_f_us_cuv01fp(t139);
                        gm_b16_8 t141 = gm_l10;
                        uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        uint64_t t143 = gm_f_us_cuv01fp(t142);
                        gm_f_fmt_seq_c3i8abi((void *)&gm_l33, (void *)NULL, cx, NULL, t136, t137, t138, t140, t141, t143);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t144 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                        if (t144) {
                        } else {
                            gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                            gm_tl572d7579hns t146;
                            GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
                            ((uint8_t *)&t146)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t146 + 8), t145);
                            memmove(gm_out, ((uint8_t *)&t146), 24);
                            goto gm_fdone;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                    uint8_t t148 = (uint8_t)(t147 == ((uint8_t)11ULL));
                    if (!t148) {
                        goto gm_lb16;
                    }
                    uint64_t t149 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                    gm_l12 = t149;
                    gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l24) + 8) + 8));
                    gm_l13 = t150;
                    void * t151 = gm_l0;
                    void * t152 = gm_l1;
                    gm_t88rcucg1b7uk t153 = gm_l13;
                    void * t154 = gm_l0;
                    gm_t88rcucg1b7uk t155 = gm_l13;
                    uint64_t t156 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t154, t155);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t157 = gm_f_us_cuv01fp(t156);
                    gm_b16_8 t158 = gm_l3;
                    uint64_t t159 = gm_l12;
                    uint64_t t160 = gm_f_us_cuv01fp(t159);
                    gm_f_fmt_seq_c3i8abi((void *)&gm_l34, (void *)NULL, cx, NULL, t151, t152, t153, t157, t158, t160);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t161 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                    if (t161) {
                    } else {
                        gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                        gm_tl572d7579hns t163;
                        GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t163 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t163 + 4, 0);
                        ((uint8_t *)&t163)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t163 + 8), t162);
                        memmove(gm_out, ((uint8_t *)&t163), 24);
                        goto gm_fdone;
                    }
                    goto gm_lb1;
                }
gm_lb16:;
                {
                    uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                    uint8_t t165 = (uint8_t)(t164 == ((uint8_t)12ULL));
                    if (!t165) {
                        goto gm_lb17;
                    }
                    gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                    gm_l14 = t166;
                    gm_b16_8 t167 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l24) + 8) + 16));
                    gm_l15 = t167;
                    void * t168 = gm_l1;
                    gm_tftb1odc739t5 t169 = gm_f_push_t83rg2m(t168, ((uint8_t)40ULL));
                    gm_l35 = t169;
                    uint8_t t170 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                    if (!t170) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_srcfde9lsgge1ld, 28, 13);
                    gm_b16_8 t171 = gm_l14;
                    gm_l16 = t171;
                    gm_l17 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t172 = gm_l17;
                            uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                            uint8_t t174 = (t172 < t173);
                            if (!t174) {
                                goto gm_lb18;
                            }
                            {
                                uint64_t t175 = gm_l17;
                                void * t176 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                uint64_t t178 = gm_l17;
                                gm_t88rcucg1b7uk t179 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t176) + (uintptr_t)t178 * 4));
                                gm_tlcm4lvu3g1a8 t180;
                                GM_ST(uint32_t, (uint8_t *)&t180 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t180 + 0), t175);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t180 + 8), t179);
                                gm_l36 = t180;
                                {
                                    {
                                        uint64_t t181 = GM_LD(uint64_t, ((uint8_t *)&gm_l36));
                                        gm_l18 = t181;
                                        gm_t88rcucg1b7uk t182 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l36) + 8));
                                        gm_l19 = t182;
                                        uint64_t t183 = gm_l18;
                                        uint8_t t184 = (t183 > ((uint64_t)0ULL));
                                        if (t184) {
                                            void * t185 = gm_l1;
                                            gm_b16_8 t186;
                                            GM_ST(void *, (uint8_t *)&t186, (void *)(gm_sg5gdnrith2i6));
                                            GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(2));
                                            gm_tftb1odc739t5 t187 = gm_f_extend_0cs85gs(t185, t186);
                                            gm_l37 = t187;
                                            uint8_t t188 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                            if (!t188) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srcfde9lsgge1ld, 30, 28);
                                        }
                                        uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                        uint8_t t190 = (t189 > ((uint64_t)0ULL));
                                        if (t190) {
                                            void * t191 = gm_l1;
                                            void * t192 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                            uint64_t t193 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                            uint64_t t194 = gm_l18;
                                            if ((uintptr_t)t194 >= (uintptr_t)(t193)) gm_trap_index((uintptr_t)t194, t193, gm_srcfde9lsgge1ld, 32, 32);
                                            gm_b16_8 t195 = GM_LD(gm_b16_8, (((uint8_t *)t192) + (uintptr_t)t194 * 16));
                                            gm_tftb1odc739t5 t196 = gm_f_extend_0cs85gs(t191, t195);
                                            gm_l38 = t196;
                                            uint8_t t197 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                            if (!t197) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srcfde9lsgge1ld, 32, 21);
                                            void * t198 = gm_l1;
                                            gm_b16_8 t199;
                                            GM_ST(void *, (uint8_t *)&t199, (void *)(gm_slgmqgkioa4u6));
                                            GM_ST(uintptr_t, (uint8_t *)&t199 + 8, (uintptr_t)(2));
                                            gm_tftb1odc739t5 t200 = gm_f_extend_0cs85gs(t198, t199);
                                            gm_l39 = t200;
                                            uint8_t t201 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                            if (!t201) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_srcfde9lsgge1ld, 33, 21);
                                        }
                                        void * t202 = gm_l0;
                                        void * t203 = gm_l1;
                                        gm_t88rcucg1b7uk t204 = gm_l19;
                                        void * t205 = gm_l0;
                                        gm_t88rcucg1b7uk t206 = gm_l2;
                                        gm_b16_8 t207 = gm_l3;
                                        uint64_t t208 = gm_l18;
                                        uint32_t t209 = (uint32_t)t208;
                                        gm_b16_8 t210 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t205, t206, t207, t209);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_f_format_value_vmle7qr((void *)&gm_l40, (void *)NULL, cx, NULL, t202, t203, t204, t210, ((uint8_t)0));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t211 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                        if (t211) {
                                        } else {
                                            gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l40) + 8));
                                            gm_tl572d7579hns t213;
                                            GM_ST(uint8_t, (uint8_t *)&t213 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t213 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t213 + 4, 0);
                                            ((uint8_t *)&t213)[0] = 1;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t213 + 8), t212);
                                            memmove(gm_out, ((uint8_t *)&t213), 24);
                                            goto gm_fdone;
                                        }
                                        goto gm_lb21;
                                    }
gm_lb22:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 29, 13);
                                }
gm_lb21:;
                            }
gm_lb20:;
                            uint64_t t214 = gm_l17;
                            uint64_t r215; bool o216 = __builtin_add_overflow(t214, ((uint64_t)1ULL), &r215);
                            uint64_t t217 = r215;
                            gm_l17 = t217;
                        }
                    }
gm_lb18:;
                    {
                        {
                            uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                            uint8_t t219 = ((t218 == ((uint64_t)1ULL)));
                            if (!t219) {
                                goto gm_lb24;
                            }
                            uint64_t t220 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint8_t t221 = ((t220 == ((uint64_t)0ULL)));
                            if (!t221) {
                                goto gm_lb24;
                            }
                            void * t222 = gm_l1;
                            gm_tftb1odc739t5 t223 = gm_f_push_t83rg2m(t222, ((uint8_t)44ULL));
                            gm_l41 = t223;
                            uint8_t t224 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                            if (!t224) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srcfde9lsgge1ld, 37, 52);
                            goto gm_lb23;
                        }
gm_lb24:;
                    }
gm_lb23:;
                    void * t225 = gm_l1;
                    gm_tftb1odc739t5 t226 = gm_f_push_t83rg2m(t225, ((uint8_t)41ULL));
                    gm_l42 = t226;
                    uint8_t t227 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                    if (!t227) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srcfde9lsgge1ld, 38, 13);
                    goto gm_lb1;
                }
gm_lb17:;
            }
            {
                {
                    uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                    uint8_t t229 = (uint8_t)(t228 == ((uint8_t)13ULL));
                    if (!t229) {
                        goto gm_lb25;
                    }
                    gm_t88rcucg1b7uk t230 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l24) + 8));
                    gm_l20 = t230;
                    void * t231 = gm_l0;
                    gm_t88rcucg1b7uk t232 = gm_l2;
                    gm_b16_8 t233 = gm_l3;
                    gm_b16_8 t234 = GM_LD(gm_b16_8, ((uint8_t *)&t233));
                    uint8_t t235 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t231, t232, t234);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t235) {
                        void * t236 = gm_l0;
                        void * t237 = gm_l1;
                        gm_t88rcucg1b7uk t238 = gm_l20;
                        void * t239 = gm_l0;
                        gm_t88rcucg1b7uk t240 = gm_l2;
                        gm_b16_8 t241 = gm_l3;
                        gm_b16_8 t242 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t239, t240, t241);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_f_format_value_vmle7qr((void *)&gm_l43, (void *)NULL, cx, NULL, t236, t237, t238, t242, ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t243 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                        if (t243) {
                        } else {
                            gm_b16_8 t244 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
                            gm_tl572d7579hns t245;
                            GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
                            ((uint8_t *)&t245)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t245 + 8), t244);
                            memmove(gm_out, ((uint8_t *)&t245), 24);
                            goto gm_fdone;
                        }
                    } else {
                        void * t246 = gm_l1;
                        gm_b16_8 t247;
                        GM_ST(void *, (uint8_t *)&t247, (void *)(gm_squedv51820an));
                        GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t248 = gm_f_extend_0cs85gs(t246, t247);
                        gm_l44 = t248;
                        uint8_t t249 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                        if (!t249) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_srcfde9lsgge1ld, 44, 17);
                    }
                    goto gm_lb1;
                }
gm_lb25:;
                {
                    uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                    uint8_t t251 = (uint8_t)(t250 == ((uint8_t)14ULL));
                    if (!t251) {
                        goto gm_lb26;
                    }
                    gm_t88rcucg1b7uk t252 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l24) + 8));
                    gm_l21 = t252;
                    gm_t88rcucg1b7uk t253 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l24) + 8) + 4));
                    gm_l22 = t253;
                    void * t254 = gm_l0;
                    gm_t88rcucg1b7uk t255 = gm_l2;
                    gm_b16_8 t256 = gm_l3;
                    gm_b16_8 t257 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t254, t255, t256);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l23 = t257;
                    void * t258 = gm_l0;
                    gm_t88rcucg1b7uk t259 = gm_l2;
                    gm_b16_8 t260 = gm_l3;
                    gm_b16_8 t261 = GM_LD(gm_b16_8, ((uint8_t *)&t260));
                    uint8_t t262 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t258, t259, t261);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t262) {
                        void * t263 = gm_l1;
                        gm_tftb1odc739t5 t264 = gm_f_push_t83rg2m(t263, ((uint8_t)33ULL));
                        gm_l45 = t264;
                        uint8_t t265 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                        if (!t265) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srcfde9lsgge1ld, 50, 17);
                        void * t266 = gm_l0;
                        void * t267 = gm_l1;
                        gm_t88rcucg1b7uk t268 = gm_l21;
                        gm_b16_8 t269 = gm_l23;
                        gm_f_format_value_vmle7qr((void *)&gm_l46, (void *)NULL, cx, NULL, t266, t267, t268, t269, ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t270 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                        if (t270) {
                        } else {
                            gm_b16_8 t271 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                            gm_tl572d7579hns t272;
                            GM_ST(uint8_t, (uint8_t *)&t272 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t272 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t272 + 4, 0);
                            ((uint8_t *)&t272)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t272 + 8), t271);
                            memmove(gm_out, ((uint8_t *)&t272), 24);
                            goto gm_fdone;
                        }
                    } else {
                        void * t273 = gm_l0;
                        void * t274 = gm_l1;
                        gm_t88rcucg1b7uk t275 = gm_l22;
                        gm_b16_8 t276 = gm_l23;
                        gm_f_format_value_vmle7qr((void *)&gm_l47, (void *)NULL, cx, NULL, t273, t274, t275, t276, ((uint8_t)0));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t277 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                        if (t277) {
                        } else {
                            gm_b16_8 t278 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                            gm_tl572d7579hns t279;
                            GM_ST(uint8_t, (uint8_t *)&t279 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t279 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t279 + 4, 0);
                            ((uint8_t *)&t279)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&t279 + 8), t278);
                            memmove(gm_out, ((uint8_t *)&t279), 24);
                            goto gm_fdone;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb26:;
            }
            {
                {
                    uint8_t t280 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                    uint8_t t281 = (uint8_t)(t280 == ((uint8_t)17ULL));
                    if (!t281) {
                        goto gm_lb27;
                    }
                    void * t282 = gm_l0;
                    void * t283 = gm_l1;
                    gm_t88rcucg1b7uk t284 = gm_l2;
                    gm_b16_8 t285 = gm_l3;
                    gm_f_fmt_adt_p6448k2((void *)&gm_l48, (void *)NULL, cx, NULL, t282, t283, t284, t285);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t286 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                    if (t286) {
                    } else {
                        gm_b16_8 t287 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                        gm_tl572d7579hns t288;
                        GM_ST(uint8_t, (uint8_t *)&t288 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t288 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t288 + 4, 0);
                        ((uint8_t *)&t288)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t288 + 8), t287);
                        memmove(gm_out, ((uint8_t *)&t288), 24);
                        goto gm_fdone;
                    }
                    goto gm_lb1;
                }
gm_lb27:;
                {
                    void * t289 = gm_l1;
                    void * t290 = gm_l0;
                    void * t291 = GM_LD(void *, (((uint8_t *)t290) + 8));
                    gm_t88rcucg1b7uk t292 = gm_l2;
                    gm_b16_8 t293 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t291, t292);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t294;
                    GM_ST(gm_b16_8, ((uint8_t *)&t294 + 0), t293);
                    gm_tftb1odc739t5 t295 = gm_f_write_jvie1fn(t289, t294);
                    gm_l49 = t295;
                    uint8_t t296 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                    if (!t296) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srcfde9lsgge1ld, 57, 14);
                    goto gm_lb1;
                }
gm_lb28:;
                gm_trap("no match arm matched the scrutinee", gm_srcfde9lsgge1ld, 9, 5);
            }
        }
gm_lb1:;
    }
    {
        gm_tl572d7579hns t297;
        GM_ST(uint8_t, (uint8_t *)&t297 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t297 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t297 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t297 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t297 + 16, 0);
        ((uint8_t *)&t297)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t297 + 8), GM_ZERO(gm_b0_1));
        memmove(gm_out, ((uint8_t *)&t297), 24);
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
