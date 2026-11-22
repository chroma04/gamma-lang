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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; void * f1; } gm_t22qobfhrnoif;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint16_t f0; uint16_t f1; } gm_tuqvtbj0p3v9c;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t22qobfhrnoif) == 16 && _Alignof(gm_t22qobfhrnoif) == 8 && offsetof(gm_t22qobfhrnoif, f0) == 0 && offsetof(gm_t22qobfhrnoif, f1) == 8, "layout of `(i: usize, x: ^Pat)`");
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; } gm_t2i8ne5ibd7v4;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[3]; uint32_t f5; gm_t9805qtonbsag f6; gm_t9qkh0rsooc8d f7; uint8_t pe[3]; } gm_t7g88i52rrtm5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
typedef struct { gm_t88rcucg1b7uk f0; gm_tl874t2atnlh3 f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t91vm1g0k2rpm;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(base: ^Expr, index: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_tuqvtbj0p3v9c) == 4 && _Alignof(gm_tuqvtbj0p3v9c) == 2 && offsetof(gm_tuqvtbj0p3v9c, f0) == 0 && offsetof(gm_tuqvtbj0p3v9c, f1) == 2, "layout of `(u16, u16)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t2i8ne5ibd7v4) == 16 && _Alignof(gm_t2i8ne5ibd7v4) == 4 && offsetof(gm_t2i8ne5ibd7v4, f0) == 0 && offsetof(gm_t2i8ne5ibd7v4, f1) == 4, "layout of `(ty: Ty, span: Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; uint64_t f6; uint8_t f7; uint8_t f8; uint8_t pe[6]; } gm_t4j35k4k4pvmb;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tiilmfd16v3fc v1; } f1; } gm_t716e5hdn64ba;
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t7g88i52rrtm5) == 24 && _Alignof(gm_t7g88i52rrtm5) == 4 && offsetof(gm_t7g88i52rrtm5, f0) == 0 && offsetof(gm_t7g88i52rrtm5, f1) == 1 && offsetof(gm_t7g88i52rrtm5, f2) == 2 && offsetof(gm_t7g88i52rrtm5, f3) == 3 && offsetof(gm_t7g88i52rrtm5, f4) == 4 && offsetof(gm_t7g88i52rrtm5, f5) == 8 && offsetof(gm_t7g88i52rrtm5, f6) == 12 && offsetof(gm_t7g88i52rrtm5, f7) == 20, "layout of `FmtSpec`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t91vm1g0k2rpm) == 40 && _Alignof(gm_t91vm1g0k2rpm) == 8 && offsetof(gm_t91vm1g0k2rpm, f0) == 0 && offsetof(gm_t91vm1g0k2rpm, f1) == 4 && offsetof(gm_t91vm1g0k2rpm, f2) == 16 && offsetof(gm_t91vm1g0k2rpm, f3) == 32 && offsetof(gm_t91vm1g0k2rpm, f4) == 33, "layout of `NumericObligation`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { uint64_t f0; gm_t5v0c8ia6a5mo f1; } gm_tgsvmddj3e85e;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_t0mglksrkfa5e f0; uint8_t f1; uint8_t pe[7]; } gm_tnpj2jtv1r8pa;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2i8ne5ibd7v4 v0; gm_thsh8t5u55gps v1; gm_t2i8ne5ibd7v4 v2; gm_t91vm1g0k2rpm v3; } f1; } gm_tpm220j5qhjke;
typedef struct { gm_th2lqbo77pvsc f0; void * f1; } gm_tpouv9r7lrri2;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tbgd14esf0jvi v1; } f1; } gm_ts5t31j0bugq1;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4j35k4k4pvmb) == 152 && _Alignof(gm_t4j35k4k4pvmb) == 8 && offsetof(gm_t4j35k4k4pvmb, f0) == 0 && offsetof(gm_t4j35k4k4pvmb, f1) == 16 && offsetof(gm_t4j35k4k4pvmb, f2) == 32 && offsetof(gm_t4j35k4k4pvmb, f3) == 48 && offsetof(gm_t4j35k4k4pvmb, f4) == 56 && offsetof(gm_t4j35k4k4pvmb, f5) == 96 && offsetof(gm_t4j35k4k4pvmb, f6) == 136 && offsetof(gm_t4j35k4k4pvmb, f7) == 144 && offsetof(gm_t4j35k4k4pvmb, f8) == 145, "layout of `ArgBinder`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t716e5hdn64ba) == 32 && _Alignof(gm_t716e5hdn64ba) == 8 && offsetof(gm_t716e5hdn64ba, f0) == 0 && offsetof(gm_t716e5hdn64ba, f1) == 8, "layout of `EffectNamed`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_ts5t31j0bugq1 f0; uint8_t f1; uint8_t pe[7]; } gm_tck5nas0nf5v8;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_t716e5hdn64ba f0; uint8_t f1; uint8_t pe[7]; } gm_tlbrdhah49ci5;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnpj2jtv1r8pa) == 48 && _Alignof(gm_tnpj2jtv1r8pa) == 8 && offsetof(gm_tnpj2jtv1r8pa, f0) == 0 && offsetof(gm_tnpj2jtv1r8pa, f1) == 40, "layout of `?FieldDef`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpm220j5qhjke) == 48 && _Alignof(gm_tpm220j5qhjke) == 8 && offsetof(gm_tpm220j5qhjke, f0) == 0 && offsetof(gm_tpm220j5qhjke, f1) == 8, "layout of `Obligation`");
_Static_assert(sizeof(gm_tpouv9r7lrri2) == 32 && _Alignof(gm_tpouv9r7lrri2) == 8 && offsetof(gm_tpouv9r7lrri2, f0) == 0 && offsetof(gm_tpouv9r7lrri2, f1) == 24, "layout of `SpreadPart`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts5t31j0bugq1) == 40 && _Alignof(gm_ts5t31j0bugq1) == 8 && offsetof(gm_ts5t31j0bugq1, f0) == 0 && offsetof(gm_ts5t31j0bugq1, f1) == 8, "layout of `Packed`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; void * f4; } gm_t150deeftog49;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tck5nas0nf5v8) == 48 && _Alignof(gm_tck5nas0nf5v8) == 8 && offsetof(gm_tck5nas0nf5v8, f0) == 0 && offsetof(gm_tck5nas0nf5v8, f1) == 40, "layout of `?Packed`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tlbrdhah49ci5) == 40 && _Alignof(gm_tlbrdhah49ci5) == 8 && offsetof(gm_tlbrdhah49ci5, f0) == 0 && offsetof(gm_tlbrdhah49ci5, f1) == 32, "layout of `?EffectNamed`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t150deeftog49) == 96 && _Alignof(gm_t150deeftog49) == 8 && offsetof(gm_t150deeftog49, f0) == 0 && offsetof(gm_t150deeftog49, f1) == 40 && offsetof(gm_t150deeftog49, f2) == 56 && offsetof(gm_t150deeftog49, f3) == 72 && offsetof(gm_t150deeftog49, f4) == 88, "layout of `WriteExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_arg_value_gavegdu {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* lit */
    gm_trqfvmjtt4sij l4; /* pre */
    void * ret;
};
struct gm_fr_argument_order_jhg33u8 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* order */
    gm_t7te2tdpebbt4 l6; /* a */
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_assert__dllohqd {
    gm_frame h;
    gm_t7te2tdpebbt4 l5; /* a */
    gm_tc3oht4ksth9d l6; /* c */
    gm_tc3oht4ksth9d l8; /* x */
    gm_trqfvmjtt4sij l10; /* both */
    void * ret;
};
struct gm_fr_bind_args_dnih8ih {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* named_here */
    gm_t7te2tdpebbt4 l9; /* a */
    gm_trqfvmjtt4sij l11; /* positional */
    gm_t5v0c8ia6a5mo l15; /* p */
    gm_ts5t31j0bugq1 l18; /* packed */
    gm_trqfvmjtt4sij l22; /* out */
    gm_t4j35k4k4pvmb l28; /* b */
    gm_trqfvmjtt4sij l31; /* stmts */
    gm_trqfvmjtt4sij l32; /* written */
    gm_t7te2tdpebbt4 l35; /* a */
    gm_tc3oht4ksth9d l36; /* e */
    gm_tc3oht4ksth9d l40; /* e */
    gm_tc3oht4ksth9d l43; /* e */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_bind_spread_parts_lmknlmc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_block_of_2gjl2i1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_call_decl_sj6f05u {
    gm_frame h;
    void * ret;
};
struct gm_fr_call_fn_2prsqv9 {
    gm_frame h;
    gm_tqqhoivahirr0 l4; /* sig */
    gm_trqfvmjtt4sij l10; /* vals */
    gm_t7te2tdpebbt4 l13; /* a */
    void * ret;
};
struct gm_fr_call_l10ebge {
    gm_frame h;
    gm_tc3oht4ksth9d l6; /* a */
    gm_t5vbkdq81qfs7 l8; /* r */
    gm_t0ialelq3ql1k l9; /* v */
    gm_t930q5l38hdap l22; /* b */
    void * ret;
};
struct gm_fr_call_value_t2nv21i {
    gm_frame h;
    gm_tddtjfggdat0d l5; /* f */
    gm_tddtjfggdat0d l6; /* f */
    gm_tddtjfggdat0d l7; /* sig */
    gm_trqfvmjtt4sij l8; /* params */
    gm_trqfvmjtt4sij l14; /* stmts */
    void * ret;
};
struct gm_fr_coercion_steps_ibqvhta {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_construct_optional_cffo1ol {
    gm_frame h;
    gm_tc3oht4ksth9d l5; /* x */
    void * ret;
};
struct gm_fr_conversion_2qjq323 {
    gm_frame h;
    gm_tpautr4br7h3l l6; /* def */
    gm_tc3oht4ksth9d l10; /* x */
    gm_tnpj2jtv1r8pa l11; /* found */
    gm_t0mglksrkfa5e l14; /* f */
    gm_t0mglksrkfa5e l15; /* f */
    gm_trqfvmjtt4sij l17; /* one */
    gm_tc3oht4ksth9d l26; /* x */
    void * ret;
};
struct gm_fr_default_spec_9ntjq2s {
    gm_frame h;
    gm_t7g88i52rrtm5 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
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
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lmbdkhb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_moftl2f {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qm5tp1g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_expect_eq_iu4ojmi {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* a */
    gm_tc3oht4ksth9d l4; /* b */
    gm_trqfvmjtt4sij l15; /* one */
    gm_trqfvmjtt4sij l17; /* pieces */
    gm_trqfvmjtt4sij l18; /* specs */
    gm_trqfvmjtt4sij l19; /* vals */
    gm_trqfvmjtt4sij l23; /* stop_args */
    gm_trqfvmjtt4sij l25; /* fail_stmts */
    gm_trqfvmjtt4sij l27; /* cond */
    gm_trqfvmjtt4sij l29; /* stmts */
    void * ret;
};
struct gm_fr_extend_gig5o6g {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fill_defaults_3fq5ut6 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* out */
    gm_t6j6smj7qoctu ret;
};
struct gm_fr_format_10ctmcq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_458q1b3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8fudcr8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9ijbt7j {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b4oiuht {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bvitrsu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c8qq9n9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cqv4884 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e79cvtl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hqkpc5s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_i1v5tqv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ivju6qo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jaikcua {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kub2tep {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lc07prd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mq9dnpb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q8r21un {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qjdh7n5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qk20moi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t3u5vlm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hoist_prefix_r1mevpl {
    gm_frame h;
    void * ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_const_ref_arg_s0f37he {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_literal_tree_om58rum {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_plain_data_ej4mh2m {
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
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qtk22f3 {
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
struct gm_fr_list_v5d1o9b_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_t2b6tm8c0vgo1 l1; /* kind */
    void * ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_next_positional_t5fspc1 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_one_expr_q6kn0hn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_b16_8 ret;
};
struct gm_fr_param_default_h93ci1m {
    gm_frame h;
    void * ret;
};
struct gm_fr_param_index_78k4iut {
    gm_frame h;
    gm_t5v0c8ia6a5mo l5; /* p */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_print_bhebp9d {
    gm_frame h;
    gm_t7te2tdpebbt4 l6; /* a */
    gm_tc3oht4ksth9d l7; /* template */
    gm_trqfvmjtt4sij l10; /* one */
    void * ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_7koe9lk {
    gm_frame h;
    gm_tpm220j5qhjke l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d1r4h19 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tcgrjq9toscke l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_eb7kbhp_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_nbmosph {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o8u01sh {
    gm_frame h;
    gm_t5v0c8ia6a5mo l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r2k6rjf {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_recursive_without_ret_48l31rm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4bta8ne_2 {
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
struct gm_fr_regrow_a6uf79v {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_emnjbt3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j7fb9h1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_m244brh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_fsnl6fa {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_tir_literal_oh960hv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_trap_t21p26s {
    gm_frame h;
    gm_tc3oht4ksth9d l3; /* m */
    void * ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_r3r7qdh {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_variadic_arg_dfdnlo3 {
    gm_frame h;
    gm_tc3oht4ksth9d l2; /* e */
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_arg_value_gavegdu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_argument_order_jhg33u8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_array_as_slice_gvflfg2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_assert__dllohqd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_bind_args_dnih8ih(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_spread_parts_lmknlmc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_build_write_k6dcf8r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_builtin_type_64h714c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_call_decl_sj6f05u(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_call_fn_2prsqv9(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_call_l10ebge(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_call_value_t2nv21i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deprecated_j3dq26t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_construct_array_qkofm98(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, gm_tf5srh4mp4b3i a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_enum_array_d7cisuu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_optional_cffo1ol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_construct_param_array_btk60nl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_construct_struct_ve6tb88(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_construct_tuple_6ee5ejf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_variant_o6is81c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_conversion_2qjq323(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out);
__attribute__((aligned(4))) void * gm_f_default_value_lo723gi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_effect_named_b8afdbp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_moftl2f(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) gm_t3mf58gv9kf3n gm_f_equalize_ni1r5ra(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expect_bool_cdo5g1q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_expect_eq_iu4ojmi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_gig5o6g(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_fill_defaults_3fq5ut6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void gm_f_format_10ctmcq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_458q1b3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8fudcr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9ijbt7j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_b4oiuht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_bvitrsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_c8qq9n9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cqv4884(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_hqkpc5s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_i1v5tqv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ivju6qo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_jaikcua(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_kub2tep(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lc07prd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mq9dnpb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_q8r21un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qjdh7n5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qk20moi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_t3u5vlm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void * gm_f_generic_call_2tlofdi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_hoists_13k2a2s(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_const_ref_arg_s0f37he(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_literal_tree_om58rum(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0);
__attribute__((aligned(4))) void gm_f_iterator_arg_r0if28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_iterator_call_as_value_00i5mch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qtk22f3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_method_call_hebo20o(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_of_n0d5f6n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_2pnaa87(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_positional_t5fspc1(void * a0);
__attribute__((aligned(4))) void gm_f_note_fixed_by_ev84og7(void * a0, void * a1, gm_csguankko4lmhd a2, gm_cseahvg8v7p0vs a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_expr_q6kn0hn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_pack_rest_args_cknfrfl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void * gm_f_param_default_h93ci1m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, void * a3, gm_t88rcucg1b7uk a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_param_index_78k4iut(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_perform_named_h7gr9vi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_perform_op_1nih4r3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_place_iter_pre_qn5c5b5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint64_t a3, void * a4);
__attribute__((aligned(4))) void * gm_f_prefix_operand_3n5jchh(void * a0);
__attribute__((aligned(4))) void gm_f_prelude_effect_gilrmm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_print_bhebp9d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_push_down_hge098p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_nbmosph(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_effects_0eel5vu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_recursive_without_ret_48l31rm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_a6uf79v(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m244brh(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_source_location_nc1oola(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_spread_parts_e62q2l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_strip_wrappers_e5thpib(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_tir_literal_oh960hv(void * a0);
__attribute__((aligned(4))) void * gm_f_trap_t21p26s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_try_untyped_8j8up35(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_name_oqtujpn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_uninit_message_u2c3peu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_untyped_literal_pfu1ft4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_variadic_arg_dfdnlo3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern uint8_t gm_krbc9rueul4[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0uum3co6vapt[23];
extern const uint8_t gm_s1cbu3jc19u4t[36];
extern const uint8_t gm_s1ekjot5tdpje[67];
extern const uint8_t gm_s2hut6059od59[42];
extern const uint8_t gm_s2r7keqsravo0[6];
extern const uint8_t gm_s3231a310mran[10];
extern const uint8_t gm_s4g2k7ups3ui6[6];
extern const uint8_t gm_s75gqsar0656b[50];
extern const uint8_t gm_s8vts15kskc6n[11];
extern const uint8_t gm_s9659cpb4brmg[36];
extern const uint8_t gm_s98peh781o3et[51];
extern const uint8_t gm_s99ra887gkc8u[39];
extern const uint8_t gm_s9ur7sj9r0g63[7];
extern const uint8_t gm_sa9e77lq9q04p[16];
extern const uint8_t gm_sbs1lj6q8rfiu[44];
extern const uint8_t gm_scgahod5hue9b[56];
extern const uint8_t gm_sdm98dp52j8ep[16];
extern const uint8_t gm_sej6t944fna3a[61];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sgnvdmoebn4i7[16];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sig8ahbvm8eb8[10];
extern const uint8_t gm_sijukd7vsocnt[68];
extern const uint8_t gm_sinmpfqtbo9in[5];
extern const uint8_t gm_sj4n6nvsgq2jn[43];
extern const uint8_t gm_sk30fte1u6en9[33];
extern const uint8_t gm_sk8r2qbdun3l0[35];
extern const uint8_t gm_slj7i6lishtfg[47];
extern const uint8_t gm_smadkict0htlf[38];
extern const uint8_t gm_sn937bfucthvg[4];
extern const uint8_t gm_so4gnucc3fl4v[7];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sodq0gu838ini[139];
extern const uint8_t gm_sp6qfs9r7ep2n[75];
extern const uint8_t gm_spi8n2atkg603[17];
extern const uint8_t gm_sq0dqiq83b924[9];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcg82mujgottmf[];
extern const char gm_srch6h8rdicu443[];
extern const char gm_srcl43af23eher3[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srg30626j66d1[140];
extern const uint8_t gm_srgp1m0q2ut8m[12];
extern const uint8_t gm_srqehfttj6ngk[4];
extern const uint8_t gm_ss6g2koqn63bf[5];
extern const uint8_t gm_ss7dlauv50bt8[81];
extern const uint8_t gm_ssi8bk0pvfr56[22];
extern const uint8_t gm_ssqmni9f9s44u[34];
extern const uint8_t gm_sue9kqmk98bh9[34];
extern const uint8_t gm_svg70al9ibq8e[56];
extern const gm_tmpl gm_tpl2btkd0vtln0o;
extern const gm_tmpl gm_tpl2fb8qlpeqlet;
extern const gm_tmpl gm_tpl335dut97l9dl;
extern const gm_tmpl gm_tpl34tavcs9635o;
extern const gm_tmpl gm_tpl4ht4dp62es1n;
extern const gm_tmpl gm_tpl5kgb1tfsut02;
extern const gm_tmpl gm_tpl84r77qupuujj;
extern const gm_tmpl gm_tpl87nh31l9js4u;
extern const gm_tmpl gm_tpl9vu8tqcv1d9s;
extern const gm_tmpl gm_tplbn5t5stpvru2;
extern const gm_tmpl gm_tplc2ir650ohv3d;
extern const gm_tmpl gm_tplcttkqubjcaqf;
extern const gm_tmpl gm_tplff72i6ku4gld;
extern const gm_tmpl gm_tpll8q455cp9mhi;
extern const gm_tmpl gm_tplm2m182mt5ljj;
extern const gm_tmpl gm_tplod6iso1fh3co;
extern const gm_tmpl gm_tpls4d1lh9jd2u8;
extern const gm_tmpl gm_tplu2tisf3oaee8;
extern const gm_tmpl gm_tplvcuursopmobk;
extern const gm_tmpl gm_tplvklr3jhlo8m3;
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
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_equ1rt9sffme(a, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_block_of_2gjl2i1 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* stmts */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_l1;
    gm_tl874t2atnlh3 t6 = gm_l2;
    void * t7 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t4, t5, ((void *)0), t6);
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
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_coercion_steps_ibqvhta F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_f_steps_for_768gb3b((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t2, t3, t4, ((uint8_t)1));
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
inline __attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_default_spec_9ntjq2s F;
    GM_STACK_CHECK_SHALLOW(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t9805qtonbsag t2;
    memset(&t2, 0, sizeof t2);
    gm_t9qkh0rsooc8d t3;
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
    gm_t7g88i52rrtm5 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 6, 0);
    GM_ST(uint8_t, (uint8_t *)&t5 + 21, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 22, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)32ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t5 + 1), t3);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 2), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t5 + 3), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t5 + 4), ((uint8_t)0));
    GM_ST(uint32_t, ((uint8_t *)&t5 + 8), ((uint32_t)0ULL));
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t5 + 12), t2);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t5 + 20), t4);
    memmove(gm_out, ((uint8_t *)&t5), 24);
    goto gm_fdone;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lmbdkhb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lmbdkhb_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_moftl2f(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_moftl2f F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qm5tp1g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qm5tp1g F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_9huj44a F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* msg */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    gm_b0_1 t6 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_empty_slice_g3tddrb_2();
    gm_tl874t2atnlh3 t6 = gm_l1;
    gm_t2b6tm8c0vgo1 t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t5);
    void * t8 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t4, &t7, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_format_10ctmcq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_10ctmcq F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sej6t944fna3a));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(60));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplff72i6ku4gld), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_458q1b3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_458q1b3 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp6qfs9r7ep2n));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(74));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4ht4dp62es1n), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8fudcr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8fudcr8 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssi8bk0pvfr56));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcttkqubjcaqf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9ijbt7j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_9ijbt7j F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sodq0gu838ini));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(138));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl335dut97l9dl), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_b4oiuht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_b4oiuht F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s9659cpb4brmg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(35));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl34tavcs9635o), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_bvitrsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bvitrsu F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2hut6059od59));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvcuursopmobk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_c8qq9n9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_c8qq9n9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss7dlauv50bt8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(80));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplm2m182mt5ljj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cqv4884(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cqv4884 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svg70al9ibq8e));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl84r77qupuujj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_e79cvtl F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sig8ahbvm8eb8));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpll8q455cp9mhi), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_hqkpc5s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hqkpc5s F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s99ra887gkc8u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl2btkd0vtln0o), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_i1v5tqv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_i1v5tqv F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s75gqsar0656b));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplu2tisf3oaee8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ivju6qo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ivju6qo F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sk8r2qbdun3l0));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(34));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplod6iso1fh3co), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jaikcua(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jaikcua F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk30fte1u6en9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplc2ir650ohv3d), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_kub2tep(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kub2tep F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srg30626j66d1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(139));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvklr3jhlo8m3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lc07prd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lc07prd F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0uum3co6vapt));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(22));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9vu8tqcv1d9s), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mq9dnpb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_mq9dnpb F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssqmni9f9s44u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl87nh31l9js4u), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q8r21un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_q8r21un F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdm98dp52j8ep));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpls4d1lh9jd2u8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qjdh7n5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qjdh7n5 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sue9kqmk98bh9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(33));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2fb8qlpeqlet), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qk20moi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qk20moi F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1cbu3jc19u4t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbn5t5stpvru2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t3u5vlm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_t3u5vlm F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slj7i6lishtfg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5kgb1tfsut02), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_infer_3o82a0b F;
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
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 252, 57);
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
    uint8_t t8 = (uint8_t)(t7 & ((uint8_t)2ULL));
    uint8_t t9 = (!(t8 == ((uint8_t)0ULL)));
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_hoist_prefix_r1mevpl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_hoist_prefix_r1mevpl F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint8_t t3 = gm_f_hoists_13k2a2s(t2);
    uint8_t t4 = (uint8_t)!t3;
    if (t4) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t5 = gm_l0;
    void * t6 = gm_l0;
    void * t7 = gm_f_prefix_operand_3n5jchh(t6);
    void * t8 = gm_f_push_down_hge098p((void *)NULL, cx, NULL, t5, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    gm_f_kind_jcnt4hm((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    uint8_t t6 = (uint8_t)((t5 == ((uint8_t)26ULL)));
    gm_t53at80pfdus3 t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)26ULL));
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_plain_data_ej4mh2m F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_error_8k27gd1(t4, GM_CONV(gm_csguankko4lmhd, t5));
    if (t6) {
        gm_l2 = ((uint8_t)1);
    } else {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        gm_t88rcucg1b7uk t9 = gm_l1;
        uint8_t t10 = gm_f_is_plain_n60iihm(t8, GM_CONV(gm_csguankko4lmhd, t9));
        gm_l2 = t10;
    }
    uint8_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8q3kspo();
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
inline __attribute__((aligned(4))) void gm_f_list_qtk22f3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qtk22f3 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_moftl2f();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d1o9b_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d1o9b_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lmbdkhb_2();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_never_ty_b949mgo F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)5ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_one_expr_q6kn0hn(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_one_expr_q6kn0hn F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l2;
    gm_trqfvmjtt4sij t2;
    gm_f_list_vp1djv1_2((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = (void *)((uint8_t *)&F.l1);
    void * t4 = gm_l0;
    gm_tftb1odc739t5 t5 = gm_f_push_d943vgp_2(t3, &(*(gm_tcgrjq9toscke *)((uint8_t *)t4)));
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcg82mujgottmf, 140, 5);
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
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
inline __attribute__((aligned(4))) void * gm_f_param_default_h93ci1m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, void * a3, gm_t88rcucg1b7uk a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_param_default_h93ci1m F;
    void * gm_l0; /* cx */
    void * gm_l1; /* p */
    uint64_t gm_l2; /* module */
    void * gm_l3; /* d */
    gm_t88rcucg1b7uk gm_l4; /* ty */
    gm_tl874t2atnlh3 gm_l5; /* span */
    GM_STACK_CHECK(&F, 138u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    void * t2 = gm_l1;
    uint8_t t3 = gm_f_is_site_8sr7vam(t2);
    uint8_t t4 = (uint8_t)!t3;
    if (t4) {
        void * t5 = gm_l0;
        uint64_t t6 = gm_l2;
        void * t7 = gm_l3;
        gm_t88rcucg1b7uk t8 = gm_l4;
        void * t9 = gm_f_default_value_lo723gi((void *)NULL, cx, NULL, t5, t6, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t9;
        goto gm_fret;
    }
    void * t10 = gm_l0;
    gm_tl874t2atnlh3 t11 = gm_l5;
    void * t12 = gm_f_source_location_nc1oola((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t12;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_7koe9lk(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_7koe9lk F;
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
        gm_b16_8 t9 = gm_f_empty_slice_qm5tp1g();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_emnjbt3(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 48), ((uint8_t *)&F.l1), 48);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d6c76ud_2 F;
    void * gm_l0; /* l */
    gm_trcmmd12cefb1 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_vj3ckdj_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h08har5_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_eb7kbhp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_eb7kbhp_2 F;
    void * gm_l0; /* l */
    gm_tlr9dp305htm7 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_lmbdkhb_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4bta8ne_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_nbmosph(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_nbmosph F;
    void * gm_l0; /* l */
    gm_t7g88i52rrtm5 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_moftl2f();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_a6uf79v(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r2k6rjf F;
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
        gm_b16_8 t9 = gm_f_empty_slice_8q3kspo();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_j7fb9h1(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_recursive_without_ret_48l31rm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_recursive_without_ret_48l31rm F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* name */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l2;
    gm_b16_8 t4 = gm_l1;
    gm_tnpifv20irnph t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
    gm_f_format_c8qq9n9((void *)&gm_l3, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcl43af23eher3, 280, 20);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_b0_1 t9 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t2, t3, t8);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4bta8ne_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4bta8ne_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_a6uf79v(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_a6uf79v F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_emnjbt3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_emnjbt3 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h08har5_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j7fb9h1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_fsnl6fa F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_temp_ilbvj8q F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shv1k5m05egbe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_tl874t2atnlh3 t5 = gm_l2;
    uint32_t t6 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t2, t3, t4, ((uint8_t)1), t5);
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
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_r3r7qdh F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    gm_b0_1 t6 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t3, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    gm_tl874t2atnlh3 t9 = gm_l1;
    void * t10 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t8, t9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 177u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)gm_c3t36dlh4g5));
    gm_b16_8 t5 = gm_l2;
    gm_t6j6smj7qoctu t6;
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    gm_f_format_e79cvtl((void *)&gm_l3, (void *)NULL, cx, NULL, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srclv0dcbbfhves, 643, 19);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_b0_1 t10 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t9);
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
__attribute__((aligned(4))) void * gm_f_arg_value_gavegdu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_arg_value_gavegdu F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l5; /* v */
    void * gm_l6; /* v */
    GM_STACK_CHECK(&F, 791u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t0mfir12leg2g gm_l7;
    {
        {
            void * t4 = gm_l0;
            void * t5 = gm_l1;
            gm_t88rcucg1b7uk t6 = gm_l2;
            gm_f_iterator_arg_r0if28f((void *)&gm_l7, (void *)NULL, cx, NULL, t4, t5, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l7))[88] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l7), 88);
            void * t8 = gm_l0;
            memmove(((uint8_t *)&F.l4), (((uint8_t *)t8) + 1160), 40);
            void * t9 = gm_l0;
            gm_trqfvmjtt4sij t10;
            gm_f_list_ir0h3tc_2((void *)&t10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t9) + 1160), t10);
            void * t11 = gm_l0;
            void * t12 = (void *)((uint8_t *)&F.l3);
            gm_t88rcucg1b7uk t13 = gm_l2;
            void * t14 = gm_f_arg_value_gavegdu((void *)NULL, cx, NULL, t11, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t14;
            void * t15 = gm_l0;
            memmove((((uint8_t *)t15) + 1160), ((uint8_t *)&F.l4), 40);
            void * t16 = gm_l5;
            F.ret = t16;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t17 = gm_l1;
    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
    uint8_t t19 = (uint8_t)((t18 == ((uint8_t)5ULL)));
    gm_t2gtbh39ptekd t20 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t17) + 8));
    gm_t2gtbh39ptekd t21;
    memset(&t21, 0, sizeof t21);
    GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)5ULL));
    if (t19) {
        void * t22 = gm_l0;
        gm_t88rcucg1b7uk t23 = gm_l2;
        uint8_t t24 = gm_f_is_plain_data_ej4mh2m((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t25 = (uint8_t)!t24;
        if (t25) {
            void * t26 = gm_l0;
            void * t27 = gm_l1;
            gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t27) + 72));
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, ((uint8_t *)t29));
            gm_t88rcucg1b7uk t31 = gm_l2;
            gm_b16_8 t32 = gm_f_uninit_message_u2c3peu((void *)NULL, cx, NULL, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t33 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t26, t28, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_t88rcucg1b7uk t34 = gm_l2;
        void * t35 = gm_l1;
        gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t35) + 72));
        gm_t2b6tm8c0vgo1 t37;
        memset(&t37, 0, sizeof t37);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)8ULL));
        void * t38 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t34, &t37, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t38;
        goto gm_fret;
    }
    void * t39 = gm_l0;
    void * t40 = gm_l0;
    void * t41 = GM_LD(void *, ((uint8_t *)t40));
    void * t42 = gm_l1;
    gm_t88rcucg1b7uk t43 = gm_l2;
    uint8_t t44 = gm_f_is_const_ref_arg_s0f37he((void *)NULL, cx, NULL, t41, t42, t43);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (((uint8_t *)t39) + 472), t44);
    void * t45 = gm_l0;
    void * t46 = gm_l1;
    gm_t88rcucg1b7uk t47 = gm_l2;
    gm_tkn42u7na3t81 t48;
    GM_ST(uint8_t, (uint8_t *)&t48 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t48 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t48), t47);
    ((uint8_t *)&t48)[4] = 1;
    void * t49 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t45, t46, t48);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t49;
    void * t50 = gm_l0;
    GM_ST(uint8_t, (((uint8_t *)t50) + 472), ((uint8_t)0));
    void * t51 = gm_l0;
    void * t52 = gm_l1;
    void * t53 = gm_l6;
    gm_t88rcucg1b7uk t54 = gm_l2;
    uint8_t t55 = gm_f_iterator_call_as_value_00i5mch((void *)NULL, cx, NULL, t51, t52, t53, t54);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t55) {
        void * t56 = gm_l0;
        void * t57 = GM_LD(void *, ((uint8_t *)t56));
        void * t58 = gm_l1;
        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t58) + 72));
        void * t60 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t57, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t60;
        goto gm_fret;
    }
    void * t61 = gm_l0;
    void * t62 = gm_l6;
    gm_t88rcucg1b7uk t63 = gm_l2;
    void * t64 = gm_f_array_as_slice_gvflfg2((void *)NULL, cx, NULL, t61, t62, t63);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t64;
    void * t65 = gm_l0;
    void * t66 = gm_l6;
    gm_t88rcucg1b7uk t67 = gm_l2;
    void * t68 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t65, t66, t67);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t68;
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
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_argument_order_jhg33u8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_argument_order_jhg33u8 F;
    gm_b16_8 gm_l0; /* params */
    gm_b16_8 gm_l1; /* args */
    uint8_t gm_l2; /* has_spread */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t08olq97rscn3 gm_l7; /* n */
    uint64_t gm_l8; /* i */
    uint8_t gm_l9; /* spreading */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* k */
    GM_STACK_CHECK(&F, 495u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l13;
    gm_tf5srh4mp4b3i gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_l2 = ((uint8_t)0);
    gm_trqfvmjtt4sij t18;
    gm_f_list_5dhgg4i((void *)&t18, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t18;
    gm_b16_8 t19 = gm_l1;
    gm_l4 = t19;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t20 = gm_l5;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t25 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t23) + (uintptr_t)t25 * 144), 144);
                        {
                            {
                                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
                                if (!t27) {
                                    goto gm_lb7;
                                }
                                void * t28 = (void *)((uint8_t *)&F.l3);
                                uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                gm_tftb1odc739t5 t30 = gm_f_push_ucgmlo2(t28, t29);
                                gm_l13 = t30;
                                uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srch6h8rdicu443, 289, 31);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                uint8_t t33 = (uint8_t)(t32 == ((uint8_t)1ULL));
                                if (!t33) {
                                    goto gm_lb8;
                                }
                                memmove(((uint8_t *)&gm_l7), (((uint8_t *)&F.l6) + 8), 32);
                                {
                                    {
                                        gm_b16_8 t34 = gm_l0;
                                        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                                        gm_tf5srh4mp4b3i t36 = gm_f_param_index_78k4iut(t34, t35);
                                        gm_l14 = t36;
                                        uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l14))[8] != 0);
                                        if (!t37) {
                                            goto gm_lb10;
                                        }
                                        uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                                        gm_l8 = t38;
                                        void * t39 = (void *)((uint8_t *)&F.l3);
                                        uint64_t t40 = gm_l8;
                                        gm_tftb1odc739t5 t41 = gm_f_push_ucgmlo2(t39, t40);
                                        gm_l15 = t41;
                                        uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                        if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srch6h8rdicu443, 291, 55);
                                        goto gm_lb9;
                                    }
gm_lb10:;
                                }
gm_lb9:;
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                uint8_t t44 = (uint8_t)(t43 == ((uint8_t)2ULL));
                                if (!t44) {
                                    goto gm_lb11;
                                }
                                gm_l2 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 288, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 287, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t45 = gm_l5;
            uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
            uint64_t t48 = r46;
            gm_l5 = t48;
        }
    }
gm_lb1:;
    uint8_t t49 = gm_l2;
    gm_l9 = t49;
    gm_l11 = ((uint64_t)1ULL);
    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    uint64_t t51 = (t50 > ((uint64_t)1ULL) ? t50 : ((uint64_t)1ULL));
    gm_l10 = t51;
    {
        for (;;) {
            uint64_t t52 = gm_l11;
            uint64_t t53 = gm_l10;
            uint8_t t54 = (t52 < t53);
            if (!t54) {
                goto gm_lb12;
            }
            {
                {
                    {
                        uint64_t t55 = gm_l11;
                        gm_l12 = t55;
                        void * t56 = GM_LD(void *, ((uint8_t *)&F.l3));
                        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                        uint64_t t58 = gm_l12;
                        uint64_t r59; bool o60 = __builtin_sub_overflow(t58, ((uint64_t)1ULL), &r59);
                        if (o60) gm_trap("integer overflow in `-`", gm_srch6h8rdicu443, 298, 24);
                        uint64_t t61 = r59;
                        if ((uintptr_t)t61 >= (uintptr_t)(t57)) gm_trap_index((uintptr_t)t61, t57, gm_srch6h8rdicu443, 298, 12);
                        uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)t56) + (uintptr_t)t61 * 8));
                        void * t63 = GM_LD(void *, ((uint8_t *)&F.l3));
                        uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                        uint64_t t65 = gm_l12;
                        if ((uintptr_t)t65 >= (uintptr_t)(t64)) gm_trap_index((uintptr_t)t65, t64, gm_srch6h8rdicu443, 298, 33);
                        uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)t63) + (uintptr_t)t65 * 8));
                        uint8_t t67 = (t62 > t66);
                        if (t67) {
                            gm_l9 = ((uint8_t)1);
                        }
                        goto gm_lb15;
                    }
gm_lb16:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 297, 5);
                }
gm_lb15:;
            }
gm_lb14:;
            uint64_t t68 = gm_l11;
            uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
            uint64_t t71 = r69;
            gm_l11 = t71;
        }
    }
gm_lb12:;
    uint8_t t72 = gm_l2;
    uint8_t t73 = gm_l9;
    gm_tqs5p4n2tq19v t74;
    GM_ST(uint8_t, ((uint8_t *)&t74 + 0), t72);
    GM_ST(uint8_t, ((uint8_t *)&t74 + 1), t73);
    F.ret = t74;
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
__attribute__((aligned(4))) void * gm_f_assert__dllohqd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_assert__dllohqd F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    void * gm_l7; /* cond */
    void * gm_l9; /* m */
    GM_STACK_CHECK(&F, 1063u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    void * gm_l14;
    void * gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_b16_8 t14 = gm_l1;
    gm_l3 = t14;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t20 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t18) + (uintptr_t)t20 * 144), 144);
                        {
                            {
                                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t22 = (uint8_t)(t21 == ((uint8_t)0ULL));
                                if (!t22) {
                                    goto gm_lb7;
                                }
                                gm_l13 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l13 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 527, 12);
                        }
gm_lb6:;
                        uint8_t t23 = gm_l13;
                        if (t23) {
                            gm_l12 = ((uint8_t)1);
                        } else {
                            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                            uint8_t t25 = (t24 > ((uint64_t)2ULL));
                            gm_l12 = t25;
                        }
                        uint8_t t26 = gm_l12;
                        if (t26) {
                            gm_l11 = ((uint8_t)1);
                        } else {
                            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                            uint8_t t28 = ((t27 == ((uint64_t)0ULL)));
                            gm_l11 = t28;
                        }
                        uint8_t t29 = gm_l11;
                        if (t29) {
                            void * t30 = gm_l0;
                            gm_tl874t2atnlh3 t31 = gm_l2;
                            gm_b16_8 t32;
                            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s98peh781o3et));
                            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(50));
                            gm_b0_1 t33 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t30, t31, t32);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t34 = gm_l0;
                            void * t35 = GM_LD(void *, ((uint8_t *)t34));
                            gm_tl874t2atnlh3 t36 = gm_l2;
                            void * t37 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t35, t36);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t37;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 526, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t38 = gm_l4;
            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
            uint64_t t41 = r39;
            gm_l4 = t41;
        }
    }
gm_lb1:;
    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t43 = ((t42 == ((uint64_t)0ULL)));
    if (t43) {
        void * t44 = gm_l0;
        gm_tl874t2atnlh3 t45 = gm_l2;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s98peh781o3et));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(50));
        gm_b0_1 t47 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t44, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t48 = gm_l0;
        void * t49 = GM_LD(void *, ((uint8_t *)t48));
        gm_tl874t2atnlh3 t50 = gm_l2;
        void * t51 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t51;
        goto gm_fret;
    }
    {
        {
            void * t52 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t53)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t53, gm_srch6h8rdicu443, 536, 23);
            uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t52) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)0ULL));
            if (!t55) {
                goto gm_lb10;
            }
            memmove(((uint8_t *)&F.l6), ((((uint8_t *)t52) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
            void * t56 = gm_l0;
            void * t57 = (void *)((uint8_t *)&F.l6);
            void * t58 = gm_l0;
            void * t59 = GM_LD(void *, ((uint8_t *)t58));
            void * t60 = (void *)(((uint8_t *)t59) + 8);
            gm_t88rcucg1b7uk t61 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t62;
            GM_ST(uint8_t, (uint8_t *)&t62 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t62 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t62), t61);
            ((uint8_t *)&t62)[4] = 1;
            void * t63 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t56, t57, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t63;
            void * t64 = gm_l0;
            void * t65 = gm_l7;
            void * t66 = gm_f_expect_bool_cdo5g1q((void *)NULL, cx, NULL, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t66;
            uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t68 = ((t67 == ((uint64_t)2ULL)));
            if (t68) {
                {
                    {
                        void * t69 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t70)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t70, gm_srch6h8rdicu443, 540, 27);
                        uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t69) + (uintptr_t)((uint64_t)1ULL) * 144) + 0));
                        uint8_t t72 = (uint8_t)(t71 == ((uint8_t)0ULL));
                        if (!t72) {
                            goto gm_lb12;
                        }
                        memmove(((uint8_t *)&F.l8), ((((uint8_t *)t69) + (uintptr_t)((uint64_t)1ULL) * 144) + 8), 88);
                        void * t73 = gm_l0;
                        void * t74 = gm_l0;
                        void * t75 = (void *)((uint8_t *)&F.l8);
                        void * t76 = gm_l0;
                        void * t77 = GM_LD(void *, ((uint8_t *)t76));
                        void * t78 = (void *)(((uint8_t *)t77) + 8);
                        gm_t88rcucg1b7uk t79 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t78);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tkn42u7na3t81 t80;
                        GM_ST(uint8_t, (uint8_t *)&t80 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t80 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t80), t79);
                        ((uint8_t *)&t80)[4] = 1;
                        void * t81 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t74, t75, t80);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t82 = gm_l0;
                        void * t83 = GM_LD(void *, ((uint8_t *)t82));
                        void * t84 = (void *)(((uint8_t *)t83) + 8);
                        gm_t88rcucg1b7uk t85 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t84);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t86 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t73, t81, t85);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l15 = t86;
                        goto gm_lb11;
                    }
gm_lb12:;
                    void * t87 = gm_l0;
                    void * t88 = GM_LD(void *, ((uint8_t *)t87));
                    gm_tl874t2atnlh3 t89 = gm_l2;
                    void * t90 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t88, t89);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t90;
                    goto gm_fret;
                    gm_l15 = ((void *)0);
                }
gm_lb11:;
                void * t91 = gm_l15;
                gm_l14 = t91;
            } else {
                void * t92 = gm_l0;
                void * t93 = GM_LD(void *, ((uint8_t *)t92));
                void * t94 = (void *)(((uint8_t *)t93) + 8);
                gm_t88rcucg1b7uk t95 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t94);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t96;
                GM_ST(void *, (uint8_t *)&t96, (void *)(gm_spi8n2atkg603));
                GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(16));
                gm_tl874t2atnlh3 t97 = gm_l2;
                gm_t2b6tm8c0vgo1 t98;
                memset(&t98, 0, sizeof t98);
                GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)5ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t98 + 8), t96);
                void * t99 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t95, &t98, t97);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t99;
            }
            void * t100 = gm_l14;
            gm_l9 = t100;
            gm_trqfvmjtt4sij t101;
            gm_f_list_vp1djv1_2((void *)&t101, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l10 = t101;
            void * t102 = (void *)((uint8_t *)&F.l10);
            void * t103 = gm_l7;
            gm_tftb1odc739t5 t104 = gm_f_push_d943vgp_2(t102, &(*(gm_tcgrjq9toscke *)((uint8_t *)t103)));
            gm_l16 = t104;
            uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
            if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srch6h8rdicu443, 546, 5);
            void * t106 = (void *)((uint8_t *)&F.l10);
            void * t107 = gm_l9;
            gm_tftb1odc739t5 t108 = gm_f_push_d943vgp_2(t106, &(*(gm_tcgrjq9toscke *)((uint8_t *)t107)));
            gm_l17 = t108;
            uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srch6h8rdicu443, 547, 5);
            void * t110 = gm_l0;
            void * t111 = GM_LD(void *, ((uint8_t *)t110));
            void * t112 = (void *)(((uint8_t *)t111) + 8);
            gm_t88rcucg1b7uk t113 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
            gm_tvdg94bk7aa22 t115;
            GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t115 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)61ULL));
            gm_tt8o5kebou62r t116;
            GM_ST(uint32_t, (uint8_t *)&t116 + 12, 0);
            GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t116 + 0), t115);
            GM_ST(gm_b16_8, ((uint8_t *)&t116 + 16), t114);
            gm_tl874t2atnlh3 t117 = gm_l2;
            gm_t2b6tm8c0vgo1 t118;
            GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t118 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t118 + 48, 0);
            GM_ST(uint64_t, (uint8_t *)&t118 + 56, 0);
            GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)33ULL));
            GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t118 + 8), t116);
            void * t119 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t113, &t118, t117);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t119;
            goto gm_fret;
        }
gm_lb10:;
        void * t120 = gm_l0;
        void * t121 = GM_LD(void *, ((uint8_t *)t120));
        gm_tl874t2atnlh3 t122 = gm_l2;
        void * t123 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t121, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t123;
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
__attribute__((aligned(4))) void gm_f_bind_args_dnih8ih(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4, gm_b16_8 a5) {
    struct gm_fr_bind_args_dnih8ih F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* params */
    uint64_t gm_l2; /* callee_module */
    gm_b16_8 gm_l3; /* args0 */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* what */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t08olq97rscn3 gm_l10; /* n */
    uint64_t gm_l12; /* max */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint8_t gm_l16; /* ok */
    gm_b16_8 gm_l17; /* args */
    gm_b16_8 gm_l19; /* a */
    gm_tl874t2atnlh3 gm_l20; /* sp */
    gm_b16_8 gm_l21; /* msg */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_tqs5p4n2tq19v gm_l25; /* order */
    uint8_t gm_l26; /* has_spread */
    uint8_t gm_l27; /* spreading */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l37; /* i */
    void * gm_l38; /* v */
    gm_t08olq97rscn3 gm_l39; /* n */
    uint64_t gm_l41; /* i */
    void * gm_l42; /* v */
    gm_b16_8 gm_l44; /* parts */
    GM_STACK_CHECK(&F, 3159u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tftb1odc739t5 gm_l45;
    uint8_t gm_l46;
    gm_tftb1odc739t5 gm_l47;
    gm_tck5nas0nf5v8 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tf5srh4mp4b3i gm_l52;
    uint64_t gm_l53;
    gm_tabb710d9mq2e gm_l54;
    gm_tf5srh4mp4b3i gm_l55;
    uint64_t gm_l56;
    gm_tabb710d9mq2e gm_l57;
    uint8_t gm_l58;
    uint8_t gm_l59;
    uint8_t gm_l60;
    gm_tabb710d9mq2e gm_l61;
    gm_th2lqbo77pvsc gm_l62;
    gm_b16_8 gm_l63;
    {
        gm_trqfvmjtt4sij t41;
        gm_f_list_epvducg((void *)&t41, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t41;
        gm_b16_8 t42 = gm_l3;
        gm_l7 = t42;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t43 = gm_l8;
                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t45 = (t43 < t44);
                if (!t45) {
                    goto gm_lb1;
                }
                {
                    void * t46 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t48 = gm_l8;
                    {
                        {
                            memmove(((uint8_t *)&F.l9), (((uint8_t *)t46) + (uintptr_t)t48 * 144), 144);
                            {
                                {
                                    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                                    uint8_t t50 = (uint8_t)(t49 == ((uint8_t)1ULL));
                                    if (!t50) {
                                        goto gm_lb7;
                                    }
                                    memmove(((uint8_t *)&gm_l10), (((uint8_t *)&F.l9) + 8), 32);
                                    void * t51 = (void *)((uint8_t *)&F.l6);
                                    gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
                                    gm_tftb1odc739t5 t53 = gm_f_push_3ef746b(t51, t52);
                                    gm_l45 = t53;
                                    uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                    if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srch6h8rdicu443, 207, 37);
                                    goto gm_lb6;
                                }
gm_lb7:;
                            }
gm_lb6:;
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 206, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t55 = gm_l8;
                uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                uint64_t t58 = r56;
                gm_l8 = t58;
            }
        }
gm_lb1:;
        gm_trqfvmjtt4sij t59;
        gm_f_list_s83jn2q((void *)&t59, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t59;
        gm_l12 = ((uint64_t)0ULL);
        gm_b16_8 t60 = gm_l1;
        gm_l13 = t60;
        gm_l14 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t61 = gm_l14;
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                uint8_t t63 = (t61 < t62);
                if (!t63) {
                    goto gm_lb8;
                }
                {
                    void * t64 = GM_LD(void *, ((uint8_t *)&gm_l13));
                    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                    uint64_t t66 = gm_l14;
                    {
                        {
                            memmove(((uint8_t *)&F.l15), (((uint8_t *)t64) + (uintptr_t)t66 * 56), 56);
                            void * t67 = (void *)((uint8_t *)&F.l15);
                            uint8_t t68 = gm_f_is_site_8sr7vam(t67);
                            uint8_t t69 = (uint8_t)!t68;
                            if (t69) {
                                gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
                                gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                                uint8_t t72 = 0;
                                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t71) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t71))) + i * 16, ((uint8_t *)&t70))) { t72 = 1; break; }
                                uint8_t t73 = (uint8_t)(!t72);
                                gm_l46 = t73;
                            } else {
                                gm_l46 = ((uint8_t)0);
                            }
                            uint8_t t74 = gm_l46;
                            gm_l16 = t74;
                            void * t75 = (void *)((uint8_t *)&F.l11);
                            uint8_t t76 = gm_l16;
                            gm_tftb1odc739t5 t77 = gm_f_push_vd1290p(t75, t76);
                            gm_l47 = t77;
                            uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                            if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_srch6h8rdicu443, 213, 9);
                            uint8_t t79 = gm_l16;
                            if (t79) {
                                uint64_t t80 = gm_l12;
                                uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                                if (o82) gm_trap("integer overflow in `+`", gm_srch6h8rdicu443, 214, 17);
                                uint64_t t83 = r81;
                                gm_l12 = t83;
                            }
                            goto gm_lb11;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 211, 5);
                    }
gm_lb11:;
                }
gm_lb10:;
                uint64_t t84 = gm_l14;
                uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                uint64_t t87 = r85;
                gm_l14 = t87;
            }
        }
gm_lb8:;
    }
    {
        gm_b16_8 t88 = gm_l3;
        gm_l17 = t88;
        {
            {
                gm_b16_8 t89 = gm_l1;
                gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                gm_b16_8 t92 = gm_l3;
                gm_tl874t2atnlh3 t93 = gm_l4;
                gm_f_pack_rest_args_cknfrfl((void *)&gm_l48, (void *)NULL, cx, NULL, t89, t91, t92, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t94 = (uint8_t)((((uint8_t *)&gm_l48))[40] != 0);
                if (!t94) {
                    goto gm_lb14;
                }
                memmove(((uint8_t *)&F.l18), ((uint8_t *)&gm_l48), 40);
                {
                    {
                        uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
                        uint8_t t96 = (uint8_t)(t95 == ((uint8_t)0ULL));
                        if (!t96) {
                            goto gm_lb16;
                        }
                        gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 8));
                        gm_l19 = t97;
                        gm_b16_8 t98 = gm_l19;
                        gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                        gm_l17 = t99;
                        goto gm_lb15;
                    }
gm_lb16:;
                    {
                        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 0));
                        uint8_t t101 = (uint8_t)(t100 == ((uint8_t)1ULL));
                        if (!t101) {
                            goto gm_lb17;
                        }
                        gm_tl874t2atnlh3 t102 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 8));
                        gm_l20 = t102;
                        gm_b16_8 t103 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l18) + 8) + 16));
                        gm_l21 = t103;
                        void * t104 = gm_l0;
                        gm_tl874t2atnlh3 t105 = gm_l20;
                        gm_b16_8 t106 = gm_l21;
                        gm_b0_1 t107 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t104, t105, t106);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_trqfvmjtt4sij t108;
                        gm_f_list_vp1djv1_2((void *)&t108, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l22 = t108;
                        gm_b16_8 t109 = gm_l1;
                        gm_l23 = t109;
                        gm_l24 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t110 = gm_l24;
                                uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                uint8_t t112 = (t110 < t111);
                                if (!t112) {
                                    goto gm_lb18;
                                }
                                {
                                    void * t113 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                    uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                    uint64_t t115 = gm_l24;
                                    {
                                        {
                                            void * t116 = (void *)((uint8_t *)&F.l22);
                                            void * t117 = gm_l0;
                                            void * t118 = GM_LD(void *, ((uint8_t *)t117));
                                            gm_tl874t2atnlh3 t119 = gm_l4;
                                            void * t120 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t118, t119);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_tftb1odc739t5 t121 = gm_f_push_d943vgp_2(t116, &(*(gm_tcgrjq9toscke *)((uint8_t *)t120)));
                                            gm_l49 = t121;
                                            uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                            if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srch6h8rdicu443, 223, 35);
                                            goto gm_lb21;
                                        }
gm_lb22:;
                                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 223, 17);
                                    }
gm_lb21:;
                                }
gm_lb20:;
                                uint64_t t123 = gm_l24;
                                uint64_t r124; bool o125 = __builtin_add_overflow(t123, ((uint64_t)1ULL), &r124);
                                uint64_t t126 = r124;
                                gm_l24 = t126;
                            }
                        }
gm_lb18:;
                        gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
                        gm_b16_8 t128 = gm_f_empty_slice_vj3ckdj_2();
                        gm_t6j6smj7qoctu t129;
                        GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t127);
                        GM_ST(gm_b16_8, ((uint8_t *)&t129 + 16), t128);
                        gm_ttu09lij36f8c t130;
                        GM_ST(uint8_t, (uint8_t *)&t130 + 33, 0);
                        GM_ST(uint16_t, (uint8_t *)&t130 + 34, 0);
                        GM_ST(uint32_t, (uint8_t *)&t130 + 36, 0);
                        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t130), t129);
                        ((uint8_t *)&t130)[32] = 1;
                        memmove(gm_out, ((uint8_t *)&t130), 40);
                        goto gm_fdone;
                    }
gm_lb17:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 218, 9);
                }
gm_lb15:;
                goto gm_lb13;
            }
gm_lb14:;
        }
gm_lb13:;
    }
    {
        gm_b16_8 t131 = gm_l1;
        gm_b16_8 t132 = gm_l17;
        gm_tqs5p4n2tq19v t133 = gm_f_argument_order_jhg33u8((void *)NULL, cx, NULL, t131, t132);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l25 = t133;
        uint8_t t134 = GM_LD(uint8_t, ((uint8_t *)&gm_l25));
        gm_l26 = t134;
        uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 1));
        gm_l27 = t135;
        gm_b16_8 t136 = gm_l1;
        gm_b16_8 t137 = gm_l5;
        gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
        uint64_t t140 = gm_l12;
        gm_trqfvmjtt4sij t141;
        gm_f_list_pmp06kq((void *)&t141, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t142;
        gm_f_list_s83jn2q((void *)&t142, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4j35k4k4pvmb t143;
        GM_ST(uint16_t, (uint8_t *)&t143 + 146, 0);
        GM_ST(uint32_t, (uint8_t *)&t143 + 148, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t143 + 0), t136);
        GM_ST(gm_b16_8, ((uint8_t *)&t143 + 16), t137);
        GM_ST(gm_b16_8, ((uint8_t *)&t143 + 32), t139);
        GM_ST(uint64_t, ((uint8_t *)&t143 + 48), t140);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t143 + 56), t141);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t143 + 96), t142);
        GM_ST(uint64_t, ((uint8_t *)&t143 + 136), ((uint64_t)0ULL));
        GM_ST(uint8_t, ((uint8_t *)&t143 + 144), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t143 + 145), ((uint8_t)0));
        F.l28 = t143;
        gm_b16_8 t144 = gm_l1;
        gm_l29 = t144;
        gm_l30 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t145 = gm_l30;
                uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                uint8_t t147 = (t145 < t146);
                if (!t147) {
                    goto gm_lb23;
                }
                {
                    void * t148 = GM_LD(void *, ((uint8_t *)&gm_l29));
                    uint64_t t149 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                    uint64_t t150 = gm_l30;
                    {
                        {
                            void * t151 = (void *)(((uint8_t *)&F.l28) + 56);
                            gm_tftb1odc739t5 t152 = gm_f_push_r2k6rjf(t151, ((void *)0));
                            gm_l50 = t152;
                            uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                            if (!t153) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srch6h8rdicu443, 233, 9);
                            void * t154 = (void *)(((uint8_t *)&F.l28) + 96);
                            gm_tftb1odc739t5 t155 = gm_f_push_vd1290p(t154, ((uint8_t)0));
                            gm_l51 = t155;
                            uint8_t t156 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                            if (!t156) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srch6h8rdicu443, 234, 9);
                            goto gm_lb26;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 232, 5);
                    }
gm_lb26:;
                }
gm_lb25:;
                uint64_t t157 = gm_l30;
                uint64_t r158; bool o159 = __builtin_add_overflow(t157, ((uint64_t)1ULL), &r158);
                uint64_t t160 = r158;
                gm_l30 = t160;
            }
        }
gm_lb23:;
        gm_trqfvmjtt4sij t161;
        gm_f_list_ir0h3tc_2((void *)&t161, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l31 = t161;
        gm_trqfvmjtt4sij t162;
        gm_f_list_5dhgg4i((void *)&t162, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l32 = t162;
        gm_b16_8 t163 = gm_l17;
        gm_l33 = t163;
        gm_l34 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t164 = gm_l34;
                uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                uint8_t t166 = (t164 < t165);
                if (!t166) {
                    goto gm_lb28;
                }
                {
                    void * t167 = GM_LD(void *, ((uint8_t *)&gm_l33));
                    uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                    uint64_t t169 = gm_l34;
                    {
                        {
                            memmove(((uint8_t *)&F.l35), (((uint8_t *)t167) + (uintptr_t)t169 * 144), 144);
                            {
                                {
                                    uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                    uint8_t t171 = (uint8_t)(t170 == ((uint8_t)0ULL));
                                    if (!t171) {
                                        goto gm_lb34;
                                    }
                                    memmove(((uint8_t *)&F.l36), (((uint8_t *)&F.l35) + 8), 88);
                                    uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&F.l28) + 144));
                                    if (t172) {
                                        void * t173 = gm_l0;
                                        gm_tl874t2atnlh3 t174 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l35) + 128));
                                        gm_b16_8 t175;
                                        GM_ST(void *, (uint8_t *)&t175, (void *)(gm_s1ekjot5tdpje));
                                        GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(66));
                                        gm_b0_1 t176 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t173, t174, t175);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    void * t177 = (void *)((uint8_t *)&F.l28);
                                    gm_tf5srh4mp4b3i t178 = gm_f_next_positional_t5fspc1(t177);
                                    gm_l52 = t178;
                                    uint8_t t179 = (uint8_t)((((uint8_t *)&gm_l52))[8] != 0);
                                    if (t179) {
                                        uint64_t t180 = GM_LD(uint64_t, ((uint8_t *)&gm_l52));
                                        gm_l53 = t180;
                                    } else {
                                        void * t181 = gm_l0;
                                        gm_tl874t2atnlh3 t182 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l35) + 128));
                                        gm_b16_8 t183 = gm_l5;
                                        uint64_t t184 = gm_l12;
                                        gm_t3877pk7lv748 t185;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t185 + 0), t183);
                                        GM_ST(uint64_t, ((uint8_t *)&t185 + 16), t184);
                                        gm_f_format_b4oiuht((void *)&gm_l54, (void *)NULL, cx, NULL, &t185);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t186 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                                        if (!t186) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 8, gm_srch6h8rdicu443, 243, 38);
                                        gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l54) + 8));
                                        gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
                                        gm_b0_1 t189 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t181, t182, t188);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t190 = gm_l0;
                                        void * t191 = (void *)((uint8_t *)&F.l36);
                                        gm_tkn42u7na3t81 t192;
                                        memset(&t192, 0, sizeof t192);
                                        void * t193 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t190, t191, t192);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb30;
                                    }
                                    uint64_t t194 = gm_l53;
                                    gm_l37 = t194;
                                    void * t195 = gm_l0;
                                    void * t196 = (void *)((uint8_t *)&F.l36);
                                    void * t197 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                    uint64_t t198 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                    uint64_t t199 = gm_l37;
                                    if ((uintptr_t)t199 >= (uintptr_t)(t198)) gm_trap_index((uintptr_t)t199, t198, gm_srch6h8rdicu443, 247, 39);
                                    gm_t88rcucg1b7uk t200 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t197) + (uintptr_t)t199 * 56) + 16));
                                    void * t201 = gm_f_arg_value_gavegdu((void *)NULL, cx, NULL, t195, t196, t200);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l38 = t201;
                                    uint8_t t202 = gm_l27;
                                    if (t202) {
                                        void * t203 = gm_l0;
                                        void * t204 = gm_l38;
                                        void * t205 = (void *)((uint8_t *)&F.l31);
                                        void * t206 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t203, t204, t205);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l38 = t206;
                                    }
                                    void * t207 = gm_l0;
                                    void * t208 = (void *)(((uint8_t *)&F.l28) + 56);
                                    void * t209 = (void *)((uint8_t *)&F.l32);
                                    uint64_t t210 = gm_l37;
                                    void * t211 = (void *)((uint8_t *)&F.l31);
                                    gm_b0_1 t212 = gm_f_place_iter_pre_qn5c5b5((void *)NULL, cx, NULL, t207, t208, t209, t210, t211);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t213 = GM_LD(void *, (((uint8_t *)&F.l28) + 56));
                                    uint64_t t214 = GM_LD(uint64_t, ((((uint8_t *)&F.l28) + 56) + 8));
                                    uint64_t t215 = gm_l37;
                                    if ((uintptr_t)t215 >= (uintptr_t)(t214)) gm_trap_index((uintptr_t)t215, t214, gm_srch6h8rdicu443, 250, 17);
                                    void * t216 = gm_l38;
                                    GM_ST(void *, (((uint8_t *)t213) + (uintptr_t)t215 * 8), t216);
                                    uint64_t t217 = GM_LD(uint64_t, (((uint8_t *)&F.l28) + 136));
                                    uint64_t r218; bool o219 = __builtin_add_overflow(t217, ((uint64_t)1ULL), &r218);
                                    if (o219) gm_trap("integer overflow in `+`", gm_srch6h8rdicu443, 251, 17);
                                    uint64_t t220 = r218;
                                    GM_ST(uint64_t, (((uint8_t *)&F.l28) + 136), t220);
                                    goto gm_lb33;
                                }
gm_lb34:;
                                {
                                    uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                    uint8_t t222 = (uint8_t)(t221 == ((uint8_t)1ULL));
                                    if (!t222) {
                                        goto gm_lb35;
                                    }
                                    memmove(((uint8_t *)&gm_l39), (((uint8_t *)&F.l35) + 8), 32);
                                    memmove(((uint8_t *)&F.l40), ((((uint8_t *)&F.l35) + 8) + 32), 88);
                                    gm_b16_8 t223 = gm_l1;
                                    gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l39));
                                    gm_tf5srh4mp4b3i t225 = gm_f_param_index_78k4iut(t223, t224);
                                    gm_l55 = t225;
                                    uint8_t t226 = (uint8_t)((((uint8_t *)&gm_l55))[8] != 0);
                                    if (t226) {
                                        uint64_t t227 = GM_LD(uint64_t, ((uint8_t *)&gm_l55));
                                        gm_l56 = t227;
                                    } else {
                                        void * t228 = gm_l0;
                                        gm_tl874t2atnlh3 t229 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l39) + 16));
                                        gm_b16_8 t230 = gm_l5;
                                        gm_b16_8 t231 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l39));
                                        gm_t6j6smj7qoctu t232;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t232 + 0), t230);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t232 + 16), t231);
                                        gm_f_format_ivju6qo((void *)&gm_l57, (void *)NULL, cx, NULL, &t232);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t233 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                        if (!t233) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 8, gm_srch6h8rdicu443, 255, 38);
                                        gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l57) + 8));
                                        gm_b16_8 t235 = GM_LD(gm_b16_8, ((uint8_t *)&t234));
                                        gm_b0_1 t236 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t228, t229, t235);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t237 = gm_l0;
                                        void * t238 = (void *)((uint8_t *)&F.l40);
                                        gm_tkn42u7na3t81 t239;
                                        memset(&t239, 0, sizeof t239);
                                        void * t240 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t237, t238, t239);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb30;
                                    }
                                    uint64_t t241 = gm_l56;
                                    gm_l41 = t241;
                                    void * t242 = GM_LD(void *, (((uint8_t *)&F.l28) + 96));
                                    uint64_t t243 = GM_LD(uint64_t, ((((uint8_t *)&F.l28) + 96) + 8));
                                    uint64_t t244 = gm_l41;
                                    if ((uintptr_t)t244 >= (uintptr_t)(t243)) gm_trap_index((uintptr_t)t244, t243, gm_srch6h8rdicu443, 259, 20);
                                    uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)t242) + (uintptr_t)t244 * 1));
                                    if (t245) {
                                        gm_l58 = ((uint8_t)1);
                                    } else {
                                        void * t246 = GM_LD(void *, (((uint8_t *)&F.l28) + 56));
                                        uint64_t t247 = GM_LD(uint64_t, ((((uint8_t *)&F.l28) + 56) + 8));
                                        uint64_t t248 = gm_l41;
                                        if ((uintptr_t)t248 >= (uintptr_t)(t247)) gm_trap_index((uintptr_t)t248, t247, gm_srch6h8rdicu443, 259, 44);
                                        {
                                            {
                                                uint8_t t249 = (uint8_t)(GM_LD(void *, (((uint8_t *)t246) + (uintptr_t)t248 * 8)) != 0);
                                                if (t249) {
                                                    goto gm_lb37;
                                                }
                                                gm_l60 = ((uint8_t)0);
                                                goto gm_lb36;
                                            }
gm_lb37:;
                                            {
                                                gm_l60 = ((uint8_t)1);
                                                goto gm_lb36;
                                            }
gm_lb38:;
                                            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 259, 44);
                                        }
gm_lb36:;
                                        uint8_t t250 = gm_l60;
                                        if (t250) {
                                            uint8_t t251 = gm_l26;
                                            uint8_t t252 = (uint8_t)!t251;
                                            gm_l59 = t252;
                                        } else {
                                            gm_l59 = ((uint8_t)0);
                                        }
                                        uint8_t t253 = gm_l59;
                                        gm_l58 = t253;
                                    }
                                    uint8_t t254 = gm_l58;
                                    if (t254) {
                                        void * t255 = gm_l0;
                                        gm_tl874t2atnlh3 t256 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l39) + 16));
                                        gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l39));
                                        gm_tnpifv20irnph t258;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t258 + 0), t257);
                                        gm_f_format_hqkpc5s((void *)&gm_l61, (void *)NULL, cx, NULL, t258);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t259 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                        if (!t259) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 8, gm_srch6h8rdicu443, 260, 38);
                                        gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                                        gm_b16_8 t261 = GM_LD(gm_b16_8, ((uint8_t *)&t260));
                                        gm_b0_1 t262 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t255, t256, t261);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    void * t263 = GM_LD(void *, (((uint8_t *)&F.l28) + 96));
                                    uint64_t t264 = GM_LD(uint64_t, ((((uint8_t *)&F.l28) + 96) + 8));
                                    uint64_t t265 = gm_l41;
                                    if ((uintptr_t)t265 >= (uintptr_t)(t264)) gm_trap_index((uintptr_t)t265, t264, gm_srch6h8rdicu443, 262, 17);
                                    GM_ST(uint8_t, (((uint8_t *)t263) + (uintptr_t)t265 * 1), ((uint8_t)1));
                                    void * t266 = gm_l0;
                                    void * t267 = (void *)((uint8_t *)&F.l40);
                                    void * t268 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                    uint64_t t269 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                    uint64_t t270 = gm_l41;
                                    if ((uintptr_t)t270 >= (uintptr_t)(t269)) gm_trap_index((uintptr_t)t270, t269, gm_srch6h8rdicu443, 264, 39);
                                    gm_t88rcucg1b7uk t271 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t268) + (uintptr_t)t270 * 56) + 16));
                                    void * t272 = gm_f_arg_value_gavegdu((void *)NULL, cx, NULL, t266, t267, t271);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l42 = t272;
                                    uint8_t t273 = gm_l27;
                                    if (t273) {
                                        void * t274 = gm_l0;
                                        void * t275 = gm_l42;
                                        void * t276 = (void *)((uint8_t *)&F.l31);
                                        void * t277 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t274, t275, t276);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l42 = t277;
                                    }
                                    void * t278 = gm_l0;
                                    void * t279 = (void *)(((uint8_t *)&F.l28) + 56);
                                    void * t280 = (void *)((uint8_t *)&F.l32);
                                    uint64_t t281 = gm_l41;
                                    void * t282 = (void *)((uint8_t *)&F.l31);
                                    gm_b0_1 t283 = gm_f_place_iter_pre_qn5c5b5((void *)NULL, cx, NULL, t278, t279, t280, t281, t282);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t284 = GM_LD(void *, (((uint8_t *)&F.l28) + 56));
                                    uint64_t t285 = GM_LD(uint64_t, ((((uint8_t *)&F.l28) + 56) + 8));
                                    uint64_t t286 = gm_l41;
                                    if ((uintptr_t)t286 >= (uintptr_t)(t285)) gm_trap_index((uintptr_t)t286, t285, gm_srch6h8rdicu443, 267, 17);
                                    void * t287 = gm_l42;
                                    GM_ST(void *, (((uint8_t *)t284) + (uintptr_t)t286 * 8), t287);
                                    goto gm_lb33;
                                }
gm_lb35:;
                                {
                                    uint8_t t288 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                    uint8_t t289 = (uint8_t)(t288 == ((uint8_t)2ULL));
                                    if (!t289) {
                                        goto gm_lb39;
                                    }
                                    memmove(((uint8_t *)&F.l43), (((uint8_t *)&F.l35) + 8), 88);
                                    void * t290 = gm_l0;
                                    void * t291 = (void *)((uint8_t *)&F.l43);
                                    void * t292 = (void *)((uint8_t *)&F.l31);
                                    gm_f_spread_parts_e62q2l2((void *)&gm_l62, (void *)NULL, cx, NULL, t290, t291, t292);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t293 = (uint8_t)((((uint8_t *)&gm_l62))[16] != 0);
                                    if (t293) {
                                        gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l62));
                                        gm_l63 = t294;
                                    } else {
                                        GM_ST(uint8_t, (((uint8_t *)&F.l28) + 145), ((uint8_t)1));
                                        goto gm_lb30;
                                    }
                                    gm_b16_8 t295 = gm_l63;
                                    gm_l44 = t295;
                                    void * t296 = gm_l0;
                                    void * t297 = (void *)((uint8_t *)&F.l28);
                                    gm_b16_8 t298 = gm_l44;
                                    gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&t298));
                                    gm_tl874t2atnlh3 t300 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l43) + 72));
                                    gm_b0_1 t301 = gm_f_bind_spread_parts_lmknlmc((void *)NULL, cx, NULL, t296, t297, t299, t300);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb33;
                                }
gm_lb39:;
                                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 239, 9);
                            }
gm_lb33:;
                            goto gm_lb31;
                        }
gm_lb32:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 238, 5);
                    }
gm_lb31:;
                }
gm_lb30:;
                uint64_t t302 = gm_l34;
                uint64_t r303; bool o304 = __builtin_add_overflow(t302, ((uint64_t)1ULL), &r303);
                uint64_t t305 = r303;
                gm_l34 = t305;
            }
        }
gm_lb28:;
    }
    {
        void * t306 = gm_l0;
        void * t307 = (void *)((uint8_t *)&F.l28);
        uint64_t t308 = gm_l2;
        gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
        gm_tl874t2atnlh3 t310 = gm_l4;
        gm_t6j6smj7qoctu t311;
        gm_f_fill_defaults_3fq5ut6((void *)&t311, (void *)NULL, cx, NULL, t306, t307, t308, t309, t310);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttu09lij36f8c t312;
        GM_ST(uint8_t, (uint8_t *)&t312 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t312 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t312 + 36, 0);
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t312), t311);
        ((uint8_t *)&t312)[32] = 1;
        memmove(gm_out, ((uint8_t *)&t312), 40);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_spread_parts_lmknlmc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_bind_spread_parts_lmknlmc F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    gm_b16_8 gm_l2; /* parts */
    gm_tl874t2atnlh3 gm_l3; /* at */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tpouv9r7lrri2 gm_l6; /* part */
    uint64_t gm_l7; /* i */
    gm_b16_8 gm_l8; /* n */
    uint64_t gm_l9; /* j */
    uint64_t gm_l10; /* j */
    GM_STACK_CHECK(&F, 1086u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tf5srh4mp4b3i gm_l11;
    uint64_t gm_l12;
    gm_tabb710d9mq2e gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tf5srh4mp4b3i gm_l17;
    uint64_t gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_b16_8 t12 = gm_l2;
    gm_l4 = t12;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t13 = gm_l5;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t18 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&gm_l6), (((uint8_t *)t16) + (uintptr_t)t18 * 32), 32);
                        gm_l7 = ((uint64_t)0ULL);
                        {
                            {
                                uint8_t t19 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
                                if (!t19) {
                                    goto gm_lb7;
                                }
                                gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                                gm_l8 = t20;
                                void * t21 = gm_l1;
                                GM_ST(uint8_t, (((uint8_t *)t21) + 144), ((uint8_t)1));
                                void * t22 = gm_l1;
                                gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)t22));
                                gm_b16_8 t24 = gm_l8;
                                gm_tf5srh4mp4b3i t25 = gm_f_param_index_78k4iut(t23, t24);
                                gm_l11 = t25;
                                uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l11))[8] != 0);
                                if (t26) {
                                    uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
                                    gm_l12 = t27;
                                } else {
                                    void * t28 = gm_l0;
                                    gm_tl874t2atnlh3 t29 = gm_l3;
                                    void * t30 = gm_l1;
                                    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t30) + 16));
                                    gm_b16_8 t32 = gm_l8;
                                    gm_t6j6smj7qoctu t33;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t31);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t33 + 16), t32);
                                    gm_f_format_10ctmcq((void *)&gm_l13, (void *)NULL, cx, NULL, &t33);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                    if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srch6h8rdicu443, 311, 30);
                                    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                                    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                                    gm_b0_1 t37 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t28, t29, t36);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t38 = gm_l1;
                                    GM_ST(uint8_t, (((uint8_t *)t38) + 145), ((uint8_t)1));
                                    goto gm_lb1;
                                }
                                uint64_t t39 = gm_l12;
                                gm_l9 = t39;
                                void * t40 = gm_l1;
                                void * t41 = GM_LD(void *, (((uint8_t *)t40) + 96));
                                uint64_t t42 = GM_LD(uint64_t, ((((uint8_t *)t40) + 96) + 8));
                                uint64_t t43 = gm_l9;
                                if ((uintptr_t)t43 >= (uintptr_t)(t42)) gm_trap_index((uintptr_t)t43, t42, gm_srch6h8rdicu443, 315, 16);
                                uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)t41) + (uintptr_t)t43 * 1));
                                if (t44) {
                                    gm_l14 = ((uint8_t)1);
                                } else {
                                    void * t45 = gm_l1;
                                    void * t46 = GM_LD(void *, (((uint8_t *)t45) + 56));
                                    uint64_t t47 = GM_LD(uint64_t, ((((uint8_t *)t45) + 56) + 8));
                                    uint64_t t48 = gm_l9;
                                    if ((uintptr_t)t48 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t48, t47, gm_srch6h8rdicu443, 315, 39);
                                    {
                                        {
                                            uint8_t t49 = (uint8_t)(GM_LD(void *, (((uint8_t *)t46) + (uintptr_t)t48 * 8)) != 0);
                                            if (t49) {
                                                goto gm_lb9;
                                            }
                                            gm_l15 = ((uint8_t)0);
                                            goto gm_lb8;
                                        }
gm_lb9:;
                                        {
                                            gm_l15 = ((uint8_t)1);
                                            goto gm_lb8;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 315, 39);
                                    }
gm_lb8:;
                                    uint8_t t50 = gm_l15;
                                    gm_l14 = t50;
                                }
                                uint8_t t51 = gm_l14;
                                if (t51) {
                                    void * t52 = gm_l0;
                                    gm_tl874t2atnlh3 t53 = gm_l3;
                                    gm_b16_8 t54 = gm_l8;
                                    gm_tnpifv20irnph t55;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
                                    gm_f_format_hqkpc5s((void *)&gm_l16, (void *)NULL, cx, NULL, t55);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                    if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srch6h8rdicu443, 316, 30);
                                    gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                                    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                                    gm_b0_1 t59 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t52, t53, t58);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t60 = gm_l1;
                                    GM_ST(uint8_t, (((uint8_t *)t60) + 145), ((uint8_t)1));
                                    goto gm_lb1;
                                }
                                uint64_t t61 = gm_l9;
                                gm_l7 = t61;
                                goto gm_lb6;
                            }
gm_lb7:;
                            void * t62 = gm_l1;
                            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)t62) + 144));
                            if (t63) {
                                void * t64 = gm_l0;
                                gm_tl874t2atnlh3 t65 = gm_l3;
                                gm_b16_8 t66;
                                GM_ST(void *, (uint8_t *)&t66, (void *)(gm_s1ekjot5tdpje));
                                GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(66));
                                gm_b0_1 t67 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t65, t66);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t68 = gm_l1;
                                GM_ST(uint8_t, (((uint8_t *)t68) + 145), ((uint8_t)1));
                                goto gm_lb1;
                            }
                            void * t69 = gm_l1;
                            gm_tf5srh4mp4b3i t70 = gm_f_next_positional_t5fspc1(t69);
                            gm_l17 = t70;
                            uint8_t t71 = (uint8_t)((((uint8_t *)&gm_l17))[8] != 0);
                            if (t71) {
                                uint64_t t72 = GM_LD(uint64_t, ((uint8_t *)&gm_l17));
                                gm_l18 = t72;
                            } else {
                                void * t73 = gm_l0;
                                gm_tl874t2atnlh3 t74 = gm_l3;
                                void * t75 = gm_l1;
                                gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)t75) + 16));
                                void * t77 = gm_l1;
                                uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)t77) + 48));
                                gm_t3877pk7lv748 t79;
                                GM_ST(gm_b16_8, ((uint8_t *)&t79 + 0), t76);
                                GM_ST(uint64_t, ((uint8_t *)&t79 + 16), t78);
                                gm_f_format_b4oiuht((void *)&gm_l19, (void *)NULL, cx, NULL, &t79);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srch6h8rdicu443, 329, 30);
                                gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                                gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                                gm_b0_1 t83 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t73, t74, t82);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t84 = gm_l1;
                                GM_ST(uint8_t, (((uint8_t *)t84) + 145), ((uint8_t)1));
                                goto gm_lb1;
                            }
                            uint64_t t85 = gm_l18;
                            gm_l10 = t85;
                            uint64_t t86 = gm_l10;
                            gm_l7 = t86;
                            void * t87 = gm_l1;
                            void * t88 = gm_l1;
                            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)t88) + 136));
                            uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                            if (o91) gm_trap("integer overflow in `+`", gm_srch6h8rdicu443, 334, 13);
                            uint64_t t92 = r90;
                            GM_ST(uint64_t, (((uint8_t *)t87) + 136), t92);
                        }
gm_lb6:;
                        void * t93 = gm_l1;
                        void * t94 = GM_LD(void *, (((uint8_t *)t93) + 56));
                        uint64_t t95 = GM_LD(uint64_t, ((((uint8_t *)t93) + 56) + 8));
                        uint64_t t96 = gm_l7;
                        if ((uintptr_t)t96 >= (uintptr_t)(t95)) gm_trap_index((uintptr_t)t96, t95, gm_srch6h8rdicu443, 336, 9);
                        void * t97 = gm_l0;
                        void * t98 = GM_LD(void *, (((uint8_t *)&gm_l6) + 24));
                        void * t99 = gm_l1;
                        void * t100 = GM_LD(void *, ((uint8_t *)t99));
                        uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)t99) + 8));
                        uint64_t t102 = gm_l7;
                        if ((uintptr_t)t102 >= (uintptr_t)(t101)) gm_trap_index((uintptr_t)t102, t101, gm_srch6h8rdicu443, 336, 50);
                        gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t100) + (uintptr_t)t102 * 56) + 16));
                        void * t104 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t97, t98, t103);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(void *, (((uint8_t *)t94) + (uintptr_t)t96 * 8), t104);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 306, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t105 = gm_l5;
            uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
            uint64_t t108 = r106;
            gm_l5 = t108;
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
__attribute__((aligned(4))) void * gm_f_call_decl_sj6f05u(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_call_decl_sj6f05u F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* d */
    gm_b16_8 gm_l2; /* name */
    void * gm_l3; /* callee */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    uint32_t gm_l6; /* f */
    gm_t88rcucg1b7uk gm_l7; /* t */
    uint32_t gm_l8; /* eff */
    uint32_t gm_l9; /* op */
    GM_STACK_CHECK(&F, 771u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t6qooom8qa2do gm_l10;
    gm_tabb710d9mq2e gm_l11;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    uint64_t t11 = gm_l1;
    gm_f_resolve_decl_pus042v((void *)&gm_l10, (void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)5ULL));
            if (!t13) {
                goto gm_lb2;
            }
            uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 8));
            gm_l6 = t14;
            void * t15 = gm_l0;
            uint32_t t16 = gm_l6;
            gm_b16_8 t17 = gm_l4;
            gm_tl874t2atnlh3 t18 = gm_l5;
            void * t19 = gm_f_call_fn_2prsqv9((void *)NULL, cx, NULL, t15, t16, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t19;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)0ULL));
            if (!t21) {
                goto gm_lb3;
            }
            gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 8));
            gm_l7 = t22;
            void * t23 = gm_l0;
            gm_t88rcucg1b7uk t24 = gm_l7;
            gm_b16_8 t25 = gm_l4;
            gm_tl874t2atnlh3 t26 = gm_l5;
            void * t27 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t23, t24, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t27;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)11ULL));
            if (!t29) {
                goto gm_lb4;
            }
            void * t30 = gm_l0;
            void * t31 = GM_LD(void *, ((uint8_t *)t30));
            gm_tl874t2atnlh3 t32 = gm_l5;
            void * t33 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t33;
            goto gm_fret;
        }
gm_lb4:;
        {
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)6ULL));
            if (!t35) {
                goto gm_lb5;
            }
            uint32_t t36 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 8));
            gm_l8 = t36;
            uint32_t t37 = GM_LD(uint32_t, ((((uint8_t *)&gm_l10) + 8) + 4));
            gm_l9 = t37;
            void * t38 = gm_l0;
            uint32_t t39 = gm_l8;
            uint32_t t40 = gm_l9;
            gm_b16_8 t41 = gm_l4;
            gm_tl874t2atnlh3 t42 = gm_l5;
            void * t43 = gm_f_perform_op_1nih4r3((void *)NULL, cx, NULL, t38, t39, t40, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t43;
            goto gm_fret;
        }
gm_lb5:;
        {
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)7ULL));
            if (!t45) {
                goto gm_lb6;
            }
            void * t46 = gm_l0;
            void * t47 = gm_l0;
            void * t48 = gm_l3;
            gm_tkn42u7na3t81 t49;
            memset(&t49, 0, sizeof t49);
            void * t50 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t47, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t51 = gm_l4;
            gm_tl874t2atnlh3 t52 = gm_l5;
            void * t53 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t46, t50, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t53;
            goto gm_fret;
        }
gm_lb6:;
        {
            void * t54 = gm_l0;
            void * t55 = gm_l3;
            gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 72));
            gm_b16_8 t57 = gm_l2;
            gm_tnpifv20irnph t58;
            GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t57);
            gm_f_format_8fudcr8((void *)&gm_l11, (void *)NULL, cx, NULL, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srch6h8rdicu443, 136, 35);
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
            gm_b0_1 t62 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t54, t56, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t63 = gm_l0;
            void * t64 = GM_LD(void *, ((uint8_t *)t63));
            gm_tl874t2atnlh3 t65 = gm_l5;
            void * t66 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t66;
            goto gm_fret;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 129, 5);
    }
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
__attribute__((aligned(4))) void * gm_f_call_fn_2prsqv9(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_call_fn_2prsqv9 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint64_t gm_l5; /* n */
    uint8_t gm_l6; /* variadic */
    gm_b16_8 gm_l7; /* fixed */
    gm_t6j6smj7qoctu gm_l8; /* bound */
    gm_b16_8 gm_l9; /* all */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    void * gm_l14; /* call */
    GM_STACK_CHECK(&F, 1222u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l15;
    gm_b16_8 gm_l16;
    gm_ttu09lij36f8c gm_l17;
    gm_t6j6smj7qoctu gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 344));
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t8) + 344) + 8));
    uint32_t t11 = gm_l1;
    uint64_t t12 = (uint64_t)t11;
    if ((uintptr_t)t12 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t12, t10, gm_srch6h8rdicu443, 144, 12);
    memmove(((uint8_t *)&F.l4), (((uint8_t *)t9) + (uintptr_t)t12 * 136), 136);
    uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)&F.l4) + 80) + 8));
    uint8_t t14 = (t13 > ((uint64_t)0ULL));
    if (t14) {
        void * t15 = gm_l0;
        uint32_t t16 = gm_l1;
        gm_b16_8 t17 = gm_l2;
        gm_tl874t2atnlh3 t18 = gm_l3;
        void * t19 = gm_f_generic_call_2tlofdi((void *)NULL, cx, NULL, t15, t16, ((void *)0), t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t19;
        goto gm_fret;
    }
    uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)&F.l4) + 40) + 8));
    gm_l5 = t20;
    uint32_t t21 = gm_l1;
    void * t22 = gm_l0;
    void * t23 = GM_LD(void *, ((uint8_t *)t22));
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t23) + 1488));
    uint8_t t25 = 0;
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t24) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t24))) + i * 4, ((uint8_t *)&t21))) { t25 = 1; break; }
    uint8_t t26 = (uint8_t)(t25);
    if (t26) {
        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t28 = gm_l5;
        uint8_t t29 = (t27 > t28);
        gm_l15 = t29;
    } else {
        gm_l15 = ((uint8_t)0);
    }
    uint8_t t30 = gm_l15;
    gm_l6 = t30;
    uint8_t t31 = gm_l6;
    if (t31) {
        void * t32 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t34 = gm_l5;
        uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t36 = (uintptr_t)t34;
        if (t35 > t36 || t36 > (uintptr_t)t33) gm_trap_slice(t35, t36, t33, gm_srch6h8rdicu443, 148, 28);
        uint64_t t37 = ((uintptr_t)t34) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 144)));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37));
        gm_l16 = t38;
    } else {
        gm_b16_8 t39 = gm_l2;
        gm_l16 = t39;
    }
    gm_b16_8 t40 = gm_l16;
    gm_l7 = t40;
    void * t41 = gm_l0;
    gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 40));
    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
    uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 32));
    gm_b16_8 t45 = gm_l7;
    gm_tl874t2atnlh3 t46 = gm_l3;
    gm_b16_8 t47;
    GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sq0dqiq83b924));
    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(8));
    gm_f_bind_args_dnih8ih((void *)&gm_l17, (void *)NULL, cx, NULL, t41, t43, t44, t45, t46, t47);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t48 = (uint8_t)((((uint8_t *)&gm_l17))[32] != 0);
    if (t48) {
        memmove(((uint8_t *)&gm_l18), ((uint8_t *)&gm_l17), 32);
    } else {
        void * t49 = gm_l0;
        void * t50 = GM_LD(void *, ((uint8_t *)t49));
        gm_tl874t2atnlh3 t51 = gm_l3;
        void * t52 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t52;
        goto gm_fret;
    }
    memmove(((uint8_t *)&gm_l8), ((uint8_t *)&gm_l18), 32);
    gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
    gm_l9 = t53;
    uint8_t t54 = gm_l6;
    if (t54) {
        gm_trqfvmjtt4sij t55;
        gm_f_list_vp1djv1_2((void *)&t55, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t55;
        void * t56 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
        gm_tftb1odc739t5 t59 = gm_f_extend_gig5o6g(t56, t58);
        gm_l19 = t59;
        uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
        if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srch6h8rdicu443, 153, 9);
        void * t61 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t63 = gm_l5;
        uint64_t t64 = (uintptr_t)t63;
        uint64_t t65 = (uintptr_t)t62;
        if (t64 > t65 || t65 > (uintptr_t)t62) gm_trap_slice(t64, t65, t62, gm_srch6h8rdicu443, 154, 18);
        uint64_t t66 = ((uintptr_t)t62) - (uintptr_t)t63;
        gm_b16_8 t67;
        GM_ST(void *, (uint8_t *)&t67, (void *)(((uint8_t *)t61 + (uintptr_t)t63 * 144)));
        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(t66));
        gm_l11 = t67;
        gm_l12 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t68 = gm_l12;
                uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t70 = (t68 < t69);
                if (!t70) {
                    goto gm_lb1;
                }
                {
                    void * t71 = GM_LD(void *, ((uint8_t *)&gm_l11));
                    uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint64_t t73 = gm_l12;
                    {
                        {
                            memmove(((uint8_t *)&F.l13), (((uint8_t *)t71) + (uintptr_t)t73 * 144), 144);
                            void * t74 = (void *)((uint8_t *)&F.l10);
                            void * t75 = gm_l0;
                            void * t76 = (void *)((uint8_t *)&F.l13);
                            void * t77 = gm_f_variadic_arg_dfdnlo3((void *)NULL, cx, NULL, t75, t76);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t78 = gm_f_push_d943vgp_2(t74, &(*(gm_tcgrjq9toscke *)((uint8_t *)t77)));
                            gm_l20 = t78;
                            uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                            if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srch6h8rdicu443, 154, 30);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 154, 9);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t80 = gm_l12;
                uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                uint64_t t83 = r81;
                gm_l12 = t83;
            }
        }
gm_lb1:;
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
        gm_l9 = t84;
    }
    void * t85 = gm_l0;
    void * t86 = (void *)(((uint8_t *)&F.l4) + 64);
    gm_tl874t2atnlh3 t87 = gm_l3;
    gm_b0_1 t88 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t85, t86, t87);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 56));
    uint32_t t90 = gm_l1;
    gm_b16_8 t91 = gm_l9;
    gm_tsth9qu1dv0g3 t92;
    memset(&t92, 0, sizeof t92);
    GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&t92 + 8), t90);
    gm_to76jli3bqddq t93;
    GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t93 + 0), t92);
    GM_ST(gm_b16_8, ((uint8_t *)&t93 + 40), t91);
    gm_tl874t2atnlh3 t94 = gm_l3;
    gm_t2b6tm8c0vgo1 t95;
    GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)34ULL));
    GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t95 + 8), t93);
    void * t96 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t89, &t95, t94);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l14 = t96;
    uint64_t t97 = GM_LD(uint64_t, ((((uint8_t *)&gm_l8) + 16) + 8));
    uint8_t t98 = ((t97 == ((uint64_t)0ULL)));
    if (t98) {
        void * t99 = gm_l14;
        F.ret = t99;
        goto gm_fret;
    }
    void * t100 = gm_l14;
    gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t100));
    gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 16));
    void * t103 = gm_l14;
    gm_tl874t2atnlh3 t104 = gm_l3;
    void * t105 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t101, t102, t103, t104);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t105;
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
__attribute__((aligned(4))) void * gm_f_call_l10ebge(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tkn42u7na3t81 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_call_l10ebge F;
    void * gm_l0; /* cx */
    void * gm_l1; /* callee */
    gm_b16_8 gm_l2; /* args */
    gm_tkn42u7na3t81 gm_l3; /* expect */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t08olq97rscn3 gm_l5; /* id */
    gm_t88rcucg1b7uk gm_l7; /* t */
    void * gm_l10; /* c */
    void * gm_l11; /* base */
    gm_t08olq97rscn3 gm_l12; /* name */
    gm_t716e5hdn64ba gm_l13; /* named */
    gm_t08olq97rscn3 gm_l14; /* name */
    gm_t88rcucg1b7uk gm_l15; /* t */
    void * gm_l16; /* base */
    gm_t08olq97rscn3 gm_l17; /* name */
    uint64_t gm_l18; /* m */
    uint64_t gm_l19; /* d */
    gm_t88rcucg1b7uk gm_l20; /* t */
    gm_t08olq97rscn3 gm_l21; /* id */
    uint32_t gm_l23; /* f */
    gm_t88rcucg1b7uk gm_l24; /* t */
    gm_b16_8 gm_l25; /* n */
    uint64_t gm_l26; /* d */
    gm_t88rcucg1b7uk gm_l27; /* t */
    uint32_t gm_l28; /* y */
    gm_t88rcucg1b7uk gm_l29; /* t */
    gm_b16_8 gm_l30; /* es */
    gm_t88rcucg1b7uk gm_l31; /* t */
    GM_STACK_CHECK(&F, 5144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l32;
    gm_tqvu78rpi6q1h gm_l33;
    gm_tkn42u7na3t81 gm_l34;
    gm_tkv1vap90vht2 gm_l35;
    uint8_t gm_l36;
    gm_tabb710d9mq2e gm_l37;
    gm_tlbrdhah49ci5 gm_l38;
    uint8_t gm_l39;
    gm_tf5srh4mp4b3i gm_l40;
    gm_t88rcucg1b7uk gm_l41;
    gm_tabb710d9mq2e gm_l42;
    gm_tf5srh4mp4b3i gm_l43;
    gm_tf5srh4mp4b3i gm_l44;
    uint64_t gm_l45;
    gm_tkn42u7na3t81 gm_l46;
    gm_tqvu78rpi6q1h gm_l47;
    gm_tabb710d9mq2e gm_l48;
    uint8_t gm_l49;
    gm_tkn42u7na3t81 gm_l50;
    gm_tabb710d9mq2e gm_l51;
    gm_tf5srh4mp4b3i gm_l52;
    gm_tkn42u7na3t81 gm_l53;
    gm_tkn42u7na3t81 gm_l54;
    {
        {
            {
                void * t62 = gm_l1;
                uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t62) + 8) + 0));
                uint8_t t64 = (uint8_t)(t63 == ((uint8_t)6ULL));
                if (!t64) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&gm_l5), ((((uint8_t *)t62) + 8) + 8), 32);
                void * t65 = gm_l0;
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                gm_f_lookup_local_6f38t7p((void *)&gm_l33, t65, t66);
                {
                    {
                        uint8_t t67 = (uint8_t)((((uint8_t *)&gm_l33))[208] != 0);
                        if (t67) {
                            goto gm_lb4;
                        }
                        gm_l32 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_l32 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 6, 38);
                }
gm_lb3:;
                uint8_t t68 = gm_l32;
                if (!t68) {
                    goto gm_lb2;
                }
                uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint8_t t70 = ((t69 == ((uint64_t)1ULL)));
                if (!t70) {
                    goto gm_lb2;
                }
                void * t71 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t72)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t72, gm_srch6h8rdicu443, 6, 111);
                uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t71) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                uint8_t t74 = (uint8_t)(t73 == ((uint8_t)0ULL));
                if (!t74) {
                    goto gm_lb2;
                }
                memmove(((uint8_t *)&F.l6), ((((uint8_t *)t71) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                {
                    {
                        void * t75 = gm_l0;
                        void * t76 = GM_LD(void *, ((uint8_t *)t75));
                        gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                        gm_tkn42u7na3t81 t78 = gm_f_type_param_3av4r8i(t76, t77);
                        gm_l34 = t78;
                        uint8_t t79 = (uint8_t)((((uint8_t *)&gm_l34))[4] != 0);
                        if (!t79) {
                            goto gm_lb7;
                        }
                        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l34));
                        gm_l7 = t80;
                        {
                            {
                                void * t81 = gm_l0;
                                void * t82 = (void *)((uint8_t *)&F.l6);
                                gm_f_try_untyped_8j8up35((void *)&gm_l35, (void *)NULL, cx, NULL, t81, t82);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t83 = (uint8_t)((((uint8_t *)&gm_l35))[208] != 0);
                                if (!t83) {
                                    goto gm_lb9;
                                }
                                memmove(((uint8_t *)&F.l8), ((uint8_t *)&gm_l35), 208);
                                {
                                    {
                                        uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                                        uint8_t t85 = (uint8_t)(t84 == ((uint8_t)0ULL));
                                        if (!t85) {
                                            goto gm_lb11;
                                        }
                                        memmove(((uint8_t *)&F.l9), (((uint8_t *)&F.l8) + 8), 200);
                                        {
                                            {
                                                uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&F.l9) + 0));
                                                uint8_t t87 = (uint8_t)(t86 == ((uint8_t)3ULL));
                                                if (!t87) {
                                                    goto gm_lb13;
                                                }
                                                gm_l36 = ((uint8_t)1);
                                                goto gm_lb12;
                                            }
gm_lb13:;
                                            {
                                                gm_l36 = ((uint8_t)0);
                                                goto gm_lb12;
                                            }
gm_lb14:;
                                            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 10, 20);
                                        }
gm_lb12:;
                                        uint8_t t88 = gm_l36;
                                        if (t88) {
                                            void * t89 = gm_l0;
                                            gm_tl874t2atnlh3 t90 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 72));
                                            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                                            gm_tnpifv20irnph t92;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t92 + 0), t91);
                                            gm_f_format_t3u5vlm((void *)&gm_l37, (void *)NULL, cx, NULL, t92);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                            if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_srch6h8rdicu443, 11, 38);
                                            gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                                            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
                                            gm_b0_1 t96 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t89, t90, t95);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            void * t97 = gm_l0;
                                            void * t98 = GM_LD(void *, ((uint8_t *)t97));
                                            gm_tl874t2atnlh3 t99 = gm_l4;
                                            void * t100 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t98, t99);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.ret = t100;
                                            goto gm_fret;
                                        }
                                        void * t101 = gm_l0;
                                        void * t102 = gm_l0;
                                        void * t103 = (void *)((uint8_t *)&F.l9);
                                        gm_tl874t2atnlh3 t104 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 72));
                                        void * t105 = gm_f_untyped_literal_pfu1ft4((void *)NULL, cx, NULL, t102, t103, t104);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_t88rcucg1b7uk t106 = gm_l7;
                                        void * t107 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t101, t105, t106);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l10 = t107;
                                        void * t108 = gm_l10;
                                        gm_tl874t2atnlh3 t109 = gm_l4;
                                        GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t108) + 72), t109);
                                        void * t110 = gm_l10;
                                        F.ret = t110;
                                        goto gm_fret;
                                    }
gm_lb11:;
                                    void * t111 = gm_l0;
                                    void * t112 = GM_LD(void *, ((uint8_t *)t111));
                                    gm_tl874t2atnlh3 t113 = gm_l4;
                                    void * t114 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t112, t113);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t114;
                                    goto gm_fret;
                                }
gm_lb10:;
                            }
gm_lb9:;
                        }
gm_lb8:;
                        goto gm_lb6;
                    }
gm_lb7:;
                }
gm_lb6:;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        {
            {
                void * t115 = gm_l1;
                uint8_t t116 = GM_LD(uint8_t, ((((uint8_t *)t115) + 8) + 0));
                uint8_t t117 = (uint8_t)(t116 == ((uint8_t)22ULL));
                if (!t117) {
                    goto gm_lb16;
                }
                void * t118 = GM_LD(void *, ((((uint8_t *)t115) + 8) + 8));
                gm_l11 = t118;
                memmove(((uint8_t *)&gm_l12), (((((uint8_t *)t115) + 8) + 8) + 8), 32);
                {
                    {
                        void * t119 = gm_l0;
                        void * t120 = gm_l11;
                        gm_f_effect_named_b8afdbp((void *)&gm_l38, (void *)NULL, cx, NULL, t119, t120);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t121 = (uint8_t)((((uint8_t *)&gm_l38))[32] != 0);
                        if (!t121) {
                            goto gm_lb18;
                        }
                        memmove(((uint8_t *)&gm_l13), ((uint8_t *)&gm_l38), 32);
                        void * t122 = gm_l0;
                        gm_b16_8 t123 = gm_l2;
                        gm_tl874t2atnlh3 t124 = gm_l4;
                        void * t125 = gm_f_perform_named_h7gr9vi((void *)NULL, cx, NULL, t122, &(*(gm_t716e5hdn64ba *)((uint8_t *)&gm_l13)), &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l12)), t123, t124);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t125;
                        goto gm_fret;
                    }
gm_lb18:;
                }
gm_lb17:;
                void * t126 = gm_l0;
                void * t127 = gm_l11;
                gm_tf5srh4mp4b3i t128 = gm_f_module_of_n0d5f6n((void *)NULL, cx, NULL, t126, t127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l40 = t128;
                {
                    {
                        uint8_t t129 = (uint8_t)((((uint8_t *)&gm_l40))[8] != 0);
                        if (t129) {
                            goto gm_lb20;
                        }
                        gm_l39 = ((uint8_t)1);
                        goto gm_lb19;
                    }
gm_lb20:;
                    {
                        gm_l39 = ((uint8_t)0);
                        goto gm_lb19;
                    }
gm_lb21:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 23, 12);
                }
gm_lb19:;
                uint8_t t130 = gm_l39;
                if (t130) {
                    void * t131 = gm_l0;
                    void * t132 = gm_l11;
                    gm_b16_8 t133 = gm_l2;
                    gm_tl874t2atnlh3 t134 = gm_l4;
                    void * t135 = gm_f_method_call_hebo20o((void *)NULL, cx, NULL, t131, t132, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l12)), t133, t134);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t135;
                    goto gm_fret;
                }
                goto gm_lb15;
            }
gm_lb16:;
        }
gm_lb15:;
        void * t136 = gm_l1;
        {
            {
                {
                    uint8_t t137 = GM_LD(uint8_t, ((((uint8_t *)t136) + 8) + 0));
                    uint8_t t138 = (uint8_t)(t137 == ((uint8_t)8ULL));
                    if (!t138) {
                        goto gm_lb23;
                    }
                    memmove(((uint8_t *)&gm_l14), ((((uint8_t *)t136) + 8) + 8), 32);
                    uint8_t t139 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
                    if (t139) {
                        gm_t88rcucg1b7uk t140 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
                        gm_l41 = t140;
                    } else {
                        void * t141 = gm_l0;
                        void * t142 = gm_l1;
                        gm_tl874t2atnlh3 t143 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t142) + 72));
                        gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                        gm_tnpifv20irnph t145;
                        GM_ST(gm_b16_8, ((uint8_t *)&t145 + 0), t144);
                        gm_f_format_bvitrsu((void *)&gm_l42, (void *)NULL, cx, NULL, t145);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t146 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                        if (!t146) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 8, gm_srch6h8rdicu443, 28, 39);
                        gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l42) + 8));
                        gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&t147));
                        gm_b0_1 t149 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t141, t143, t148);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t150 = gm_l0;
                        void * t151 = GM_LD(void *, ((uint8_t *)t150));
                        gm_tl874t2atnlh3 t152 = gm_l4;
                        void * t153 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t151, t152);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t153;
                        goto gm_fret;
                    }
                    gm_t88rcucg1b7uk t154 = gm_l41;
                    gm_l15 = t154;
                    void * t155 = gm_l0;
                    void * t156 = gm_l0;
                    void * t157 = gm_l0;
                    void * t158 = (void *)(((uint8_t *)t157) + 96);
                    void * t159 = gm_l0;
                    void * t160 = GM_LD(void *, ((uint8_t *)t159));
                    gm_t88rcucg1b7uk t161 = gm_l15;
                    gm_t88rcucg1b7uk t162 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t158, t160, t161);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t163 = GM_CONV(gm_t88rcucg1b7uk, gm_f_strip_wrappers_e5thpib(t156, GM_CONV(gm_csguankko4lmhd, t162)));
                    gm_b16_8 t164 = gm_l2;
                    gm_th2lqbo77pvsc t165;
                    GM_ST(uint8_t, (uint8_t *)&t165 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t165 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t165 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t165), t164);
                    ((uint8_t *)&t165)[16] = 1;
                    gm_tl874t2atnlh3 t166 = gm_l4;
                    void * t167 = gm_f_construct_variant_o6is81c((void *)NULL, cx, NULL, t155, t163, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l14)), &t165, t166);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t167;
                    goto gm_fret;
                }
gm_lb23:;
                {
                    uint8_t t168 = GM_LD(uint8_t, ((((uint8_t *)t136) + 8) + 0));
                    uint8_t t169 = (uint8_t)(t168 == ((uint8_t)22ULL));
                    if (!t169) {
                        goto gm_lb24;
                    }
                    void * t170 = GM_LD(void *, ((((uint8_t *)t136) + 8) + 8));
                    gm_l16 = t170;
                    memmove(((uint8_t *)&gm_l17), (((((uint8_t *)t136) + 8) + 8) + 8), 32);
                    {
                        {
                            void * t171 = gm_l0;
                            void * t172 = gm_l16;
                            gm_tf5srh4mp4b3i t173 = gm_f_module_of_n0d5f6n((void *)NULL, cx, NULL, t171, t172);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l43 = t173;
                            uint8_t t174 = (uint8_t)((((uint8_t *)&gm_l43))[8] != 0);
                            if (!t174) {
                                goto gm_lb26;
                            }
                            uint64_t t175 = GM_LD(uint64_t, ((uint8_t *)&gm_l43));
                            gm_l18 = t175;
                            void * t176 = gm_l0;
                            void * t177 = GM_LD(void *, ((uint8_t *)t176));
                            uint64_t t178 = gm_l18;
                            void * t179 = gm_l0;
                            uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)t179) + 8));
                            gm_tf5srh4mp4b3i t181 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t177, t178, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l17)), t180);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l44 = t181;
                            uint8_t t182 = (uint8_t)((((uint8_t *)&gm_l44))[8] != 0);
                            if (t182) {
                                uint64_t t183 = GM_LD(uint64_t, ((uint8_t *)&gm_l44));
                                gm_l45 = t183;
                            } else {
                                void * t184 = gm_l0;
                                void * t185 = GM_LD(void *, ((uint8_t *)t184));
                                gm_tl874t2atnlh3 t186 = gm_l4;
                                void * t187 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t185, t186);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t187;
                                goto gm_fret;
                            }
                            uint64_t t188 = gm_l45;
                            gm_l19 = t188;
                            void * t189 = gm_l0;
                            uint64_t t190 = gm_l19;
                            gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
                            void * t192 = gm_l1;
                            gm_b16_8 t193 = gm_l2;
                            gm_tl874t2atnlh3 t194 = gm_l4;
                            void * t195 = gm_f_call_decl_sj6f05u((void *)NULL, cx, NULL, t189, t190, t191, t192, t193, t194);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t195;
                            goto gm_fret;
                        }
gm_lb26:;
                    }
gm_lb25:;
                    {
                        {
                            void * t196 = gm_l0;
                            void * t197 = gm_l16;
                            gm_tkn42u7na3t81 t198 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t196, t197);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l46 = t198;
                            uint8_t t199 = (uint8_t)((((uint8_t *)&gm_l46))[4] != 0);
                            if (!t199) {
                                goto gm_lb28;
                            }
                            gm_t88rcucg1b7uk t200 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l46));
                            gm_l20 = t200;
                            void * t201 = gm_l0;
                            gm_t88rcucg1b7uk t202 = gm_l20;
                            gm_b16_8 t203 = gm_l2;
                            gm_th2lqbo77pvsc t204;
                            GM_ST(uint8_t, (uint8_t *)&t204 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t204 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t204 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t204), t203);
                            ((uint8_t *)&t204)[16] = 1;
                            gm_tl874t2atnlh3 t205 = gm_l4;
                            void * t206 = gm_f_construct_variant_o6is81c((void *)NULL, cx, NULL, t201, t202, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l17)), &t204, t205);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t206;
                            goto gm_fret;
                        }
gm_lb28:;
                    }
gm_lb27:;
                    void * t207 = gm_l0;
                    gm_tl874t2atnlh3 t208 = gm_l4;
                    gm_b16_8 t209;
                    GM_ST(void *, (uint8_t *)&t209, (void *)(gm_sgnvdmoebn4i7));
                    GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(15));
                    void * t210 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t207, t208, t209);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t210;
                    goto gm_fret;
                }
gm_lb24:;
            }
            {
                {
                    {
                        uint8_t t211 = GM_LD(uint8_t, ((((uint8_t *)t136) + 8) + 0));
                        uint8_t t212 = (uint8_t)(t211 == ((uint8_t)6ULL));
                        if (!t212) {
                            goto gm_lb29;
                        }
                        memmove(((uint8_t *)&gm_l21), ((((uint8_t *)t136) + 8) + 8), 32);
                        {
                            {
                                void * t213 = gm_l0;
                                gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                gm_f_lookup_local_6f38t7p((void *)&gm_l47, t213, t214);
                                uint8_t t215 = (uint8_t)((((uint8_t *)&gm_l47))[208] != 0);
                                if (!t215) {
                                    goto gm_lb31;
                                }
                                memmove(((uint8_t *)&F.l22), ((uint8_t *)&gm_l47), 208);
                                {
                                    {
                                        uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                                        uint8_t t217 = (uint8_t)(t216 == ((uint8_t)3ULL));
                                        if (!t217) {
                                            goto gm_lb33;
                                        }
                                        uint32_t t218 = GM_LD(uint32_t, (((uint8_t *)&F.l22) + 8));
                                        gm_l23 = t218;
                                        uint32_t t219 = gm_l23;
                                        void * t220 = gm_l0;
                                        gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)t220) + 760));
                                        uint8_t t222 = 0;
                                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t221) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t221))) + i * 4, ((uint8_t *)&t219))) { t222 = 1; break; }
                                        uint8_t t223 = (uint8_t)(t222);
                                        if (t223) {
                                            void * t224 = gm_l0;
                                            gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                            gm_tl874t2atnlh3 t226 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l21) + 16));
                                            gm_b0_1 t227 = gm_f_recursive_without_ret_48l31rm((void *)NULL, cx, NULL, t224, t225, t226);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            void * t228 = gm_l0;
                                            void * t229 = GM_LD(void *, ((uint8_t *)t228));
                                            gm_tl874t2atnlh3 t230 = gm_l4;
                                            void * t231 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t229, t230);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.ret = t231;
                                            goto gm_fret;
                                        }
                                        void * t232 = gm_l0;
                                        uint32_t t233 = gm_l23;
                                        gm_b16_8 t234 = gm_l2;
                                        gm_tl874t2atnlh3 t235 = gm_l4;
                                        void * t236 = gm_f_call_fn_2prsqv9((void *)NULL, cx, NULL, t232, t233, t234, t235);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t236;
                                        goto gm_fret;
                                    }
gm_lb33:;
                                }
gm_lb32:;
                                {
                                    {
                                        uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                                        uint8_t t238 = (uint8_t)(t237 == ((uint8_t)5ULL));
                                        if (!t238) {
                                            goto gm_lb35;
                                        }
                                        gm_t88rcucg1b7uk t239 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l22) + 8));
                                        gm_l24 = t239;
                                        void * t240 = gm_l0;
                                        void * t241 = GM_LD(void *, ((uint8_t *)t240));
                                        void * t242 = (void *)(((uint8_t *)t241) + 8);
                                        gm_t88rcucg1b7uk t243 = gm_l24;
                                        uint8_t t244 = gm_f_is_error_8k27gd1(t242, GM_CONV(gm_csguankko4lmhd, t243));
                                        if (t244) {
                                            void * t245 = gm_l0;
                                            void * t246 = GM_LD(void *, ((uint8_t *)t245));
                                            gm_tl874t2atnlh3 t247 = gm_l4;
                                            void * t248 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t246, t247);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.ret = t248;
                                            goto gm_fret;
                                        }
                                        void * t249 = gm_l0;
                                        gm_t88rcucg1b7uk t250 = gm_l24;
                                        gm_b16_8 t251 = gm_l2;
                                        gm_tl874t2atnlh3 t252 = gm_l4;
                                        void * t253 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t249, t250, t251, t252);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t253;
                                        goto gm_fret;
                                    }
gm_lb35:;
                                }
gm_lb34:;
                                uint8_t t254 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                                uint8_t t255 = (uint8_t)((t254 == ((uint8_t)2ULL)));
                                gm_t930q5l38hdap t256 = GM_LD(gm_t930q5l38hdap, ((uint8_t *)&F.l22));
                                gm_t930q5l38hdap t257;
                                memset(&t257, 0, sizeof t257);
                                GM_ST(uint8_t, ((uint8_t *)&t257 + 0), ((uint8_t)2ULL));
                                if (t255) {
                                    void * t258 = gm_l0;
                                    void * t259 = gm_l1;
                                    gm_tl874t2atnlh3 t260 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t259) + 72));
                                    gm_b16_8 t261 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                    gm_tnpifv20irnph t262;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t262 + 0), t261);
                                    gm_f_format_q8r21un((void *)&gm_l48, (void *)NULL, cx, NULL, t262);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t263 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                    if (!t263) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 8, gm_srch6h8rdicu443, 57, 43);
                                    gm_b16_8 t264 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                                    gm_b16_8 t265 = GM_LD(gm_b16_8, ((uint8_t *)&t264));
                                    gm_b0_1 t266 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t258, t260, t265);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t267 = gm_l0;
                                    void * t268 = GM_LD(void *, ((uint8_t *)t267));
                                    gm_tl874t2atnlh3 t269 = gm_l4;
                                    void * t270 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t268, t269);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t270;
                                    goto gm_fret;
                                }
                                void * t271 = gm_l0;
                                void * t272 = gm_l0;
                                void * t273 = gm_l1;
                                gm_tkn42u7na3t81 t274;
                                memset(&t274, 0, sizeof t274);
                                void * t275 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t272, t273, t274);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t276 = gm_l2;
                                gm_tl874t2atnlh3 t277 = gm_l4;
                                void * t278 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t271, t275, t276, t277);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t278;
                                goto gm_fret;
                            }
gm_lb31:;
                        }
gm_lb30:;
                    }
                    {
                        void * t279 = gm_l0;
                        void * t280 = GM_LD(void *, ((uint8_t *)t279));
                        gm_b16_8 t281 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                        gm_tkn42u7na3t81 t282 = gm_f_type_param_3av4r8i(t280, t281);
                        gm_l50 = t282;
                        {
                            {
                                uint8_t t283 = (uint8_t)((((uint8_t *)&gm_l50))[4] != 0);
                                if (t283) {
                                    goto gm_lb37;
                                }
                                gm_l49 = ((uint8_t)0);
                                goto gm_lb36;
                            }
gm_lb37:;
                            {
                                gm_l49 = ((uint8_t)1);
                                goto gm_lb36;
                            }
gm_lb38:;
                            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 62, 16);
                        }
gm_lb36:;
                        uint8_t t284 = gm_l49;
                        if (t284) {
                            gm_b16_8 t285 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                            gm_l25 = t285;
                            void * t286 = gm_l0;
                            void * t287 = gm_l1;
                            gm_tl874t2atnlh3 t288 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t287) + 72));
                            gm_b16_8 t289 = gm_l25;
                            gm_b16_8 t290 = gm_l25;
                            gm_b16_8 t291 = gm_l25;
                            gm_b16_8 t292 = gm_l25;
                            gm_t0uuhaljsf410 t293;
                            GM_ST(gm_b16_8, ((uint8_t *)&t293 + 0), t289);
                            GM_ST(gm_b16_8, ((uint8_t *)&t293 + 16), t290);
                            GM_ST(gm_b16_8, ((uint8_t *)&t293 + 32), t291);
                            GM_ST(gm_b16_8, ((uint8_t *)&t293 + 48), t292);
                            gm_f_format_9ijbt7j((void *)&gm_l51, (void *)NULL, cx, NULL, &t293);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t294 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                            if (!t294) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 8, gm_srch6h8rdicu443, 64, 39);
                            gm_b16_8 t295 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                            gm_b16_8 t296 = GM_LD(gm_b16_8, ((uint8_t *)&t295));
                            gm_b0_1 t297 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t286, t288, t296);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t298 = gm_l0;
                            void * t299 = GM_LD(void *, ((uint8_t *)t298));
                            gm_tl874t2atnlh3 t300 = gm_l4;
                            void * t301 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t299, t300);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t301;
                            goto gm_fret;
                        }
                        {
                            {
                                void * t302 = gm_l0;
                                void * t303 = GM_LD(void *, ((uint8_t *)t302));
                                void * t304 = gm_l0;
                                uint64_t t305 = GM_LD(uint64_t, (((uint8_t *)t304) + 8));
                                gm_b16_8 t306 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                gm_tf5srh4mp4b3i t307 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t303, t305, t306);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l52 = t307;
                                uint8_t t308 = (uint8_t)((((uint8_t *)&gm_l52))[8] != 0);
                                if (!t308) {
                                    goto gm_lb40;
                                }
                                uint64_t t309 = GM_LD(uint64_t, ((uint8_t *)&gm_l52));
                                gm_l26 = t309;
                                void * t310 = gm_l0;
                                void * t311 = GM_LD(void *, ((uint8_t *)t310));
                                uint64_t t312 = gm_l26;
                                gm_tl874t2atnlh3 t313 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l21) + 16));
                                gm_b0_1 t314 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t311, t312, t313);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t315 = gm_l0;
                                uint64_t t316 = gm_l26;
                                gm_b16_8 t317 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                void * t318 = gm_l1;
                                gm_b16_8 t319 = gm_l2;
                                gm_tl874t2atnlh3 t320 = gm_l4;
                                void * t321 = gm_f_call_decl_sj6f05u((void *)NULL, cx, NULL, t315, t316, t317, t318, t319, t320);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t321;
                                goto gm_fret;
                            }
gm_lb40:;
                        }
gm_lb39:;
                    }
                    {
                        {
                            {
                                gm_b16_8 t322 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                gm_b16_8 t323;
                                GM_ST(void *, (uint8_t *)&t323, (void *)(gm_s2r7keqsravo0));
                                GM_ST(uintptr_t, (uint8_t *)&t323 + 8, (uintptr_t)(5));
                                uint8_t t324 = (gm_eq0iqiubfm7e(((uint8_t *)&t322), ((uint8_t *)&t323)));
                                if (!t324) {
                                    goto gm_lb42;
                                }
                                void * t325 = gm_l0;
                                gm_b16_8 t326 = gm_l2;
                                gm_tl874t2atnlh3 t327 = gm_l4;
                                void * t328 = gm_f_print_bhebp9d((void *)NULL, cx, NULL, t325, ((uint8_t)1), t326, t327);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t328;
                                goto gm_fret;
                            }
gm_lb42:;
                            {
                                gm_b16_8 t329 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                gm_b16_8 t330;
                                GM_ST(void *, (uint8_t *)&t330, (void *)(gm_so4gnucc3fl4v));
                                GM_ST(uintptr_t, (uint8_t *)&t330 + 8, (uintptr_t)(6));
                                uint8_t t331 = (gm_eq0iqiubfm7e(((uint8_t *)&t329), ((uint8_t *)&t330)));
                                if (!t331) {
                                    goto gm_lb43;
                                }
                                void * t332 = gm_l0;
                                gm_b16_8 t333 = gm_l2;
                                gm_tl874t2atnlh3 t334 = gm_l4;
                                void * t335 = gm_f_print_bhebp9d((void *)NULL, cx, NULL, t332, ((uint8_t)0), t333, t334);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t335;
                                goto gm_fret;
                            }
gm_lb43:;
                            {
                                gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                gm_b16_8 t337;
                                GM_ST(void *, (uint8_t *)&t337, (void *)(gm_sinmpfqtbo9in));
                                GM_ST(uintptr_t, (uint8_t *)&t337 + 8, (uintptr_t)(4));
                                uint8_t t338 = (gm_eq0iqiubfm7e(((uint8_t *)&t336), ((uint8_t *)&t337)));
                                if (!t338) {
                                    goto gm_lb44;
                                }
                                void * t339 = gm_l0;
                                gm_b16_8 t340 = gm_l2;
                                gm_tl874t2atnlh3 t341 = gm_l4;
                                void * t342 = gm_f_trap_t21p26s((void *)NULL, cx, NULL, t339, t340, t341);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t342;
                                goto gm_fret;
                            }
gm_lb44:;
                            {
                                gm_b16_8 t343 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                gm_b16_8 t344;
                                GM_ST(void *, (uint8_t *)&t344, (void *)(gm_s9ur7sj9r0g63));
                                GM_ST(uintptr_t, (uint8_t *)&t344 + 8, (uintptr_t)(6));
                                uint8_t t345 = (gm_eq0iqiubfm7e(((uint8_t *)&t343), ((uint8_t *)&t344)));
                                if (!t345) {
                                    goto gm_lb45;
                                }
                                void * t346 = gm_l0;
                                gm_b16_8 t347 = gm_l2;
                                gm_tl874t2atnlh3 t348 = gm_l4;
                                void * t349 = gm_f_assert__dllohqd((void *)NULL, cx, NULL, t346, t347, t348);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t349;
                                goto gm_fret;
                            }
gm_lb45:;
                            {
                                gm_b16_8 t350 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                gm_b16_8 t351;
                                GM_ST(void *, (uint8_t *)&t351, (void *)(gm_s3231a310mran));
                                GM_ST(uintptr_t, (uint8_t *)&t351 + 8, (uintptr_t)(9));
                                uint8_t t352 = (gm_eq0iqiubfm7e(((uint8_t *)&t350), ((uint8_t *)&t351)));
                                if (!t352) {
                                    goto gm_lb46;
                                }
                                void * t353 = gm_l0;
                                gm_b16_8 t354 = gm_l2;
                                gm_tl874t2atnlh3 t355 = gm_l4;
                                void * t356 = gm_f_expect_eq_iu4ojmi((void *)NULL, cx, NULL, t353, t354, t355);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t356;
                                goto gm_fret;
                            }
gm_lb46:;
                            {
                                goto gm_lb41;
                            }
gm_lb47:;
                            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 71, 13);
                        }
gm_lb41:;
                        {
                            {
                                void * t357 = gm_l0;
                                void * t358 = GM_LD(void *, ((uint8_t *)t357));
                                gm_b16_8 t359 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                gm_tkn42u7na3t81 t360 = gm_f_builtin_type_64h714c((void *)NULL, cx, NULL, t358, t359);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l53 = t360;
                                uint8_t t361 = (uint8_t)((((uint8_t *)&gm_l53))[4] != 0);
                                if (!t361) {
                                    goto gm_lb49;
                                }
                                gm_t88rcucg1b7uk t362 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l53));
                                gm_l27 = t362;
                                void * t363 = gm_l0;
                                gm_t88rcucg1b7uk t364 = gm_l27;
                                gm_b16_8 t365 = gm_l2;
                                gm_tl874t2atnlh3 t366 = gm_l4;
                                void * t367 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t363, t364, t365, t366);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t367;
                                goto gm_fret;
                            }
gm_lb49:;
                        }
gm_lb48:;
                    }
                    {
                        {
                            {
                                gm_b16_8 t368 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                                gm_b16_8 t369;
                                GM_ST(void *, (uint8_t *)&t369, (void *)(gm_s4g2k7ups3ui6));
                                GM_ST(uintptr_t, (uint8_t *)&t369 + 8, (uintptr_t)(5));
                                uint8_t t370 = (gm_eq0iqiubfm7e(((uint8_t *)&t368), ((uint8_t *)&t369)));
                                if (!t370) {
                                    goto gm_lb51;
                                }
                                void * t371 = gm_l0;
                                void * t372 = GM_LD(void *, ((uint8_t *)t371));
                                uint8_t t373 = (uint8_t)(((((uint8_t *)t372) + 2076))[4] != 0);
                                if (!t373) {
                                    goto gm_lb51;
                                }
                                uint32_t t374 = GM_LD(uint32_t, (((uint8_t *)t372) + 2076));
                                gm_l28 = t374;
                                void * t375 = gm_l0;
                                uint32_t t376 = gm_l28;
                                gm_b16_8 t377 = gm_l2;
                                gm_tl874t2atnlh3 t378 = gm_l4;
                                void * t379 = gm_f_perform_op_1nih4r3((void *)NULL, cx, NULL, t375, t376, ((uint32_t)0ULL), t377, t378);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t379;
                                goto gm_fret;
                            }
gm_lb51:;
                        }
gm_lb50:;
                        void * t380 = gm_l0;
                        void * t381 = gm_l0;
                        void * t382 = gm_l1;
                        gm_tkn42u7na3t81 t383;
                        memset(&t383, 0, sizeof t383);
                        void * t384 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t381, t382, t383);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t385 = gm_l2;
                        gm_tl874t2atnlh3 t386 = gm_l4;
                        void * t387 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t380, t384, t385, t386);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t387;
                        goto gm_fret;
                    }
                }
gm_lb29:;
            }
            {
                {
                    uint8_t t388 = GM_LD(uint8_t, ((((uint8_t *)t136) + 8) + 0));
                    uint8_t t389 = (uint8_t)(t388 == ((uint8_t)19ULL));
                    if (!t389) {
                        goto gm_lb52;
                    }
                    {
                        {
                            void * t390 = gm_l0;
                            void * t391 = gm_l1;
                            gm_tkn42u7na3t81 t392 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t390, t391);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l54 = t392;
                            uint8_t t393 = (uint8_t)((((uint8_t *)&gm_l54))[4] != 0);
                            if (!t393) {
                                goto gm_lb54;
                            }
                            gm_t88rcucg1b7uk t394 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l54));
                            gm_l29 = t394;
                            void * t395 = gm_l0;
                            gm_t88rcucg1b7uk t396 = gm_l29;
                            gm_b16_8 t397 = gm_l2;
                            gm_tl874t2atnlh3 t398 = gm_l4;
                            void * t399 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t395, t396, t397, t398);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t399;
                            goto gm_fret;
                        }
gm_lb54:;
                    }
gm_lb53:;
                    {
                        {
                            void * t400 = gm_l0;
                            void * t401 = GM_LD(void *, ((uint8_t *)t400));
                            void * t402 = gm_l0;
                            uint64_t t403 = GM_LD(uint64_t, (((uint8_t *)t402) + 8));
                            void * t404 = gm_l1;
                            uint8_t t405 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t401, t403, t404);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (!t405) {
                                goto gm_lb56;
                            }
                            void * t406 = gm_l0;
                            void * t407 = GM_LD(void *, ((uint8_t *)t406));
                            void * t408 = (void *)(((uint8_t *)t407) + 8);
                            void * t409 = gm_l0;
                            void * t410 = gm_l1;
                            gm_t88rcucg1b7uk t411 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t409, t410);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t412 = gm_f_is_error_8k27gd1(t408, GM_CONV(gm_csguankko4lmhd, t411));
                            if (!t412) {
                                goto gm_lb56;
                            }
                            void * t413 = gm_l0;
                            void * t414 = GM_LD(void *, ((uint8_t *)t413));
                            gm_tl874t2atnlh3 t415 = gm_l4;
                            void * t416 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t414, t415);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t416;
                            goto gm_fret;
                        }
gm_lb56:;
                    }
gm_lb55:;
                    void * t417 = gm_l0;
                    void * t418 = gm_l0;
                    void * t419 = gm_l1;
                    gm_tkn42u7na3t81 t420;
                    memset(&t420, 0, sizeof t420);
                    void * t421 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t418, t419, t420);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t422 = gm_l2;
                    gm_tl874t2atnlh3 t423 = gm_l4;
                    void * t424 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t417, t421, t422, t423);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t424;
                    goto gm_fret;
                }
gm_lb52:;
                {
                    uint8_t t425 = GM_LD(uint8_t, ((((uint8_t *)t136) + 8) + 0));
                    uint8_t t426 = (uint8_t)(t425 == ((uint8_t)10ULL));
                    if (!t426) {
                        goto gm_lb57;
                    }
                    gm_b16_8 t427 = GM_LD(gm_b16_8, ((((uint8_t *)t136) + 8) + 8));
                    gm_l30 = t427;
                    {
                        {
                            uint64_t t428 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                            uint8_t t429 = (t428 > ((uint64_t)0ULL));
                            if (!t429) {
                                goto gm_lb59;
                            }
                            void * t430 = gm_l0;
                            void * t431 = GM_LD(void *, ((uint8_t *)t430));
                            void * t432 = gm_l0;
                            uint64_t t433 = GM_LD(uint64_t, (((uint8_t *)t432) + 8));
                            void * t434 = gm_l1;
                            uint8_t t435 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t431, t433, t434);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (!t435) {
                                goto gm_lb59;
                            }
                            void * t436 = gm_l0;
                            void * t437 = gm_l1;
                            gm_t88rcucg1b7uk t438 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t436, t437);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l31 = t438;
                            void * t439 = gm_l0;
                            void * t440 = GM_LD(void *, ((uint8_t *)t439));
                            void * t441 = (void *)(((uint8_t *)t440) + 8);
                            gm_t88rcucg1b7uk t442 = gm_l31;
                            uint8_t t443 = gm_f_is_error_8k27gd1(t441, GM_CONV(gm_csguankko4lmhd, t442));
                            if (t443) {
                                void * t444 = gm_l0;
                                void * t445 = GM_LD(void *, ((uint8_t *)t444));
                                gm_tl874t2atnlh3 t446 = gm_l4;
                                void * t447 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t445, t446);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t447;
                                goto gm_fret;
                            }
                            void * t448 = gm_l0;
                            gm_t88rcucg1b7uk t449 = gm_l31;
                            gm_b16_8 t450 = gm_l2;
                            gm_tl874t2atnlh3 t451 = gm_l4;
                            void * t452 = gm_f_conversion_2qjq323((void *)NULL, cx, NULL, t448, t449, t450, t451);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t452;
                            goto gm_fret;
                        }
gm_lb59:;
                    }
gm_lb58:;
                    void * t453 = gm_l0;
                    void * t454 = gm_l0;
                    void * t455 = gm_l1;
                    gm_tkn42u7na3t81 t456;
                    memset(&t456, 0, sizeof t456);
                    void * t457 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t454, t455, t456);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t458 = gm_l2;
                    gm_tl874t2atnlh3 t459 = gm_l4;
                    void * t460 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t453, t457, t458, t459);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t460;
                    goto gm_fret;
                }
gm_lb57:;
            }
            {
                {
                    void * t461 = gm_l0;
                    void * t462 = gm_l0;
                    void * t463 = gm_l1;
                    gm_tkn42u7na3t81 t464;
                    memset(&t464, 0, sizeof t464);
                    void * t465 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t462, t463, t464);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t466 = gm_l2;
                    gm_tl874t2atnlh3 t467 = gm_l4;
                    void * t468 = gm_f_call_value_t2nv21i((void *)NULL, cx, NULL, t461, t465, t466, t467);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t468;
                    goto gm_fret;
                }
gm_lb60:;
                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 25, 5);
            }
        }
gm_lb22:;
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
__attribute__((aligned(4))) void * gm_f_call_value_t2nv21i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_call_value_t2nv21i F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_t88rcucg1b7uk gm_l11; /* p */
    gm_t6j6smj7qoctu gm_l12; /* bound */
    void * gm_l13; /* callee */
    void * gm_l15; /* call */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_trcmmd12cefb1 gm_l18; /* st */
    GM_STACK_CHECK(&F, 1494u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tddtjfggdat0d gm_l19;
    gm_t53at80pfdus3 gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_ttu09lij36f8c gm_l23;
    gm_t6j6smj7qoctu gm_l24;
    gm_tftb1odc739t5 gm_l25;
    void * t18 = gm_l0;
    void * t19 = (void *)(((uint8_t *)t18) + 96);
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    void * t22 = gm_l1;
    gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t22));
    gm_t88rcucg1b7uk t24 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t19, t21, t23);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t24;
    void * t25 = gm_l0;
    void * t26 = GM_LD(void *, ((uint8_t *)t25));
    void * t27 = (void *)(((uint8_t *)t26) + 8);
    gm_t88rcucg1b7uk t28 = gm_l4;
    gm_f_kind_jcnt4hm((void *)&gm_l20, t27, GM_CONV(gm_csguankko4lmhd, t28));
    {
        {
            {
                {
                    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                    uint8_t t30 = (uint8_t)(t29 == ((uint8_t)16ULL));
                    if (!t30) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l20) + 8), 48);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)15ULL));
                    if (!t32) {
                        goto gm_lb5;
                    }
                    memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l20) + 8), 48);
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            memmove(((uint8_t *)&gm_l19), ((uint8_t *)&F.l5), 48);
            goto gm_lb1;
        }
gm_lb2:;
        {
            void * t33 = gm_l0;
            void * t34 = GM_LD(void *, ((uint8_t *)t33));
            void * t35 = (void *)(((uint8_t *)t34) + 8);
            gm_t88rcucg1b7uk t36 = gm_l4;
            uint8_t t37 = gm_f_is_error_8k27gd1(t35, GM_CONV(gm_csguankko4lmhd, t36));
            uint8_t t38 = (uint8_t)!t37;
            if (t38) {
                void * t39 = gm_l0;
                void * t40 = gm_l1;
                gm_tl874t2atnlh3 t41 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t40) + 72));
                void * t42 = gm_l0;
                gm_t88rcucg1b7uk t43 = gm_l4;
                gm_b16_8 t44 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t42, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t45;
                GM_ST(gm_b16_8, ((uint8_t *)&t45 + 0), t44);
                gm_f_format_jaikcua((void *)&gm_l21, (void *)NULL, cx, NULL, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_srch6h8rdicu443, 110, 60);
                gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
                gm_b0_1 t49 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t39, t41, t48);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t50 = gm_l0;
            void * t51 = GM_LD(void *, ((uint8_t *)t50));
            gm_tl874t2atnlh3 t52 = gm_l3;
            void * t53 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t53;
            goto gm_fret;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 107, 12);
    }
gm_lb1:;
    memmove(((uint8_t *)&F.l7), ((uint8_t *)&gm_l19), 48);
    gm_trqfvmjtt4sij t54;
    gm_f_list_af74s0q((void *)&t54, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l8 = t54;
    gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
    gm_l9 = t55;
    gm_l10 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t56 = gm_l10;
            uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            uint8_t t58 = (t56 < t57);
            if (!t58) {
                goto gm_lb7;
            }
            {
                void * t59 = GM_LD(void *, ((uint8_t *)&gm_l9));
                uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint64_t t61 = gm_l10;
                {
                    {
                        gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t59) + (uintptr_t)t61 * 4));
                        gm_l11 = t62;
                        void * t63 = (void *)((uint8_t *)&F.l8);
                        gm_b16_8 t64;
                        GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sfdbfrv46lihd));
                        GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(1));
                        gm_t88rcucg1b7uk t65 = gm_l11;
                        gm_tl874t2atnlh3 t66 = gm_l3;
                        gm_t9805qtonbsag t67;
                        memset(&t67, 0, sizeof t67);
                        gm_t5v0c8ia6a5mo t68;
                        GM_ST(uint8_t, (uint8_t *)&t68 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t68 + 50, 0);
                        GM_ST(uint32_t, (uint8_t *)&t68 + 52, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t64);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t68 + 16), t65);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t68 + 20), t66);
                        GM_ST(void *, ((uint8_t *)&t68 + 32), ((void *)0));
                        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t68 + 40), t67);
                        GM_ST(uint8_t, ((uint8_t *)&t68 + 48), ((uint8_t)0));
                        gm_tftb1odc739t5 t69 = gm_f_push_o8u01sh(t63, &t68);
                        gm_l22 = t69;
                        uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                        if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srch6h8rdicu443, 115, 27);
                        goto gm_lb10;
                    }
gm_lb11:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 115, 5);
                }
gm_lb10:;
            }
gm_lb9:;
            uint64_t t71 = gm_l10;
            uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
            uint64_t t74 = r72;
            gm_l10 = t74;
        }
    }
gm_lb7:;
    void * t75 = gm_l0;
    gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
    gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
    void * t78 = gm_l0;
    uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)t78) + 8));
    gm_b16_8 t80 = gm_l2;
    gm_tl874t2atnlh3 t81 = gm_l3;
    gm_b16_8 t82;
    GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sq0dqiq83b924));
    GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(8));
    gm_f_bind_args_dnih8ih((void *)&gm_l23, (void *)NULL, cx, NULL, t75, t77, t79, t80, t81, t82);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t83 = (uint8_t)((((uint8_t *)&gm_l23))[32] != 0);
    if (t83) {
        memmove(((uint8_t *)&gm_l24), ((uint8_t *)&gm_l23), 32);
    } else {
        void * t84 = gm_l0;
        void * t85 = GM_LD(void *, ((uint8_t *)t84));
        gm_tl874t2atnlh3 t86 = gm_l3;
        void * t87 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t85, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t87;
        goto gm_fret;
    }
    memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l24), 32);
    void * t88 = gm_l0;
    void * t89 = (void *)(((uint8_t *)&F.l7) + 24);
    gm_tl874t2atnlh3 t90 = gm_l3;
    gm_b0_1 t91 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t88, t89, t90);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t92 = gm_l1;
    gm_l13 = t92;
    gm_trqfvmjtt4sij t93;
    gm_f_list_ir0h3tc_2((void *)&t93, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l14 = t93;
    uint64_t t94 = GM_LD(uint64_t, ((((uint8_t *)&gm_l12) + 16) + 8));
    uint8_t t95 = (t94 > ((uint64_t)0ULL));
    if (t95) {
        void * t96 = gm_l0;
        void * t97 = gm_l1;
        void * t98 = (void *)((uint8_t *)&F.l14);
        void * t99 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t96, t97, t98);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t99;
    }
    gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
    void * t101 = gm_l13;
    gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
    gm_tsth9qu1dv0g3 t103;
    GM_ST(uint8_t, (uint8_t *)&t103 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t103 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t103 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t103 + 24, 0);
    GM_ST(uint64_t, (uint8_t *)&t103 + 32, 0);
    GM_ST(uint8_t, ((uint8_t *)&t103 + 0), ((uint8_t)2ULL));
    GM_ST(void *, ((uint8_t *)&t103 + 8), t101);
    gm_to76jli3bqddq t104;
    GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t104 + 0), t103);
    GM_ST(gm_b16_8, ((uint8_t *)&t104 + 40), t102);
    gm_tl874t2atnlh3 t105 = gm_l3;
    gm_t2b6tm8c0vgo1 t106;
    GM_ST(uint8_t, (uint8_t *)&t106 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t106 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t106 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)34ULL));
    GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t106 + 8), t104);
    void * t107 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t100, &t106, t105);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l15 = t107;
    uint64_t t108 = GM_LD(uint64_t, ((((uint8_t *)&gm_l12) + 16) + 8));
    uint8_t t109 = ((t108 == ((uint64_t)0ULL)));
    if (t109) {
        void * t110 = gm_l15;
        F.ret = t110;
        goto gm_fret;
    }
    gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
    gm_l16 = t111;
    gm_l17 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t112 = gm_l17;
            uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
            uint8_t t114 = (t112 < t113);
            if (!t114) {
                goto gm_lb12;
            }
            {
                void * t115 = GM_LD(void *, ((uint8_t *)&gm_l16));
                uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint64_t t117 = gm_l17;
                {
                    {
                        memmove(((uint8_t *)&gm_l18), (((uint8_t *)t115) + (uintptr_t)t117 * 32), 32);
                        void * t118 = (void *)((uint8_t *)&F.l14);
                        gm_tftb1odc739t5 t119 = gm_f_push_d6c76ud_2(t118, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l18)));
                        gm_l25 = t119;
                        uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                        if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srch6h8rdicu443, 123, 27);
                        goto gm_lb15;
                    }
gm_lb16:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 123, 5);
                }
gm_lb15:;
            }
gm_lb14:;
            uint64_t t121 = gm_l17;
            uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
            uint64_t t124 = r122;
            gm_l17 = t124;
        }
    }
gm_lb12:;
    void * t125 = gm_l15;
    gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t125));
    gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
    void * t128 = gm_l15;
    gm_tl874t2atnlh3 t129 = gm_l3;
    void * t130 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t126, t127, t128, t129);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t130;
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
inline __attribute__((aligned(4))) void * gm_f_construct_optional_cffo1ol(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_construct_optional_cffo1ol F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* inner */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l6; /* v */
    GM_STACK_CHECK(&F, 704u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l7;
    uint8_t gm_l8;
    gm_tabb710d9mq2e gm_l9;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t8 = (!(t7 == ((uint64_t)1ULL)));
    if (t8) {
        gm_l7 = ((uint8_t)1);
    } else {
        void * t9 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t10, gm_srch6h8rdicu443, 483, 29);
        {
            {
                uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t9) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                uint8_t t12 = (uint8_t)(t11 == ((uint8_t)0ULL));
                if (!t12) {
                    goto gm_lb2;
                }
                gm_l8 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l8 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 483, 29);
        }
gm_lb1:;
        uint8_t t13 = gm_l8;
        gm_l7 = t13;
    }
    uint8_t t14 = gm_l7;
    if (t14) {
        void * t15 = gm_l0;
        gm_tl874t2atnlh3 t16 = gm_l4;
        void * t17 = gm_l0;
        gm_t88rcucg1b7uk t18 = gm_l1;
        gm_b16_8 t19 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t20;
        GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t19);
        gm_f_format_mq9dnpb((void *)&gm_l9, (void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srch6h8rdicu443, 484, 24);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        gm_b0_1 t24 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t15, t16, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t25 = gm_l0;
        void * t26 = GM_LD(void *, ((uint8_t *)t25));
        gm_tl874t2atnlh3 t27 = gm_l4;
        void * t28 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t28;
        goto gm_fret;
    }
    {
        {
            void * t29 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t30, gm_srch6h8rdicu443, 487, 23);
            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t29) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)0ULL));
            if (!t32) {
                goto gm_lb5;
            }
            memmove(((uint8_t *)&F.l5), ((((uint8_t *)t29) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 8) + 0));
            uint8_t t34 = (uint8_t)((t33 == ((uint8_t)4ULL)));
            gm_t2gtbh39ptekd t35 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)&F.l5) + 8));
            gm_t2gtbh39ptekd t36;
            memset(&t36, 0, sizeof t36);
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)4ULL));
            if (t34) {
                gm_t88rcucg1b7uk t37 = gm_l1;
                gm_tl874t2atnlh3 t38 = gm_l4;
                gm_t2b6tm8c0vgo1 t39;
                memset(&t39, 0, sizeof t39);
                GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)7ULL));
                void * t40 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t37, &t39, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t40;
                goto gm_fret;
            }
            void * t41 = gm_l0;
            void * t42 = gm_l0;
            void * t43 = (void *)((uint8_t *)&F.l5);
            gm_t88rcucg1b7uk t44 = gm_l2;
            gm_tkn42u7na3t81 t45;
            GM_ST(uint8_t, (uint8_t *)&t45 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t45 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t45), t44);
            ((uint8_t *)&t45)[4] = 1;
            void * t46 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t42, t43, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t47 = gm_l2;
            void * t48 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t41, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t48;
            gm_t88rcucg1b7uk t49 = gm_l1;
            void * t50 = gm_l6;
            gm_tmn7n0n9raou5 t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)2ULL));
            gm_t8jsc4n9vp2ib t52;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t52 + 0), t51);
            GM_ST(void *, ((uint8_t *)&t52 + 8), t50);
            gm_tl874t2atnlh3 t53 = gm_l4;
            gm_t2b6tm8c0vgo1 t54;
            memset(&t54, 0, sizeof t54);
            GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t54 + 8), t52);
            void * t55 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t49, &t54, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t55;
            goto gm_fret;
        }
gm_lb5:;
        void * t56 = gm_l0;
        void * t57 = GM_LD(void *, ((uint8_t *)t56));
        gm_tl874t2atnlh3 t58 = gm_l4;
        void * t59 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t57, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t59;
        goto gm_fret;
    }
gm_lb4:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_conversion_2qjq323(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_conversion_2qjq323 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    uint32_t gm_l5; /* id */
    uint8_t gm_l7; /* one_positional */
    gm_b16_8 gm_l8; /* fields */
    gm_t08olq97rscn3 gm_l9; /* n */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    void * gm_l16; /* v */
    gm_b16_8 gm_l18; /* elems */
    gm_b16_8 gm_l19; /* names */
    uint64_t gm_l20; /* len */
    gm_t88rcucg1b7uk gm_l21; /* elem */
    gm_tnnjeerolos0a gm_l22; /* kind */
    gm_t88rcucg1b7uk gm_l23; /* index */
    gm_t88rcucg1b7uk gm_l24; /* inner */
    gm_t88rcucg1b7uk gm_l25; /* elem */
    gm_t88rcucg1b7uk gm_l27; /* u */
    void * gm_l28; /* v */
    void * gm_l29; /* v */
    gm_t88rcucg1b7uk gm_l30; /* vt */
    GM_STACK_CHECK(&F, 3901u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l31;
    uint8_t gm_l32;
    uint8_t gm_l33;
    uint8_t gm_l34;
    uint8_t gm_l35;
    uint8_t gm_l36;
    uint8_t gm_l37;
    gm_tabb710d9mq2e gm_l38;
    uint8_t gm_l39;
    gm_t0mglksrkfa5e gm_l40;
    gm_tabb710d9mq2e gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_t53at80pfdus3 gm_l43;
    uint8_t gm_l44;
    uint8_t gm_l45;
    gm_tabb710d9mq2e gm_l46;
    gm_tkn42u7na3t81 gm_l47;
    uint8_t gm_l48;
    gm_taari1s0m51a7 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tkn42u7na3t81 gm_l51;
    uint8_t gm_l52;
    gm_taari1s0m51a7 gm_l53;
    uint8_t gm_l54;
    uint8_t gm_l55;
    uint8_t gm_l56;
    gm_t53at80pfdus3 gm_l57;
    uint8_t gm_l58;
    gm_t9qkh0rsooc8d gm_l59;
    uint8_t gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tabb710d9mq2e gm_l62;
    uint8_t gm_l63;
    gm_th2lqbo77pvsc gm_l64;
    gm_tftb1odc739t5 gm_l65;
    {
        void * t65 = gm_l0;
        void * t66 = GM_LD(void *, ((uint8_t *)t65));
        void * t67 = (void *)(((uint8_t *)t66) + 8);
        gm_l4 = t67;
        {
            {
                void * t68 = gm_l4;
                gm_t88rcucg1b7uk t69 = gm_l1;
                gm_f_kind_jcnt4hm((void *)&gm_l31, t68, GM_CONV(gm_csguankko4lmhd, t69));
                uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l31) + 0));
                uint8_t t71 = (uint8_t)(t70 == ((uint8_t)17ULL));
                if (!t71) {
                    goto gm_lb2;
                }
                uint32_t t72 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 8));
                gm_l5 = t72;
                void * t73 = gm_l0;
                void * t74 = GM_LD(void *, ((uint8_t *)t73));
                void * t75 = GM_LD(void *, (((uint8_t *)t74) + 1088));
                uint64_t t76 = GM_LD(uint64_t, ((((uint8_t *)t74) + 1088) + 8));
                uint32_t t77 = gm_l5;
                uint64_t t78 = (uint64_t)t77;
                if ((uintptr_t)t78 >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)t78, t76, gm_srch6h8rdicu443, 406, 15);
                {
                    {
                        uint8_t t79 = GM_LD(uint8_t, (((((uint8_t *)t75) + (uintptr_t)t78 * 184) + 24) + 0));
                        uint8_t t80 = (uint8_t)(t79 == ((uint8_t)0ULL));
                        if (!t80) {
                            goto gm_lb4;
                        }
                        memmove(((uint8_t *)&F.l6), (((((uint8_t *)t75) + (uintptr_t)t78 * 184) + 24) + 8), 48);
                        uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint8_t t82 = ((t81 == ((uint64_t)1ULL)));
                        if (t82) {
                            void * t83 = GM_LD(void *, ((uint8_t *)&gm_l2));
                            uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t84)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t84, gm_srch6h8rdicu443, 409, 53);
                            {
                                {
                                    uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t83) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                                    uint8_t t86 = (uint8_t)(t85 == ((uint8_t)0ULL));
                                    if (!t86) {
                                        goto gm_lb6;
                                    }
                                    gm_l33 = ((uint8_t)1);
                                    goto gm_lb5;
                                }
gm_lb6:;
                                {
                                    gm_l33 = ((uint8_t)0);
                                    goto gm_lb5;
                                }
gm_lb7:;
                                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 409, 53);
                            }
gm_lb5:;
                            uint8_t t87 = gm_l33;
                            gm_l32 = t87;
                        } else {
                            gm_l32 = ((uint8_t)0);
                        }
                        uint8_t t88 = gm_l32;
                        gm_l7 = t88;
                        uint8_t t89 = gm_l7;
                        if (t89) {
                            {
                                {
                                    uint8_t t90 = (uint8_t)(((((uint8_t *)&F.l6) + 40))[4] != 0);
                                    if (t90) {
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
                                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 410, 44);
                            }
gm_lb8:;
                            uint8_t t91 = gm_l35;
                            gm_l34 = t91;
                        } else {
                            gm_l34 = ((uint8_t)0);
                        }
                        uint8_t t92 = gm_l34;
                        uint8_t t93 = (uint8_t)!t92;
                        if (t93) {
                            void * t94 = gm_l0;
                            gm_t88rcucg1b7uk t95 = gm_l1;
                            gm_b16_8 t96 = gm_l2;
                            gm_tl874t2atnlh3 t97 = gm_l3;
                            void * t98 = gm_f_construct_struct_ve6tb88((void *)NULL, cx, NULL, t94, t95, t96, t97);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t98;
                            goto gm_fret;
                        }
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        uint8_t t99 = GM_LD(uint8_t, (((((uint8_t *)t75) + (uintptr_t)t78 * 184) + 24) + 0));
                        uint8_t t100 = (uint8_t)(t99 == ((uint8_t)2ULL));
                        if (!t100) {
                            goto gm_lb11;
                        }
                        gm_b16_8 t101 = GM_LD(gm_b16_8, (((((uint8_t *)t75) + (uintptr_t)t78 * 184) + 24) + 8));
                        gm_l8 = t101;
                        uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint8_t t103 = (!(t102 == ((uint64_t)1ULL)));
                        if (t103) {
                            gm_l36 = ((uint8_t)1);
                        } else {
                            void * t104 = GM_LD(void *, ((uint8_t *)&gm_l2));
                            uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t105)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t105, gm_srch6h8rdicu443, 414, 41);
                            {
                                {
                                    uint8_t t106 = GM_LD(uint8_t, ((((uint8_t *)t104) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                                    uint8_t t107 = (uint8_t)(t106 == ((uint8_t)1ULL));
                                    if (!t107) {
                                        goto gm_lb13;
                                    }
                                    gm_l37 = ((uint8_t)0);
                                    goto gm_lb12;
                                }
gm_lb13:;
                                {
                                    gm_l37 = ((uint8_t)1);
                                    goto gm_lb12;
                                }
gm_lb14:;
                                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 414, 41);
                            }
gm_lb12:;
                            uint8_t t108 = gm_l37;
                            gm_l36 = t108;
                        }
                        uint8_t t109 = gm_l36;
                        if (t109) {
                            void * t110 = gm_l0;
                            gm_tl874t2atnlh3 t111 = gm_l3;
                            void * t112 = gm_l0;
                            gm_t88rcucg1b7uk t113 = gm_l1;
                            gm_b16_8 t114 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t112, t113);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t115;
                            GM_ST(gm_b16_8, ((uint8_t *)&t115 + 0), t114);
                            gm_f_format_cqv4884((void *)&gm_l38, (void *)NULL, cx, NULL, t115);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t116 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                            if (!t116) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_srch6h8rdicu443, 415, 36);
                            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                            gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                            gm_b0_1 t119 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t110, t111, t118);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t120 = gm_l0;
                            void * t121 = GM_LD(void *, ((uint8_t *)t120));
                            gm_tl874t2atnlh3 t122 = gm_l3;
                            void * t123 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t121, t122);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t123;
                            goto gm_fret;
                        }
                        {
                            {
                                void * t124 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t125)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t125, gm_srch6h8rdicu443, 418, 33);
                                uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t124) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                                uint8_t t127 = (uint8_t)(t126 == ((uint8_t)1ULL));
                                if (!t127) {
                                    goto gm_lb16;
                                }
                                memmove(((uint8_t *)&gm_l9), ((((uint8_t *)t124) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 32);
                                memmove(((uint8_t *)&F.l10), (((((uint8_t *)t124) + (uintptr_t)((uint64_t)0ULL) * 144) + 8) + 32), 88);
                                gm_tnpj2jtv1r8pa t128;
                                memset(&t128, 0, sizeof t128);
                                F.l11 = t128;
                                gm_b16_8 t129 = gm_l8;
                                gm_l12 = t129;
                                gm_l13 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t130 = gm_l13;
                                        uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                        uint8_t t132 = (t130 < t131);
                                        if (!t132) {
                                            goto gm_lb17;
                                        }
                                        {
                                            void * t133 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                            uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                            uint64_t t135 = gm_l13;
                                            {
                                                {
                                                    memmove(((uint8_t *)&F.l14), (((uint8_t *)t133) + (uintptr_t)t135 * 40), 40);
                                                    {
                                                        {
                                                            {
                                                                {
                                                                    uint8_t t136 = (uint8_t)((((uint8_t *)&F.l11))[40] != 0);
                                                                    if (t136) {
                                                                        goto gm_lb25;
                                                                    }
                                                                    gm_l39 = ((uint8_t)1);
                                                                    goto gm_lb24;
                                                                }
gm_lb25:;
                                                                {
                                                                    gm_l39 = ((uint8_t)0);
                                                                    goto gm_lb24;
                                                                }
gm_lb26:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 421, 24);
                                                            }
gm_lb24:;
                                                            uint8_t t137 = gm_l39;
                                                            if (!t137) {
                                                                goto gm_lb23;
                                                            }
                                                            gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                                            gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                                                            uint8_t t140 = (gm_eq0iqiubfm7e(((uint8_t *)&t138), ((uint8_t *)&t139)));
                                                            if (!t140) {
                                                                goto gm_lb23;
                                                            }
                                                            gm_t0mglksrkfa5e t141 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)&F.l14));
                                                            gm_tnpj2jtv1r8pa t142;
                                                            GM_ST(uint8_t, (uint8_t *)&t142 + 41, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t142 + 42, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&t142 + 44, 0);
                                                            GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&t142), t141);
                                                            ((uint8_t *)&t142)[40] = 1;
                                                            F.l11 = t142;
                                                            goto gm_lb22;
                                                        }
gm_lb23:;
                                                    }
gm_lb22:;
                                                    goto gm_lb20;
                                                }
gm_lb21:;
                                                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 420, 17);
                                            }
gm_lb20:;
                                        }
gm_lb19:;
                                        uint64_t t143 = gm_l13;
                                        uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
                                        uint64_t t146 = r144;
                                        gm_l13 = t146;
                                    }
                                }
gm_lb17:;
                                uint8_t t147 = (uint8_t)((((uint8_t *)&F.l11))[40] != 0);
                                if (t147) {
                                    memmove(((uint8_t *)&gm_l40), ((uint8_t *)&F.l11), 40);
                                } else {
                                    void * t148 = gm_l0;
                                    gm_tl874t2atnlh3 t149 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l9) + 16));
                                    void * t150 = gm_l0;
                                    gm_t88rcucg1b7uk t151 = gm_l1;
                                    gm_b16_8 t152 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t150, t151);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                                    gm_t6j6smj7qoctu t154;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t154 + 0), t152);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t154 + 16), t153);
                                    gm_f_format_lc07prd((void *)&gm_l41, (void *)NULL, cx, NULL, &t154);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t155 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                    if (!t155) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_srch6h8rdicu443, 424, 38);
                                    gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
                                    gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                                    gm_b0_1 t158 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t148, t149, t157);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t159 = gm_l0;
                                    void * t160 = GM_LD(void *, ((uint8_t *)t159));
                                    gm_tl874t2atnlh3 t161 = gm_l3;
                                    void * t162 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t160, t161);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t162;
                                    goto gm_fret;
                                }
                                memmove(((uint8_t *)&F.l15), ((uint8_t *)&gm_l40), 40);
                                void * t163 = gm_l0;
                                void * t164 = gm_l0;
                                void * t165 = (void *)((uint8_t *)&F.l10);
                                gm_t88rcucg1b7uk t166 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16));
                                gm_tkn42u7na3t81 t167;
                                GM_ST(uint8_t, (uint8_t *)&t167 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t167 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t167), t166);
                                ((uint8_t *)&t167)[4] = 1;
                                void * t168 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t164, t165, t167);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t88rcucg1b7uk t169 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16));
                                void * t170 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t163, t168, t169);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l16 = t170;
                                gm_trqfvmjtt4sij t171;
                                gm_f_list_vp1djv1_2((void *)&t171, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l17 = t171;
                                void * t172 = (void *)((uint8_t *)&F.l17);
                                void * t173 = gm_l16;
                                gm_tftb1odc739t5 t174 = gm_f_push_d943vgp_2(t172, &(*(gm_tcgrjq9toscke *)((uint8_t *)t173)));
                                gm_l42 = t174;
                                uint8_t t175 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                                if (!t175) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srch6h8rdicu443, 429, 17);
                                gm_t88rcucg1b7uk t176 = gm_l1;
                                gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                                gm_tl874t2atnlh3 t178 = gm_l3;
                                gm_t2b6tm8c0vgo1 t179;
                                memset(&t179, 0, sizeof t179);
                                GM_ST(uint8_t, ((uint8_t *)&t179 + 0), ((uint8_t)35ULL));
                                GM_ST(gm_b16_8, ((uint8_t *)&t179 + 8), t177);
                                void * t180 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t176, &t179, t178);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t180;
                                goto gm_fret;
                            }
gm_lb16:;
                            void * t181 = gm_l0;
                            void * t182 = GM_LD(void *, ((uint8_t *)t181));
                            gm_tl874t2atnlh3 t183 = gm_l3;
                            void * t184 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t182, t183);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t184;
                            goto gm_fret;
                        }
gm_lb15:;
                    }
gm_lb11:;
                    {
                        goto gm_lb3;
                    }
gm_lb27:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 406, 9);
                }
gm_lb3:;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        void * t185 = gm_l4;
        gm_t88rcucg1b7uk t186 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&gm_l43, t185, GM_CONV(gm_csguankko4lmhd, t186));
        {
            {
                uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l43) + 0));
                uint8_t t188 = (uint8_t)(t187 == ((uint8_t)12ULL));
                if (!t188) {
                    goto gm_lb29;
                }
                gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
                gm_l18 = t189;
                gm_b16_8 t190 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l43) + 8) + 16));
                gm_l19 = t190;
                void * t191 = gm_l0;
                gm_t88rcucg1b7uk t192 = gm_l1;
                gm_b16_8 t193 = gm_l18;
                gm_b16_8 t194 = gm_l19;
                gm_b16_8 t195 = gm_l2;
                gm_tl874t2atnlh3 t196 = gm_l3;
                void * t197 = gm_f_construct_tuple_6ee5ejf((void *)NULL, cx, NULL, t191, t192, t193, t194, t195, t196);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t197;
                goto gm_fret;
            }
gm_lb29:;
            {
                uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&gm_l43) + 0));
                uint8_t t199 = (uint8_t)(t198 == ((uint8_t)11ULL));
                if (!t199) {
                    goto gm_lb30;
                }
                uint64_t t200 = GM_LD(uint64_t, (((uint8_t *)&gm_l43) + 8));
                gm_l20 = t200;
                gm_t88rcucg1b7uk t201 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l43) + 8) + 8));
                gm_l21 = t201;
                gm_tnnjeerolos0a t202 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&gm_l43) + 8) + 12));
                gm_l22 = t202;
                {
                    {
                        uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                        uint8_t t204 = (uint8_t)(t203 == ((uint8_t)1ULL));
                        if (!t204) {
                            goto gm_lb32;
                        }
                        gm_t88rcucg1b7uk t205 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 4));
                        gm_l23 = t205;
                        void * t206 = gm_l0;
                        gm_t88rcucg1b7uk t207 = gm_l1;
                        gm_t88rcucg1b7uk t208 = gm_l23;
                        gm_t88rcucg1b7uk t209 = gm_l21;
                        gm_b16_8 t210 = gm_l2;
                        gm_tl874t2atnlh3 t211 = gm_l3;
                        void * t212 = gm_f_construct_enum_array_d7cisuu((void *)NULL, cx, NULL, t206, t207, t208, t209, t210, t211);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t212;
                        goto gm_fret;
                    }
gm_lb32:;
                }
gm_lb31:;
                void * t213 = gm_l0;
                gm_t88rcucg1b7uk t214 = gm_l1;
                gm_tkn42u7na3t81 t215;
                GM_ST(uint8_t, (uint8_t *)&t215 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t215 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t215), t214);
                ((uint8_t *)&t215)[4] = 1;
                uint64_t t216 = gm_l20;
                gm_tf5srh4mp4b3i t217;
                GM_ST(uint8_t, (uint8_t *)&t217 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t217 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t217 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t217), t216);
                ((uint8_t *)&t217)[8] = 1;
                gm_t88rcucg1b7uk t218 = gm_l21;
                gm_b16_8 t219 = gm_l2;
                gm_tl874t2atnlh3 t220 = gm_l3;
                void * t221 = gm_f_construct_array_qkofm98((void *)NULL, cx, NULL, t213, t215, t217, t218, t219, t220);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t221;
                goto gm_fret;
            }
gm_lb30:;
            {
                uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l43) + 0));
                uint8_t t223 = (uint8_t)(t222 == ((uint8_t)13ULL));
                if (!t223) {
                    goto gm_lb33;
                }
                gm_t88rcucg1b7uk t224 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l43) + 8));
                gm_l24 = t224;
                void * t225 = gm_l0;
                gm_t88rcucg1b7uk t226 = gm_l1;
                gm_t88rcucg1b7uk t227 = gm_l24;
                gm_b16_8 t228 = gm_l2;
                gm_tl874t2atnlh3 t229 = gm_l3;
                void * t230 = gm_f_construct_optional_cffo1ol((void *)NULL, cx, NULL, t225, t226, t227, t228, t229);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t230;
                goto gm_fret;
            }
gm_lb33:;
            {
                uint8_t t231 = GM_LD(uint8_t, (((uint8_t *)&gm_l43) + 0));
                uint8_t t232 = (uint8_t)(t231 == ((uint8_t)20ULL));
                if (!t232) {
                    goto gm_lb34;
                }
                gm_t88rcucg1b7uk t233 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l43) + 8) + 4));
                gm_l25 = t233;
                void * t234 = gm_l0;
                gm_t88rcucg1b7uk t235 = gm_l1;
                gm_t88rcucg1b7uk t236 = gm_l25;
                gm_b16_8 t237 = gm_l2;
                gm_tl874t2atnlh3 t238 = gm_l3;
                void * t239 = gm_f_construct_param_array_btk60nl((void *)NULL, cx, NULL, t234, t235, t236, t237, t238);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t239;
                goto gm_fret;
            }
gm_lb34:;
            {
                goto gm_lb28;
            }
gm_lb35:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 435, 5);
        }
gm_lb28:;
    }
    {
        uint64_t t240 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint8_t t241 = (!(t240 == ((uint64_t)1ULL)));
        if (t241) {
            gm_l44 = ((uint8_t)1);
        } else {
            void * t242 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t243 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t243)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t243, gm_srch6h8rdicu443, 445, 29);
            {
                {
                    uint8_t t244 = GM_LD(uint8_t, ((((uint8_t *)t242) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                    uint8_t t245 = (uint8_t)(t244 == ((uint8_t)0ULL));
                    if (!t245) {
                        goto gm_lb37;
                    }
                    gm_l45 = ((uint8_t)0);
                    goto gm_lb36;
                }
gm_lb37:;
                {
                    gm_l45 = ((uint8_t)1);
                    goto gm_lb36;
                }
gm_lb38:;
                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 445, 29);
            }
gm_lb36:;
            uint8_t t246 = gm_l45;
            gm_l44 = t246;
        }
        uint8_t t247 = gm_l44;
        if (t247) {
            void * t248 = gm_l0;
            gm_tl874t2atnlh3 t249 = gm_l3;
            void * t250 = gm_l0;
            gm_t88rcucg1b7uk t251 = gm_l1;
            gm_b16_8 t252 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t250, t251);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t253;
            GM_ST(gm_b16_8, ((uint8_t *)&t253 + 0), t252);
            gm_f_format_458q1b3((void *)&gm_l46, (void *)NULL, cx, NULL, t253);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t254 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
            if (!t254) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 8, gm_srch6h8rdicu443, 446, 24);
            gm_b16_8 t255 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
            gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&t255));
            gm_b0_1 t257 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t248, t249, t256);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t258 = gm_l0;
            void * t259 = GM_LD(void *, ((uint8_t *)t258));
            gm_tl874t2atnlh3 t260 = gm_l3;
            void * t261 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t259, t260);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t261;
            goto gm_fret;
        }
        {
            {
                {
                    {
                        void * t262 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t263 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t263)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t263, gm_srch6h8rdicu443, 449, 23);
                        uint8_t t264 = GM_LD(uint8_t, ((((uint8_t *)t262) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                        uint8_t t265 = (uint8_t)(t264 == ((uint8_t)0ULL));
                        if (!t265) {
                            goto gm_lb40;
                        }
                        memmove(((uint8_t *)&F.l26), ((((uint8_t *)t262) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                        {
                            {
                                void * t266 = gm_l0;
                                void * t267 = GM_LD(void *, ((uint8_t *)t266));
                                gm_t88rcucg1b7uk t268 = gm_l1;
                                gm_tkn42u7na3t81 t269 = gm_f_bits_struct_ty_l1kjtap(t267, GM_CONV(gm_csguankko4lmhd, t268));
                                gm_l47 = t269;
                                uint8_t t270 = (uint8_t)((((uint8_t *)&gm_l47))[4] != 0);
                                if (!t270) {
                                    goto gm_lb42;
                                }
                                gm_t88rcucg1b7uk t271 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l47));
                                gm_l27 = t271;
                                void * t272 = gm_l0;
                                void * t273 = (void *)((uint8_t *)&F.l26);
                                gm_t88rcucg1b7uk t274 = gm_l27;
                                gm_tkn42u7na3t81 t275;
                                GM_ST(uint8_t, (uint8_t *)&t275 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t275 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t275), t274);
                                ((uint8_t *)&t275)[4] = 1;
                                void * t276 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t272, t273, t275);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l28 = t276;
                                void * t277 = gm_l0;
                                void * t278 = (void *)(((uint8_t *)t277) + 96);
                                void * t279 = gm_l4;
                                void * t280 = gm_l28;
                                gm_t88rcucg1b7uk t281 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t280));
                                gm_taari1s0m51a7 t282 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t278, t279, t281);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l49 = t282;
                                {
                                    {
                                        uint8_t t283 = (uint8_t)((((uint8_t *)&gm_l49))[1] != 0);
                                        if (t283) {
                                            goto gm_lb44;
                                        }
                                        gm_l48 = ((uint8_t)0);
                                        goto gm_lb43;
                                    }
gm_lb44:;
                                    {
                                        gm_l48 = ((uint8_t)1);
                                        goto gm_lb43;
                                    }
gm_lb45:;
                                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 453, 12);
                                }
gm_lb43:;
                                uint8_t t284 = gm_l48;
                                if (t284) {
                                    void * t285 = gm_l0;
                                    void * t286 = gm_l28;
                                    gm_t88rcucg1b7uk t287 = gm_l27;
                                    void * t288 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t285, t286, t287);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l28 = t288;
                                }
                                void * t289 = gm_l0;
                                void * t290 = (void *)(((uint8_t *)t289) + 320);
                                void * t291 = gm_l28;
                                gm_t88rcucg1b7uk t292 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t291));
                                gm_t88rcucg1b7uk t293 = gm_l1;
                                gm_tl874t2atnlh3 t294 = gm_l3;
                                gm_thsh8t5u55gps t295;
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t295 + 0), t292);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t295 + 4), t293);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t295 + 8), t294);
                                gm_tpm220j5qhjke t296;
                                GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
                                GM_ST(uint32_t, (uint8_t *)&t296 + 28, 0);
                                GM_ST(uint64_t, (uint8_t *)&t296 + 32, 0);
                                GM_ST(uint64_t, (uint8_t *)&t296 + 40, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t296 + 0), ((uint8_t)1ULL));
                                GM_ST(gm_thsh8t5u55gps, ((uint8_t *)&t296 + 8), t295);
                                gm_tftb1odc739t5 t297 = gm_f_push_7koe9lk(t290, &t296);
                                gm_l50 = t297;
                                uint8_t t298 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                                if (!t298) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srch6h8rdicu443, 455, 9);
                                gm_t88rcucg1b7uk t299 = gm_l1;
                                void * t300 = gm_l28;
                                gm_tvdg94bk7aa22 t301;
                                GM_ST(uint8_t, (uint8_t *)&t301 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t301 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t301 + 4, 0);
                                GM_ST(uint32_t, (uint8_t *)&t301 + 8, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t301 + 0), ((uint8_t)0ULL));
                                gm_b16_8 t302 = gm_f_one_expr_q6kn0hn((void *)NULL, cx, NULL, t300);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tt8o5kebou62r t303;
                                GM_ST(uint32_t, (uint8_t *)&t303 + 12, 0);
                                GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t303 + 0), t301);
                                GM_ST(gm_b16_8, ((uint8_t *)&t303 + 16), t302);
                                gm_tl874t2atnlh3 t304 = gm_l3;
                                gm_t2b6tm8c0vgo1 t305;
                                GM_ST(uint8_t, (uint8_t *)&t305 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t305 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t305 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t305 + 40, 0);
                                GM_ST(uint64_t, (uint8_t *)&t305 + 48, 0);
                                GM_ST(uint64_t, (uint8_t *)&t305 + 56, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t305 + 0), ((uint8_t)33ULL));
                                GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t305 + 8), t303);
                                void * t306 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t299, &t305, t304);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t306;
                                goto gm_fret;
                            }
gm_lb42:;
                        }
gm_lb41:;
                    }
                    {
                        void * t307 = gm_l0;
                        void * t308 = (void *)((uint8_t *)&F.l26);
                        void * t309 = (void *)((uint8_t *)&F.l26);
                        uint8_t t310 = gm_f_is_literal_tree_om58rum(t309);
                        if (t310) {
                            gm_t88rcucg1b7uk t311 = gm_l1;
                            gm_tkn42u7na3t81 t312;
                            GM_ST(uint8_t, (uint8_t *)&t312 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t312 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t312), t311);
                            ((uint8_t *)&t312)[4] = 1;
                            gm_l51 = t312;
                        } else {
                            gm_tkn42u7na3t81 t313;
                            memset(&t313, 0, sizeof t313);
                            gm_l51 = t313;
                        }
                        gm_tkn42u7na3t81 t314 = gm_l51;
                        void * t315 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t307, t308, t314);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l29 = t315;
                        {
                            {
                                void * t316 = gm_l0;
                                void * t317 = (void *)(((uint8_t *)t316) + 96);
                                void * t318 = gm_l4;
                                void * t319 = gm_l29;
                                gm_t88rcucg1b7uk t320 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t319));
                                gm_taari1s0m51a7 t321 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t317, t318, t320);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l53 = t321;
                                {
                                    {
                                        uint8_t t322 = (uint8_t)((((uint8_t *)&gm_l53))[1] != 0);
                                        if (t322) {
                                            goto gm_lb49;
                                        }
                                        gm_l52 = ((uint8_t)0);
                                        goto gm_lb48;
                                    }
gm_lb49:;
                                    {
                                        gm_l52 = ((uint8_t)1);
                                        goto gm_lb48;
                                    }
gm_lb50:;
                                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 460, 8);
                                }
gm_lb48:;
                                uint8_t t323 = gm_l52;
                                if (!t323) {
                                    goto gm_lb47;
                                }
                                void * t324 = gm_l4;
                                gm_t88rcucg1b7uk t325 = gm_l1;
                                uint8_t t326 = gm_f_is_integer_8d4f9ek(t324, GM_CONV(gm_csguankko4lmhd, t325));
                                if (t326) {
                                    gm_l54 = ((uint8_t)1);
                                } else {
                                    void * t327 = gm_l4;
                                    gm_t88rcucg1b7uk t328 = gm_l1;
                                    gm_f_kind_jcnt4hm((void *)&gm_l57, t327, GM_CONV(gm_csguankko4lmhd, t328));
                                    {
                                        {
                                            uint8_t t329 = GM_LD(uint8_t, (((uint8_t *)&gm_l57) + 0));
                                            uint8_t t330 = (uint8_t)(t329 == ((uint8_t)3ULL));
                                            if (!t330) {
                                                goto gm_lb52;
                                            }
                                            gm_l56 = ((uint8_t)1);
                                            goto gm_lb51;
                                        }
gm_lb52:;
                                        {
                                            gm_l56 = ((uint8_t)0);
                                            goto gm_lb51;
                                        }
gm_lb53:;
                                        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 460, 76);
                                    }
gm_lb51:;
                                    uint8_t t331 = gm_l56;
                                    if (t331) {
                                        void * t332 = gm_l29;
                                        uint8_t t333 = gm_f_tir_literal_oh960hv(t332);
                                        gm_l55 = t333;
                                    } else {
                                        gm_l55 = ((uint8_t)0);
                                    }
                                    uint8_t t334 = gm_l55;
                                    gm_l54 = t334;
                                }
                                uint8_t t335 = gm_l54;
                                if (!t335) {
                                    goto gm_lb47;
                                }
                                void * t336 = gm_l0;
                                void * t337 = (void *)(((uint8_t *)t336) + 96);
                                void * t338 = gm_l4;
                                void * t339 = gm_l29;
                                gm_t88rcucg1b7uk t340 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t339));
                                gm_t88rcucg1b7uk t341 = gm_l1;
                                gm_t9qkh0rsooc8d t342 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t337, t338, t340, t341);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l59 = t342;
                                {
                                    {
                                        uint8_t t343 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                        if (t343) {
                                            goto gm_lb55;
                                        }
                                        gm_l58 = ((uint8_t)0);
                                        goto gm_lb54;
                                    }
gm_lb55:;
                                    {
                                        gm_l58 = ((uint8_t)1);
                                        goto gm_lb54;
                                    }
gm_lb56:;
                                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 461, 12);
                                }
gm_lb54:;
                                uint8_t t344 = gm_l58;
                                if (t344) {
                                    void * t345 = gm_l29;
                                    uint8_t t346 = gm_f_tir_literal_oh960hv(t345);
                                    uint8_t t347 = (uint8_t)!t346;
                                    if (t347) {
                                        void * t348 = gm_l0;
                                        void * t349 = (void *)(((uint8_t *)t348) + 96);
                                        void * t350 = gm_l4;
                                        void * t351 = gm_l29;
                                        gm_t88rcucg1b7uk t352 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t351));
                                        gm_tl874t2atnlh3 t353 = gm_l3;
                                        gm_f_note_fixed_by_ev84og7(t349, t350, GM_CONV(gm_csguankko4lmhd, t352), GM_CONV(gm_cseahvg8v7p0vs, t353));
                                    }
                                    void * t354 = gm_l29;
                                    F.ret = t354;
                                    goto gm_fret;
                                }
                                void * t355 = gm_l29;
                                uint8_t t356 = gm_f_tir_literal_oh960hv(t355);
                                uint8_t t357 = (uint8_t)!t356;
                                if (t357) {
                                    gm_l60 = ((uint8_t)1);
                                } else {
                                    void * t358 = gm_l0;
                                    void * t359 = (void *)(((uint8_t *)t358) + 96);
                                    void * t360 = gm_l4;
                                    void * t361 = gm_l29;
                                    gm_t88rcucg1b7uk t362 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t361));
                                    gm_taari1s0m51a7 t363 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t359, t360, t362);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t9qkh0rsooc8d t364;
                                    GM_ST(uint8_t, ((uint8_t *)&t364 + 0), ((uint8_t)1ULL));
                                    gm_taari1s0m51a7 t365;
                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t365), t364);
                                    ((uint8_t *)&t365)[1] = 1;
                                    uint8_t t366 = (!gm_eq5307j5k9ms(((uint8_t *)&t363), ((uint8_t *)&t365)));
                                    gm_l60 = t366;
                                }
                                uint8_t t367 = gm_l60;
                                if (t367) {
                                    void * t368 = gm_l0;
                                    void * t369 = (void *)(((uint8_t *)t368) + 320);
                                    void * t370 = gm_l29;
                                    gm_t88rcucg1b7uk t371 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t370));
                                    gm_t88rcucg1b7uk t372 = gm_l1;
                                    gm_tl874t2atnlh3 t373 = gm_l3;
                                    gm_thsh8t5u55gps t374;
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t374 + 0), t371);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t374 + 4), t372);
                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t374 + 8), t373);
                                    gm_tpm220j5qhjke t375;
                                    GM_ST(uint8_t, (uint8_t *)&t375 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t375 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t375 + 4, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t375 + 28, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t375 + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t375 + 40, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t375 + 0), ((uint8_t)1ULL));
                                    GM_ST(gm_thsh8t5u55gps, ((uint8_t *)&t375 + 8), t374);
                                    gm_tftb1odc739t5 t376 = gm_f_push_7koe9lk(t369, &t375);
                                    gm_l61 = t376;
                                    uint8_t t377 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                    if (!t377) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srch6h8rdicu443, 466, 13);
                                    gm_t88rcucg1b7uk t378 = gm_l1;
                                    void * t379 = gm_l29;
                                    gm_tvdg94bk7aa22 t380;
                                    GM_ST(uint8_t, (uint8_t *)&t380 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t380 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t380 + 4, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t380 + 8, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t380 + 0), ((uint8_t)0ULL));
                                    gm_b16_8 t381 = gm_f_one_expr_q6kn0hn((void *)NULL, cx, NULL, t379);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tt8o5kebou62r t382;
                                    GM_ST(uint32_t, (uint8_t *)&t382 + 12, 0);
                                    GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t382 + 0), t380);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t382 + 16), t381);
                                    gm_tl874t2atnlh3 t383 = gm_l3;
                                    gm_t2b6tm8c0vgo1 t384;
                                    GM_ST(uint8_t, (uint8_t *)&t384 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t384 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t384 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t384 + 40, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t384 + 48, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t384 + 56, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t384 + 0), ((uint8_t)33ULL));
                                    GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t384 + 8), t382);
                                    void * t385 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t378, &t384, t383);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t385;
                                    goto gm_fret;
                                }
                                void * t386 = gm_l0;
                                gm_tl874t2atnlh3 t387 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 72));
                                void * t388 = gm_l0;
                                gm_t88rcucg1b7uk t389 = gm_l1;
                                gm_b16_8 t390 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t388, t389);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tnpifv20irnph t391;
                                GM_ST(gm_b16_8, ((uint8_t *)&t391 + 0), t390);
                                gm_f_format_i1v5tqv((void *)&gm_l62, (void *)NULL, cx, NULL, t391);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t392 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                if (!t392) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 8, gm_srch6h8rdicu443, 469, 26);
                                gm_b16_8 t393 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l62) + 8));
                                gm_b16_8 t394 = GM_LD(gm_b16_8, ((uint8_t *)&t393));
                                gm_b0_1 t395 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t386, t387, t394);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t396 = gm_l29;
                                F.ret = t396;
                                goto gm_fret;
                            }
gm_lb47:;
                        }
gm_lb46:;
                    }
                    {
                        void * t397 = gm_l0;
                        void * t398 = (void *)(((uint8_t *)t397) + 96);
                        void * t399 = gm_l0;
                        void * t400 = GM_LD(void *, ((uint8_t *)t399));
                        void * t401 = gm_l29;
                        gm_t88rcucg1b7uk t402 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t401));
                        gm_t88rcucg1b7uk t403 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t398, t400, t402);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l30 = t403;
                        {
                            {
                                void * t404 = gm_l4;
                                gm_t88rcucg1b7uk t405 = gm_l30;
                                uint8_t t406 = gm_f_has_infer_3o82a0b(t404, GM_CONV(gm_csguankko4lmhd, t405));
                                uint8_t t407 = (uint8_t)!t406;
                                if (!t407) {
                                    goto gm_lb58;
                                }
                                gm_t88rcucg1b7uk t408 = gm_l30;
                                gm_t88rcucg1b7uk t409 = gm_l1;
                                uint8_t t410 = (!gm_eq9m6mvtmgde(((uint8_t *)&t408), ((uint8_t *)&t409)));
                                if (!t410) {
                                    goto gm_lb58;
                                }
                                void * t411 = gm_l0;
                                gm_t88rcucg1b7uk t412 = gm_l30;
                                gm_t88rcucg1b7uk t413 = gm_l1;
                                gm_f_coercion_steps_ibqvhta((void *)&gm_l64, (void *)NULL, cx, NULL, t411, t412, t413);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                {
                                    {
                                        uint8_t t414 = (uint8_t)((((uint8_t *)&gm_l64))[16] != 0);
                                        if (t414) {
                                            goto gm_lb60;
                                        }
                                        gm_l63 = ((uint8_t)0);
                                        goto gm_lb59;
                                    }
gm_lb60:;
                                    {
                                        gm_l63 = ((uint8_t)1);
                                        goto gm_lb59;
                                    }
gm_lb61:;
                                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 474, 50);
                                }
gm_lb59:;
                                uint8_t t415 = gm_l63;
                                if (!t415) {
                                    goto gm_lb58;
                                }
                                void * t416 = gm_l0;
                                void * t417 = gm_l29;
                                gm_t88rcucg1b7uk t418 = gm_l1;
                                void * t419 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t416, t417, t418);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t419;
                                goto gm_fret;
                            }
gm_lb58:;
                        }
gm_lb57:;
                        {
                            {
                                void * t420 = gm_l4;
                                gm_t88rcucg1b7uk t421 = gm_l30;
                                uint8_t t422 = gm_f_has_infer_3o82a0b(t420, GM_CONV(gm_csguankko4lmhd, t421));
                                uint8_t t423 = (uint8_t)!t422;
                                if (!t423) {
                                    goto gm_lb63;
                                }
                                gm_t88rcucg1b7uk t424 = gm_l30;
                                gm_t88rcucg1b7uk t425 = gm_l1;
                                uint8_t t426 = (gm_eq9m6mvtmgde(((uint8_t *)&t424), ((uint8_t *)&t425)));
                                if (!t426) {
                                    goto gm_lb63;
                                }
                                void * t427 = gm_l4;
                                gm_t88rcucg1b7uk t428 = gm_l30;
                                uint8_t t429 = gm_f_is_error_8k27gd1(t427, GM_CONV(gm_csguankko4lmhd, t428));
                                uint8_t t430 = (uint8_t)!t429;
                                if (!t430) {
                                    goto gm_lb63;
                                }
                                gm_t88rcucg1b7uk t431 = gm_l1;
                                void * t432 = gm_l29;
                                gm_tmn7n0n9raou5 t433;
                                GM_ST(uint8_t, (uint8_t *)&t433 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t433 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t433 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t433 + 0), ((uint8_t)8ULL));
                                gm_t8jsc4n9vp2ib t434;
                                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t434 + 0), t433);
                                GM_ST(void *, ((uint8_t *)&t434 + 8), t432);
                                gm_tl874t2atnlh3 t435 = gm_l3;
                                gm_t2b6tm8c0vgo1 t436;
                                memset(&t436, 0, sizeof t436);
                                GM_ST(uint8_t, ((uint8_t *)&t436 + 0), ((uint8_t)32ULL));
                                GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t436 + 8), t434);
                                void * t437 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t431, &t436, t435);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t437;
                                goto gm_fret;
                            }
gm_lb63:;
                        }
gm_lb62:;
                    }
                    {
                        void * t438 = gm_l0;
                        void * t439 = (void *)(((uint8_t *)t438) + 320);
                        void * t440 = gm_l29;
                        gm_t88rcucg1b7uk t441 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t440));
                        gm_t88rcucg1b7uk t442 = gm_l1;
                        gm_tl874t2atnlh3 t443 = gm_l3;
                        gm_thsh8t5u55gps t444;
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t444 + 0), t441);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t444 + 4), t442);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t444 + 8), t443);
                        gm_tpm220j5qhjke t445;
                        GM_ST(uint8_t, (uint8_t *)&t445 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t445 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t445 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t445 + 28, 0);
                        GM_ST(uint64_t, (uint8_t *)&t445 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t445 + 40, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t445 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_thsh8t5u55gps, ((uint8_t *)&t445 + 8), t444);
                        gm_tftb1odc739t5 t446 = gm_f_push_7koe9lk(t439, &t445);
                        gm_l65 = t446;
                        uint8_t t447 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                        if (!t447) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 1, gm_srch6h8rdicu443, 477, 5);
                        gm_t88rcucg1b7uk t448 = gm_l1;
                        void * t449 = gm_l29;
                        gm_tvdg94bk7aa22 t450;
                        GM_ST(uint8_t, (uint8_t *)&t450 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t450 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t450 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t450 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t450 + 0), ((uint8_t)0ULL));
                        gm_b16_8 t451 = gm_f_one_expr_q6kn0hn((void *)NULL, cx, NULL, t449);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tt8o5kebou62r t452;
                        GM_ST(uint32_t, (uint8_t *)&t452 + 12, 0);
                        GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t452 + 0), t450);
                        GM_ST(gm_b16_8, ((uint8_t *)&t452 + 16), t451);
                        gm_tl874t2atnlh3 t453 = gm_l3;
                        gm_t2b6tm8c0vgo1 t454;
                        GM_ST(uint8_t, (uint8_t *)&t454 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t454 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t454 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t454 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t454 + 48, 0);
                        GM_ST(uint64_t, (uint8_t *)&t454 + 56, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t454 + 0), ((uint8_t)33ULL));
                        GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t454 + 8), t452);
                        void * t455 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t448, &t454, t453);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t455;
                        goto gm_fret;
                    }
                }
gm_lb40:;
            }
            {
                void * t456 = gm_l0;
                void * t457 = GM_LD(void *, ((uint8_t *)t456));
                gm_tl874t2atnlh3 t458 = gm_l3;
                void * t459 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t457, t458);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t459;
                goto gm_fret;
            }
        }
gm_lb39:;
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
__attribute__((aligned(4))) void * gm_f_expect_eq_iu4ojmi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_expect_eq_iu4ojmi F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_tiilmfd16v3fc gm_l5; /* eref */
    void * gm_l6; /* tys */
    void * gm_l7; /* l0 */
    void * gm_l8; /* r0 */
    gm_t3mf58gv9kf3n gm_l9; /* eq */
    gm_t88rcucg1b7uk gm_l10; /* lt */
    gm_t88rcucg1b7uk gm_l11; /* rt */
    uint32_t gm_l12; /* x */
    uint32_t gm_l13; /* y */
    void * gm_l14; /* differ */
    gm_tnpifv20irnph gm_l16; /* set */
    void * gm_l20; /* write */
    void * gm_l21; /* msg */
    void * gm_l22; /* loc */
    void * gm_l24; /* stop */
    void * gm_l26; /* fail */
    void * gm_l28; /* check */
    GM_STACK_CHECK(&F, 3338u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l30;
    uint8_t gm_l31;
    uint8_t gm_l32;
    uint8_t gm_l33;
    gm_tqu3m2l3n8mnc gm_l34;
    gm_tiilmfd16v3fc gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tftb1odc739t5 gm_l43;
    gm_tq7s98ihonkju gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_tftb1odc739t5 gm_l46;
    gm_tftb1odc739t5 gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    {
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t13 = (!(t12 == ((uint64_t)2ULL)));
        if (t13) {
            gm_l31 = ((uint8_t)1);
        } else {
            void * t14 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t15, gm_srch6h8rdicu443, 554, 29);
            {
                {
                    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t14) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)0ULL));
                    if (!t17) {
                        goto gm_lb2;
                    }
                    gm_l32 = ((uint8_t)0);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    gm_l32 = ((uint8_t)1);
                    goto gm_lb1;
                }
gm_lb3:;
                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 554, 29);
            }
gm_lb1:;
            uint8_t t18 = gm_l32;
            gm_l31 = t18;
        }
        uint8_t t19 = gm_l31;
        if (t19) {
            gm_l30 = ((uint8_t)1);
        } else {
            void * t20 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t21, gm_srch6h8rdicu443, 554, 67);
            {
                {
                    uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t20) + (uintptr_t)((uint64_t)1ULL) * 144) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)0ULL));
                    if (!t23) {
                        goto gm_lb5;
                    }
                    gm_l33 = ((uint8_t)0);
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    gm_l33 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 554, 67);
            }
gm_lb4:;
            uint8_t t24 = gm_l33;
            gm_l30 = t24;
        }
        uint8_t t25 = gm_l30;
        if (t25) {
            void * t26 = gm_l0;
            gm_tl874t2atnlh3 t27 = gm_l2;
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_scgahod5hue9b));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(55));
            gm_b0_1 t29 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t26, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t30 = gm_l0;
            void * t31 = GM_LD(void *, ((uint8_t *)t30));
            gm_tl874t2atnlh3 t32 = gm_l2;
            void * t33 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t33;
            goto gm_fret;
        }
        {
            {
                {
                    {
                        void * t34 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t35, gm_srch6h8rdicu443, 558, 23);
                        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t34) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                        uint8_t t37 = (uint8_t)(t36 == ((uint8_t)0ULL));
                        if (!t37) {
                            goto gm_lb8;
                        }
                        memmove(((uint8_t *)&F.l3), ((((uint8_t *)t34) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                        {
                            {
                                {
                                    {
                                        void * t38 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t39, gm_srch6h8rdicu443, 559, 23);
                                        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t38) + (uintptr_t)((uint64_t)1ULL) * 144) + 0));
                                        uint8_t t41 = (uint8_t)(t40 == ((uint8_t)0ULL));
                                        if (!t41) {
                                            goto gm_lb10;
                                        }
                                        memmove(((uint8_t *)&F.l4), ((((uint8_t *)t38) + (uintptr_t)((uint64_t)1ULL) * 144) + 8), 88);
                                        void * t42 = gm_l0;
                                        void * t43 = GM_LD(void *, ((uint8_t *)t42));
                                        gm_b16_8 t44;
                                        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_ss6g2koqn63bf));
                                        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(4));
                                        gm_f_prelude_effect_gilrmm5((void *)&gm_l34, (void *)NULL, cx, NULL, t43, t44);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t45 = (uint8_t)((((uint8_t *)&gm_l34))[24] != 0);
                                        if (t45) {
                                            memmove(((uint8_t *)&gm_l35), ((uint8_t *)&gm_l34), 24);
                                        } else {
                                            void * t46 = gm_l0;
                                            void * t47 = GM_LD(void *, ((uint8_t *)t46));
                                            gm_tl874t2atnlh3 t48 = gm_l2;
                                            void * t49 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t47, t48);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            F.ret = t49;
                                            goto gm_fret;
                                        }
                                        memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l35), 24);
                                        void * t50 = gm_l0;
                                        void * t51 = GM_LD(void *, ((uint8_t *)t50));
                                        void * t52 = (void *)(((uint8_t *)t51) + 8);
                                        gm_l6 = t52;
                                        void * t53 = gm_l0;
                                        void * t54 = (void *)((uint8_t *)&F.l3);
                                        gm_tkn42u7na3t81 t55;
                                        memset(&t55, 0, sizeof t55);
                                        void * t56 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t53, t54, t55);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l7 = t56;
                                        void * t57 = gm_l0;
                                        void * t58 = (void *)((uint8_t *)&F.l4);
                                        void * t59 = gm_l7;
                                        gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t59));
                                        gm_tkn42u7na3t81 t61;
                                        GM_ST(uint8_t, (uint8_t *)&t61 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t61 + 6, 0);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t61), t60);
                                        ((uint8_t *)&t61)[4] = 1;
                                        void * t62 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t57, t58, t61);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l8 = t62;
                                        void * t63 = gm_l0;
                                        void * t64 = gm_l7;
                                        void * t65 = gm_l8;
                                        gm_tl874t2atnlh3 t66 = gm_l2;
                                        gm_t3mf58gv9kf3n t67 = gm_f_equalize_ni1r5ra((void *)NULL, cx, NULL, t63, t64, t65, t66);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l9 = t67;
                                        void * t68 = gm_l0;
                                        void * t69 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                        gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t69));
                                        gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 72));
                                        gm_t88rcucg1b7uk t72 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t68, t70, t71);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l10 = t72;
                                        void * t73 = gm_l0;
                                        void * t74 = GM_LD(void *, (((uint8_t *)&gm_l9) + 8));
                                        gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t74));
                                        gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 72));
                                        gm_t88rcucg1b7uk t77 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t73, t75, t76);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l11 = t77;
                                        void * t78 = gm_l0;
                                        gm_t88rcucg1b7uk t79 = gm_l10;
                                        gm_tl874t2atnlh3 t80 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l3) + 72));
                                        uint32_t t81 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t78, t79, t80);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l12 = t81;
                                        void * t82 = gm_l0;
                                        gm_t88rcucg1b7uk t83 = gm_l11;
                                        gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 72));
                                        uint32_t t85 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t82, t83, t84);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l13 = t85;
                                    }
                                    {
                                        void * t86 = gm_l6;
                                        gm_t88rcucg1b7uk t87 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t86);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_t88rcucg1b7uk t88 = gm_l10;
                                        uint32_t t89 = gm_l12;
                                        gm_tl874t2atnlh3 t90 = gm_l2;
                                        gm_t2b6tm8c0vgo1 t91;
                                        memset(&t91, 0, sizeof t91);
                                        GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)15ULL));
                                        GM_ST(uint32_t, ((uint8_t *)&t91 + 8), t89);
                                        void * t92 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t88, &t91, t90);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_t88rcucg1b7uk t93 = gm_l11;
                                        uint32_t t94 = gm_l13;
                                        gm_tl874t2atnlh3 t95 = gm_l2;
                                        gm_t2b6tm8c0vgo1 t96;
                                        memset(&t96, 0, sizeof t96);
                                        GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)15ULL));
                                        GM_ST(uint32_t, ((uint8_t *)&t96 + 8), t94);
                                        void * t97 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t93, &t96, t95);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tlcl4u558l6p5 t98;
                                        GM_ST(uint8_t, (uint8_t *)&t98 + 17, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t98 + 18, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t98 + 20, 0);
                                        GM_ST(void *, ((uint8_t *)&t98 + 0), t92);
                                        GM_ST(void *, ((uint8_t *)&t98 + 8), t97);
                                        GM_ST(uint8_t, ((uint8_t *)&t98 + 16), ((uint8_t)1));
                                        gm_tl874t2atnlh3 t99 = gm_l2;
                                        gm_t2b6tm8c0vgo1 t100;
                                        memset(&t100, 0, sizeof t100);
                                        GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)43ULL));
                                        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t100 + 8), t98);
                                        void * t101 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t87, &t100, t99);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l14 = t101;
                                        gm_trqfvmjtt4sij t102;
                                        gm_f_list_084fcd6((void *)&t102, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l15 = t102;
                                        void * t103 = (void *)((uint8_t *)&F.l15);
                                        gm_tftb1odc739t5 t104 = gm_f_push_d1r4h19(t103, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l5)));
                                        gm_l36 = t104;
                                        uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                        if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srch6h8rdicu443, 571, 5);
                                        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
                                        gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                                        gm_tnpifv20irnph t108 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t107);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l16 = t108;
                                        void * t109 = gm_l0;
                                        void * t110 = (void *)((uint8_t *)&gm_l16);
                                        gm_tl874t2atnlh3 t111 = gm_l2;
                                        gm_b0_1 t112 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t109, t110, t111);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_trqfvmjtt4sij t113;
                                        gm_f_list_epvducg((void *)&t113, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l17 = t113;
                                        void * t114 = (void *)((uint8_t *)&F.l17);
                                        gm_b16_8 t115;
                                        GM_ST(void *, (uint8_t *)&t115, (void *)(gm_s8vts15kskc6n));
                                        GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(10));
                                        gm_tftb1odc739t5 t116 = gm_f_push_3ef746b(t114, t115);
                                        gm_l37 = t116;
                                        uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                        if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srch6h8rdicu443, 576, 5);
                                        void * t118 = (void *)((uint8_t *)&F.l17);
                                        gm_b16_8 t119;
                                        GM_ST(void *, (uint8_t *)&t119, (void *)(gm_srgp1m0q2ut8m));
                                        GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(11));
                                        gm_tftb1odc739t5 t120 = gm_f_push_3ef746b(t118, t119);
                                        gm_l38 = t120;
                                        uint8_t t121 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                        if (!t121) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srch6h8rdicu443, 577, 5);
                                        void * t122 = (void *)((uint8_t *)&F.l17);
                                        gm_b16_8 t123;
                                        GM_ST(void *, (uint8_t *)&t123, (void *)(gm_so514oj5nbhcu));
                                        GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(1));
                                        gm_tftb1odc739t5 t124 = gm_f_push_3ef746b(t122, t123);
                                        gm_l39 = t124;
                                        uint8_t t125 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                        if (!t125) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_srch6h8rdicu443, 578, 5);
                                        gm_trqfvmjtt4sij t126;
                                        gm_f_list_qtk22f3((void *)&t126, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l18 = t126;
                                        void * t127 = (void *)((uint8_t *)&F.l18);
                                        gm_t7g88i52rrtm5 t128;
                                        gm_f_default_spec_9ntjq2s((void *)&t128);
                                        gm_tftb1odc739t5 t129 = gm_f_push_nbmosph(t127, &t128);
                                        gm_l40 = t129;
                                        uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                        if (!t130) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srch6h8rdicu443, 580, 5);
                                        void * t131 = (void *)((uint8_t *)&F.l18);
                                        gm_t7g88i52rrtm5 t132;
                                        gm_f_default_spec_9ntjq2s((void *)&t132);
                                        gm_tftb1odc739t5 t133 = gm_f_push_nbmosph(t131, &t132);
                                        gm_l41 = t133;
                                    }
                                    {
                                        uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                        if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srch6h8rdicu443, 581, 5);
                                        gm_trqfvmjtt4sij t135;
                                        gm_f_list_vp1djv1_2((void *)&t135, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l19 = t135;
                                        void * t136 = (void *)((uint8_t *)&F.l19);
                                        gm_t88rcucg1b7uk t137 = gm_l11;
                                        uint32_t t138 = gm_l13;
                                        gm_tl874t2atnlh3 t139 = gm_l2;
                                        gm_t2b6tm8c0vgo1 t140;
                                        memset(&t140, 0, sizeof t140);
                                        GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)15ULL));
                                        GM_ST(uint32_t, ((uint8_t *)&t140 + 8), t138);
                                        void * t141 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t137, &t140, t139);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t142 = gm_f_push_d943vgp_2(t136, &(*(gm_tcgrjq9toscke *)((uint8_t *)t141)));
                                        gm_l42 = t142;
                                        uint8_t t143 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                                        if (!t143) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srch6h8rdicu443, 583, 5);
                                        void * t144 = (void *)((uint8_t *)&F.l19);
                                        gm_t88rcucg1b7uk t145 = gm_l10;
                                        uint32_t t146 = gm_l12;
                                        gm_tl874t2atnlh3 t147 = gm_l2;
                                        gm_t2b6tm8c0vgo1 t148;
                                        memset(&t148, 0, sizeof t148);
                                        GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)15ULL));
                                        GM_ST(uint32_t, ((uint8_t *)&t148 + 8), t146);
                                        void * t149 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t145, &t148, t147);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t150 = gm_f_push_d943vgp_2(t144, &(*(gm_tcgrjq9toscke *)((uint8_t *)t149)));
                                        gm_l43 = t150;
                                        uint8_t t151 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                                        if (!t151) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_srch6h8rdicu443, 584, 5);
                                        void * t152 = gm_l6;
                                        gm_t88rcucg1b7uk t153 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t152);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tiilmfd16v3fc t154 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l5));
                                        gm_t419mo45g7j0a t155;
                                        GM_ST(uint32_t, (uint8_t *)&t155 + 28, 0);
                                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t155 + 0), t154);
                                        GM_ST(uint32_t, ((uint8_t *)&t155 + 24), ((uint32_t)0ULL));
                                        gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                                        gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                                        gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                                        gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&t158));
                                        gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                        gm_th1g69d7vpipg t161;
                                        GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t161 + 8), t155);
                                        gm_t150deeftog49 t162;
                                        GM_ST(gm_th1g69d7vpipg, ((uint8_t *)&t162 + 0), t161);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t162 + 40), t157);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t162 + 56), t159);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t162 + 72), t160);
                                        GM_ST(void *, ((uint8_t *)&t162 + 88), ((void *)0));
                                        gm_tq7s98ihonkju t163 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t162, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krbc9rueul4)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l44 = t163;
                                        uint8_t t164 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                                        if (!t164) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_srch6h8rdicu443, 585, 40);
                                        void * t165 = GM_LD(void *, (((uint8_t *)&gm_l44) + 8));
                                        gm_tl874t2atnlh3 t166 = gm_l2;
                                        gm_t2b6tm8c0vgo1 t167;
                                        memset(&t167, 0, sizeof t167);
                                        GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)55ULL));
                                        GM_ST(void *, ((uint8_t *)&t167 + 8), t165);
                                        void * t168 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t153, &t167, t166);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l20 = t168;
                                        void * t169 = gm_l6;
                                        gm_t88rcucg1b7uk t170 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t169);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t171;
                                        GM_ST(void *, (uint8_t *)&t171, (void *)(gm_smadkict0htlf));
                                        GM_ST(uintptr_t, (uint8_t *)&t171 + 8, (uintptr_t)(37));
                                        gm_tl874t2atnlh3 t172 = gm_l2;
                                        gm_t2b6tm8c0vgo1 t173;
                                        memset(&t173, 0, sizeof t173);
                                        GM_ST(uint8_t, ((uint8_t *)&t173 + 0), ((uint8_t)5ULL));
                                        GM_ST(gm_b16_8, ((uint8_t *)&t173 + 8), t171);
                                        void * t174 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t170, &t173, t172);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l21 = t174;
                                        void * t175 = gm_l0;
                                        gm_tl874t2atnlh3 t176 = gm_l2;
                                        void * t177 = gm_f_source_location_nc1oola((void *)NULL, cx, NULL, t175, t176);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l22 = t177;
                                        gm_trqfvmjtt4sij t178;
                                        gm_f_list_vp1djv1_2((void *)&t178, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l23 = t178;
                                        void * t179 = (void *)((uint8_t *)&F.l23);
                                        void * t180 = gm_l21;
                                        gm_tftb1odc739t5 t181 = gm_f_push_d943vgp_2(t179, &(*(gm_tcgrjq9toscke *)((uint8_t *)t180)));
                                        gm_l45 = t181;
                                    }
                                    {
                                        uint8_t t182 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                        if (!t182) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srch6h8rdicu443, 589, 5);
                                        void * t183 = (void *)((uint8_t *)&F.l23);
                                        void * t184 = gm_l22;
                                        gm_tftb1odc739t5 t185 = gm_f_push_d943vgp_2(t183, &(*(gm_tcgrjq9toscke *)((uint8_t *)t184)));
                                        gm_l46 = t185;
                                        uint8_t t186 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                        if (!t186) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_srch6h8rdicu443, 590, 5);
                                        void * t187 = gm_l6;
                                        gm_t88rcucg1b7uk t188 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t187);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tiilmfd16v3fc t189 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l5));
                                        gm_t419mo45g7j0a t190;
                                        GM_ST(uint32_t, (uint8_t *)&t190 + 28, 0);
                                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t190 + 0), t189);
                                        GM_ST(uint32_t, ((uint8_t *)&t190 + 24), ((uint32_t)1ULL));
                                        gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&F.l23));
                                        gm_tsth9qu1dv0g3 t192;
                                        GM_ST(uint8_t, (uint8_t *)&t192 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t192 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t192 + 4, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t192 + 0), ((uint8_t)1ULL));
                                        GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t192 + 8), t190);
                                        gm_to76jli3bqddq t193;
                                        GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t193 + 0), t192);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t193 + 40), t191);
                                        gm_tl874t2atnlh3 t194 = gm_l2;
                                        gm_t2b6tm8c0vgo1 t195;
                                        GM_ST(uint8_t, (uint8_t *)&t195 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t195 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t195 + 4, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t195 + 0), ((uint8_t)34ULL));
                                        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t195 + 8), t193);
                                        void * t196 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t188, &t195, t194);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l24 = t196;
                                        gm_trqfvmjtt4sij t197;
                                        gm_f_list_ir0h3tc_2((void *)&t197, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l25 = t197;
                                        void * t198 = (void *)((uint8_t *)&F.l25);
                                        void * t199 = gm_l20;
                                        gm_trcmmd12cefb1 t200;
                                        GM_ST(uint8_t, (uint8_t *)&t200 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t200 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t200 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t200 + 16, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t200 + 24, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t200 + 0), ((uint8_t)4ULL));
                                        GM_ST(void *, ((uint8_t *)&t200 + 8), t199);
                                        gm_tftb1odc739t5 t201 = gm_f_push_d6c76ud_2(t198, &t200);
                                        gm_l47 = t201;
                                        uint8_t t202 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                                        if (!t202) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_srch6h8rdicu443, 593, 5);
                                        void * t203 = gm_l6;
                                        gm_t88rcucg1b7uk t204 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t203);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
                                        void * t206 = gm_l24;
                                        gm_tl874t2atnlh3 t207 = gm_l2;
                                        void * t208 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t204, t205, t206, t207);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l26 = t208;
                                        gm_trqfvmjtt4sij t209;
                                        gm_f_list_v5d1o9b_2((void *)&t209, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l27 = t209;
                                        void * t210 = (void *)((uint8_t *)&F.l27);
                                        void * t211 = gm_l14;
                                        gm_tlr9dp305htm7 t212;
                                        GM_ST(uint8_t, (uint8_t *)&t212 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t212 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t212 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t212 + 16, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t212 + 0), ((uint8_t)0ULL));
                                        GM_ST(void *, ((uint8_t *)&t212 + 8), t211);
                                        gm_tftb1odc739t5 t213 = gm_f_push_eb7kbhp_2(t210, &t212);
                                        gm_l48 = t213;
                                        uint8_t t214 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                        if (!t214) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srch6h8rdicu443, 596, 5);
                                        void * t215 = gm_l6;
                                        gm_t88rcucg1b7uk t216 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t215);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&F.l27));
                                        void * t218 = gm_l26;
                                        gm_trl2khmuphncd t219;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t219 + 0), t217);
                                        GM_ST(void *, ((uint8_t *)&t219 + 16), t218);
                                        GM_ST(void *, ((uint8_t *)&t219 + 24), ((void *)0));
                                        gm_tl874t2atnlh3 t220 = gm_l2;
                                        gm_t2b6tm8c0vgo1 t221;
                                        GM_ST(uint8_t, (uint8_t *)&t221 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t221 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t221 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t221 + 40, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t221 + 48, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t221 + 56, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t221 + 0), ((uint8_t)47ULL));
                                        GM_ST(gm_trl2khmuphncd, ((uint8_t *)&t221 + 8), t219);
                                        void * t222 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t216, &t221, t220);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l28 = t222;
                                        gm_trqfvmjtt4sij t223;
                                        gm_f_list_ir0h3tc_2((void *)&t223, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l29 = t223;
                                        void * t224 = (void *)((uint8_t *)&F.l29);
                                        uint32_t t225 = gm_l12;
                                        void * t226 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                        gm_ttggq7ng4kp3p t227;
                                        GM_ST(uint32_t, (uint8_t *)&t227 + 4, 0);
                                        GM_ST(uint32_t, ((uint8_t *)&t227 + 0), t225);
                                        GM_ST(void *, ((uint8_t *)&t227 + 8), t226);
                                        gm_trcmmd12cefb1 t228;
                                        GM_ST(uint8_t, (uint8_t *)&t228 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t228 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t228 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t228 + 24, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t228 + 8), t227);
                                        gm_tftb1odc739t5 t229 = gm_f_push_d6c76ud_2(t224, &t228);
                                        gm_l49 = t229;
                                    }
                                    {
                                        uint8_t t230 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                        if (!t230) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srch6h8rdicu443, 599, 5);
                                        void * t231 = (void *)((uint8_t *)&F.l29);
                                        uint32_t t232 = gm_l13;
                                        void * t233 = GM_LD(void *, (((uint8_t *)&gm_l9) + 8));
                                        gm_ttggq7ng4kp3p t234;
                                        GM_ST(uint32_t, (uint8_t *)&t234 + 4, 0);
                                        GM_ST(uint32_t, ((uint8_t *)&t234 + 0), t232);
                                        GM_ST(void *, ((uint8_t *)&t234 + 8), t233);
                                        gm_trcmmd12cefb1 t235;
                                        GM_ST(uint8_t, (uint8_t *)&t235 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t235 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t235 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t235 + 24, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t235 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t235 + 8), t234);
                                        gm_tftb1odc739t5 t236 = gm_f_push_d6c76ud_2(t231, &t235);
                                        gm_l50 = t236;
                                        uint8_t t237 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                                        if (!t237) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srch6h8rdicu443, 600, 5);
                                        void * t238 = (void *)((uint8_t *)&F.l29);
                                        void * t239 = gm_l28;
                                        gm_trcmmd12cefb1 t240;
                                        GM_ST(uint8_t, (uint8_t *)&t240 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t240 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t240 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t240 + 16, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t240 + 24, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t240 + 0), ((uint8_t)4ULL));
                                        GM_ST(void *, ((uint8_t *)&t240 + 8), t239);
                                        gm_tftb1odc739t5 t241 = gm_f_push_d6c76ud_2(t238, &t240);
                                        gm_l51 = t241;
                                        uint8_t t242 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                                        if (!t242) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srch6h8rdicu443, 601, 5);
                                        void * t243 = gm_l0;
                                        void * t244 = GM_LD(void *, ((uint8_t *)t243));
                                        gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)&F.l29));
                                        gm_tl874t2atnlh3 t246 = gm_l2;
                                        void * t247 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t244, t245, t246);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t247;
                                        goto gm_fret;
                                    }
                                }
gm_lb10:;
                            }
                            {
                                void * t248 = gm_l0;
                                void * t249 = GM_LD(void *, ((uint8_t *)t248));
                                gm_tl874t2atnlh3 t250 = gm_l2;
                                void * t251 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t249, t250);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t251;
                                goto gm_fret;
                            }
                        }
gm_lb9:;
                    }
                }
gm_lb8:;
            }
            {
                void * t252 = gm_l0;
                void * t253 = GM_LD(void *, ((uint8_t *)t252));
                gm_tl874t2atnlh3 t254 = gm_l2;
                void * t255 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t253, t254);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t255;
                goto gm_fret;
            }
        }
gm_lb7:;
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
__attribute__((aligned(4))) void gm_f_fill_defaults_3fq5ut6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_fill_defaults_3fq5ut6 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint64_t gm_l2; /* callee_module */
    gm_b16_8 gm_l3; /* pre */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l5; /* params */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    void * gm_l10; /* slot */
    void * gm_l11; /* v */
    void * gm_l12; /* p */
    void * gm_l13; /* d */
    GM_STACK_CHECK(&F, 927u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t22qobfhrnoif gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tftb1odc739t5 gm_l19;
    void * t11 = gm_l1;
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)t11));
    gm_l5 = t12;
    gm_trqfvmjtt4sij t13;
    gm_f_list_vp1djv1_2((void *)&t13, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t13;
    void * t14 = gm_l1;
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 56));
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
    gm_l7 = t16;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l8;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb1;
            }
            {
                uint64_t t20 = gm_l8;
                void * t21 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t23 = gm_l8;
                void * t24 = GM_LD(void *, (((uint8_t *)t21) + (uintptr_t)t23 * 8));
                gm_t22qobfhrnoif t25;
                GM_ST(uint64_t, ((uint8_t *)&t25 + 0), t20);
                GM_ST(void *, ((uint8_t *)&t25 + 8), t24);
                gm_l14 = t25;
                {
                    {
                        uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                        gm_l9 = t26;
                        void * t27 = GM_LD(void *, (((uint8_t *)&gm_l14) + 8));
                        gm_l10 = t27;
                        {
                            {
                                uint8_t t28 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l10)) != 0);
                                if (!t28) {
                                    goto gm_lb7;
                                }
                                void * t29 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                gm_l11 = t29;
                                void * t30 = (void *)((uint8_t *)&F.l6);
                                void * t31 = gm_l11;
                                gm_tftb1odc739t5 t32 = gm_f_push_d943vgp_2(t30, &(*(gm_tcgrjq9toscke *)((uint8_t *)t31)));
                                gm_l15 = t32;
                                uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srch6h8rdicu443, 347, 13);
                                goto gm_lb3;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        void * t34 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t36 = gm_l9;
                        if ((uintptr_t)t36 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t36, t35, gm_srch6h8rdicu443, 350, 15);
                        void * t37 = (void *)(((uint8_t *)t34) + (uintptr_t)t36 * 56);
                        gm_l12 = t37;
                        {
                            {
                                void * t38 = gm_l12;
                                uint8_t t39 = (uint8_t)(GM_LD(void *, (((uint8_t *)t38) + 32)) != 0);
                                if (!t39) {
                                    goto gm_lb9;
                                }
                                void * t40 = GM_LD(void *, (((uint8_t *)t38) + 32));
                                gm_l13 = t40;
                                void * t41 = (void *)((uint8_t *)&F.l6);
                                void * t42 = gm_l0;
                                void * t43 = gm_l12;
                                uint64_t t44 = gm_l2;
                                void * t45 = gm_l13;
                                void * t46 = gm_l12;
                                gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t46) + 16));
                                gm_tl874t2atnlh3 t48 = gm_l4;
                                void * t49 = gm_f_param_default_h93ci1m((void *)NULL, cx, NULL, t42, t43, t44, t45, t47, t48);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t50 = gm_f_push_d943vgp_2(t41, &(*(gm_tcgrjq9toscke *)((uint8_t *)t49)));
                                gm_l16 = t50;
                                uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srch6h8rdicu443, 352, 13);
                                goto gm_lb3;
                            }
gm_lb9:;
                        }
gm_lb8:;
                        void * t52 = gm_l1;
                        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)t52) + 145));
                        uint8_t t54 = (uint8_t)!t53;
                        if (t54) {
                            void * t55 = gm_l12;
                            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)t55));
                            gm_b16_8 t57;
                            GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sfdbfrv46lihd));
                            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(1));
                            uint8_t t58 = (gm_eq0iqiubfm7e(((uint8_t *)&t56), ((uint8_t *)&t57)));
                            if (t58) {
                                void * t59 = gm_l0;
                                gm_tl874t2atnlh3 t60 = gm_l4;
                                void * t61 = gm_l1;
                                gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)t61) + 16));
                                uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                gm_t3877pk7lv748 t64;
                                GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t62);
                                GM_ST(uint64_t, ((uint8_t *)&t64 + 16), t63);
                                gm_f_format_qjdh7n5((void *)&gm_l17, (void *)NULL, cx, NULL, &t64);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srch6h8rdicu443, 357, 32);
                                gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                                gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
                                gm_b0_1 t68 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t59, t60, t67);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            } else {
                                void * t69 = gm_l0;
                                gm_tl874t2atnlh3 t70 = gm_l4;
                                void * t71 = gm_l12;
                                gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)t71));
                                gm_tnpifv20irnph t73;
                                GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
                                gm_f_format_qk20moi((void *)&gm_l18, (void *)NULL, cx, NULL, t73);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                if (!t74) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srch6h8rdicu443, 359, 32);
                                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                                gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                                gm_b0_1 t77 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t69, t70, t76);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                        }
                        void * t78 = (void *)((uint8_t *)&F.l6);
                        void * t79 = gm_l0;
                        void * t80 = GM_LD(void *, ((uint8_t *)t79));
                        gm_tl874t2atnlh3 t81 = gm_l4;
                        void * t82 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t80, t81);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t83 = gm_f_push_d943vgp_2(t78, &(*(gm_tcgrjq9toscke *)((uint8_t *)t82)));
                        gm_l19 = t83;
                        uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                        if (!t84) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srch6h8rdicu443, 362, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 345, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t85 = gm_l8;
            uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
            uint64_t t88 = r86;
            gm_l8 = t88;
        }
    }
gm_lb1:;
    gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    gm_b16_8 t90 = gm_l3;
    gm_t6j6smj7qoctu t91;
    GM_ST(gm_b16_8, ((uint8_t *)&t91 + 0), t89);
    GM_ST(gm_b16_8, ((uint8_t *)&t91 + 16), t90);
    memmove(gm_out, ((uint8_t *)&t91), 32);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_const_ref_arg_s0f37he(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_is_const_ref_arg_s0f37he F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l3; /* x */
    void * gm_l4; /* inner */
    void * gm_l5; /* h */
    uint8_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 214u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * gm_l7;
    gm_t53at80pfdus3 gm_l8;
    void * t11 = gm_l1;
    gm_l3 = t11;
    {
        for (;;) {
            void * t12 = gm_l3;
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)9ULL));
            if (!t14) {
                goto gm_lb1;
            }
            void * t15 = GM_LD(void *, ((((uint8_t *)t12) + 8) + 8));
            gm_l4 = t15;
            {
                void * t16 = gm_l4;
                gm_l3 = t16;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        {
            void * t17 = gm_l3;
            void * t18 = gm_f_hoist_prefix_r1mevpl((void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t18;
            uint8_t t19 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l7)) != 0);
            if (!t19) {
                goto gm_lb5;
            }
            void * t20 = GM_LD(void *, ((uint8_t *)&gm_l7));
            gm_l5 = t20;
            void * t21 = gm_l5;
            gm_l3 = t21;
            goto gm_lb4;
        }
gm_lb5:;
    }
gm_lb4:;
    {
        {
            void * t22 = gm_l3;
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)12ULL));
            if (!t24) {
                goto gm_lb7;
            }
            uint8_t t25 = GM_LD(uint8_t, (((((uint8_t *)t22) + 8) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)2ULL));
            if (!t26) {
                goto gm_lb7;
            }
            {
                {
                    void * t27 = gm_l0;
                    void * t28 = (void *)(((uint8_t *)t27) + 8);
                    gm_t88rcucg1b7uk t29 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l8, t28, GM_CONV(gm_csguankko4lmhd, t29));
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)8ULL));
                    if (!t31) {
                        goto gm_lb9;
                    }
                    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 8));
                    gm_l6 = t32;
                    uint8_t t33 = gm_l6;
                    F.ret = t33;
                    goto gm_fret;
                }
gm_lb9:;
            }
gm_lb8:;
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_literal_tree_om58rum(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_literal_tree_om58rum F;
    void * gm_l0; /* e */
    void * gm_l1; /* inner */
    void * gm_l2; /* operand */
    void * gm_l3; /* lhs */
    void * gm_l4; /* rhs */
    GM_STACK_CHECK(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l5;
    uint8_t gm_l6;
    void * t11 = gm_l0;
    {
        {
            {
                {
                    uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
                    uint8_t t13 = (uint8_t)(t12 == ((uint8_t)0ULL));
                    if (!t13) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
                    uint8_t t15 = (uint8_t)(t14 == ((uint8_t)1ULL));
                    if (!t15) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l5 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)9ULL));
            if (!t17) {
                goto gm_lb6;
            }
            void * t18 = GM_LD(void *, ((((uint8_t *)t11) + 8) + 8));
            gm_l1 = t18;
            void * t19 = gm_l1;
            uint8_t t20 = gm_f_is_literal_tree_om58rum(t19);
            gm_l5 = t20;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)12ULL));
            if (!t22) {
                goto gm_lb7;
            }
            void * t23 = GM_LD(void *, (((((uint8_t *)t11) + 8) + 8) + 8));
            gm_l2 = t23;
            void * t24 = gm_l2;
            uint8_t t25 = gm_f_is_literal_tree_om58rum(t24);
            gm_l5 = t25;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)13ULL));
            if (!t27) {
                goto gm_lb8;
            }
            void * t28 = GM_LD(void *, (((((uint8_t *)t11) + 8) + 8) + 8));
            gm_l3 = t28;
            void * t29 = GM_LD(void *, (((((uint8_t *)t11) + 8) + 8) + 16));
            gm_l4 = t29;
            void * t30 = gm_l3;
            uint8_t t31 = gm_f_is_literal_tree_om58rum(t30);
            if (t31) {
                void * t32 = gm_l4;
                uint8_t t33 = gm_f_is_literal_tree_om58rum(t32);
                gm_l6 = t33;
            } else {
                gm_l6 = ((uint8_t)0);
            }
            uint8_t t34 = gm_l6;
            gm_l5 = t34;
            goto gm_lb1;
        }
gm_lb8:;
        {
            gm_l5 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 494, 54);
    }
gm_lb1:;
    uint8_t t35 = gm_l5;
    F.ret = t35;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_positional_t5fspc1(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_positional_t5fspc1 F;
    void * gm_l0; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 228u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        for (;;) {
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 136));
            void * t7 = gm_l0;
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
            uint8_t t9 = (t6 < t8);
            if (!t9) {
                goto gm_lb1;
            }
            void * t10 = gm_l0;
            void * t11 = GM_LD(void *, (((uint8_t *)t10) + 32));
            uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t10) + 32) + 8));
            void * t13 = gm_l0;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 136));
            if ((uintptr_t)t14 >= (uintptr_t)(t12)) gm_trap_index((uintptr_t)t14, t12, gm_srch6h8rdicu443, 196, 41);
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t11) + (uintptr_t)t14 * 1));
            uint8_t t16 = (uint8_t)!t15;
            if (!t16) {
                goto gm_lb1;
            }
            {
                void * t17 = gm_l0;
                void * t18 = gm_l0;
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 136));
                uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
                if (o21) gm_trap("integer overflow in `+`", gm_srch6h8rdicu443, 196, 64);
                uint64_t t22 = r20;
                GM_ST(uint64_t, (((uint8_t *)t17) + 136), t22);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t23 = gm_l0;
    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 136));
    void * t25 = gm_l0;
    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 8));
    uint8_t t27 = (t24 >= t26);
    if (t27) {
        gm_tf5srh4mp4b3i t28;
        memset(&t28, 0, sizeof t28);
        F.ret = t28;
        goto gm_fret;
    }
    void * t29 = gm_l0;
    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)t29) + 136));
    gm_tf5srh4mp4b3i t31;
    GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t31), t30);
    ((uint8_t *)&t31)[8] = 1;
    F.ret = t31;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_param_index_78k4iut(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_index_78k4iut F;
    gm_b16_8 gm_l0; /* params */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 442u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tgsvmddj3e85e gm_l6;
    gm_b16_8 t7 = gm_l0;
    gm_l2 = t7;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t8 = gm_l3;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                uint64_t t11 = gm_l3;
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t14 = gm_l3;
                gm_t5v0c8ia6a5mo t15 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t12) + (uintptr_t)t14 * 56));
                gm_tgsvmddj3e85e t16;
                GM_ST(uint64_t, ((uint8_t *)&t16 + 0), t11);
                GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t16 + 8), t15);
                gm_l6 = t16;
                {
                    {
                        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
                        gm_l4 = t17;
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l6) + 8), 56);
                        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                        gm_b16_8 t19 = gm_l1;
                        uint8_t t20 = (gm_eq0iqiubfm7e(((uint8_t *)&t18), ((uint8_t *)&t19)));
                        if (t20) {
                            uint64_t t21 = gm_l4;
                            gm_tf5srh4mp4b3i t22;
                            GM_ST(uint8_t, (uint8_t *)&t22 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t22 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t22 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t22), t21);
                            ((uint8_t *)&t22)[8] = 1;
                            F.ret = t22;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 171, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t23 = gm_l3;
            uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
            uint64_t t26 = r24;
            gm_l3 = t26;
        }
    }
gm_lb1:;
    gm_tf5srh4mp4b3i t27;
    memset(&t27, 0, sizeof t27);
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_print_bhebp9d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_print_bhebp9d F;
    void * gm_l0; /* cx */
    uint8_t gm_l1; /* to_out */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l8; /* effect_name */
    gm_tiilmfd16v3fc gm_l9; /* eref */
    gm_tnpifv20irnph gm_l11; /* set */
    GM_STACK_CHECK(&F, 946u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    gm_b16_8 gm_l15;
    gm_tqu3m2l3n8mnc gm_l16;
    gm_tiilmfd16v3fc gm_l17;
    gm_tftb1odc739t5 gm_l18;
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t16 = ((t15 == ((uint64_t)0ULL)));
    if (t16) {
        gm_l12 = ((uint8_t)1);
    } else {
        void * t17 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t18, gm_srch6h8rdicu443, 607, 25);
        {
            {
                uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t17) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                uint8_t t20 = (uint8_t)(t19 == ((uint8_t)0ULL));
                if (!t20) {
                    goto gm_lb2;
                }
                gm_l13 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l13 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 607, 25);
        }
gm_lb1:;
        uint8_t t21 = gm_l13;
        gm_l12 = t21;
    }
    uint8_t t22 = gm_l12;
    if (t22) {
        void * t23 = gm_l0;
        gm_tl874t2atnlh3 t24 = gm_l3;
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sijukd7vsocnt));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(67));
        gm_b0_1 t26 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t23, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t27 = gm_l0;
        void * t28 = GM_LD(void *, ((uint8_t *)t27));
        gm_tl874t2atnlh3 t29 = gm_l3;
        void * t30 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t30;
        goto gm_fret;
    }
    gm_b16_8 t31 = gm_l2;
    gm_l4 = t31;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t32 = gm_l5;
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t34 = (t32 < t33);
            if (!t34) {
                goto gm_lb4;
            }
            {
                void * t35 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t37 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t35) + (uintptr_t)t37 * 144), 144);
                        {
                            {
                                uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                uint8_t t39 = (uint8_t)(t38 == ((uint8_t)1ULL));
                                if (!t39) {
                                    goto gm_lb10;
                                }
                                gm_l14 = ((uint8_t)1);
                                goto gm_lb9;
                            }
gm_lb10:;
                            {
                                gm_l14 = ((uint8_t)0);
                                goto gm_lb9;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 612, 12);
                        }
gm_lb9:;
                        uint8_t t40 = gm_l14;
                        if (t40) {
                            void * t41 = gm_l0;
                            gm_tl874t2atnlh3 t42 = gm_l3;
                            gm_b16_8 t43;
                            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sijukd7vsocnt));
                            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(67));
                            gm_b0_1 t44 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t41, t42, t43);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t45 = gm_l0;
                            void * t46 = GM_LD(void *, ((uint8_t *)t45));
                            gm_tl874t2atnlh3 t47 = gm_l3;
                            void * t48 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t46, t47);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t48;
                            goto gm_fret;
                        }
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 611, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t49 = gm_l5;
            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
            uint64_t t52 = r50;
            gm_l5 = t52;
        }
    }
gm_lb4:;
    {
        {
            void * t53 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t54)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t54, gm_srch6h8rdicu443, 617, 30);
            uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t53) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)0ULL));
            if (!t56) {
                goto gm_lb13;
            }
            memmove(((uint8_t *)&F.l7), ((((uint8_t *)t53) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
            uint8_t t57 = gm_l1;
            if (t57) {
                gm_b16_8 t58;
                GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sn937bfucthvg));
                GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(3));
                gm_l15 = t58;
            } else {
                gm_b16_8 t59;
                GM_ST(void *, (uint8_t *)&t59, (void *)(gm_srqehfttj6ngk));
                GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(3));
                gm_l15 = t59;
            }
            gm_b16_8 t60 = gm_l15;
            gm_l8 = t60;
            void * t61 = gm_l0;
            void * t62 = GM_LD(void *, ((uint8_t *)t61));
            gm_b16_8 t63 = gm_l8;
            gm_f_prelude_effect_gilrmm5((void *)&gm_l16, (void *)NULL, cx, NULL, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t64 = (uint8_t)((((uint8_t *)&gm_l16))[24] != 0);
            if (t64) {
                memmove(((uint8_t *)&gm_l17), ((uint8_t *)&gm_l16), 24);
            } else {
                void * t65 = gm_l0;
                void * t66 = GM_LD(void *, ((uint8_t *)t65));
                gm_tl874t2atnlh3 t67 = gm_l3;
                void * t68 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t66, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t68;
                goto gm_fret;
            }
            memmove(((uint8_t *)&gm_l9), ((uint8_t *)&gm_l17), 24);
            gm_trqfvmjtt4sij t69;
            gm_f_list_084fcd6((void *)&t69, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l10 = t69;
            void * t70 = (void *)((uint8_t *)&F.l10);
            gm_tftb1odc739t5 t71 = gm_f_push_d1r4h19(t70, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l9)));
            gm_l18 = t71;
            uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srch6h8rdicu443, 621, 5);
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
            gm_tnpifv20irnph t75 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t75;
            void * t76 = gm_l0;
            void * t77 = (void *)((uint8_t *)&gm_l11);
            gm_tl874t2atnlh3 t78 = gm_l3;
            gm_b0_1 t79 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t76, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t80 = gm_l0;
            gm_tiilmfd16v3fc t81 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l9));
            gm_t419mo45g7j0a t82;
            GM_ST(uint32_t, (uint8_t *)&t82 + 28, 0);
            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t82 + 0), t81);
            GM_ST(uint32_t, ((uint8_t *)&t82 + 24), ((uint32_t)0ULL));
            void * t83 = (void *)((uint8_t *)&F.l7);
            void * t84 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t86 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t87 = (uintptr_t)t85;
            if (t86 > t87 || t87 > (uintptr_t)t85) gm_trap_slice(t86, t87, t85, gm_srch6h8rdicu443, 624, 57);
            uint64_t t88 = ((uintptr_t)t85) - (uintptr_t)((uint64_t)1ULL);
            gm_b16_8 t89;
            GM_ST(void *, (uint8_t *)&t89, (void *)(((uint8_t *)t84 + (uintptr_t)((uint64_t)1ULL) * 144)));
            GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(t88));
            gm_tl874t2atnlh3 t90 = gm_l3;
            gm_th1g69d7vpipg t91;
            GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t91 + 8), t82);
            void * t92 = gm_f_build_write_k6dcf8r((void *)NULL, cx, NULL, t80, &t91, t83, t89, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t92;
            goto gm_fret;
        }
gm_lb13:;
        void * t93 = gm_l0;
        void * t94 = GM_LD(void *, ((uint8_t *)t93));
        gm_tl874t2atnlh3 t95 = gm_l3;
        void * t96 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t94, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t96;
        goto gm_fret;
    }
gm_lb12:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_tir_literal_oh960hv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_tir_literal_oh960hv F;
    void * gm_l0; /* e */
    void * gm_l1; /* operand */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    GM_STACK_CHECK(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    uint8_t gm_l5;
    void * t11 = gm_l0;
    {
        {
            {
                {
                    uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
                    uint8_t t13 = (uint8_t)(t12 == ((uint8_t)0ULL));
                    if (!t13) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
                    uint8_t t15 = (uint8_t)(t14 == ((uint8_t)1ULL));
                    if (!t15) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)2ULL));
                    if (!t17) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                goto gm_lb2;
            }
gm_lb3:;
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)29ULL));
            if (!t19) {
                goto gm_lb7;
            }
            void * t20 = GM_LD(void *, (((((uint8_t *)t11) + 8) + 8) + 8));
            gm_l1 = t20;
            void * t21 = gm_l1;
            uint8_t t22 = gm_f_tir_literal_oh960hv(t21);
            gm_l4 = t22;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)30ULL));
            if (!t24) {
                goto gm_lb8;
            }
            void * t25 = GM_LD(void *, (((((uint8_t *)t11) + 8) + 8) + 8));
            gm_l2 = t25;
            void * t26 = GM_LD(void *, (((((uint8_t *)t11) + 8) + 8) + 16));
            gm_l3 = t26;
            void * t27 = gm_l2;
            uint8_t t28 = gm_f_tir_literal_oh960hv(t27);
            if (t28) {
                void * t29 = gm_l3;
                uint8_t t30 = gm_f_tir_literal_oh960hv(t29);
                gm_l5 = t30;
            } else {
                gm_l5 = ((uint8_t)0);
            }
            uint8_t t31 = gm_l5;
            gm_l4 = t31;
            goto gm_lb1;
        }
gm_lb8:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 503, 40);
    }
gm_lb1:;
    uint8_t t32 = gm_l4;
    F.ret = t32;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_trap_t21p26s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_trap_t21p26s F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* args */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l4; /* msg */
    GM_STACK_CHECK(&F, 624u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * gm_l5;
    void * gm_l6;
    uint8_t gm_l7;
    void * gm_l8;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
    if (t10) {
        void * t11 = gm_l0;
        void * t12 = GM_LD(void *, ((uint8_t *)t11));
        void * t13 = (void *)(((uint8_t *)t12) + 8);
        gm_t88rcucg1b7uk t14 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sinmpfqtbo9in));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(4));
        gm_tl874t2atnlh3 t16 = gm_l2;
        gm_t2b6tm8c0vgo1 t17;
        memset(&t17, 0, sizeof t17);
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)5ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 8), t15);
        void * t18 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t14, &t17, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t18;
    } else {
        {
            {
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint8_t t20 = ((t19 == ((uint64_t)1ULL)));
                if (!t20) {
                    goto gm_lb2;
                }
                void * t21 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t22, gm_srch6h8rdicu443, 514, 33);
                {
                    {
                        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t21) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                        uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
                        if (!t24) {
                            goto gm_lb4;
                        }
                        gm_l7 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_l7 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 514, 33);
                }
gm_lb3:;
                uint8_t t25 = gm_l7;
                if (!t25) {
                    goto gm_lb2;
                }
                {
                    {
                        void * t26 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t27, gm_srch6h8rdicu443, 515, 27);
                        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t26) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                        uint8_t t29 = (uint8_t)(t28 == ((uint8_t)0ULL));
                        if (!t29) {
                            goto gm_lb7;
                        }
                        memmove(((uint8_t *)&F.l3), ((((uint8_t *)t26) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                        void * t30 = gm_l0;
                        void * t31 = gm_l0;
                        void * t32 = (void *)((uint8_t *)&F.l3);
                        void * t33 = gm_l0;
                        void * t34 = GM_LD(void *, ((uint8_t *)t33));
                        void * t35 = (void *)(((uint8_t *)t34) + 8);
                        gm_t88rcucg1b7uk t36 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t35);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tkn42u7na3t81 t37;
                        GM_ST(uint8_t, (uint8_t *)&t37 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t37 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t37), t36);
                        ((uint8_t *)&t37)[4] = 1;
                        void * t38 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t31, t32, t37);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t39 = gm_l0;
                        void * t40 = GM_LD(void *, ((uint8_t *)t39));
                        void * t41 = (void *)(((uint8_t *)t40) + 8);
                        gm_t88rcucg1b7uk t42 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t41);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t43 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t30, t38, t42);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l8 = t43;
                        goto gm_lb6;
                    }
gm_lb7:;
                    void * t44 = gm_l0;
                    void * t45 = GM_LD(void *, ((uint8_t *)t44));
                    gm_tl874t2atnlh3 t46 = gm_l2;
                    void * t47 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t45, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t47;
                    goto gm_fret;
                    gm_l8 = ((void *)0);
                }
gm_lb6:;
                void * t48 = gm_l8;
                gm_l6 = t48;
                goto gm_lb1;
            }
gm_lb2:;
            void * t49 = gm_l0;
            gm_tl874t2atnlh3 t50 = gm_l2;
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sj4n6nvsgq2jn));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(42));
            gm_b0_1 t52 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t49, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t53 = gm_l0;
            void * t54 = GM_LD(void *, ((uint8_t *)t53));
            gm_tl874t2atnlh3 t55 = gm_l2;
            void * t56 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t54, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t56;
            goto gm_fret;
            gm_l6 = ((void *)0);
        }
gm_lb1:;
        void * t57 = gm_l6;
        gm_l5 = t57;
    }
    void * t58 = gm_l5;
    gm_l4 = t58;
    void * t59 = gm_l0;
    void * t60 = GM_LD(void *, ((uint8_t *)t59));
    void * t61 = (void *)(((uint8_t *)t60) + 8);
    gm_t88rcucg1b7uk t62 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t61);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t63 = gm_l4;
    gm_tl874t2atnlh3 t64 = gm_l2;
    gm_t2b6tm8c0vgo1 t65;
    memset(&t65, 0, sizeof t65);
    GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)52ULL));
    GM_ST(void *, ((uint8_t *)&t65 + 8), t63);
    void * t66 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t62, &t65, t64);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t66;
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
__attribute__((aligned(4))) void * gm_f_variadic_arg_dfdnlo3(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_variadic_arg_dfdnlo3 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* a */
    void * gm_l3; /* v */
    gm_t88rcucg1b7uk gm_l4; /* t */
    uint16_t gm_l5; /* bits */
    uint16_t gm_l6; /* bits */
    gm_t88rcucg1b7uk gm_l7; /* inner */
    uint8_t gm_l8; /* ok */
    GM_STACK_CHECK(&F, 508u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l9;
    gm_t53at80pfdus3 gm_l10;
    gm_tabb710d9mq2e gm_l11;
    {
        {
            void * t18 = gm_l1;
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t18) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)0ULL));
            if (!t20) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), (((uint8_t *)t18) + 8), 88);
            void * t21 = gm_l0;
            void * t22 = (void *)((uint8_t *)&F.l2);
            gm_tkn42u7na3t81 t23;
            memset(&t23, 0, sizeof t23);
            void * t24 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t21, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t24;
            void * t25 = gm_l0;
            void * t26 = (void *)(((uint8_t *)t25) + 96);
            void * t27 = gm_l0;
            void * t28 = GM_LD(void *, ((uint8_t *)t27));
            void * t29 = gm_l3;
            gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t29));
            gm_t88rcucg1b7uk t31 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t26, t28, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t31;
            void * t32 = gm_l0;
            void * t33 = GM_LD(void *, ((uint8_t *)t32));
            void * t34 = (void *)(((uint8_t *)t33) + 8);
            gm_t88rcucg1b7uk t35 = gm_l4;
            gm_f_kind_jcnt4hm((void *)&gm_l10, t34, GM_CONV(gm_csguankko4lmhd, t35));
            {
                {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)0ULL));
                    if (!t37) {
                        goto gm_lb4;
                    }
                    uint16_t t38 = GM_LD(uint16_t, ((((uint8_t *)&gm_l10) + 8) + 2));
                    gm_l5 = t38;
                    uint16_t t39 = gm_l5;
                    gm_tuqvtbj0p3v9c t40;
                    GM_ST(uint16_t, ((uint8_t *)&t40 + 0), ((uint16_t)32ULL));
                    GM_ST(uint16_t, ((uint8_t *)&t40 + 2), ((uint16_t)64ULL));
                    uint8_t t41 = 0;
                    if (gm_eq9oqmmiu8qc(((uint8_t *)&t40) + 0, ((uint8_t *)&t39))) t41 = 1;
                    if (gm_eq9oqmmiu8qc(((uint8_t *)&t40) + 2, ((uint8_t *)&t39))) t41 = 1;
                    uint8_t t42 = (uint8_t)(t41);
                    gm_l9 = t42;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    {
                        {
                            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)1ULL));
                            if (!t44) {
                                goto gm_lb7;
                            }
                            goto gm_lb6;
                        }
gm_lb7:;
                        {
                            uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)2ULL));
                            if (!t46) {
                                goto gm_lb8;
                            }
                            goto gm_lb6;
                        }
gm_lb8:;
                        {
                            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)7ULL));
                            if (!t48) {
                                goto gm_lb9;
                            }
                            goto gm_lb6;
                        }
gm_lb9:;
                        {
                            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)8ULL));
                            if (!t50) {
                                goto gm_lb10;
                            }
                            goto gm_lb6;
                        }
gm_lb10:;
                        {
                            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)9ULL));
                            if (!t52) {
                                goto gm_lb11;
                            }
                            goto gm_lb6;
                        }
gm_lb11:;
                        {
                            uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                            uint8_t t54 = (uint8_t)(t53 == ((uint8_t)23ULL));
                            if (!t54) {
                                goto gm_lb12;
                            }
                            goto gm_lb6;
                        }
gm_lb12:;
                        {
                            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)26ULL));
                            if (!t56) {
                                goto gm_lb13;
                            }
                            goto gm_lb6;
                        }
gm_lb13:;
                        goto gm_lb5;
                    }
gm_lb6:;
                    gm_l9 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                    uint8_t t58 = (uint8_t)(t57 == ((uint8_t)3ULL));
                    if (!t58) {
                        goto gm_lb14;
                    }
                    uint16_t t59 = GM_LD(uint16_t, (((uint8_t *)&gm_l10) + 8));
                    gm_l6 = t59;
                    uint16_t t60 = gm_l6;
                    uint8_t t61 = ((t60 == ((uint16_t)64ULL)));
                    gm_l9 = t61;
                    goto gm_lb3;
                }
gm_lb14:;
                {
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l10) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)13ULL));
                    if (!t63) {
                        goto gm_lb15;
                    }
                    gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 8));
                    gm_l7 = t64;
                    void * t65 = gm_l0;
                    void * t66 = GM_LD(void *, ((uint8_t *)t65));
                    gm_t88rcucg1b7uk t67 = gm_l7;
                    uint8_t t68 = gm_f_is_nullable_pointer_nkie06s(t66, GM_CONV(gm_csguankko4lmhd, t67));
                    gm_l9 = t68;
                    goto gm_lb3;
                }
gm_lb15:;
                {
                    gm_l9 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb16:;
                gm_trap("no match arm matched the scrutinee", gm_srch6h8rdicu443, 636, 11);
            }
gm_lb3:;
            uint8_t t69 = gm_l9;
            gm_l8 = t69;
            uint8_t t70 = gm_l8;
            uint8_t t71 = (uint8_t)!t70;
            if (t71) {
                void * t72 = gm_l0;
                gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l2) + 72));
                void * t74 = gm_l0;
                gm_t88rcucg1b7uk t75 = gm_l4;
                gm_b16_8 t76 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t74, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t77;
                GM_ST(gm_b16_8, ((uint8_t *)&t77 + 0), t76);
                gm_f_format_kub2tep((void *)&gm_l11, (void *)NULL, cx, NULL, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srch6h8rdicu443, 643, 34);
                gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                gm_b0_1 t81 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t72, t73, t80);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t82 = gm_l3;
            F.ret = t82;
            goto gm_fret;
        }
gm_lb2:;
        void * t83 = gm_l0;
        void * t84 = gm_l1;
        gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t84) + 128));
        gm_b16_8 t86;
        GM_ST(void *, (uint8_t *)&t86, (void *)(gm_sbs1lj6q8rfiu));
        GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(43));
        gm_b0_1 t87 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t83, t85, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t88 = gm_l0;
        void * t89 = GM_LD(void *, ((uint8_t *)t88));
        void * t90 = gm_l1;
        gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t90) + 128));
        void * t92 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t89, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t92;
        goto gm_fret;
    }
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
