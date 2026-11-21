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
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t p3[6]; gm_b16_8 f3; void * f4; void * f5; gm_tl874t2atnlh3 f6; uint8_t pe[4]; } gm_t24301fas1b2a;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tndsf0a4f4lj0;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t24301fas1b2a) == 72 && _Alignof(gm_t24301fas1b2a) == 8 && offsetof(gm_t24301fas1b2a, f0) == 0 && offsetof(gm_t24301fas1b2a, f1) == 16 && offsetof(gm_t24301fas1b2a, f2) == 17 && offsetof(gm_t24301fas1b2a, f3) == 24 && offsetof(gm_t24301fas1b2a, f4) == 40 && offsetof(gm_t24301fas1b2a, f5) == 48 && offsetof(gm_t24301fas1b2a, f6) == 56, "layout of `FieldDecl`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_b16_8 f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t99vmhfllumbh;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_b16_8 f0; gm_tv2uf4leil5hq f1; } gm_taiafe1jasg4m;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { gm_t08olq97rscn3 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; void * f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tc0sbs1tp6d7f;
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
typedef struct { gm_t08olq97rscn3 f0; gm_t79rbdv4rafe5 f1; } gm_tcuumjkg5eni6;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { uint8_t f0; union { gm_ts6oqppjh0ba3 v0; } f1; } gm_tjsp7bhdn2e8j;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tndsf0a4f4lj0) == 48 && _Alignof(gm_tndsf0a4f4lj0) == 8 && offsetof(gm_tndsf0a4f4lj0, f0) == 0 && offsetof(gm_tndsf0a4f4lj0, f1) == 16 && offsetof(gm_tndsf0a4f4lj0, f2) == 24 && offsetof(gm_tndsf0a4f4lj0, f3) == 32, "layout of `Arm`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tnljbma3s3h16;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tpv68m88f9lcr;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_th2lqbo77pvsc v1; } f1; } gm_tvul7d1mdc2eq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnljbma3s3h16 v0; gm_t7bjbmukv26e7 v1; } f1; } gm_t03a3imodqi32;
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
typedef struct { gm_t1s75i08qtptd f0; gm_t08olq97rscn3 f1; gm_t79rbdv4rafe5 f2; } gm_t43d6nbdfap6p;
typedef struct { gm_b16_8 f0; gm_tjsp7bhdn2e8j f1; uint8_t p2[5]; void * f2; } gm_t4d1qrgh0vnq1;
typedef struct { gm_tvul7d1mdc2eq f0; uint8_t f1; uint8_t pe[7]; } gm_t4eh5qkgoio6s;
typedef struct { gm_t08olq97rscn3 f0; gm_tlivgedjs5ps3 f1; } gm_t70h2c775g4n7;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t99vmhfllumbh) == 72 && _Alignof(gm_t99vmhfllumbh) == 8 && offsetof(gm_t99vmhfllumbh, f0) == 0 && offsetof(gm_t99vmhfllumbh, f1) == 32 && offsetof(gm_t99vmhfllumbh, f2) == 48 && offsetof(gm_t99vmhfllumbh, f3) == 56, "layout of `Clause`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_taiafe1jasg4m) == 80 && _Alignof(gm_taiafe1jasg4m) == 8 && offsetof(gm_taiafe1jasg4m, f0) == 0 && offsetof(gm_taiafe1jasg4m, f1) == 16, "layout of `(attrs: []Attribute, binding: Binding)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tc0sbs1tp6d7f) == 80 && _Alignof(gm_tc0sbs1tp6d7f) == 8 && offsetof(gm_tc0sbs1tp6d7f, f0) == 0 && offsetof(gm_tc0sbs1tp6d7f, f1) == 32 && offsetof(gm_tc0sbs1tp6d7f, f2) == 40 && offsetof(gm_tc0sbs1tp6d7f, f3) == 56 && offsetof(gm_tc0sbs1tp6d7f, f4) == 64, "layout of `OpDecl`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tcuumjkg5eni6) == 72 && _Alignof(gm_tcuumjkg5eni6) == 8 && offsetof(gm_tcuumjkg5eni6, f0) == 0 && offsetof(gm_tcuumjkg5eni6, f1) == 32, "layout of `IfBranch`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t24301fas1b2a v0; gm_t7bjbmukv26e7 v1; } f1; } gm_tfc3oljlofib6;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjsp7bhdn2e8j) == 3 && _Alignof(gm_tjsp7bhdn2e8j) == 1 && offsetof(gm_tjsp7bhdn2e8j, f0) == 0 && offsetof(gm_tjsp7bhdn2e8j, f1) == 1, "layout of `AssignOp`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnljbma3s3h16) == 64 && _Alignof(gm_tnljbma3s3h16) == 8 && offsetof(gm_tnljbma3s3h16, f0) == 0 && offsetof(gm_tnljbma3s3h16, f1) == 32 && offsetof(gm_tnljbma3s3h16, f2) == 40 && offsetof(gm_tnljbma3s3h16, f3) == 48, "layout of `Variant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
typedef struct { uint64_t f0; gm_tvul7d1mdc2eq f1; } gm_toho9hmgj287v;
_Static_assert(sizeof(gm_tpv68m88f9lcr) == 56 && _Alignof(gm_tpv68m88f9lcr) == 8 && offsetof(gm_tpv68m88f9lcr, f0) == 0 && offsetof(gm_tpv68m88f9lcr, f1) == 32 && offsetof(gm_tpv68m88f9lcr, f2) == 40, "layout of `Constraint`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
_Static_assert(sizeof(gm_tvul7d1mdc2eq) == 32 && _Alignof(gm_tvul7d1mdc2eq) == 8 && offsetof(gm_tvul7d1mdc2eq, f0) == 0 && offsetof(gm_tvul7d1mdc2eq, f1) == 8, "layout of `Pick`");
_Static_assert(sizeof(gm_t03a3imodqi32) == 72 && _Alignof(gm_t03a3imodqi32) == 8 && offsetof(gm_t03a3imodqi32, f0) == 0 && offsetof(gm_t03a3imodqi32, f1) == 8, "layout of `UnionMember`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t43d6nbdfap6p) == 112 && _Alignof(gm_t43d6nbdfap6p) == 8 && offsetof(gm_t43d6nbdfap6p, f0) == 0 && offsetof(gm_t43d6nbdfap6p, f1) == 40 && offsetof(gm_t43d6nbdfap6p, f2) == 72, "layout of `WhileStmt`");
_Static_assert(sizeof(gm_t4d1qrgh0vnq1) == 32 && _Alignof(gm_t4d1qrgh0vnq1) == 8 && offsetof(gm_t4d1qrgh0vnq1, f0) == 0 && offsetof(gm_t4d1qrgh0vnq1, f1) == 16 && offsetof(gm_t4d1qrgh0vnq1, f2) == 24, "layout of `(places: []Expr, op: AssignOp, value: ^Expr)`");
_Static_assert(sizeof(gm_t4eh5qkgoio6s) == 40 && _Alignof(gm_t4eh5qkgoio6s) == 8 && offsetof(gm_t4eh5qkgoio6s, f0) == 0 && offsetof(gm_t4eh5qkgoio6s, f1) == 32, "layout of `?Sx`");
_Static_assert(sizeof(gm_t70h2c775g4n7) == 104 && _Alignof(gm_t70h2c775g4n7) == 8 && offsetof(gm_t70h2c775g4n7, f0) == 0 && offsetof(gm_t70h2c775g4n7, f1) == 32, "layout of `(name: Ident, sig: FnSig)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tfc3oljlofib6) == 80 && _Alignof(gm_tfc3oljlofib6) == 8 && offsetof(gm_tfc3oljlofib6, f0) == 0 && offsetof(gm_tfc3oljlofib6, f1) == 8, "layout of `StructMember`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t70h2c775g4n7 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_tg69e9d4krfe7;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
_Static_assert(sizeof(gm_toho9hmgj287v) == 40 && _Alignof(gm_toho9hmgj287v) == 8 && offsetof(gm_toho9hmgj287v, f0) == 0 && offsetof(gm_toho9hmgj287v, f1) == 8, "layout of `(i: usize, x: Sx)`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { gm_t0d02142k4tu8 f0; void * f1; void * f2; } gm_t15nkjiutlvf3;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_b16_8 f0; gm_tg69e9d4krfe7 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t7rh4gj19r55v;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tg69e9d4krfe7) == 112 && _Alignof(gm_tg69e9d4krfe7) == 8 && offsetof(gm_tg69e9d4krfe7, f0) == 0 && offsetof(gm_tg69e9d4krfe7, f1) == 8, "layout of `ForeignMemberKind`");
typedef struct { gm_t1s75i08qtptd f0; gm_t0d02142k4tu8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tnrgict8sthcv;
typedef struct { gm_t0d02142k4tu8 f0; void * f1; } gm_tuqaojgkqdcop;
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t15nkjiutlvf3) == 80 && _Alignof(gm_t15nkjiutlvf3) == 8 && offsetof(gm_t15nkjiutlvf3, f0) == 0 && offsetof(gm_t15nkjiutlvf3, f1) == 64 && offsetof(gm_t15nkjiutlvf3, f2) == 72, "layout of `(pat: Pattern, value: ^Expr, fallback: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
_Static_assert(sizeof(gm_t7rh4gj19r55v) == 144 && _Alignof(gm_t7rh4gj19r55v) == 8 && offsetof(gm_t7rh4gj19r55v, f0) == 0 && offsetof(gm_t7rh4gj19r55v, f1) == 16 && offsetof(gm_t7rh4gj19r55v, f2) == 128, "layout of `ForeignMember`");
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_taiafe1jasg4m v0; gm_t4d1qrgh0vnq1 v1; void * v2; gm_tgtub0v94d5u5 v3; void * v4; gm_t43d6nbdfap6p v5; void * v6; gm_t15nkjiutlvf3 v7; gm_t79rbdv4rafe5 v8; } f1; } gm_tem8p0t66u1ce;
_Static_assert(sizeof(gm_tnrgict8sthcv) == 120 && _Alignof(gm_tnrgict8sthcv) == 8 && offsetof(gm_tnrgict8sthcv, f0) == 0 && offsetof(gm_tnrgict8sthcv, f1) == 40 && offsetof(gm_tnrgict8sthcv, f2) == 104, "layout of `PatArg`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuqaojgkqdcop v0; void * v1; } f1; } gm_tq5j0547p8452;
_Static_assert(sizeof(gm_tuqaojgkqdcop) == 72 && _Alignof(gm_tuqaojgkqdcop) == 8 && offsetof(gm_tuqaojgkqdcop, f0) == 0 && offsetof(gm_tuqaojgkqdcop, f1) == 64, "layout of `(pat: Pattern, value: ^Expr)`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_tem8p0t66u1ce) == 120 && _Alignof(gm_tem8p0t66u1ce) == 8 && offsetof(gm_tem8p0t66u1ce, f0) == 0 && offsetof(gm_tem8p0t66u1ce, f1) == 8, "layout of `StmtKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tem8p0t66u1ce f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_to88ae1b67aev;
_Static_assert(sizeof(gm_tq5j0547p8452) == 80 && _Alignof(gm_tq5j0547p8452) == 8 && offsetof(gm_tq5j0547p8452, f0) == 0 && offsetof(gm_tq5j0547p8452, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_to88ae1b67aev) == 144 && _Alignof(gm_to88ae1b67aev) == 8 && offsetof(gm_to88ae1b67aev, f0) == 0 && offsetof(gm_to88ae1b67aev, f1) == 8 && offsetof(gm_to88ae1b67aev, f2) == 128, "layout of `Stmt`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_assign_op_name_8crhefq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_atom_of_bytes_dj32elq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_binop_name_1d6k17q {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_bnode_hu9fd4q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* rest */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_const_type_o8v21gi {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* v */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_arg_9iv8pdv {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* e */
    gm_tc3oht4ksth9d l3; /* e */
    gm_tc3oht4ksth9d l4; /* e */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_arm_8jsad7u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* pats */
    gm_t0d02142k4tu8 l4; /* p */
    gm_trqfvmjtt4sij l5; /* v */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_attr_qdd7k06 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_trqfvmjtt4sij l3; /* xs */
    gm_tc3oht4ksth9d l6; /* e */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_binding_b27qkdk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_trqfvmjtt4sij l2; /* ns */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_block_5utjqg0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_to88ae1b67aev l4; /* s */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_clause_kje51q9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* params */
    gm_trqfvmjtt4sij l5; /* v */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_cond_cu0s815 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tq5j0547p8452 l4; /* part */
    gm_t0d02142k4tu8 l5; /* pat */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_constraints_68aa75n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tpv68m88f9lcr l4; /* c */
    gm_t4eh5qkgoio6s ret;
};
struct gm_fr_dump_effect_lit_4vqnm1n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tc0sbs1tp6d7f l5; /* op */
    gm_trqfvmjtt4sij l7; /* ov */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_effects_4fqcm0g {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tc3oht4ksth9d l4; /* e */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_expr_l3bbdps {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_trqfvmjtt4sij l7; /* text */
    gm_tv1o1j44ii4db l18; /* t */
    gm_tioi3jh9v22g5 l24; /* x */
    gm_t7te2tdpebbt4 l36; /* a */
    gm_t99vmhfllumbh l69; /* c */
    gm_t7rh4gj19r55v l74; /* m */
    gm_tndsf0a4f4lj0 l79; /* a */
    gm_trqfvmjtt4sij l82; /* hs */
    gm_tc3oht4ksth9d l85; /* h */
    gm_trqfvmjtt4sij l89; /* ps */
    gm_t0d02142k4tu8 l92; /* p */
    gm_t79rbdv4rafe5 l93; /* b */
    gm_tchr52g0ema53 l95; /* b */
    gm_tet9guhev5r4u l101; /* i */
    gm_trqfvmjtt4sij l103; /* items */
    gm_t7te2tdpebbt4 l106; /* a */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_file_cno3fos {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_tvpnktv5fo8nl l5; /* item */
    gm_b0_1 ret;
};
struct gm_fr_dump_fn_lit_tchlprc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* tail */
    gm_t79rbdv4rafe5 l3; /* b */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_fn_sig_4ip3qer {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* tail */
    gm_trqfvmjtt4sij l3; /* v */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_foreign_member_vrht12f {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tlivgedjs5ps3 l3; /* sig */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_handler_decl_htmrmsf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_t99vmhfllumbh l6; /* c */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_if_3qo5nul {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tcuumjkg5eni6 l4; /* b */
    gm_trqfvmjtt4sij l5; /* bv */
    gm_t79rbdv4rafe5 l6; /* b */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_item_bs3lq5m {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tv2uf4leil5hq l4; /* b */
    gm_t7bjbmukv26e7 l5; /* w */
    gm_tcfiotghd1s5k l7; /* t */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_items_atcioo9 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* v */
    gm_tvpnktv5fo8nl l5; /* item */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_param_kr39os0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* ns */
    gm_trqfvmjtt4sij l5; /* v */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_params_vfgaabj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tf2orqcrphmek l4; /* p */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_pat_arg_ai8l7kq {
    gm_frame h;
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_pattern_q6u5c24 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_trqfvmjtt4sij l11; /* items */
    gm_tnrgict8sthcv l14; /* a */
    gm_tnrgict8sthcv l18; /* a */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_stmt_tfqg5dm {
    gm_frame h;
    gm_tv2uf4leil5hq l2; /* binding */
    gm_trqfvmjtt4sij l3; /* v */
    gm_trqfvmjtt4sij l7; /* ps */
    gm_tc3oht4ksth9d l10; /* p */
    gm_trqfvmjtt4sij l11; /* v */
    gm_t43d6nbdfap6p l16; /* w */
    gm_trqfvmjtt4sij l17; /* v */
    gm_t0d02142k4tu8 l20; /* pat */
    gm_trqfvmjtt4sij l23; /* v */
    gm_t79rbdv4rafe5 l24; /* b */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_struct_lit_n9dqlnm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tfc3oljlofib6 l6; /* m */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_struct_member_q034tdn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_t24301fas1b2a l2; /* f */
    gm_trqfvmjtt4sij l4; /* ns */
    gm_t7bjbmukv26e7 l9; /* w */
    gm_trqfvmjtt4sij l10; /* th */
    gm_tfc3oljlofib6 l13; /* x */
    gm_trqfvmjtt4sij l15; /* el */
    gm_tfc3oljlofib6 l18; /* x */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_tuple_elem_bul8rgi {
    gm_frame h;
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_union_lit_leu6qp2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_t03a3imodqi32 l7; /* m */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_union_member_4lopa45 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tnljbma3s3h16 l2; /* var */
    gm_t7bjbmukv26e7 l5; /* w */
    gm_trqfvmjtt4sij l6; /* th */
    gm_t03a3imodqi32 l9; /* x */
    gm_trqfvmjtt4sij l11; /* el */
    gm_t03a3imodqi32 l14; /* x */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_when_items_16tgm3i {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_when_stmt_hdq14ev {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_t79rbdv4rafe5 l2; /* b */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_empty_slice_bjhhdfn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_i72a9oi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fits_mco2o3v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_flat_hstd39j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_float_suffix_7g3bh9c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_format_32n178h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c0h6o29 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ii9pvls {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s4n90cc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_ident_sx_kd0v352 {
    gm_frame h;
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_is_integer_ab2ohk2 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_list_ccsq5cl {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_list_cicccjr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* rest */
    gm_trqfvmjtt4sij l3; /* items */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_node1_gpsjtpc {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* rest */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_node2_b4qpqat {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* rest */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_node_dm1r8dn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* rest */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_push_attrs_a5l439c {
    gm_frame h;
    gm_tkacitepg5fk0 l4; /* a */
    gm_b0_1 ret;
};
struct gm_fr_push_mjiecfq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_q1n4vfn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_render_7k3fqtk {
    gm_frame h;
    gm_trqfvmjtt4sij l13; /* text */
    gm_b0_1 ret;
};
struct gm_fr_str_lit_a2k13t3 {
    gm_frame h;
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_unop_name_j99tcv9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_visibility_u2ipb12 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_write_v26t591 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_b0_1 ret;
};
__attribute__((aligned(4))) gm_b16_8 gm_f_assign_op_name_8crhefq(void *env, gm_node *cx, gm_node *inst, gm_tjsp7bhdn2e8j a0);
inline __attribute__((aligned(4))) void gm_f_atom_of_bytes_dj32elq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_name_1d6k17q(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
inline __attribute__((aligned(4))) void gm_f_bnode_hu9fd4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) uint64_t gm_f_char_count_76unqru(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_const_type_o8v21gi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_dump_arg_9iv8pdv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_arm_8jsad7u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_attr_qdd7k06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_binding_b27qkdk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_block_5utjqg0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_clause_kje51q9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_cond_cu0s815(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_constraints_68aa75n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_dump_effect_lit_4vqnm1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_effects_4fqcm0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_dump_expr_l3bbdps(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_dump_file_cno3fos(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_dump_fn_lit_tchlprc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_fn_sig_4ip3qer(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_dump_foreign_member_vrht12f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_handler_decl_htmrmsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_if_3qo5nul(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_item_bs3lq5m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_items_atcioo9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_dump_param_kr39os0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_params_vfgaabj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_dump_pat_arg_ai8l7kq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_pattern_q6u5c24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_stmt_tfqg5dm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_struct_lit_n9dqlnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_struct_member_q034tdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_tuple_elem_bul8rgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_union_lit_leu6qp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_union_member_4lopa45(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_when_items_16tgm3i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_when_stmt_hdq14ev(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_bjhhdfn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_bytes_25mo226(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_i72a9oi(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_fits_mco2o3v(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_flat_hstd39j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_float_suffix_7g3bh9c(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_c0h6o29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_ii9pvls(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s4n90cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_ident_sx_kd0v352(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_tvul7d1mdc2eq gm_f_ident_sx_kd0v352_c(gm_t08olq97rscn3 a0);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0);
inline __attribute__((aligned(4))) void gm_f_list_ccsq5cl(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_list_cicccjr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint8_t a2);
inline __attribute__((aligned(4))) void gm_f_node1_gpsjtpc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_node2_b4qpqat(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_node_dm1r8dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_attrs_a5l439c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_mjiecfq(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q1n4vfn(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_7k3fqtk(void *env, gm_node *cx, gm_node *inst, void * a0, int64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_str_lit_a2k13t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_unop_name_j99tcv9(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_visibility_u2ipb12(void *gm_out, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_visibility_u2ipb12_c(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_v26t591(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
extern uint8_t gm_cddg9214lam[8] __attribute__((aligned(8)));
extern const uint8_t gm_s0gu0jo9fq1ca[11];
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s0jho24omhfui[7];
extern const uint8_t gm_s0l81imssm78p[7];
extern const uint8_t gm_s11b339fv56g1[13];
extern const uint8_t gm_s167ok8osvuve[11];
extern const uint8_t gm_s19ndlkgdtviu[6];
extern const uint8_t gm_s1jboc8lkvac0[6];
extern const uint8_t gm_s1k3lfare8sei[4];
extern const uint8_t gm_s1m38bo3naic1[8];
extern const uint8_t gm_s20t5qopq8n9u[5];
extern const uint8_t gm_s252dj7ek002d[8];
extern const uint8_t gm_s277jq8ulrihr[7];
extern const uint8_t gm_s283bs8hsleb3[9];
extern const uint8_t gm_s2f5ea3l0ep8h[9];
extern const uint8_t gm_s3bakerohlr2a[3];
extern const uint8_t gm_s3brj349ts8fs[9];
extern const uint8_t gm_s3ia9jm7npe6t[10];
extern const uint8_t gm_s3r7cg0hbrkdg[17];
extern const uint8_t gm_s3s50a53ci7rk[7];
extern const uint8_t gm_s3t7gv740qicd[10];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s4cess8gum6i7[6];
extern const uint8_t gm_s4dis7c905evg[10];
extern const uint8_t gm_s4fd35uidvgkn[4];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s5dbqnns2epkg[6];
extern const uint8_t gm_s5qkai093c1q0[6];
extern const uint8_t gm_s5rv6umk4akl0[6];
extern const uint8_t gm_s66uvortgrtsg[4];
extern const uint8_t gm_s688e8m4pgcda[6];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s6pilvbjelm5p[4];
extern const uint8_t gm_s76ud0alkjfse[4];
extern const uint8_t gm_s79a8ngj68lbf[5];
extern const uint8_t gm_s79s3h98matgr[6];
extern const uint8_t gm_s7dkbj8tvt9e3[7];
extern const uint8_t gm_s7ntlu0vonte9[12];
extern const uint8_t gm_s7s32h358ordg[6];
extern const uint8_t gm_s80ddhqa1o9i9[7];
extern const uint8_t gm_s8aj3oag1l1u0[14];
extern const uint8_t gm_s8av3ru5o85v4[6];
extern const uint8_t gm_s8e72gdumihb1[7];
extern const uint8_t gm_s8m5q4i8uoe5j[6];
extern const uint8_t gm_s8p1h96ji9kh0[6];
extern const uint8_t gm_s8rsdirj4dmun[4];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_s9es2tos1bhfp[5];
extern const uint8_t gm_s9r64t1166q1f[5];
extern const uint8_t gm_sa26g0ksgdkma[10];
extern const uint8_t gm_samlp70hmirme[3];
extern const uint8_t gm_saufuk3rs2i2m[15];
extern const uint8_t gm_sb88rajuutaep[4];
extern const uint8_t gm_sbiv3c5qa6ipi[6];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_scc8p0a9eachg[4];
extern const uint8_t gm_scqusp7lgvp02[4];
extern const uint8_t gm_scvhpklutvpjg[4];
extern const uint8_t gm_sd4qrpv3lpilp[4];
extern const uint8_t gm_sd9regb2pbgv1[8];
extern const uint8_t gm_sdfmdfp384tf7[11];
extern const uint8_t gm_sdl8mgtvebfcs[3];
extern const uint8_t gm_se31bb9e7icn2[11];
extern const uint8_t gm_seb76g152eseg[4];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_ser8ip7vtbv6c[9];
extern const uint8_t gm_ses9qtp6kdd22[8];
extern const uint8_t gm_sf3ii7gekd9ll[13];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfkmkcagsnqnb[7];
extern const uint8_t gm_sg1h22ve4uqmq[4];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_sgf1euukkdve3[14];
extern const uint8_t gm_sgmcjv273u32m[11];
extern const uint8_t gm_sgs4qi36v3o5v[8];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_sh4e4kqha06b3[7];
extern const uint8_t gm_sh4niqnmh73co[6];
extern const uint8_t gm_shhr1hlq1ethc[4];
extern const uint8_t gm_shmknkannurll[5];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_shofa8pdml9fb[13];
extern const uint8_t gm_shs3nem6t7u8g[4];
extern const uint8_t gm_si1m7q6ia0hto[7];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sjjc564tiep5s[13];
extern const uint8_t gm_sjkj5g78lejrj[4];
extern const uint8_t gm_sjnl36ocas0o1[11];
extern const uint8_t gm_sjqs7q3ostqbm[4];
extern const uint8_t gm_sk5s72mtujop8[5];
extern const uint8_t gm_sk83con0mdsfe[6];
extern const uint8_t gm_skissj10nclhr[6];
extern const uint8_t gm_sl4ns6hobicht[6];
extern const uint8_t gm_sl55obh2ok05j[4];
extern const uint8_t gm_sl5jkfh6104lp[5];
extern const uint8_t gm_sl9nvct67or45[3];
extern const uint8_t gm_sllcm1pqagh28[6];
extern const uint8_t gm_sltau19rt9o73[8];
extern const uint8_t gm_sm56lhobvb6g5[6];
extern const uint8_t gm_sm8obe455p2d9[3];
extern const uint8_t gm_sm8si2u0a1u17[5];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_smedhknhdrf46[11];
extern const uint8_t gm_smglbbtmdmip2[6];
extern const uint8_t gm_smimi08sfrg0n[5];
extern const uint8_t gm_smjq2fi49hdnf[11];
extern const uint8_t gm_smu48daue2qai[6];
extern const uint8_t gm_sn581p3lbgrjg[9];
extern const uint8_t gm_sn6sqlran7h3r[7];
extern const uint8_t gm_snas2pfnujj1i[15];
extern const uint8_t gm_snftglmif3ta2[11];
extern const uint8_t gm_so0ilvhoi1hom[5];
extern const uint8_t gm_so8qnkabacf8o[5];
extern const uint8_t gm_sobcm12sogdng[5];
extern const uint8_t gm_sofbtko7t8taa[5];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_soi4hbchhjloe[4];
extern const uint8_t gm_soifq4vd92jh0[5];
extern const uint8_t gm_spbv5h8c0ukao[5];
extern const uint8_t gm_spj9t9b8lqlpp[8];
extern const uint8_t gm_spl4qnmq2arsc[13];
extern const uint8_t gm_sqbhgmmp8kn4m[3];
extern const uint8_t gm_sqflguscq1uc5[6];
extern const uint8_t gm_sqpev88g6capg[9];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const uint8_t gm_sqrfl54mgc34i[8];
extern const uint8_t gm_squ5nrnqo0b0l[6];
extern const uint8_t gm_squedv51820an[5];
extern const uint8_t gm_sr31o7m0aohvd[9];
extern const uint8_t gm_sr899mamktpvo[7];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6b5gn9iggr1p[];
extern const char gm_srck6bk8q21r07v[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srfagufvviad7[6];
extern const uint8_t gm_srj7urnaf6a1u[9];
extern const uint8_t gm_srltb43nuabaq[5];
extern const uint8_t gm_srq9bjb7d7qo7[15];
extern const uint8_t gm_srri8c6al1v1g[5];
extern const uint8_t gm_ss496b65r9omj[6];
extern const uint8_t gm_ssbg0bg772d67[5];
extern const uint8_t gm_sskduiq2hlj37[3];
extern const uint8_t gm_ssqg2h6hrn099[9];
extern const uint8_t gm_st38p0j1i82np[11];
extern const uint8_t gm_stugmrojdno85[6];
extern const uint8_t gm_su137hja0ebcd[6];
extern const uint8_t gm_suctgno4l10a5[9];
extern const uint8_t gm_sugq7dl55ocon[7];
extern const uint8_t gm_sukutl224cgkm[8];
extern const uint8_t gm_sup2jn5hml593[6];
extern const uint8_t gm_sv0fbf330lmcd[8];
extern const uint8_t gm_svqdrhms9i231[7];
extern const uint8_t gm_svvuql4te1jp4[9];
extern const gm_tmpl gm_tpl3melp777ubs8;
extern const gm_tmpl gm_tpli0tmonaamd45;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplr35nagqnpmad;
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) void gm_f_atom_of_bytes_dj32elq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_atom_of_bytes_dj32elq F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_trqfvmjtt4sij t2;
    gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    gm_b16_8 t3 = gm_l0;
    void * t4 = (void *)((uint8_t *)&F.l1);
    gm_b0_1 t5 = gm_f_escape_bytes_25mo226((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_tvul7d1mdc2eq t8;
    GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t8 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)0ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 8), t7);
    memmove(gm_out, ((uint8_t *)&t8), 32);
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
inline __attribute__((aligned(4))) void gm_f_bnode_hu9fd4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_bnode_hu9fd4q F;
    gm_b16_8 gm_l0; /* head */
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_b16_8 t2 = gm_l0;
    gm_trqfvmjtt4sij t3 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_make_list_cicccjr((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t2, &t3, ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_bjhhdfn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_bjhhdfn F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_i72a9oi(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_i72a9oi F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_q1n4vfn(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 32);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 32)));
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
inline __attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_32n178h F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_c0h6o29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_c0h6o29 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scqusp7lgvp02));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplr35nagqnpmad), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ii9pvls(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ii9pvls F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl55obh2ok05j));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3melp777ubs8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s4n90cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_s4n90cc F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd4qrpv3lpilp));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpli0tmonaamd45), t3);
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
inline __attribute__((aligned(4))) void gm_f_ident_sx_kd0v352(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ident_sx_kd0v352 F;
    gm_t08olq97rscn3 gm_l0; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)a0));
    gm_tvul7d1mdc2eq t3;
    GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t3 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 8), t2);
    memmove(gm_out, ((uint8_t *)&t3), 32);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_ab2ohk2 F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t2 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    uint8_t t3 = ((t2 == ((uint64_t)1ULL)));
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_list_ccsq5cl(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ccsq5cl F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_bjhhdfn();
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
inline __attribute__((aligned(4))) void gm_f_node1_gpsjtpc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_node1_gpsjtpc F;
    gm_b16_8 gm_l0; /* head */
    gm_tvul7d1mdc2eq gm_l1; /* x */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_tftb1odc739t5 gm_l3;
    gm_trqfvmjtt4sij t2;
    gm_f_list_ccsq5cl((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t2;
    void * t3 = (void *)((uint8_t *)&F.l2);
    gm_tftb1odc739t5 t4 = gm_f_push_mjiecfq(t3, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l1)));
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srck6bk8q21r07v, 50, 5);
    gm_b16_8 t6 = gm_l0;
    gm_trqfvmjtt4sij t7 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t6, &t7);
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
inline __attribute__((aligned(4))) void gm_f_node_dm1r8dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_node_dm1r8dn F;
    gm_b16_8 gm_l0; /* head */
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_b16_8 t2 = gm_l0;
    gm_trqfvmjtt4sij t3 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_make_list_cicccjr((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t2, &t3, ((uint8_t)0));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_mjiecfq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_mjiecfq F;
    void * gm_l0; /* l */
    gm_tvul7d1mdc2eq gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_bjhhdfn();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_q1n4vfn(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q1n4vfn(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_q1n4vfn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tabb710d9mq2e gm_l2;
    gm_trqfvmjtt4sij t2;
    gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_b0_1 t4 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t5 = (void *)((uint8_t *)&F.l1);
    gm_f_into_slice_hq0te56((void *)&gm_l2, t5);
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srclv3pj5es0f8o, 624, 12);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_v26t591(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1) {
    struct gm_fr_write_v26t591 F;
    void * gm_l1; /* out */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l1;
    gm_b0_1 t3 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = gm_f_is_integer_ab2ohk2(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)));
    if (t4) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t5 = gm_l1;
    gm_tftb1odc739t5 t6 = gm_f_push_t83rg2m(t5, ((uint8_t)47ULL));
    gm_l2 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_src6b5gn9iggr1p, 130, 5);
    void * t8 = gm_l1;
    gm_b0_1 t9 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l0) + 48)), t8);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_assign_op_name_8crhefq(void *env, gm_node *cx, gm_node *inst, gm_tjsp7bhdn2e8j a0) {
    struct gm_fr_assign_op_name_8crhefq F;
    gm_tjsp7bhdn2e8j gm_l0; /* op */
    gm_ts6oqppjh0ba3 gm_l1; /* b */
    GM_STACK_CHECK(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l2;
    gm_tabb710d9mq2e gm_l3;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)0ULL));
            if (!t6) {
                goto gm_lb2;
            }
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sh0j2ggs1vvoa));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
            gm_l2 = t7;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)1ULL));
            if (!t9) {
                goto gm_lb3;
            }
            gm_ts6oqppjh0ba3 t10 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l0) + 1));
            gm_l1 = t10;
            gm_ts6oqppjh0ba3 t11 = gm_l1;
            gm_b16_8 t12 = gm_f_binop_text_0ljfc0r(t11);
            gm_tnpifv20irnph t13;
            GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
            gm_f_format_ii9pvls((void *)&gm_l3, (void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srck6bk8q21r07v, 175, 21);
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            gm_l2 = t16;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 173, 61);
    }
gm_lb1:;
    gm_b16_8 t17 = gm_l2;
    F.ret = t17;
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
extern __attribute__((aligned(4))) void gm_f_atom_of_bytes_dj32elq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_binop_name_1d6k17q(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_binop_name_1d6k17q F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)13ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s3s50a53ci7rk));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(6));
            gm_l1 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)15ULL));
            if (!t10) {
                goto gm_lb3;
            }
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sh4e4kqha06b3));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(6));
            gm_l1 = t11;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_ts6oqppjh0ba3 t12 = gm_l0;
            gm_b16_8 t13 = gm_f_binop_text_0ljfc0r(t12);
            gm_l1 = t13;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 158, 42);
    }
gm_lb1:;
    gm_b16_8 t14 = gm_l1;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_bnode_hu9fd4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_const_type_o8v21gi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1, void * a2) {
    struct gm_fr_const_type_o8v21gi F;
    gm_b16_8 gm_l0; /* head */
    uint8_t gm_l1; /* is_const */
    void * gm_l2; /* inner */
    GM_STACK_CHECK(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_trqfvmjtt4sij t2;
    gm_f_list_ccsq5cl((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t2;
    uint8_t t3 = gm_l1;
    if (t3) {
        void * t4 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_stugmrojdno85));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(5));
        gm_tvul7d1mdc2eq t6;
        GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t6 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t6 + 8), t5);
        gm_tftb1odc739t5 t7 = gm_f_push_mjiecfq(t4, &t6);
        gm_l4 = t7;
        uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srck6bk8q21r07v, 499, 19);
    }
    void * t9 = (void *)((uint8_t *)&F.l3);
    void * t10 = gm_l2;
    gm_tvul7d1mdc2eq t11;
    gm_f_dump_expr_l3bbdps((void *)&t11, (void *)NULL, cx, NULL, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t12 = gm_f_push_mjiecfq(t9, &t11);
    gm_l5 = t12;
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srck6bk8q21r07v, 500, 5);
    gm_b16_8 t14 = gm_l0;
    gm_trqfvmjtt4sij t15 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l3));
    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t14, &t15);
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
inline __attribute__((aligned(4))) void gm_f_dump_arg_9iv8pdv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_arg_9iv8pdv F;
    void * gm_l0; /* a */
    gm_t08olq97rscn3 gm_l2; /* n */
    GM_STACK_CHECK(&F, 238u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tvul7d1mdc2eq gm_l5;
    void * t6 = gm_l0;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t6) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)0ULL));
            if (!t8) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l1), (((uint8_t *)t6) + 8), 88);
            void * t9 = (void *)((uint8_t *)&F.l1);
            gm_f_dump_expr_l3bbdps((void *)&gm_l5, (void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t6) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)1ULL));
            if (!t11) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&gm_l2), (((uint8_t *)t6) + 8), 32);
            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t6) + 8) + 32), 88);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s19ndlkgdtviu));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(5));
            gm_tvul7d1mdc2eq t13;
            gm_f_ident_sx_kd0v352((void *)&t13, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)));
            void * t14 = (void *)((uint8_t *)&F.l3);
            gm_tvul7d1mdc2eq t15;
            gm_f_dump_expr_l3bbdps((void *)&t15, (void *)NULL, cx, NULL, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node2_b4qpqat((void *)&gm_l5, (void *)NULL, cx, NULL, t12, &t13, &t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t6) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)2ULL));
            if (!t17) {
                goto gm_lb4;
            }
            memmove(((uint8_t *)&F.l4), (((uint8_t *)t6) + 8), 88);
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s80ddhqa1o9i9));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(6));
            void * t19 = (void *)((uint8_t *)&F.l4);
            gm_tvul7d1mdc2eq t20;
            gm_f_dump_expr_l3bbdps((void *)&t20, (void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node1_gpsjtpc((void *)&gm_l5, (void *)NULL, cx, NULL, t18, &t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 510, 52);
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
inline __attribute__((aligned(4))) void gm_f_dump_arm_8jsad7u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_arm_8jsad7u F;
    void * gm_l0; /* a */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    void * gm_l6; /* g */
    GM_STACK_CHECK(&F, 550u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_trqfvmjtt4sij t9;
    gm_f_list_ccsq5cl((void *)&t9, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t9;
    void * t10 = gm_l0;
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)t10));
    gm_l2 = t11;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l3;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t17 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t15) + (uintptr_t)t17 * 64), 64);
                        void * t18 = (void *)((uint8_t *)&F.l1);
                        void * t19 = (void *)((uint8_t *)&F.l4);
                        gm_tvul7d1mdc2eq t20;
                        gm_f_dump_pattern_q6u5c24((void *)&t20, (void *)NULL, cx, NULL, t19);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t21 = gm_f_push_mjiecfq(t18, &t20);
                        gm_l7 = t21;
                        uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srck6bk8q21r07v, 534, 23);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 534, 5);
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
    gm_trqfvmjtt4sij t27;
    gm_f_list_ccsq5cl((void *)&t27, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t27;
    void * t28 = (void *)((uint8_t *)&F.l5);
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_so0ilvhoi1hom));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(4));
    gm_tvul7d1mdc2eq t30;
    gm_f_node_dm1r8dn((void *)&t30, (void *)NULL, cx, NULL, t29, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t31 = gm_f_push_mjiecfq(t28, &t30);
    gm_l8 = t31;
    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srck6bk8q21r07v, 536, 5);
    {
        {
            void * t33 = gm_l0;
            uint8_t t34 = (uint8_t)(GM_LD(void *, (((uint8_t *)t33) + 16)) != 0);
            if (!t34) {
                goto gm_lb7;
            }
            void * t35 = GM_LD(void *, (((uint8_t *)t33) + 16));
            gm_l6 = t35;
            void * t36 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sqflguscq1uc5));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(5));
            void * t38 = gm_l6;
            gm_tvul7d1mdc2eq t39;
            gm_f_dump_expr_l3bbdps((void *)&t39, (void *)NULL, cx, NULL, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t40;
            gm_f_node1_gpsjtpc((void *)&t40, (void *)NULL, cx, NULL, t37, &t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t41 = gm_f_push_mjiecfq(t36, &t40);
            gm_l9 = t41;
            uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 537, 23);
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    void * t43 = (void *)((uint8_t *)&F.l5);
    void * t44 = gm_l0;
    void * t45 = GM_LD(void *, (((uint8_t *)t44) + 24));
    gm_tvul7d1mdc2eq t46;
    gm_f_dump_expr_l3bbdps((void *)&t46, (void *)NULL, cx, NULL, t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t47 = gm_f_push_mjiecfq(t43, &t46);
    gm_l10 = t47;
    uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srck6bk8q21r07v, 538, 5);
    gm_b16_8 t49;
    GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s76ud0alkjfse));
    GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(3));
    gm_trqfvmjtt4sij t50 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l5));
    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t49, &t50);
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
inline __attribute__((aligned(4))) void gm_f_dump_attr_qdd7k06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_attr_qdd7k06 F;
    void * gm_l0; /* a */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 449u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_trqfvmjtt4sij t9;
    gm_f_list_ccsq5cl((void *)&t9, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t9;
    void * t10 = (void *)((uint8_t *)&F.l1);
    void * t11 = gm_l0;
    gm_tvul7d1mdc2eq t12;
    gm_f_ident_sx_kd0v352((void *)&t12, &(*(gm_t08olq97rscn3 *)((uint8_t *)t11)));
    gm_tftb1odc739t5 t13 = gm_f_push_mjiecfq(t10, &t12);
    gm_l7 = t13;
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srck6bk8q21r07v, 180, 5);
    {
        {
            void * t15 = gm_l0;
            uint8_t t16 = (uint8_t)(((((uint8_t *)t15) + 32))[16] != 0);
            if (!t16) {
                goto gm_lb2;
            }
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 32));
            gm_l2 = t17;
            gm_trqfvmjtt4sij t18;
            gm_f_list_ccsq5cl((void *)&t18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l3 = t18;
            gm_b16_8 t19 = gm_l2;
            gm_l4 = t19;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t20 = gm_l5;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t22 = (t20 < t21);
                    if (!t22) {
                        goto gm_lb3;
                    }
                    {
                        void * t23 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t25 = gm_l5;
                        {
                            {
                                memmove(((uint8_t *)&F.l6), (((uint8_t *)t23) + (uintptr_t)t25 * 88), 88);
                                void * t26 = (void *)((uint8_t *)&F.l3);
                                void * t27 = (void *)((uint8_t *)&F.l6);
                                gm_tvul7d1mdc2eq t28;
                                gm_f_dump_expr_l3bbdps((void *)&t28, (void *)NULL, cx, NULL, t27);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t29 = gm_f_push_mjiecfq(t26, &t28);
                                gm_l8 = t29;
                                uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                                if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srck6bk8q21r07v, 183, 25);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 183, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t31 = gm_l5;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l5 = t34;
                }
            }
gm_lb3:;
            void * t35 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s20t5qopq8n9u));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(4));
            gm_tvul7d1mdc2eq t37;
            gm_f_node_dm1r8dn((void *)&t37, (void *)NULL, cx, NULL, t36, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t38 = gm_f_push_mjiecfq(t35, &t37);
            gm_l9 = t38;
            uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 184, 9);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t40;
    GM_ST(void *, (uint8_t *)&t40, (void *)(gm_so8qnkabacf8o));
    GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(4));
    gm_trqfvmjtt4sij t41 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t40, &t41);
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
__attribute__((aligned(4))) void gm_f_dump_binding_b27qkdk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_binding_b27qkdk F;
    void * gm_l0; /* b */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t08olq97rscn3 gm_l5; /* n */
    void * gm_l6; /* t */
    GM_STACK_CHECK(&F, 781u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_trqfvmjtt4sij t9;
    gm_f_list_ccsq5cl((void *)&t9, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t9;
    void * t10 = (void *)((uint8_t *)&F.l1);
    void * t11 = gm_l0;
    gm_t9qkh0rsooc8d t12 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t11) + 32));
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
    uint8_t t14 = (uint8_t)((t13 == ((uint8_t)0ULL)));
    gm_t9qkh0rsooc8d t15;
    GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)0ULL));
    if (t14) {
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_stugmrojdno85));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(5));
        gm_l7 = t16;
    } else {
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_soi4hbchhjloe));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(3));
        gm_l7 = t17;
    }
    gm_b16_8 t18 = gm_l7;
    gm_tvul7d1mdc2eq t19;
    GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t19 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t19 + 8), t18);
    gm_tftb1odc739t5 t20 = gm_f_push_mjiecfq(t10, &t19);
    gm_l8 = t20;
    uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srck6bk8q21r07v, 261, 5);
    void * t22 = gm_l0;
    uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t22) + 8) + 8));
    uint8_t t24 = ((t23 == ((uint64_t)1ULL)));
    if (t24) {
        void * t25 = (void *)((uint8_t *)&F.l1);
        void * t26 = gm_l0;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 8));
        uint64_t t28 = GM_LD(uint64_t, ((((uint8_t *)t26) + 8) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t28)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t28, gm_srck6bk8q21r07v, 263, 25);
        gm_tvul7d1mdc2eq t29;
        gm_f_ident_sx_kd0v352((void *)&t29, &(*(gm_t08olq97rscn3 *)(((uint8_t *)t27) + (uintptr_t)((uint64_t)0ULL) * 32)));
        gm_tftb1odc739t5 t30 = gm_f_push_mjiecfq(t25, &t29);
        gm_l9 = t30;
        uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 263, 9);
    } else {
        gm_trqfvmjtt4sij t32;
        gm_f_list_ccsq5cl((void *)&t32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t32;
        void * t33 = gm_l0;
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t33) + 8));
        gm_l3 = t34;
        gm_l4 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t35 = gm_l4;
                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint8_t t37 = (t35 < t36);
                if (!t37) {
                    goto gm_lb1;
                }
                {
                    void * t38 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t t40 = gm_l4;
                    {
                        {
                            memmove(((uint8_t *)&gm_l5), (((uint8_t *)t38) + (uintptr_t)t40 * 32), 32);
                            void * t41 = (void *)((uint8_t *)&F.l2);
                            gm_tvul7d1mdc2eq t42;
                            gm_f_ident_sx_kd0v352((void *)&t42, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l5)));
                            gm_tftb1odc739t5 t43 = gm_f_push_mjiecfq(t41, &t42);
                            gm_l10 = t43;
                            uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                            if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srck6bk8q21r07v, 266, 28);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 266, 9);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t45 = gm_l4;
                uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                uint64_t t48 = r46;
                gm_l4 = t48;
            }
        }
gm_lb1:;
        void * t49 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_sm56lhobvb6g5));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(5));
        gm_tvul7d1mdc2eq t51;
        gm_f_node_dm1r8dn((void *)&t51, (void *)NULL, cx, NULL, t50, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t52 = gm_f_push_mjiecfq(t49, &t51);
        gm_l11 = t52;
        uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srck6bk8q21r07v, 267, 9);
    }
    {
        {
            void * t54 = gm_l0;
            uint8_t t55 = (uint8_t)(GM_LD(void *, (((uint8_t *)t54) + 24)) != 0);
            if (!t55) {
                goto gm_lb7;
            }
            void * t56 = GM_LD(void *, (((uint8_t *)t54) + 24));
            gm_l6 = t56;
            void * t57 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sqbhgmmp8kn4m));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(2));
            void * t59 = gm_l6;
            gm_tvul7d1mdc2eq t60;
            gm_f_dump_expr_l3bbdps((void *)&t60, (void *)NULL, cx, NULL, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t61;
            gm_f_node1_gpsjtpc((void *)&t61, (void *)NULL, cx, NULL, t58, &t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t62 = gm_f_push_mjiecfq(t57, &t61);
            gm_l12 = t62;
            uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srck6bk8q21r07v, 269, 20);
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    void * t64 = (void *)((uint8_t *)&F.l1);
    void * t65 = gm_l0;
    void * t66 = GM_LD(void *, (((uint8_t *)t65) + 40));
    gm_tvul7d1mdc2eq t67;
    gm_f_dump_expr_l3bbdps((void *)&t67, (void *)NULL, cx, NULL, t66);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t68 = gm_f_push_mjiecfq(t64, &t67);
    gm_l13 = t68;
    uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srck6bk8q21r07v, 270, 5);
    gm_b16_8 t70;
    GM_ST(void *, (uint8_t *)&t70, (void *)(gm_s1m38bo3naic1));
    GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(7));
    gm_trqfvmjtt4sij t71 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t70, &t71);
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
inline __attribute__((aligned(4))) void gm_f_dump_block_5utjqg0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_block_5utjqg0 F;
    void * gm_l0; /* b */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 262u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    gm_trqfvmjtt4sij t7;
    gm_f_list_ccsq5cl((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t7;
    void * t8 = gm_l0;
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 8));
    gm_l2 = t9;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l3;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t15 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t13) + (uintptr_t)t15 * 144), 144);
                        void * t16 = (void *)((uint8_t *)&F.l1);
                        void * t17 = (void *)((uint8_t *)&F.l4);
                        gm_tvul7d1mdc2eq t18;
                        gm_f_dump_stmt_tfqg5dm((void *)&t18, (void *)NULL, cx, NULL, t17);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t19 = gm_f_push_mjiecfq(t16, &t18);
                        gm_l5 = t19;
                        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srck6bk8q21r07v, 276, 24);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 276, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t21 = gm_l3;
            uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
            uint64_t t24 = r22;
            gm_l3 = t24;
        }
    }
gm_lb1:;
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s688e8m4pgcda));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(5));
    gm_trqfvmjtt4sij t26 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t25, &t26);
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
inline __attribute__((aligned(4))) void gm_f_dump_clause_kje51q9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_clause_kje51q9 F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t08olq97rscn3 gm_l4; /* p */
    GM_STACK_CHECK(&F, 501u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_trqfvmjtt4sij t7;
    gm_f_list_ccsq5cl((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t7;
    void * t8 = gm_l0;
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 32));
    gm_l2 = t9;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l3;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t15 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t13) + (uintptr_t)t15 * 32), 32);
                        void * t16 = (void *)((uint8_t *)&F.l1);
                        gm_tvul7d1mdc2eq t17;
                        gm_f_ident_sx_kd0v352((void *)&t17, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l4)));
                        gm_tftb1odc739t5 t18 = gm_f_push_mjiecfq(t16, &t17);
                        gm_l6 = t18;
                        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srck6bk8q21r07v, 243, 25);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 243, 5);
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
    gm_trqfvmjtt4sij t24;
    gm_f_list_ccsq5cl((void *)&t24, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t24;
    void * t25 = (void *)((uint8_t *)&F.l5);
    void * t26 = gm_l0;
    gm_tvul7d1mdc2eq t27;
    gm_f_ident_sx_kd0v352((void *)&t27, &(*(gm_t08olq97rscn3 *)((uint8_t *)t26)));
    gm_tftb1odc739t5 t28 = gm_f_push_mjiecfq(t25, &t27);
    gm_l7 = t28;
    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srck6bk8q21r07v, 245, 5);
    void * t30 = (void *)((uint8_t *)&F.l5);
    gm_b16_8 t31;
    GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sfkmkcagsnqnb));
    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(6));
    gm_tvul7d1mdc2eq t32;
    gm_f_node_dm1r8dn((void *)&t32, (void *)NULL, cx, NULL, t31, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t33 = gm_f_push_mjiecfq(t30, &t32);
    gm_l8 = t33;
    uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srck6bk8q21r07v, 246, 5);
    void * t35 = (void *)((uint8_t *)&F.l5);
    void * t36 = gm_l0;
    void * t37 = GM_LD(void *, (((uint8_t *)t36) + 48));
    gm_tvul7d1mdc2eq t38;
    gm_f_dump_expr_l3bbdps((void *)&t38, (void *)NULL, cx, NULL, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t39 = gm_f_push_mjiecfq(t35, &t38);
    gm_l9 = t39;
    uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 247, 5);
    gm_b16_8 t41;
    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s7dkbj8tvt9e3));
    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(6));
    gm_trqfvmjtt4sij t42 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l5));
    gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t41, &t42);
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
inline __attribute__((aligned(4))) void gm_f_dump_cond_cu0s815(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_cond_cu0s815 F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    void * gm_l6; /* value */
    void * gm_l7; /* e */
    GM_STACK_CHECK(&F, 394u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tvul7d1mdc2eq gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_trqfvmjtt4sij t10;
    gm_f_list_ccsq5cl((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t10;
    void * t11 = gm_l0;
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)t11));
    gm_l2 = t12;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t13 = gm_l3;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t18 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t16) + (uintptr_t)t18 * 80), 80);
                        void * t19 = (void *)((uint8_t *)&F.l1);
                        {
                            {
                                uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                uint8_t t21 = (uint8_t)(t20 == ((uint8_t)0ULL));
                                if (!t21) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&F.l5), (((uint8_t *)&F.l4) + 8), 64);
                                void * t22 = GM_LD(void *, ((((uint8_t *)&F.l4) + 8) + 64));
                                gm_l6 = t22;
                                gm_b16_8 t23;
                                GM_ST(void *, (uint8_t *)&t23, (void *)(gm_shmknkannurll));
                                GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
                                void * t24 = (void *)((uint8_t *)&F.l5);
                                gm_tvul7d1mdc2eq t25;
                                gm_f_dump_pattern_q6u5c24((void *)&t25, (void *)NULL, cx, NULL, t24);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t26 = gm_l6;
                                gm_tvul7d1mdc2eq t27;
                                gm_f_dump_expr_l3bbdps((void *)&t27, (void *)NULL, cx, NULL, t26);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_f_node2_b4qpqat((void *)&gm_l8, (void *)NULL, cx, NULL, t23, &t25, &t27);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
                                if (!t29) {
                                    goto gm_lb8;
                                }
                                void * t30 = GM_LD(void *, (((uint8_t *)&F.l4) + 8));
                                gm_l7 = t30;
                                void * t31 = gm_l7;
                                gm_f_dump_expr_l3bbdps((void *)&gm_l8, (void *)NULL, cx, NULL, t31);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 335, 16);
                        }
gm_lb6:;
                        gm_tftb1odc739t5 t32 = gm_f_push_mjiecfq(t19, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l8)));
                        gm_l9 = t32;
                        uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 335, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 334, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t34 = gm_l3;
            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
            uint64_t t37 = r35;
            gm_l3 = t37;
        }
    }
gm_lb1:;
    gm_b16_8 t38;
    GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sobcm12sogdng));
    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(4));
    gm_trqfvmjtt4sij t39 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t38, &t39);
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
inline __attribute__((aligned(4))) void gm_f_dump_constraints_68aa75n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dump_constraints_68aa75n F;
    gm_b16_8 gm_l0; /* cs */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 423u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
    if (t8) {
        gm_t4eh5qkgoio6s t9;
        memset(&t9, 0, sizeof t9);
        memmove(gm_out, ((uint8_t *)&t9), 40);
        goto gm_fdone;
    }
    gm_trqfvmjtt4sij t10;
    gm_f_list_ccsq5cl((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t10;
    gm_b16_8 t11 = gm_l0;
    gm_l2 = t11;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l3;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t17 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t15) + (uintptr_t)t17 * 56), 56);
                        void * t18 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t19;
                        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sjnl36ocas0o1));
                        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(10));
                        gm_tvul7d1mdc2eq t20;
                        gm_f_ident_sx_kd0v352((void *)&t20, &(*(gm_t08olq97rscn3 *)((uint8_t *)&F.l4)));
                        void * t21 = GM_LD(void *, (((uint8_t *)&F.l4) + 32));
                        gm_tvul7d1mdc2eq t22;
                        gm_f_dump_expr_l3bbdps((void *)&t22, (void *)NULL, cx, NULL, t21);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tvul7d1mdc2eq t23;
                        gm_f_node2_b4qpqat((void *)&t23, (void *)NULL, cx, NULL, t19, &t20, &t22);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t24 = gm_f_push_mjiecfq(t18, &t23);
                        gm_l5 = t24;
                        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                        if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srck6bk8q21r07v, 575, 19);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 575, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t26 = gm_l3;
            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
            uint64_t t29 = r27;
            gm_l3 = t29;
        }
    }
gm_lb1:;
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_smglbbtmdmip2));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(5));
    gm_tvul7d1mdc2eq t31;
    gm_f_node_dm1r8dn((void *)&t31, (void *)NULL, cx, NULL, t30, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4eh5qkgoio6s t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&t32 + 36, 0);
    GM_ST(gm_tvul7d1mdc2eq, ((uint8_t *)&t32), t31);
    ((uint8_t *)&t32)[32] = 1;
    memmove(gm_out, ((uint8_t *)&t32), 40);
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
__attribute__((aligned(4))) void gm_f_dump_effect_lit_4vqnm1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_effect_lit_4vqnm1n F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l2; /* p */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* kind */
    void * gm_l8; /* r */
    GM_STACK_CHECK(&F, 831u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l9;
    gm_b16_8 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_trqfvmjtt4sij t15;
    gm_f_list_ccsq5cl((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t15;
    {
        {
            void * t16 = gm_l0;
            uint8_t t17 = (uint8_t)((((uint8_t *)t16))[16] != 0);
            if (!t17) {
                goto gm_lb2;
            }
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)t16));
            gm_l2 = t18;
            void * t19 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t20 = gm_l2;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            gm_tvul7d1mdc2eq t22;
            gm_f_dump_params_vfgaabj((void *)&t22, (void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t23 = gm_f_push_mjiecfq(t19, &t22);
            gm_l9 = t23;
            uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 661, 24);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t25 = gm_l0;
    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 24));
    gm_l3 = t26;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t27 = gm_l4;
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t29 = (t27 < t28);
            if (!t29) {
                goto gm_lb3;
            }
            {
                void * t30 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t32 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t30) + (uintptr_t)t32 * 80), 80);
                        {
                            {
                                uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 32) + 0));
                                uint8_t t34 = (uint8_t)(t33 == ((uint8_t)0ULL));
                                if (!t34) {
                                    goto gm_lb9;
                                }
                                gm_b16_8 t35;
                                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s579fb7l5l6df));
                                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(2));
                                gm_l10 = t35;
                                goto gm_lb8;
                            }
gm_lb9:;
                            {
                                uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 32) + 0));
                                uint8_t t37 = (uint8_t)(t36 == ((uint8_t)1ULL));
                                if (!t37) {
                                    goto gm_lb10;
                                }
                                gm_b16_8 t38;
                                GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sb88rajuutaep));
                                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(3));
                                gm_l10 = t38;
                                goto gm_lb8;
                            }
gm_lb10:;
                            {
                                uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 32) + 0));
                                uint8_t t40 = (uint8_t)(t39 == ((uint8_t)2ULL));
                                if (!t40) {
                                    goto gm_lb11;
                                }
                                gm_b16_8 t41;
                                GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s66uvortgrtsg));
                                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(3));
                                gm_l10 = t41;
                                goto gm_lb8;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 663, 17);
                        }
gm_lb8:;
                        gm_b16_8 t42 = gm_l10;
                        gm_l6 = t42;
                        gm_trqfvmjtt4sij t43;
                        gm_f_list_ccsq5cl((void *)&t43, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l7 = t43;
                        void * t44 = (void *)((uint8_t *)&F.l7);
                        gm_tvul7d1mdc2eq t45;
                        gm_f_ident_sx_kd0v352((void *)&t45, &(*(gm_t08olq97rscn3 *)((uint8_t *)&F.l5)));
                        gm_tftb1odc739t5 t46 = gm_f_push_mjiecfq(t44, &t45);
                        gm_l11 = t46;
                        uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srck6bk8q21r07v, 669, 9);
                        void * t48 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t49 = gm_l6;
                        gm_tvul7d1mdc2eq t50;
                        GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t50 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 8), t49);
                        gm_tftb1odc739t5 t51 = gm_f_push_mjiecfq(t48, &t50);
                        gm_l12 = t51;
                        uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srck6bk8q21r07v, 670, 9);
                        void * t53 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                        gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                        gm_tvul7d1mdc2eq t56;
                        gm_f_dump_params_vfgaabj((void *)&t56, (void *)NULL, cx, NULL, t55);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t57 = gm_f_push_mjiecfq(t53, &t56);
                        gm_l13 = t57;
                        uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srck6bk8q21r07v, 671, 9);
                        {
                            {
                                uint8_t t59 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l5) + 56)) != 0);
                                if (!t59) {
                                    goto gm_lb13;
                                }
                                void * t60 = GM_LD(void *, (((uint8_t *)&F.l5) + 56));
                                gm_l8 = t60;
                                void * t61 = (void *)((uint8_t *)&F.l7);
                                gm_b16_8 t62;
                                GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s1k3lfare8sei));
                                GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(3));
                                void * t63 = gm_l8;
                                gm_tvul7d1mdc2eq t64;
                                gm_f_dump_expr_l3bbdps((void *)&t64, (void *)NULL, cx, NULL, t63);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tvul7d1mdc2eq t65;
                                gm_f_node1_gpsjtpc((void *)&t65, (void *)NULL, cx, NULL, t62, &t64);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t66 = gm_f_push_mjiecfq(t61, &t65);
                                gm_l14 = t66;
                                uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                                if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srck6bk8q21r07v, 672, 26);
                                goto gm_lb12;
                            }
gm_lb13:;
                        }
gm_lb12:;
                        void * t68 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t69;
                        GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sskduiq2hlj37));
                        GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(2));
                        gm_tvul7d1mdc2eq t70;
                        gm_f_node_dm1r8dn((void *)&t70, (void *)NULL, cx, NULL, t69, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l7)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t71 = gm_f_push_mjiecfq(t68, &t70);
                        gm_l15 = t71;
                        uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                        if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srck6bk8q21r07v, 673, 9);
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 662, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t73 = gm_l4;
            uint64_t r74; bool o75 = __builtin_add_overflow(t73, ((uint64_t)1ULL), &r74);
            uint64_t t76 = r74;
            gm_l4 = t76;
        }
    }
gm_lb3:;
    gm_b16_8 t77;
    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_svqdrhms9i231));
    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(6));
    gm_trqfvmjtt4sij t78 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t77, &t78);
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
inline __attribute__((aligned(4))) void gm_f_dump_effects_4fqcm0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dump_effects_4fqcm0g F;
    gm_b16_8 gm_l0; /* effects */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 262u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    gm_trqfvmjtt4sij t7;
    gm_f_list_ccsq5cl((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t7;
    gm_b16_8 t8 = gm_l0;
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
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t12) + (uintptr_t)t14 * 88), 88);
                        void * t15 = (void *)((uint8_t *)&F.l1);
                        void * t16 = (void *)((uint8_t *)&F.l4);
                        gm_tvul7d1mdc2eq t17;
                        gm_f_dump_expr_l3bbdps((void *)&t17, (void *)NULL, cx, NULL, t16);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t18 = gm_f_push_mjiecfq(t15, &t17);
                        gm_l5 = t18;
                        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srck6bk8q21r07v, 253, 24);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 253, 5);
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
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sgbumttiou4de));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(7));
    gm_trqfvmjtt4sij t25 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t24, &t25);
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
__attribute__((aligned(4))) void gm_f_dump_expr_l3bbdps(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_expr_l3bbdps F;
    void * gm_l0; /* e */
    void * gm_l2; /* l */
    gm_tj2m9dbh1ekv6 gm_l3; /* s */
    uint32_t gm_l4; /* n */
    uint32_t gm_l5; /* n */
    void * gm_l6; /* l */
    gm_t9qkh0rsooc8d gm_l8; /* s */
    gm_tibupt4dmcqai gm_l9; /* s */
    uint8_t gm_l10; /* b */
    gm_t08olq97rscn3 gm_l11; /* id */
    gm_t08olq97rscn3 gm_l12; /* id */
    void * gm_l13; /* x */
    void * gm_l14; /* x */
    gm_b16_8 gm_l15; /* elems */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l19; /* op */
    void * gm_l20; /* operand */
    gm_ts6oqppjh0ba3 gm_l21; /* op */
    void * gm_l22; /* lhs */
    void * gm_l23; /* rhs */
    gm_t08olq97rscn3 gm_l25; /* c */
    void * gm_l26; /* x */
    void * gm_l27; /* x */
    void * gm_l28; /* lhs */
    void * gm_l29; /* rhs */
    void * gm_l30; /* x */
    void * gm_l31; /* callee */
    gm_b16_8 gm_l32; /* args */
    uint8_t gm_l33; /* trailing_comma */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    void * gm_l37; /* base */
    void * gm_l38; /* index */
    gm_tao63pk3r18v5 gm_l39; /* x */
    void * gm_l40; /* lo */
    void * gm_l41; /* hi */
    void * gm_l42; /* base */
    gm_t08olq97rscn3 gm_l43; /* n */
    void * gm_l44; /* base */
    uint32_t gm_l45; /* index */
    void * gm_l46; /* x */
    uint8_t gm_l47; /* is_const */
    void * gm_l48; /* inner */
    uint8_t gm_l49; /* is_const */
    void * gm_l50; /* inner */
    uint8_t gm_l51; /* is_const */
    void * gm_l52; /* inner */
    gm_t3mf58gv9kf3n gm_l53; /* a */
    gm_tvul7d1mdc2eq gm_l54; /* length */
    void * gm_l55; /* l */
    void * gm_l56; /* err */
    void * gm_l57; /* ok */
    void * gm_l58; /* sig */
    void * gm_l59; /* x */
    void * gm_l60; /* s */
    void * gm_l61; /* u */
    void * gm_l62; /* eff */
    gm_b16_8 gm_l63; /* attrs */
    void * gm_l64; /* inner */
    void * gm_l65; /* f */
    void * gm_l66; /* h */
    gm_b16_8 gm_l67; /* <tmp> */
    uint64_t gm_l68; /* <tmp> */
    gm_tibupt4dmcqai gm_l70; /* s */
    void * gm_l71; /* f */
    gm_b16_8 gm_l72; /* <tmp> */
    uint64_t gm_l73; /* <tmp> */
    void * gm_l75; /* i */
    void * gm_l76; /* m */
    gm_b16_8 gm_l77; /* <tmp> */
    uint64_t gm_l78; /* <tmp> */
    void * gm_l80; /* w */
    gm_t08olq97rscn3 gm_l81; /* l */
    gm_b16_8 gm_l83; /* <tmp> */
    uint64_t gm_l84; /* <tmp> */
    void * gm_l86; /* x */
    void * gm_l87; /* f */
    gm_t08olq97rscn3 gm_l88; /* l */
    gm_b16_8 gm_l90; /* <tmp> */
    uint64_t gm_l91; /* <tmp> */
    void * gm_l94; /* x */
    gm_t08olq97rscn3 gm_l96; /* l */
    void * gm_l97; /* x */
    gm_t08olq97rscn3 gm_l98; /* l */
    gm_t3mf58gv9kf3n gm_l99; /* r */
    void * gm_l100; /* x */
    gm_b16_8 gm_l102; /* xs */
    gm_b16_8 gm_l104; /* <tmp> */
    uint64_t gm_l105; /* <tmp> */
    GM_STACK_CHECK(&F, 8534u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l107;
    gm_b16_8 gm_l108;
    gm_tabb710d9mq2e gm_l109;
    gm_tabb710d9mq2e gm_l110;
    gm_tftb1odc739t5 gm_l111;
    gm_b16_8 gm_l112;
    gm_tftb1odc739t5 gm_l113;
    gm_tftb1odc739t5 gm_l114;
    gm_b16_8 gm_l115;
    gm_b16_8 gm_l116;
    gm_tftb1odc739t5 gm_l117;
    gm_tftb1odc739t5 gm_l118;
    gm_tftb1odc739t5 gm_l119;
    gm_tftb1odc739t5 gm_l120;
    gm_tftb1odc739t5 gm_l121;
    gm_tftb1odc739t5 gm_l122;
    gm_tftb1odc739t5 gm_l123;
    gm_tftb1odc739t5 gm_l124;
    gm_tftb1odc739t5 gm_l125;
    gm_tftb1odc739t5 gm_l126;
    gm_tftb1odc739t5 gm_l127;
    gm_tftb1odc739t5 gm_l128;
    gm_tftb1odc739t5 gm_l129;
    gm_tftb1odc739t5 gm_l130;
    gm_tabb710d9mq2e gm_l131;
    gm_tftb1odc739t5 gm_l132;
    gm_tftb1odc739t5 gm_l133;
    gm_tftb1odc739t5 gm_l134;
    gm_tftb1odc739t5 gm_l135;
    gm_tftb1odc739t5 gm_l136;
    gm_tftb1odc739t5 gm_l137;
    gm_tftb1odc739t5 gm_l138;
    gm_tftb1odc739t5 gm_l139;
    gm_tftb1odc739t5 gm_l140;
    gm_tftb1odc739t5 gm_l141;
    gm_tftb1odc739t5 gm_l142;
    gm_tftb1odc739t5 gm_l143;
    gm_tftb1odc739t5 gm_l144;
    gm_tftb1odc739t5 gm_l145;
    gm_tftb1odc739t5 gm_l146;
    gm_tftb1odc739t5 gm_l147;
    gm_tftb1odc739t5 gm_l148;
    gm_tftb1odc739t5 gm_l149;
    gm_tftb1odc739t5 gm_l150;
    gm_tftb1odc739t5 gm_l151;
    gm_tftb1odc739t5 gm_l152;
    gm_tftb1odc739t5 gm_l153;
    gm_tftb1odc739t5 gm_l154;
    gm_tftb1odc739t5 gm_l155;
    {
        gm_trqfvmjtt4sij t127;
        gm_f_list_ccsq5cl((void *)&t127, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t127;
        void * t128 = gm_l0;
        {
            {
                {
                    uint8_t t129 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t130 = (uint8_t)(t129 == ((uint8_t)0ULL));
                    if (!t130) {
                        goto gm_lb2;
                    }
                    void * t131 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l2 = t131;
                    void * t132 = (void *)((uint8_t *)&F.l1);
                    void * t133 = gm_l2;
                    gm_b16_8 t134 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)t133)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                    gm_tvul7d1mdc2eq t136;
                    GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t136 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t136 + 8), t135);
                    gm_tftb1odc739t5 t137 = gm_f_push_mjiecfq(t132, &t136);
                    gm_l107 = t137;
                    uint8_t t138 = (uint8_t)!((((uint8_t *)&gm_l107))[0] != 0);
                    if (!t138) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l107) + 1, gm_srck6bk8q21r07v, 349, 13);
                    {
                        {
                            void * t139 = gm_l2;
                            uint8_t t140 = (uint8_t)(((((uint8_t *)t139) + 48))[8] != 0);
                            if (!t140) {
                                goto gm_lb4;
                            }
                            gm_tj2m9dbh1ekv6 t141 = GM_LD(gm_tj2m9dbh1ekv6, (((uint8_t *)t139) + 48));
                            gm_l3 = t141;
                            void * t142 = (void *)((uint8_t *)&F.l1);
                            {
                                {
                                    uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                    uint8_t t144 = (uint8_t)(t143 == ((uint8_t)0ULL));
                                    if (!t144) {
                                        goto gm_lb6;
                                    }
                                    uint32_t t145 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
                                    gm_l4 = t145;
                                    uint32_t t146 = gm_l4;
                                    gm_t88rcucg1b7uk t147;
                                    GM_ST(uint32_t, ((uint8_t *)&t147 + 0), t146);
                                    gm_f_format_s4n90cc((void *)&gm_l109, (void *)NULL, cx, NULL, t147);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t148 = (uint8_t)!((((uint8_t *)&gm_l109))[0] != 0);
                                    if (!t148) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l109) + 8, gm_srck6bk8q21r07v, 352, 30);
                                    gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l109) + 8));
                                    gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                                    gm_l108 = t150;
                                    goto gm_lb5;
                                }
gm_lb6:;
                                {
                                    uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                    uint8_t t152 = (uint8_t)(t151 == ((uint8_t)1ULL));
                                    if (!t152) {
                                        goto gm_lb7;
                                    }
                                    uint32_t t153 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
                                    gm_l5 = t153;
                                    uint32_t t154 = gm_l5;
                                    gm_t88rcucg1b7uk t155;
                                    GM_ST(uint32_t, ((uint8_t *)&t155 + 0), t154);
                                    gm_f_format_c0h6o29((void *)&gm_l110, (void *)NULL, cx, NULL, t155);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t156 = (uint8_t)!((((uint8_t *)&gm_l110))[0] != 0);
                                    if (!t156) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l110) + 8, gm_srck6bk8q21r07v, 353, 30);
                                    gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l110) + 8));
                                    gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
                                    gm_l108 = t158;
                                    goto gm_lb5;
                                }
gm_lb7:;
                                {
                                    uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                    uint8_t t160 = (uint8_t)(t159 == ((uint8_t)2ULL));
                                    if (!t160) {
                                        goto gm_lb8;
                                    }
                                    gm_b16_8 t161;
                                    GM_ST(void *, (uint8_t *)&t161, (void *)(gm_s6groq37ram3h));
                                    GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(5));
                                    gm_l108 = t161;
                                    goto gm_lb5;
                                }
gm_lb8:;
                                {
                                    uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                    uint8_t t163 = (uint8_t)(t162 == ((uint8_t)3ULL));
                                    if (!t163) {
                                        goto gm_lb9;
                                    }
                                    gm_b16_8 t164;
                                    GM_ST(void *, (uint8_t *)&t164, (void *)(gm_s0i3i86nphpsh));
                                    GM_ST(uintptr_t, (uint8_t *)&t164 + 8, (uintptr_t)(5));
                                    gm_l108 = t164;
                                    goto gm_lb5;
                                }
gm_lb9:;
                                gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 351, 30);
                            }
gm_lb5:;
                            gm_b16_8 t165 = gm_l108;
                            gm_tvul7d1mdc2eq t166;
                            GM_ST(uint8_t, (uint8_t *)&t166 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t166 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t166 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t166 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t166 + 8), t165);
                            gm_tftb1odc739t5 t167 = gm_f_push_mjiecfq(t142, &t166);
                            gm_l111 = t167;
                            uint8_t t168 = (uint8_t)!((((uint8_t *)&gm_l111))[0] != 0);
                            if (!t168) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l111) + 1, gm_srck6bk8q21r07v, 351, 17);
                            goto gm_lb3;
                        }
gm_lb4:;
                    }
gm_lb3:;
                    void * t169 = gm_l2;
                    uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)t169) + 60));
                    if (t170) {
                        gm_b16_8 t171;
                        GM_ST(void *, (uint8_t *)&t171, (void *)(gm_ssbg0bg772d67));
                        GM_ST(uintptr_t, (uint8_t *)&t171 + 8, (uintptr_t)(4));
                        gm_l112 = t171;
                    } else {
                        gm_b16_8 t172;
                        GM_ST(void *, (uint8_t *)&t172, (void *)(gm_sjkj5g78lejrj));
                        GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(3));
                        gm_l112 = t172;
                    }
                    gm_b16_8 t173 = gm_l112;
                    gm_trqfvmjtt4sij t174 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t173, &t174);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t175 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t176 = (uint8_t)(t175 == ((uint8_t)1ULL));
                    if (!t176) {
                        goto gm_lb10;
                    }
                    void * t177 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l6 = t177;
                    gm_trqfvmjtt4sij t178;
                    gm_f_list_uvfu1k1((void *)&t178, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l7 = t178;
                    void * t179 = gm_l6;
                    void * t180 = (void *)((uint8_t *)&F.l7);
                    gm_b0_1 t181 = gm_f_write_v26t591((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)t179)), t180);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t182 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t183 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                    gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&t183));
                    gm_tvul7d1mdc2eq t185;
                    GM_ST(uint8_t, (uint8_t *)&t185 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t185 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t185 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t185 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t185 + 8), t184);
                    gm_tftb1odc739t5 t186 = gm_f_push_mjiecfq(t182, &t185);
                    gm_l113 = t186;
                    uint8_t t187 = (uint8_t)!((((uint8_t *)&gm_l113))[0] != 0);
                    if (!t187) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l113) + 1, gm_srck6bk8q21r07v, 363, 13);
                    {
                        {
                            void * t188 = gm_l6;
                            uint8_t t189 = (uint8_t)(((((uint8_t *)t188) + 96))[1] != 0);
                            if (!t189) {
                                goto gm_lb12;
                            }
                            gm_t9qkh0rsooc8d t190 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t188) + 96));
                            gm_l8 = t190;
                            void * t191 = (void *)((uint8_t *)&F.l1);
                            gm_t9qkh0rsooc8d t192 = gm_l8;
                            gm_b16_8 t193 = gm_f_float_suffix_7g3bh9c(t192);
                            gm_tvul7d1mdc2eq t194;
                            GM_ST(uint8_t, (uint8_t *)&t194 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t194 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t194 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t194 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t194 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t194 + 8), t193);
                            gm_tftb1odc739t5 t195 = gm_f_push_mjiecfq(t191, &t194);
                            gm_l114 = t195;
                            uint8_t t196 = (uint8_t)!((((uint8_t *)&gm_l114))[0] != 0);
                            if (!t196) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l114) + 1, gm_srck6bk8q21r07v, 364, 32);
                            goto gm_lb11;
                        }
gm_lb12:;
                    }
gm_lb11:;
                    void * t197 = gm_l6;
                    uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)t197) + 98));
                    if (t198) {
                        gm_b16_8 t199;
                        GM_ST(void *, (uint8_t *)&t199, (void *)(gm_soifq4vd92jh0));
                        GM_ST(uintptr_t, (uint8_t *)&t199 + 8, (uintptr_t)(4));
                        gm_l115 = t199;
                    } else {
                        gm_b16_8 t200;
                        GM_ST(void *, (uint8_t *)&t200, (void *)(gm_s4cess8gum6i7));
                        GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(5));
                        gm_l115 = t200;
                    }
                    gm_b16_8 t201 = gm_l115;
                    gm_trqfvmjtt4sij t202 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t201, &t202);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb10:;
                {
                    uint8_t t203 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t204 = (uint8_t)(t203 == ((uint8_t)2ULL));
                    if (!t204) {
                        goto gm_lb13;
                    }
                    memmove(((uint8_t *)&gm_l9), ((((uint8_t *)t128) + 8) + 8), 24);
                    gm_tibupt4dmcqai t205 = GM_LD(gm_tibupt4dmcqai, ((uint8_t *)&gm_l9));
                    gm_f_str_lit_a2k13t3((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, &t205);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb13:;
                {
                    uint8_t t206 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t207 = (uint8_t)(t206 == ((uint8_t)3ULL));
                    if (!t207) {
                        goto gm_lb14;
                    }
                    uint8_t t208 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 8));
                    gm_l10 = t208;
                    gm_b16_8 t209;
                    GM_ST(void *, (uint8_t *)&t209, (void *)(gm_s929lennfd3g6));
                    GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(4));
                    uint8_t t210 = gm_l10;
                    if (t210) {
                        gm_b16_8 t211;
                        GM_ST(void *, (uint8_t *)&t211, (void *)(gm_sehko5bb96s6p));
                        GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(4));
                        gm_l116 = t211;
                    } else {
                        gm_b16_8 t212;
                        GM_ST(void *, (uint8_t *)&t212, (void *)(gm_sogehh8t1m7tn));
                        GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(5));
                        gm_l116 = t212;
                    }
                    gm_b16_8 t213 = gm_l116;
                    gm_tvul7d1mdc2eq t214;
                    GM_ST(uint8_t, (uint8_t *)&t214 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t214 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t214 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t214 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t214 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t214 + 8), t213);
                    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t209, &t214);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb14:;
            }
            {
                {
                    uint8_t t215 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t216 = (uint8_t)(t215 == ((uint8_t)4ULL));
                    if (!t216) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t217;
                    GM_ST(void *, (uint8_t *)&t217, (void *)(gm_squedv51820an));
                    GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(4));
                    gm_tvul7d1mdc2eq t218;
                    GM_ST(uint8_t, (uint8_t *)&t218 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t218 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t218 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t218 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t218 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t218 + 8), t217);
                    memmove(gm_out, ((uint8_t *)&t218), 32);
                    goto gm_fdone;
                }
gm_lb15:;
                {
                    uint8_t t219 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t220 = (uint8_t)(t219 == ((uint8_t)5ULL));
                    if (!t220) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t221;
                    GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sr899mamktpvo));
                    GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(6));
                    gm_tvul7d1mdc2eq t222;
                    GM_ST(uint8_t, (uint8_t *)&t222 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t222 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t222 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t222 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t222 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t222 + 8), t221);
                    memmove(gm_out, ((uint8_t *)&t222), 32);
                    goto gm_fdone;
                }
gm_lb16:;
                {
                    uint8_t t223 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t224 = (uint8_t)(t223 == ((uint8_t)6ULL));
                    if (!t224) {
                        goto gm_lb17;
                    }
                    memmove(((uint8_t *)&gm_l11), ((((uint8_t *)t128) + 8) + 8), 32);
                    gm_b16_8 t225;
                    GM_ST(void *, (uint8_t *)&t225, (void *)(gm_sup2jn5hml593));
                    GM_ST(uintptr_t, (uint8_t *)&t225 + 8, (uintptr_t)(5));
                    gm_tvul7d1mdc2eq t226;
                    gm_f_ident_sx_kd0v352((void *)&t226, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l11)));
                    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t225, &t226);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb17:;
                {
                    uint8_t t227 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t228 = (uint8_t)(t227 == ((uint8_t)7ULL));
                    if (!t228) {
                        goto gm_lb18;
                    }
                    gm_b16_8 t229;
                    GM_ST(void *, (uint8_t *)&t229, (void *)(gm_snftglmif3ta2));
                    GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(10));
                    gm_tvul7d1mdc2eq t230;
                    GM_ST(uint8_t, (uint8_t *)&t230 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t230 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t230 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t230 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t230 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t230 + 8), t229);
                    memmove(gm_out, ((uint8_t *)&t230), 32);
                    goto gm_fdone;
                }
gm_lb18:;
                {
                    uint8_t t231 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t232 = (uint8_t)(t231 == ((uint8_t)8ULL));
                    if (!t232) {
                        goto gm_lb19;
                    }
                    memmove(((uint8_t *)&gm_l12), ((((uint8_t *)t128) + 8) + 8), 32);
                    gm_b16_8 t233;
                    GM_ST(void *, (uint8_t *)&t233, (void *)(gm_sg1h22ve4uqmq));
                    GM_ST(uintptr_t, (uint8_t *)&t233 + 8, (uintptr_t)(3));
                    gm_tvul7d1mdc2eq t234;
                    gm_f_ident_sx_kd0v352((void *)&t234, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l12)));
                    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t233, &t234);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb19:;
                {
                    uint8_t t235 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t236 = (uint8_t)(t235 == ((uint8_t)9ULL));
                    if (!t236) {
                        goto gm_lb20;
                    }
                    void * t237 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l13 = t237;
                    gm_b16_8 t238;
                    GM_ST(void *, (uint8_t *)&t238, (void *)(gm_s7s32h358ordg));
                    GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(5));
                    void * t239 = gm_l13;
                    gm_tvul7d1mdc2eq t240;
                    gm_f_dump_expr_l3bbdps((void *)&t240, (void *)NULL, cx, NULL, t239);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t238, &t240);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb20:;
                {
                    uint8_t t241 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t242 = (uint8_t)(t241 == ((uint8_t)11ULL));
                    if (!t242) {
                        goto gm_lb21;
                    }
                    void * t243 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l14 = t243;
                    gm_b16_8 t244;
                    GM_ST(void *, (uint8_t *)&t244, (void *)(gm_s2f5ea3l0ep8h));
                    GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)(8));
                    void * t245 = gm_l14;
                    gm_tvul7d1mdc2eq t246;
                    gm_f_dump_expr_l3bbdps((void *)&t246, (void *)NULL, cx, NULL, t245);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t244, &t246);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb21:;
                {
                    uint8_t t247 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t248 = (uint8_t)(t247 == ((uint8_t)10ULL));
                    if (!t248) {
                        goto gm_lb22;
                    }
                    gm_b16_8 t249 = GM_LD(gm_b16_8, ((((uint8_t *)t128) + 8) + 8));
                    gm_l15 = t249;
                    gm_b16_8 t250 = gm_l15;
                    gm_l16 = t250;
                    gm_l17 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t251 = gm_l17;
                            uint64_t t252 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                            uint8_t t253 = (t251 < t252);
                            if (!t253) {
                                goto gm_lb23;
                            }
                            {
                                void * t254 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                uint64_t t255 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                uint64_t t256 = gm_l17;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l18), (((uint8_t *)t254) + (uintptr_t)t256 * 136), 136);
                                        void * t257 = (void *)((uint8_t *)&F.l1);
                                        void * t258 = (void *)((uint8_t *)&F.l18);
                                        gm_tvul7d1mdc2eq t259;
                                        gm_f_dump_tuple_elem_bul8rgi((void *)&t259, (void *)NULL, cx, NULL, t258);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t260 = gm_f_push_mjiecfq(t257, &t259);
                                        gm_l117 = t260;
                                        uint8_t t261 = (uint8_t)!((((uint8_t *)&gm_l117))[0] != 0);
                                        if (!t261) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l117) + 1, gm_srck6bk8q21r07v, 377, 30);
                                        goto gm_lb26;
                                    }
gm_lb27:;
                                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 377, 13);
                                }
gm_lb26:;
                            }
gm_lb25:;
                            uint64_t t262 = gm_l17;
                            uint64_t r263; bool o264 = __builtin_add_overflow(t262, ((uint64_t)1ULL), &r263);
                            uint64_t t265 = r263;
                            gm_l17 = t265;
                        }
                    }
gm_lb23:;
                    gm_b16_8 t266;
                    GM_ST(void *, (uint8_t *)&t266, (void *)(gm_sk83con0mdsfe));
                    GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)(5));
                    gm_trqfvmjtt4sij t267 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t266, &t267);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb22:;
            }
            {
                {
                    uint8_t t268 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t269 = (uint8_t)(t268 == ((uint8_t)12ULL));
                    if (!t269) {
                        goto gm_lb28;
                    }
                    gm_t9qkh0rsooc8d t270 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t128) + 8) + 8));
                    gm_l19 = t270;
                    void * t271 = GM_LD(void *, (((((uint8_t *)t128) + 8) + 8) + 8));
                    gm_l20 = t271;
                    gm_b16_8 t272;
                    GM_ST(void *, (uint8_t *)&t272, (void *)(gm_sbiv3c5qa6ipi));
                    GM_ST(uintptr_t, (uint8_t *)&t272 + 8, (uintptr_t)(5));
                    gm_t9qkh0rsooc8d t273 = gm_l19;
                    gm_b16_8 t274 = gm_f_unop_name_j99tcv9(t273);
                    void * t275 = gm_l20;
                    gm_tvul7d1mdc2eq t276;
                    GM_ST(uint8_t, (uint8_t *)&t276 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t276 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t276 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t276 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t276 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t276 + 8), t274);
                    gm_tvul7d1mdc2eq t277;
                    gm_f_dump_expr_l3bbdps((void *)&t277, (void *)NULL, cx, NULL, t275);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t272, &t276, &t277);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb28:;
                {
                    uint8_t t278 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t279 = (uint8_t)(t278 == ((uint8_t)13ULL));
                    if (!t279) {
                        goto gm_lb29;
                    }
                    gm_ts6oqppjh0ba3 t280 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t128) + 8) + 8));
                    gm_l21 = t280;
                    void * t281 = GM_LD(void *, (((((uint8_t *)t128) + 8) + 8) + 8));
                    gm_l22 = t281;
                    void * t282 = GM_LD(void *, (((((uint8_t *)t128) + 8) + 8) + 16));
                    gm_l23 = t282;
                    void * t283 = (void *)((uint8_t *)&F.l1);
                    gm_ts6oqppjh0ba3 t284 = gm_l21;
                    gm_b16_8 t285 = gm_f_binop_name_1d6k17q(t284);
                    gm_tvul7d1mdc2eq t286;
                    GM_ST(uint8_t, (uint8_t *)&t286 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t286 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t286 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t286 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t286 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t286 + 8), t285);
                    gm_tftb1odc739t5 t287 = gm_f_push_mjiecfq(t283, &t286);
                    gm_l118 = t287;
                    uint8_t t288 = (uint8_t)!((((uint8_t *)&gm_l118))[0] != 0);
                    if (!t288) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l118) + 1, gm_srck6bk8q21r07v, 382, 13);
                    void * t289 = (void *)((uint8_t *)&F.l1);
                    void * t290 = gm_l22;
                    gm_tvul7d1mdc2eq t291;
                    gm_f_dump_expr_l3bbdps((void *)&t291, (void *)NULL, cx, NULL, t290);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t292 = gm_f_push_mjiecfq(t289, &t291);
                    gm_l119 = t292;
                    uint8_t t293 = (uint8_t)!((((uint8_t *)&gm_l119))[0] != 0);
                    if (!t293) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l119) + 1, gm_srck6bk8q21r07v, 383, 13);
                    void * t294 = (void *)((uint8_t *)&F.l1);
                    void * t295 = gm_l23;
                    gm_tvul7d1mdc2eq t296;
                    gm_f_dump_expr_l3bbdps((void *)&t296, (void *)NULL, cx, NULL, t295);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t297 = gm_f_push_mjiecfq(t294, &t296);
                    gm_l120 = t297;
                    uint8_t t298 = (uint8_t)!((((uint8_t *)&gm_l120))[0] != 0);
                    if (!t298) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l120) + 1, gm_srck6bk8q21r07v, 384, 13);
                    gm_b16_8 t299;
                    GM_ST(void *, (uint8_t *)&t299, (void *)(gm_s8e72gdumihb1));
                    GM_ST(uintptr_t, (uint8_t *)&t299 + 8, (uintptr_t)(6));
                    gm_trqfvmjtt4sij t300 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t299, &t300);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb29:;
                {
                    uint8_t t301 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t302 = (uint8_t)(t301 == ((uint8_t)14ULL));
                    if (!t302) {
                        goto gm_lb30;
                    }
                    memmove(((uint8_t *)&F.l24), ((((uint8_t *)t128) + 8) + 8), 56);
                    void * t303 = (void *)((uint8_t *)&F.l1);
                    void * t304 = GM_LD(void *, ((uint8_t *)&F.l24));
                    gm_tvul7d1mdc2eq t305;
                    gm_f_dump_expr_l3bbdps((void *)&t305, (void *)NULL, cx, NULL, t304);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t306 = gm_f_push_mjiecfq(t303, &t305);
                    gm_l121 = t306;
                    uint8_t t307 = (uint8_t)!((((uint8_t *)&gm_l121))[0] != 0);
                    if (!t307) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l121) + 1, gm_srck6bk8q21r07v, 388, 13);
                    {
                        {
                            uint8_t t308 = (uint8_t)(((((uint8_t *)&F.l24) + 8))[32] != 0);
                            if (!t308) {
                                goto gm_lb32;
                            }
                            memmove(((uint8_t *)&gm_l25), (((uint8_t *)&F.l24) + 8), 32);
                            void * t309 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t310;
                            GM_ST(void *, (uint8_t *)&t310, (void *)(gm_sqrfl54mgc34i));
                            GM_ST(uintptr_t, (uint8_t *)&t310 + 8, (uintptr_t)(7));
                            gm_tvul7d1mdc2eq t311;
                            gm_f_ident_sx_kd0v352((void *)&t311, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l25)));
                            gm_tvul7d1mdc2eq t312;
                            gm_f_node1_gpsjtpc((void *)&t312, (void *)NULL, cx, NULL, t310, &t311);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t313 = gm_f_push_mjiecfq(t309, &t312);
                            gm_l122 = t313;
                            uint8_t t314 = (uint8_t)!((((uint8_t *)&gm_l122))[0] != 0);
                            if (!t314) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l122) + 1, gm_srck6bk8q21r07v, 389, 33);
                            goto gm_lb31;
                        }
gm_lb32:;
                    }
gm_lb31:;
                    void * t315 = (void *)((uint8_t *)&F.l1);
                    void * t316 = GM_LD(void *, (((uint8_t *)&F.l24) + 48));
                    gm_tvul7d1mdc2eq t317;
                    gm_f_dump_expr_l3bbdps((void *)&t317, (void *)NULL, cx, NULL, t316);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t318 = gm_f_push_mjiecfq(t315, &t317);
                    gm_l123 = t318;
                    uint8_t t319 = (uint8_t)!((((uint8_t *)&gm_l123))[0] != 0);
                    if (!t319) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l123) + 1, gm_srck6bk8q21r07v, 390, 13);
                    gm_b16_8 t320;
                    GM_ST(void *, (uint8_t *)&t320, (void *)(gm_s9es2tos1bhfp));
                    GM_ST(uintptr_t, (uint8_t *)&t320 + 8, (uintptr_t)(4));
                    gm_trqfvmjtt4sij t321 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t320, &t321);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb30:;
            }
            {
                {
                    uint8_t t322 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t323 = (uint8_t)(t322 == ((uint8_t)15ULL));
                    if (!t323) {
                        goto gm_lb33;
                    }
                    void * t324 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l26 = t324;
                    gm_b16_8 t325;
                    GM_ST(void *, (uint8_t *)&t325, (void *)(gm_sdfmdfp384tf7));
                    GM_ST(uintptr_t, (uint8_t *)&t325 + 8, (uintptr_t)(10));
                    void * t326 = gm_l26;
                    gm_tvul7d1mdc2eq t327;
                    gm_f_dump_expr_l3bbdps((void *)&t327, (void *)NULL, cx, NULL, t326);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t325, &t327);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb33:;
                {
                    uint8_t t328 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t329 = (uint8_t)(t328 == ((uint8_t)16ULL));
                    if (!t329) {
                        goto gm_lb34;
                    }
                    void * t330 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l27 = t330;
                    gm_b16_8 t331;
                    GM_ST(void *, (uint8_t *)&t331, (void *)(gm_st38p0j1i82np));
                    GM_ST(uintptr_t, (uint8_t *)&t331 + 8, (uintptr_t)(10));
                    void * t332 = gm_l27;
                    gm_tvul7d1mdc2eq t333;
                    gm_f_dump_expr_l3bbdps((void *)&t333, (void *)NULL, cx, NULL, t332);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t331, &t333);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb34:;
                {
                    uint8_t t334 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t335 = (uint8_t)(t334 == ((uint8_t)17ULL));
                    if (!t335) {
                        goto gm_lb35;
                    }
                    void * t336 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l28 = t336;
                    void * t337 = GM_LD(void *, (((((uint8_t *)t128) + 8) + 8) + 8));
                    gm_l29 = t337;
                    gm_b16_8 t338;
                    GM_ST(void *, (uint8_t *)&t338, (void *)(gm_s79a8ngj68lbf));
                    GM_ST(uintptr_t, (uint8_t *)&t338 + 8, (uintptr_t)(4));
                    void * t339 = gm_l28;
                    gm_tvul7d1mdc2eq t340;
                    gm_f_dump_expr_l3bbdps((void *)&t340, (void *)NULL, cx, NULL, t339);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t341 = gm_l29;
                    gm_tvul7d1mdc2eq t342;
                    gm_f_dump_expr_l3bbdps((void *)&t342, (void *)NULL, cx, NULL, t341);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t338, &t340, &t342);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb35:;
                {
                    uint8_t t343 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t344 = (uint8_t)(t343 == ((uint8_t)18ULL));
                    if (!t344) {
                        goto gm_lb36;
                    }
                    void * t345 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l30 = t345;
                    gm_b16_8 t346;
                    GM_ST(void *, (uint8_t *)&t346, (void *)(gm_sh4niqnmh73co));
                    GM_ST(uintptr_t, (uint8_t *)&t346 + 8, (uintptr_t)(5));
                    void * t347 = gm_l30;
                    gm_tvul7d1mdc2eq t348;
                    gm_f_dump_expr_l3bbdps((void *)&t348, (void *)NULL, cx, NULL, t347);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t346, &t348);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb36:;
                {
                    uint8_t t349 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t350 = (uint8_t)(t349 == ((uint8_t)19ULL));
                    if (!t350) {
                        goto gm_lb37;
                    }
                    void * t351 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l31 = t351;
                    gm_b16_8 t352 = GM_LD(gm_b16_8, (((((uint8_t *)t128) + 8) + 8) + 8));
                    gm_l32 = t352;
                    uint8_t t353 = GM_LD(uint8_t, (((((uint8_t *)t128) + 8) + 8) + 24));
                    gm_l33 = t353;
                    void * t354 = (void *)((uint8_t *)&F.l1);
                    void * t355 = gm_l31;
                    gm_tvul7d1mdc2eq t356;
                    gm_f_dump_expr_l3bbdps((void *)&t356, (void *)NULL, cx, NULL, t355);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t357 = gm_f_push_mjiecfq(t354, &t356);
                    gm_l124 = t357;
                    uint8_t t358 = (uint8_t)!((((uint8_t *)&gm_l124))[0] != 0);
                    if (!t358) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l124) + 1, gm_srck6bk8q21r07v, 398, 13);
                    gm_b16_8 t359 = gm_l32;
                    gm_l34 = t359;
                    gm_l35 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t360 = gm_l35;
                            uint64_t t361 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                            uint8_t t362 = (t360 < t361);
                            if (!t362) {
                                goto gm_lb38;
                            }
                            {
                                void * t363 = GM_LD(void *, ((uint8_t *)&gm_l34));
                                uint64_t t364 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                                uint64_t t365 = gm_l35;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l36), (((uint8_t *)t363) + (uintptr_t)t365 * 144), 144);
                                        void * t366 = (void *)((uint8_t *)&F.l1);
                                        void * t367 = (void *)((uint8_t *)&F.l36);
                                        gm_tvul7d1mdc2eq t368;
                                        gm_f_dump_arg_9iv8pdv((void *)&t368, (void *)NULL, cx, NULL, t367);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t369 = gm_f_push_mjiecfq(t366, &t368);
                                        gm_l125 = t369;
                                        uint8_t t370 = (uint8_t)!((((uint8_t *)&gm_l125))[0] != 0);
                                        if (!t370) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l125) + 1, gm_srck6bk8q21r07v, 399, 29);
                                        goto gm_lb41;
                                    }
gm_lb42:;
                                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 399, 13);
                                }
gm_lb41:;
                            }
gm_lb40:;
                            uint64_t t371 = gm_l35;
                            uint64_t r372; bool o373 = __builtin_add_overflow(t371, ((uint64_t)1ULL), &r372);
                            uint64_t t374 = r372;
                            gm_l35 = t374;
                        }
                    }
gm_lb38:;
                    uint8_t t375 = gm_l33;
                    if (t375) {
                        void * t376 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t377;
                        GM_ST(void *, (uint8_t *)&t377, (void *)(gm_srq9bjb7d7qo7));
                        GM_ST(uintptr_t, (uint8_t *)&t377 + 8, (uintptr_t)(14));
                        gm_tvul7d1mdc2eq t378;
                        GM_ST(uint8_t, (uint8_t *)&t378 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t378 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t378 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t378 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t378 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t378 + 8), t377);
                        gm_tftb1odc739t5 t379 = gm_f_push_mjiecfq(t376, &t378);
                        gm_l126 = t379;
                        uint8_t t380 = (uint8_t)!((((uint8_t *)&gm_l126))[0] != 0);
                        if (!t380) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l126) + 1, gm_srck6bk8q21r07v, 400, 33);
                    }
                    gm_b16_8 t381;
                    GM_ST(void *, (uint8_t *)&t381, (void *)(gm_spbv5h8c0ukao));
                    GM_ST(uintptr_t, (uint8_t *)&t381 + 8, (uintptr_t)(4));
                    gm_trqfvmjtt4sij t382 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t381, &t382);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb37:;
            }
            {
                {
                    uint8_t t383 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t384 = (uint8_t)(t383 == ((uint8_t)20ULL));
                    if (!t384) {
                        goto gm_lb43;
                    }
                    void * t385 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l37 = t385;
                    void * t386 = GM_LD(void *, (((((uint8_t *)t128) + 8) + 8) + 8));
                    gm_l38 = t386;
                    gm_b16_8 t387;
                    GM_ST(void *, (uint8_t *)&t387, (void *)(gm_s5dbqnns2epkg));
                    GM_ST(uintptr_t, (uint8_t *)&t387 + 8, (uintptr_t)(5));
                    void * t388 = gm_l37;
                    gm_tvul7d1mdc2eq t389;
                    gm_f_dump_expr_l3bbdps((void *)&t389, (void *)NULL, cx, NULL, t388);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t390 = gm_l38;
                    gm_tvul7d1mdc2eq t391;
                    gm_f_dump_expr_l3bbdps((void *)&t391, (void *)NULL, cx, NULL, t390);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t387, &t389, &t391);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb43:;
                {
                    uint8_t t392 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t393 = (uint8_t)(t392 == ((uint8_t)21ULL));
                    if (!t393) {
                        goto gm_lb44;
                    }
                    memmove(((uint8_t *)&gm_l39), ((((uint8_t *)t128) + 8) + 8), 32);
                    void * t394 = (void *)((uint8_t *)&F.l1);
                    void * t395 = GM_LD(void *, ((uint8_t *)&gm_l39));
                    gm_tvul7d1mdc2eq t396;
                    gm_f_dump_expr_l3bbdps((void *)&t396, (void *)NULL, cx, NULL, t395);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t397 = gm_f_push_mjiecfq(t394, &t396);
                    gm_l127 = t397;
                    uint8_t t398 = (uint8_t)!((((uint8_t *)&gm_l127))[0] != 0);
                    if (!t398) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l127) + 1, gm_srck6bk8q21r07v, 405, 13);
                    {
                        {
                            uint8_t t399 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l39) + 8)) != 0);
                            if (!t399) {
                                goto gm_lb46;
                            }
                            void * t400 = GM_LD(void *, (((uint8_t *)&gm_l39) + 8));
                            gm_l40 = t400;
                            void * t401 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t402;
                            GM_ST(void *, (uint8_t *)&t402, (void *)(gm_s3bakerohlr2a));
                            GM_ST(uintptr_t, (uint8_t *)&t402 + 8, (uintptr_t)(2));
                            void * t403 = gm_l40;
                            gm_tvul7d1mdc2eq t404;
                            gm_f_dump_expr_l3bbdps((void *)&t404, (void *)NULL, cx, NULL, t403);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tvul7d1mdc2eq t405;
                            gm_f_node1_gpsjtpc((void *)&t405, (void *)NULL, cx, NULL, t402, &t404);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t406 = gm_f_push_mjiecfq(t401, &t405);
                            gm_l128 = t406;
                            uint8_t t407 = (uint8_t)!((((uint8_t *)&gm_l128))[0] != 0);
                            if (!t407) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l128) + 1, gm_srck6bk8q21r07v, 406, 29);
                            goto gm_lb45;
                        }
gm_lb46:;
                    }
gm_lb45:;
                    {
                        {
                            uint8_t t408 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l39) + 16)) != 0);
                            if (!t408) {
                                goto gm_lb48;
                            }
                            void * t409 = GM_LD(void *, (((uint8_t *)&gm_l39) + 16));
                            gm_l41 = t409;
                            void * t410 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t411;
                            GM_ST(void *, (uint8_t *)&t411, (void *)(gm_samlp70hmirme));
                            GM_ST(uintptr_t, (uint8_t *)&t411 + 8, (uintptr_t)(2));
                            void * t412 = gm_l41;
                            gm_tvul7d1mdc2eq t413;
                            gm_f_dump_expr_l3bbdps((void *)&t413, (void *)NULL, cx, NULL, t412);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tvul7d1mdc2eq t414;
                            gm_f_node1_gpsjtpc((void *)&t414, (void *)NULL, cx, NULL, t411, &t413);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t415 = gm_f_push_mjiecfq(t410, &t414);
                            gm_l129 = t415;
                            uint8_t t416 = (uint8_t)!((((uint8_t *)&gm_l129))[0] != 0);
                            if (!t416) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l129) + 1, gm_srck6bk8q21r07v, 407, 29);
                            goto gm_lb47;
                        }
gm_lb48:;
                    }
gm_lb47:;
                    uint8_t t417 = GM_LD(uint8_t, (((uint8_t *)&gm_l39) + 24));
                    if (t417) {
                        void * t418 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t419;
                        GM_ST(void *, (uint8_t *)&t419, (void *)(gm_s3ia9jm7npe6t));
                        GM_ST(uintptr_t, (uint8_t *)&t419 + 8, (uintptr_t)(9));
                        gm_tvul7d1mdc2eq t420;
                        GM_ST(uint8_t, (uint8_t *)&t420 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t420 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t420 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t420 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t420 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t420 + 8), t419);
                        gm_tftb1odc739t5 t421 = gm_f_push_mjiecfq(t418, &t420);
                        gm_l130 = t421;
                        uint8_t t422 = (uint8_t)!((((uint8_t *)&gm_l130))[0] != 0);
                        if (!t422) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l130) + 1, gm_srck6bk8q21r07v, 408, 30);
                    }
                    gm_b16_8 t423;
                    GM_ST(void *, (uint8_t *)&t423, (void *)(gm_ss496b65r9omj));
                    GM_ST(uintptr_t, (uint8_t *)&t423 + 8, (uintptr_t)(5));
                    gm_trqfvmjtt4sij t424 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t423, &t424);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb44:;
            }
            {
                {
                    uint8_t t425 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t426 = (uint8_t)(t425 == ((uint8_t)22ULL));
                    if (!t426) {
                        goto gm_lb49;
                    }
                    void * t427 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l42 = t427;
                    memmove(((uint8_t *)&gm_l43), (((((uint8_t *)t128) + 8) + 8) + 8), 32);
                    gm_b16_8 t428;
                    GM_ST(void *, (uint8_t *)&t428, (void *)(gm_s1jboc8lkvac0));
                    GM_ST(uintptr_t, (uint8_t *)&t428 + 8, (uintptr_t)(5));
                    void * t429 = gm_l42;
                    gm_tvul7d1mdc2eq t430;
                    gm_f_dump_expr_l3bbdps((void *)&t430, (void *)NULL, cx, NULL, t429);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tvul7d1mdc2eq t431;
                    gm_f_ident_sx_kd0v352((void *)&t431, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l43)));
                    gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t428, &t430, &t431);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb49:;
                {
                    uint8_t t432 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t433 = (uint8_t)(t432 == ((uint8_t)23ULL));
                    if (!t433) {
                        goto gm_lb50;
                    }
                    void * t434 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l44 = t434;
                    uint32_t t435 = GM_LD(uint32_t, (((((uint8_t *)t128) + 8) + 8) + 8));
                    gm_l45 = t435;
                    gm_b16_8 t436;
                    GM_ST(void *, (uint8_t *)&t436, (void *)(gm_s7ntlu0vonte9));
                    GM_ST(uintptr_t, (uint8_t *)&t436 + 8, (uintptr_t)(11));
                    void * t437 = gm_l44;
                    gm_tvul7d1mdc2eq t438;
                    gm_f_dump_expr_l3bbdps((void *)&t438, (void *)NULL, cx, NULL, t437);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint32_t t439 = gm_l45;
                    gm_t88rcucg1b7uk t440;
                    GM_ST(uint32_t, ((uint8_t *)&t440 + 0), t439);
                    gm_f_format_32n178h((void *)&gm_l131, (void *)NULL, cx, NULL, t440);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t441 = (uint8_t)!((((uint8_t *)&gm_l131))[0] != 0);
                    if (!t441) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l131) + 8, gm_srck6bk8q21r07v, 412, 91);
                    gm_b16_8 t442 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l131) + 8));
                    gm_b16_8 t443 = GM_LD(gm_b16_8, ((uint8_t *)&t442));
                    gm_tvul7d1mdc2eq t444;
                    GM_ST(uint8_t, (uint8_t *)&t444 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t444 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t444 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t444 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t444 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t444 + 8), t443);
                    gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t436, &t438, &t444);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb50:;
                {
                    uint8_t t445 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t446 = (uint8_t)(t445 == ((uint8_t)24ULL));
                    if (!t446) {
                        goto gm_lb51;
                    }
                    void * t447 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l46 = t447;
                    gm_b16_8 t448;
                    GM_ST(void *, (uint8_t *)&t448, (void *)(gm_s8aj3oag1l1u0));
                    GM_ST(uintptr_t, (uint8_t *)&t448 + 8, (uintptr_t)(13));
                    void * t449 = gm_l46;
                    gm_tvul7d1mdc2eq t450;
                    gm_f_dump_expr_l3bbdps((void *)&t450, (void *)NULL, cx, NULL, t449);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t448, &t450);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb51:;
                {
                    uint8_t t451 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t452 = (uint8_t)(t451 == ((uint8_t)25ULL));
                    if (!t452) {
                        goto gm_lb52;
                    }
                    uint8_t t453 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 8));
                    gm_l47 = t453;
                    void * t454 = GM_LD(void *, (((((uint8_t *)t128) + 8) + 8) + 8));
                    gm_l48 = t454;
                    gm_b16_8 t455;
                    GM_ST(void *, (uint8_t *)&t455, (void *)(gm_svvuql4te1jp4));
                    GM_ST(uintptr_t, (uint8_t *)&t455 + 8, (uintptr_t)(8));
                    uint8_t t456 = gm_l47;
                    void * t457 = gm_l48;
                    gm_f_const_type_o8v21gi((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t455, t456, t457);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb52:;
                {
                    uint8_t t458 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t459 = (uint8_t)(t458 == ((uint8_t)26ULL));
                    if (!t459) {
                        goto gm_lb53;
                    }
                    uint8_t t460 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 8));
                    gm_l49 = t460;
                    void * t461 = GM_LD(void *, (((((uint8_t *)t128) + 8) + 8) + 8));
                    gm_l50 = t461;
                    gm_b16_8 t462;
                    GM_ST(void *, (uint8_t *)&t462, (void *)(gm_s0gu0jo9fq1ca));
                    GM_ST(uintptr_t, (uint8_t *)&t462 + 8, (uintptr_t)(10));
                    uint8_t t463 = gm_l49;
                    void * t464 = gm_l50;
                    gm_f_const_type_o8v21gi((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t462, t463, t464);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb53:;
            }
            {
                {
                    uint8_t t465 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t466 = (uint8_t)(t465 == ((uint8_t)27ULL));
                    if (!t466) {
                        goto gm_lb54;
                    }
                    uint8_t t467 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 8));
                    gm_l51 = t467;
                    void * t468 = GM_LD(void *, (((((uint8_t *)t128) + 8) + 8) + 8));
                    gm_l52 = t468;
                    gm_b16_8 t469;
                    GM_ST(void *, (uint8_t *)&t469, (void *)(gm_sgf1euukkdve3));
                    GM_ST(uintptr_t, (uint8_t *)&t469 + 8, (uintptr_t)(13));
                    uint8_t t470 = gm_l51;
                    void * t471 = gm_l52;
                    gm_f_const_type_o8v21gi((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t469, t470, t471);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb54:;
                {
                    uint8_t t472 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t473 = (uint8_t)(t472 == ((uint8_t)28ULL));
                    if (!t473) {
                        goto gm_lb55;
                    }
                    gm_t3mf58gv9kf3n t474 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t128) + 8) + 8));
                    gm_l53 = t474;
                    gm_b16_8 t475;
                    GM_ST(void *, (uint8_t *)&t475, (void *)(gm_sfdbfrv46lihd));
                    GM_ST(uintptr_t, (uint8_t *)&t475 + 8, (uintptr_t)(1));
                    gm_tvul7d1mdc2eq t476;
                    GM_ST(uint8_t, (uint8_t *)&t476 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t476 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t476 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t476 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t476 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t476 + 8), t475);
                    gm_l54 = t476;
                    {
                        {
                            uint8_t t477 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l53)) != 0);
                            if (!t477) {
                                goto gm_lb57;
                            }
                            void * t478 = GM_LD(void *, ((uint8_t *)&gm_l53));
                            gm_l55 = t478;
                            void * t479 = gm_l55;
                            gm_f_dump_expr_l3bbdps((void *)&gm_l54, (void *)NULL, cx, NULL, t479);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb56;
                        }
gm_lb57:;
                    }
gm_lb56:;
                    gm_b16_8 t480;
                    GM_ST(void *, (uint8_t *)&t480, (void *)(gm_smedhknhdrf46));
                    GM_ST(uintptr_t, (uint8_t *)&t480 + 8, (uintptr_t)(10));
                    void * t481 = GM_LD(void *, (((uint8_t *)&gm_l53) + 8));
                    gm_tvul7d1mdc2eq t482 = GM_LD(gm_tvul7d1mdc2eq, ((uint8_t *)&gm_l54));
                    gm_tvul7d1mdc2eq t483;
                    gm_f_dump_expr_l3bbdps((void *)&t483, (void *)NULL, cx, NULL, t481);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t480, &t482, &t483);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb55:;
                {
                    uint8_t t484 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t485 = (uint8_t)(t484 == ((uint8_t)29ULL));
                    if (!t485) {
                        goto gm_lb58;
                    }
                    void * t486 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l56 = t486;
                    void * t487 = GM_LD(void *, (((((uint8_t *)t128) + 8) + 8) + 8));
                    gm_l57 = t487;
                    gm_b16_8 t488;
                    GM_ST(void *, (uint8_t *)&t488, (void *)(gm_s3r7cg0hbrkdg));
                    GM_ST(uintptr_t, (uint8_t *)&t488 + 8, (uintptr_t)(16));
                    void * t489 = gm_l56;
                    gm_tvul7d1mdc2eq t490;
                    gm_f_dump_expr_l3bbdps((void *)&t490, (void *)NULL, cx, NULL, t489);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t491 = gm_l57;
                    gm_tvul7d1mdc2eq t492;
                    gm_f_dump_expr_l3bbdps((void *)&t492, (void *)NULL, cx, NULL, t491);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t488, &t490, &t492);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb58:;
                {
                    uint8_t t493 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t494 = (uint8_t)(t493 == ((uint8_t)30ULL));
                    if (!t494) {
                        goto gm_lb59;
                    }
                    void * t495 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l58 = t495;
                    gm_b16_8 t496;
                    GM_ST(void *, (uint8_t *)&t496, (void *)(gm_s252dj7ek002d));
                    GM_ST(uintptr_t, (uint8_t *)&t496 + 8, (uintptr_t)(7));
                    void * t497 = gm_l58;
                    gm_trqfvmjtt4sij t498;
                    gm_f_list_ccsq5cl((void *)&t498, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_dump_fn_sig_4ip3qer((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t496, t497, &t498);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb59:;
            }
            {
                {
                    uint8_t t499 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t500 = (uint8_t)(t499 == ((uint8_t)31ULL));
                    if (!t500) {
                        goto gm_lb60;
                    }
                    void * t501 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l59 = t501;
                    gm_b16_8 t502;
                    GM_ST(void *, (uint8_t *)&t502, (void *)(gm_sa26g0ksgdkma));
                    GM_ST(uintptr_t, (uint8_t *)&t502 + 8, (uintptr_t)(9));
                    void * t503 = gm_l59;
                    gm_tvul7d1mdc2eq t504;
                    gm_f_dump_expr_l3bbdps((void *)&t504, (void *)NULL, cx, NULL, t503);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t502, &t504);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb60:;
                {
                    uint8_t t505 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t506 = (uint8_t)(t505 == ((uint8_t)32ULL));
                    if (!t506) {
                        goto gm_lb61;
                    }
                    void * t507 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l60 = t507;
                    void * t508 = gm_l60;
                    gm_f_dump_struct_lit_n9dqlnm((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t508);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb61:;
                {
                    uint8_t t509 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t510 = (uint8_t)(t509 == ((uint8_t)33ULL));
                    if (!t510) {
                        goto gm_lb62;
                    }
                    void * t511 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l61 = t511;
                    void * t512 = gm_l61;
                    gm_f_dump_union_lit_leu6qp2((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t512);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb62:;
                {
                    uint8_t t513 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t514 = (uint8_t)(t513 == ((uint8_t)34ULL));
                    if (!t514) {
                        goto gm_lb63;
                    }
                    void * t515 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l62 = t515;
                    void * t516 = gm_l62;
                    gm_f_dump_effect_lit_4vqnm1n((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t516);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb63:;
                {
                    uint8_t t517 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t518 = (uint8_t)(t517 == ((uint8_t)35ULL));
                    if (!t518) {
                        goto gm_lb64;
                    }
                    gm_b16_8 t519 = GM_LD(gm_b16_8, ((((uint8_t *)t128) + 8) + 8));
                    gm_l63 = t519;
                    void * t520 = GM_LD(void *, (((((uint8_t *)t128) + 8) + 8) + 16));
                    gm_l64 = t520;
                    void * t521 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t522 = gm_l63;
                    gm_b16_8 t523 = GM_LD(gm_b16_8, ((uint8_t *)&t522));
                    gm_b0_1 t524 = gm_f_push_attrs_a5l439c((void *)NULL, cx, NULL, t521, t523);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t525 = (void *)((uint8_t *)&F.l1);
                    void * t526 = gm_l64;
                    gm_tvul7d1mdc2eq t527;
                    gm_f_dump_expr_l3bbdps((void *)&t527, (void *)NULL, cx, NULL, t526);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t528 = gm_f_push_mjiecfq(t525, &t527);
                    gm_l132 = t528;
                    uint8_t t529 = (uint8_t)!((((uint8_t *)&gm_l132))[0] != 0);
                    if (!t529) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l132) + 1, gm_srck6bk8q21r07v, 430, 13);
                    gm_b16_8 t530;
                    GM_ST(void *, (uint8_t *)&t530, (void *)(gm_s167ok8osvuve));
                    GM_ST(uintptr_t, (uint8_t *)&t530 + 8, (uintptr_t)(10));
                    gm_trqfvmjtt4sij t531 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t530, &t531);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb64:;
                {
                    uint8_t t532 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t533 = (uint8_t)(t532 == ((uint8_t)36ULL));
                    if (!t533) {
                        goto gm_lb65;
                    }
                    void * t534 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l65 = t534;
                    void * t535 = gm_l65;
                    gm_f_dump_fn_lit_tchlprc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t535);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb65:;
            }
            {
                {
                    uint8_t t536 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t537 = (uint8_t)(t536 == ((uint8_t)37ULL));
                    if (!t537) {
                        goto gm_lb66;
                    }
                    void * t538 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l66 = t538;
                    void * t539 = (void *)((uint8_t *)&F.l1);
                    void * t540 = gm_l66;
                    void * t541 = GM_LD(void *, ((uint8_t *)t540));
                    gm_tvul7d1mdc2eq t542;
                    gm_f_dump_expr_l3bbdps((void *)&t542, (void *)NULL, cx, NULL, t541);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t543 = gm_f_push_mjiecfq(t539, &t542);
                    gm_l133 = t543;
                    uint8_t t544 = (uint8_t)!((((uint8_t *)&gm_l133))[0] != 0);
                    if (!t544) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l133) + 1, gm_srck6bk8q21r07v, 435, 13);
                    void * t545 = gm_l66;
                    gm_b16_8 t546 = GM_LD(gm_b16_8, (((uint8_t *)t545) + 8));
                    gm_l67 = t546;
                    gm_l68 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t547 = gm_l68;
                            uint64_t t548 = GM_LD(uint64_t, (((uint8_t *)&gm_l67) + 8));
                            uint8_t t549 = (t547 < t548);
                            if (!t549) {
                                goto gm_lb67;
                            }
                            {
                                void * t550 = GM_LD(void *, ((uint8_t *)&gm_l67));
                                uint64_t t551 = GM_LD(uint64_t, (((uint8_t *)&gm_l67) + 8));
                                uint64_t t552 = gm_l68;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l69), (((uint8_t *)t550) + (uintptr_t)t552 * 72), 72);
                                        void * t553 = (void *)((uint8_t *)&F.l1);
                                        void * t554 = (void *)((uint8_t *)&F.l69);
                                        gm_tvul7d1mdc2eq t555;
                                        gm_f_dump_clause_kje51q9((void *)&t555, (void *)NULL, cx, NULL, t554);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t556 = gm_f_push_mjiecfq(t553, &t555);
                                        gm_l134 = t556;
                                        uint8_t t557 = (uint8_t)!((((uint8_t *)&gm_l134))[0] != 0);
                                        if (!t557) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l134) + 1, gm_srck6bk8q21r07v, 436, 34);
                                        goto gm_lb70;
                                    }
gm_lb71:;
                                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 436, 13);
                                }
gm_lb70:;
                            }
gm_lb69:;
                            uint64_t t558 = gm_l68;
                            uint64_t r559; bool o560 = __builtin_add_overflow(t558, ((uint64_t)1ULL), &r559);
                            uint64_t t561 = r559;
                            gm_l68 = t561;
                        }
                    }
gm_lb67:;
                    gm_b16_8 t562;
                    GM_ST(void *, (uint8_t *)&t562, (void *)(gm_sic5vsema0r36));
                    GM_ST(uintptr_t, (uint8_t *)&t562 + 8, (uintptr_t)(7));
                    gm_trqfvmjtt4sij t563 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t562, &t563);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb66:;
                {
                    uint8_t t564 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t565 = (uint8_t)(t564 == ((uint8_t)38ULL));
                    if (!t565) {
                        goto gm_lb72;
                    }
                    memmove(((uint8_t *)&gm_l70), ((((uint8_t *)t128) + 8) + 8), 24);
                    gm_b16_8 t566;
                    GM_ST(void *, (uint8_t *)&t566, (void *)(gm_sg3kpa3h16no3));
                    GM_ST(uintptr_t, (uint8_t *)&t566 + 8, (uintptr_t)(6));
                    gm_tvul7d1mdc2eq t567;
                    gm_f_str_lit_a2k13t3((void *)&t567, (void *)NULL, cx, NULL, &(*(gm_tibupt4dmcqai *)((uint8_t *)&gm_l70)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t566, &t567);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb72:;
                {
                    uint8_t t568 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t569 = (uint8_t)(t568 == ((uint8_t)39ULL));
                    if (!t569) {
                        goto gm_lb73;
                    }
                    void * t570 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l71 = t570;
                    void * t571 = (void *)((uint8_t *)&F.l1);
                    void * t572 = gm_l71;
                    gm_tvul7d1mdc2eq t573;
                    gm_f_str_lit_a2k13t3((void *)&t573, (void *)NULL, cx, NULL, &(*(gm_tibupt4dmcqai *)((uint8_t *)t572)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t574 = gm_f_push_mjiecfq(t571, &t573);
                    gm_l135 = t574;
                    uint8_t t575 = (uint8_t)!((((uint8_t *)&gm_l135))[0] != 0);
                    if (!t575) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l135) + 1, gm_srck6bk8q21r07v, 441, 13);
                    void * t576 = gm_l71;
                    gm_b16_8 t577 = GM_LD(gm_b16_8, (((uint8_t *)t576) + 24));
                    gm_l72 = t577;
                    gm_l73 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t578 = gm_l73;
                            uint64_t t579 = GM_LD(uint64_t, (((uint8_t *)&gm_l72) + 8));
                            uint8_t t580 = (t578 < t579);
                            if (!t580) {
                                goto gm_lb74;
                            }
                            {
                                void * t581 = GM_LD(void *, ((uint8_t *)&gm_l72));
                                uint64_t t582 = GM_LD(uint64_t, (((uint8_t *)&gm_l72) + 8));
                                uint64_t t583 = gm_l73;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l74), (((uint8_t *)t581) + (uintptr_t)t583 * 144), 144);
                                        void * t584 = (void *)((uint8_t *)&F.l1);
                                        void * t585 = (void *)((uint8_t *)&F.l74);
                                        gm_tvul7d1mdc2eq t586;
                                        gm_f_dump_foreign_member_vrht12f((void *)&t586, (void *)NULL, cx, NULL, t585);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t587 = gm_f_push_mjiecfq(t584, &t586);
                                        gm_l136 = t587;
                                        uint8_t t588 = (uint8_t)!((((uint8_t *)&gm_l136))[0] != 0);
                                        if (!t588) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l136) + 1, gm_srck6bk8q21r07v, 442, 34);
                                        goto gm_lb77;
                                    }
gm_lb78:;
                                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 442, 13);
                                }
gm_lb77:;
                            }
gm_lb76:;
                            uint64_t t589 = gm_l73;
                            uint64_t r590; bool o591 = __builtin_add_overflow(t589, ((uint64_t)1ULL), &r590);
                            uint64_t t592 = r590;
                            gm_l73 = t592;
                        }
                    }
gm_lb74:;
                    gm_b16_8 t593;
                    GM_ST(void *, (uint8_t *)&t593, (void *)(gm_sv0fbf330lmcd));
                    GM_ST(uintptr_t, (uint8_t *)&t593 + 8, (uintptr_t)(7));
                    gm_trqfvmjtt4sij t594 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t593, &t594);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb73:;
            }
            {
                {
                    uint8_t t595 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t596 = (uint8_t)(t595 == ((uint8_t)40ULL));
                    if (!t596) {
                        goto gm_lb79;
                    }
                    void * t597 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l75 = t597;
                    void * t598 = gm_l75;
                    gm_f_dump_if_3qo5nul((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t598);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb79:;
                {
                    uint8_t t599 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t600 = (uint8_t)(t599 == ((uint8_t)41ULL));
                    if (!t600) {
                        goto gm_lb80;
                    }
                    void * t601 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l76 = t601;
                    void * t602 = (void *)((uint8_t *)&F.l1);
                    void * t603 = gm_l76;
                    void * t604 = GM_LD(void *, ((uint8_t *)t603));
                    gm_tvul7d1mdc2eq t605;
                    gm_f_dump_expr_l3bbdps((void *)&t605, (void *)NULL, cx, NULL, t604);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t606 = gm_f_push_mjiecfq(t602, &t605);
                    gm_l137 = t606;
                    uint8_t t607 = (uint8_t)!((((uint8_t *)&gm_l137))[0] != 0);
                    if (!t607) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l137) + 1, gm_srck6bk8q21r07v, 447, 13);
                    void * t608 = gm_l76;
                    gm_b16_8 t609 = GM_LD(gm_b16_8, (((uint8_t *)t608) + 8));
                    gm_l77 = t609;
                    gm_l78 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t610 = gm_l78;
                            uint64_t t611 = GM_LD(uint64_t, (((uint8_t *)&gm_l77) + 8));
                            uint8_t t612 = (t610 < t611);
                            if (!t612) {
                                goto gm_lb81;
                            }
                            {
                                void * t613 = GM_LD(void *, ((uint8_t *)&gm_l77));
                                uint64_t t614 = GM_LD(uint64_t, (((uint8_t *)&gm_l77) + 8));
                                uint64_t t615 = gm_l78;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l79), (((uint8_t *)t613) + (uintptr_t)t615 * 48), 48);
                                        void * t616 = (void *)((uint8_t *)&F.l1);
                                        void * t617 = (void *)((uint8_t *)&F.l79);
                                        gm_tvul7d1mdc2eq t618;
                                        gm_f_dump_arm_8jsad7u((void *)&t618, (void *)NULL, cx, NULL, t617);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t619 = gm_f_push_mjiecfq(t616, &t618);
                                        gm_l138 = t619;
                                        uint8_t t620 = (uint8_t)!((((uint8_t *)&gm_l138))[0] != 0);
                                        if (!t620) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l138) + 1, gm_srck6bk8q21r07v, 448, 31);
                                        goto gm_lb84;
                                    }
gm_lb85:;
                                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 448, 13);
                                }
gm_lb84:;
                            }
gm_lb83:;
                            uint64_t t621 = gm_l78;
                            uint64_t r622; bool o623 = __builtin_add_overflow(t621, ((uint64_t)1ULL), &r622);
                            uint64_t t624 = r622;
                            gm_l78 = t624;
                        }
                    }
gm_lb81:;
                    gm_b16_8 t625;
                    GM_ST(void *, (uint8_t *)&t625, (void *)(gm_s8m5q4i8uoe5j));
                    GM_ST(uintptr_t, (uint8_t *)&t625 + 8, (uintptr_t)(5));
                    gm_trqfvmjtt4sij t626 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t625, &t626);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb80:;
                {
                    uint8_t t627 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t628 = (uint8_t)(t627 == ((uint8_t)42ULL));
                    if (!t628) {
                        goto gm_lb86;
                    }
                    void * t629 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l80 = t629;
                    {
                        {
                            void * t630 = gm_l80;
                            uint8_t t631 = (uint8_t)((((uint8_t *)t630))[32] != 0);
                            if (!t631) {
                                goto gm_lb88;
                            }
                            memmove(((uint8_t *)&gm_l81), ((uint8_t *)t630), 32);
                            void * t632 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t633;
                            GM_ST(void *, (uint8_t *)&t633, (void *)(gm_su137hja0ebcd));
                            GM_ST(uintptr_t, (uint8_t *)&t633 + 8, (uintptr_t)(5));
                            gm_tvul7d1mdc2eq t634;
                            gm_f_ident_sx_kd0v352((void *)&t634, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l81)));
                            gm_tvul7d1mdc2eq t635;
                            gm_f_node1_gpsjtpc((void *)&t635, (void *)NULL, cx, NULL, t633, &t634);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t636 = gm_f_push_mjiecfq(t632, &t635);
                            gm_l139 = t636;
                            uint8_t t637 = (uint8_t)!((((uint8_t *)&gm_l139))[0] != 0);
                            if (!t637) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l139) + 1, gm_srck6bk8q21r07v, 452, 31);
                            goto gm_lb87;
                        }
gm_lb88:;
                    }
gm_lb87:;
                    gm_trqfvmjtt4sij t638;
                    gm_f_list_ccsq5cl((void *)&t638, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l82 = t638;
                    void * t639 = gm_l80;
                    gm_b16_8 t640 = GM_LD(gm_b16_8, (((uint8_t *)t639) + 40));
                    gm_l83 = t640;
                    gm_l84 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t641 = gm_l84;
                            uint64_t t642 = GM_LD(uint64_t, (((uint8_t *)&gm_l83) + 8));
                            uint8_t t643 = (t641 < t642);
                            if (!t643) {
                                goto gm_lb89;
                            }
                            {
                                void * t644 = GM_LD(void *, ((uint8_t *)&gm_l83));
                                uint64_t t645 = GM_LD(uint64_t, (((uint8_t *)&gm_l83) + 8));
                                uint64_t t646 = gm_l84;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l85), (((uint8_t *)t644) + (uintptr_t)t646 * 88), 88);
                                        void * t647 = (void *)((uint8_t *)&F.l82);
                                        void * t648 = (void *)((uint8_t *)&F.l85);
                                        gm_tvul7d1mdc2eq t649;
                                        gm_f_dump_expr_l3bbdps((void *)&t649, (void *)NULL, cx, NULL, t648);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t650 = gm_f_push_mjiecfq(t647, &t649);
                                        gm_l140 = t650;
                                        uint8_t t651 = (uint8_t)!((((uint8_t *)&gm_l140))[0] != 0);
                                        if (!t651) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l140) + 1, gm_srck6bk8q21r07v, 454, 35);
                                        goto gm_lb92;
                                    }
gm_lb93:;
                                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 454, 13);
                                }
gm_lb92:;
                            }
gm_lb91:;
                            uint64_t t652 = gm_l84;
                            uint64_t r653; bool o654 = __builtin_add_overflow(t652, ((uint64_t)1ULL), &r653);
                            uint64_t t655 = r653;
                            gm_l84 = t655;
                        }
                    }
gm_lb89:;
                    void * t656 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t657;
                    GM_ST(void *, (uint8_t *)&t657, (void *)(gm_srj7urnaf6a1u));
                    GM_ST(uintptr_t, (uint8_t *)&t657 + 8, (uintptr_t)(8));
                    gm_tvul7d1mdc2eq t658;
                    gm_f_node_dm1r8dn((void *)&t658, (void *)NULL, cx, NULL, t657, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l82)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t659 = gm_f_push_mjiecfq(t656, &t658);
                    gm_l141 = t659;
                    uint8_t t660 = (uint8_t)!((((uint8_t *)&gm_l141))[0] != 0);
                    if (!t660) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l141) + 1, gm_srck6bk8q21r07v, 455, 13);
                    {
                        {
                            void * t661 = gm_l80;
                            uint8_t t662 = (uint8_t)(GM_LD(void *, (((uint8_t *)t661) + 56)) != 0);
                            if (!t662) {
                                goto gm_lb95;
                            }
                            void * t663 = GM_LD(void *, (((uint8_t *)t661) + 56));
                            gm_l86 = t663;
                            void * t664 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t665;
                            GM_ST(void *, (uint8_t *)&t665, (void *)(gm_sdl8mgtvebfcs));
                            GM_ST(uintptr_t, (uint8_t *)&t665 + 8, (uintptr_t)(2));
                            void * t666 = gm_l86;
                            gm_tvul7d1mdc2eq t667;
                            gm_f_dump_expr_l3bbdps((void *)&t667, (void *)NULL, cx, NULL, t666);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tvul7d1mdc2eq t668;
                            gm_f_node1_gpsjtpc((void *)&t668, (void *)NULL, cx, NULL, t665, &t667);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t669 = gm_f_push_mjiecfq(t664, &t668);
                            gm_l142 = t669;
                            uint8_t t670 = (uint8_t)!((((uint8_t *)&gm_l142))[0] != 0);
                            if (!t670) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l142) + 1, gm_srck6bk8q21r07v, 456, 28);
                            goto gm_lb94;
                        }
gm_lb95:;
                    }
gm_lb94:;
                    void * t671 = (void *)((uint8_t *)&F.l1);
                    void * t672 = gm_l80;
                    void * t673 = (void *)(((uint8_t *)t672) + 64);
                    gm_tvul7d1mdc2eq t674;
                    gm_f_dump_block_5utjqg0((void *)&t674, (void *)NULL, cx, NULL, t673);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t675 = gm_f_push_mjiecfq(t671, &t674);
                    gm_l143 = t675;
                    uint8_t t676 = (uint8_t)!((((uint8_t *)&gm_l143))[0] != 0);
                    if (!t676) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l143) + 1, gm_srck6bk8q21r07v, 457, 13);
                    gm_b16_8 t677;
                    GM_ST(void *, (uint8_t *)&t677, (void *)(gm_srri8c6al1v1g));
                    GM_ST(uintptr_t, (uint8_t *)&t677 + 8, (uintptr_t)(4));
                    gm_trqfvmjtt4sij t678 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t677, &t678);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb86:;
            }
            {
                {
                    uint8_t t679 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t680 = (uint8_t)(t679 == ((uint8_t)43ULL));
                    if (!t680) {
                        goto gm_lb96;
                    }
                    void * t681 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l87 = t681;
                    {
                        {
                            void * t682 = gm_l87;
                            uint8_t t683 = (uint8_t)((((uint8_t *)t682))[32] != 0);
                            if (!t683) {
                                goto gm_lb98;
                            }
                            memmove(((uint8_t *)&gm_l88), ((uint8_t *)t682), 32);
                            void * t684 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t685;
                            GM_ST(void *, (uint8_t *)&t685, (void *)(gm_su137hja0ebcd));
                            GM_ST(uintptr_t, (uint8_t *)&t685 + 8, (uintptr_t)(5));
                            gm_tvul7d1mdc2eq t686;
                            gm_f_ident_sx_kd0v352((void *)&t686, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l88)));
                            gm_tvul7d1mdc2eq t687;
                            gm_f_node1_gpsjtpc((void *)&t687, (void *)NULL, cx, NULL, t685, &t686);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t688 = gm_f_push_mjiecfq(t684, &t687);
                            gm_l144 = t688;
                            uint8_t t689 = (uint8_t)!((((uint8_t *)&gm_l144))[0] != 0);
                            if (!t689) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l144) + 1, gm_srck6bk8q21r07v, 461, 31);
                            goto gm_lb97;
                        }
gm_lb98:;
                    }
gm_lb97:;
                    gm_trqfvmjtt4sij t690;
                    gm_f_list_ccsq5cl((void *)&t690, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l89 = t690;
                    void * t691 = gm_l87;
                    gm_b16_8 t692 = GM_LD(gm_b16_8, (((uint8_t *)t691) + 40));
                    gm_l90 = t692;
                    gm_l91 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t693 = gm_l91;
                            uint64_t t694 = GM_LD(uint64_t, (((uint8_t *)&gm_l90) + 8));
                            uint8_t t695 = (t693 < t694);
                            if (!t695) {
                                goto gm_lb99;
                            }
                            {
                                void * t696 = GM_LD(void *, ((uint8_t *)&gm_l90));
                                uint64_t t697 = GM_LD(uint64_t, (((uint8_t *)&gm_l90) + 8));
                                uint64_t t698 = gm_l91;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l92), (((uint8_t *)t696) + (uintptr_t)t698 * 64), 64);
                                        void * t699 = (void *)((uint8_t *)&F.l89);
                                        void * t700 = (void *)((uint8_t *)&F.l92);
                                        gm_tvul7d1mdc2eq t701;
                                        gm_f_dump_pattern_q6u5c24((void *)&t701, (void *)NULL, cx, NULL, t700);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t702 = gm_f_push_mjiecfq(t699, &t701);
                                        gm_l145 = t702;
                                        uint8_t t703 = (uint8_t)!((((uint8_t *)&gm_l145))[0] != 0);
                                        if (!t703) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l145) + 1, gm_srck6bk8q21r07v, 463, 31);
                                        goto gm_lb102;
                                    }
gm_lb103:;
                                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 463, 13);
                                }
gm_lb102:;
                            }
gm_lb101:;
                            uint64_t t704 = gm_l91;
                            uint64_t r705; bool o706 = __builtin_add_overflow(t704, ((uint64_t)1ULL), &r705);
                            uint64_t t707 = r705;
                            gm_l91 = t707;
                        }
                    }
gm_lb99:;
                    void * t708 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t709;
                    GM_ST(void *, (uint8_t *)&t709, (void *)(gm_so0ilvhoi1hom));
                    GM_ST(uintptr_t, (uint8_t *)&t709 + 8, (uintptr_t)(4));
                    gm_tvul7d1mdc2eq t710;
                    gm_f_node_dm1r8dn((void *)&t710, (void *)NULL, cx, NULL, t709, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l89)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t711 = gm_f_push_mjiecfq(t708, &t710);
                    gm_l146 = t711;
                    uint8_t t712 = (uint8_t)!((((uint8_t *)&gm_l146))[0] != 0);
                    if (!t712) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l146) + 1, gm_srck6bk8q21r07v, 464, 13);
                    void * t713 = (void *)((uint8_t *)&F.l1);
                    void * t714 = gm_l87;
                    void * t715 = GM_LD(void *, (((uint8_t *)t714) + 56));
                    gm_tvul7d1mdc2eq t716;
                    gm_f_dump_expr_l3bbdps((void *)&t716, (void *)NULL, cx, NULL, t715);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t717 = gm_f_push_mjiecfq(t713, &t716);
                    gm_l147 = t717;
                    uint8_t t718 = (uint8_t)!((((uint8_t *)&gm_l147))[0] != 0);
                    if (!t718) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l147) + 1, gm_srck6bk8q21r07v, 465, 13);
                    void * t719 = (void *)((uint8_t *)&F.l1);
                    void * t720 = gm_l87;
                    void * t721 = (void *)(((uint8_t *)t720) + 64);
                    gm_tvul7d1mdc2eq t722;
                    gm_f_dump_block_5utjqg0((void *)&t722, (void *)NULL, cx, NULL, t721);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t723 = gm_f_push_mjiecfq(t719, &t722);
                    gm_l148 = t723;
                    uint8_t t724 = (uint8_t)!((((uint8_t *)&gm_l148))[0] != 0);
                    if (!t724) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l148) + 1, gm_srck6bk8q21r07v, 466, 13);
                    gm_b16_8 t725;
                    GM_ST(void *, (uint8_t *)&t725, (void *)(gm_scc8p0a9eachg));
                    GM_ST(uintptr_t, (uint8_t *)&t725 + 8, (uintptr_t)(3));
                    gm_trqfvmjtt4sij t726 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t725, &t726);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb96:;
            }
            {
                {
                    uint8_t t727 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t728 = (uint8_t)(t727 == ((uint8_t)44ULL));
                    if (!t728) {
                        goto gm_lb104;
                    }
                    memmove(((uint8_t *)&F.l93), ((((uint8_t *)t128) + 8) + 8), 40);
                    void * t729 = (void *)((uint8_t *)&F.l93);
                    gm_f_dump_block_5utjqg0((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t729);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb104:;
                {
                    uint8_t t730 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t731 = (uint8_t)(t730 == ((uint8_t)45ULL));
                    if (!t731) {
                        goto gm_lb105;
                    }
                    uint8_t t732 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t128) + 8) + 8)) != 0);
                    if (t732) {
                        goto gm_lb105;
                    }
                    gm_b16_8 t733;
                    GM_ST(void *, (uint8_t *)&t733, (void *)(gm_s277jq8ulrihr));
                    GM_ST(uintptr_t, (uint8_t *)&t733 + 8, (uintptr_t)(6));
                    gm_trqfvmjtt4sij t734 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t733, &t734);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb105:;
                {
                    uint8_t t735 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t736 = (uint8_t)(t735 == ((uint8_t)45ULL));
                    if (!t736) {
                        goto gm_lb106;
                    }
                    uint8_t t737 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t128) + 8) + 8)) != 0);
                    if (!t737) {
                        goto gm_lb106;
                    }
                    void * t738 = GM_LD(void *, ((((uint8_t *)t128) + 8) + 8));
                    gm_l94 = t738;
                    gm_b16_8 t739;
                    GM_ST(void *, (uint8_t *)&t739, (void *)(gm_s277jq8ulrihr));
                    GM_ST(uintptr_t, (uint8_t *)&t739 + 8, (uintptr_t)(6));
                    void * t740 = gm_l94;
                    gm_tvul7d1mdc2eq t741;
                    gm_f_dump_expr_l3bbdps((void *)&t741, (void *)NULL, cx, NULL, t740);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t739, &t741);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb106:;
                {
                    uint8_t t742 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t743 = (uint8_t)(t742 == ((uint8_t)46ULL));
                    if (!t743) {
                        goto gm_lb107;
                    }
                    memmove(((uint8_t *)&F.l95), ((((uint8_t *)t128) + 8) + 8), 48);
                    {
                        {
                            uint8_t t744 = (uint8_t)((((uint8_t *)&F.l95))[32] != 0);
                            if (!t744) {
                                goto gm_lb109;
                            }
                            memmove(((uint8_t *)&gm_l96), ((uint8_t *)&F.l95), 32);
                            void * t745 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t746;
                            GM_ST(void *, (uint8_t *)&t746, (void *)(gm_su137hja0ebcd));
                            GM_ST(uintptr_t, (uint8_t *)&t746 + 8, (uintptr_t)(5));
                            gm_tvul7d1mdc2eq t747;
                            gm_f_ident_sx_kd0v352((void *)&t747, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l96)));
                            gm_tvul7d1mdc2eq t748;
                            gm_f_node1_gpsjtpc((void *)&t748, (void *)NULL, cx, NULL, t746, &t747);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t749 = gm_f_push_mjiecfq(t745, &t748);
                            gm_l149 = t749;
                            uint8_t t750 = (uint8_t)!((((uint8_t *)&gm_l149))[0] != 0);
                            if (!t750) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l149) + 1, gm_srck6bk8q21r07v, 473, 31);
                            goto gm_lb108;
                        }
gm_lb109:;
                    }
gm_lb108:;
                    {
                        {
                            uint8_t t751 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l95) + 40)) != 0);
                            if (!t751) {
                                goto gm_lb111;
                            }
                            void * t752 = GM_LD(void *, (((uint8_t *)&F.l95) + 40));
                            gm_l97 = t752;
                            void * t753 = (void *)((uint8_t *)&F.l1);
                            void * t754 = gm_l97;
                            gm_tvul7d1mdc2eq t755;
                            gm_f_dump_expr_l3bbdps((void *)&t755, (void *)NULL, cx, NULL, t754);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t756 = gm_f_push_mjiecfq(t753, &t755);
                            gm_l150 = t756;
                            uint8_t t757 = (uint8_t)!((((uint8_t *)&gm_l150))[0] != 0);
                            if (!t757) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l150) + 1, gm_srck6bk8q21r07v, 474, 31);
                            goto gm_lb110;
                        }
gm_lb111:;
                    }
gm_lb110:;
                    gm_b16_8 t758;
                    GM_ST(void *, (uint8_t *)&t758, (void *)(gm_s8av3ru5o85v4));
                    GM_ST(uintptr_t, (uint8_t *)&t758 + 8, (uintptr_t)(5));
                    gm_trqfvmjtt4sij t759 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t758, &t759);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb107:;
            }
            {
                {
                    uint8_t t760 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t761 = (uint8_t)(t760 == ((uint8_t)47ULL));
                    if (!t761) {
                        goto gm_lb112;
                    }
                    uint8_t t762 = (uint8_t)((((((uint8_t *)t128) + 8) + 8))[32] != 0);
                    if (t762) {
                        goto gm_lb112;
                    }
                    gm_b16_8 t763;
                    GM_ST(void *, (uint8_t *)&t763, (void *)(gm_sr31o7m0aohvd));
                    GM_ST(uintptr_t, (uint8_t *)&t763 + 8, (uintptr_t)(8));
                    gm_trqfvmjtt4sij t764 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t763, &t764);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb112:;
                {
                    uint8_t t765 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t766 = (uint8_t)(t765 == ((uint8_t)47ULL));
                    if (!t766) {
                        goto gm_lb113;
                    }
                    uint8_t t767 = (uint8_t)((((((uint8_t *)t128) + 8) + 8))[32] != 0);
                    if (!t767) {
                        goto gm_lb113;
                    }
                    memmove(((uint8_t *)&gm_l98), ((((uint8_t *)t128) + 8) + 8), 32);
                    gm_b16_8 t768;
                    GM_ST(void *, (uint8_t *)&t768, (void *)(gm_sr31o7m0aohvd));
                    GM_ST(uintptr_t, (uint8_t *)&t768 + 8, (uintptr_t)(8));
                    gm_b16_8 t769;
                    GM_ST(void *, (uint8_t *)&t769, (void *)(gm_su137hja0ebcd));
                    GM_ST(uintptr_t, (uint8_t *)&t769 + 8, (uintptr_t)(5));
                    gm_tvul7d1mdc2eq t770;
                    gm_f_ident_sx_kd0v352((void *)&t770, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l98)));
                    gm_tvul7d1mdc2eq t771;
                    gm_f_node1_gpsjtpc((void *)&t771, (void *)NULL, cx, NULL, t769, &t770);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t768, &t771);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb113:;
                {
                    uint8_t t772 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t773 = (uint8_t)(t772 == ((uint8_t)48ULL));
                    if (!t773) {
                        goto gm_lb114;
                    }
                    gm_t3mf58gv9kf3n t774 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t128) + 8) + 8));
                    gm_l99 = t774;
                    void * t775 = (void *)((uint8_t *)&F.l1);
                    void * t776 = GM_LD(void *, ((uint8_t *)&gm_l99));
                    gm_tvul7d1mdc2eq t777;
                    gm_f_dump_expr_l3bbdps((void *)&t777, (void *)NULL, cx, NULL, t776);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t778 = gm_f_push_mjiecfq(t775, &t777);
                    gm_l151 = t778;
                    uint8_t t779 = (uint8_t)!((((uint8_t *)&gm_l151))[0] != 0);
                    if (!t779) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l151) + 1, gm_srck6bk8q21r07v, 480, 13);
                    {
                        {
                            uint8_t t780 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l99) + 8)) != 0);
                            if (!t780) {
                                goto gm_lb116;
                            }
                            void * t781 = GM_LD(void *, (((uint8_t *)&gm_l99) + 8));
                            gm_l100 = t781;
                            void * t782 = (void *)((uint8_t *)&F.l1);
                            void * t783 = gm_l100;
                            gm_tvul7d1mdc2eq t784;
                            gm_f_dump_expr_l3bbdps((void *)&t784, (void *)NULL, cx, NULL, t783);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t785 = gm_f_push_mjiecfq(t782, &t784);
                            gm_l152 = t785;
                            uint8_t t786 = (uint8_t)!((((uint8_t *)&gm_l152))[0] != 0);
                            if (!t786) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l152) + 1, gm_srck6bk8q21r07v, 481, 31);
                            goto gm_lb115;
                        }
gm_lb116:;
                    }
gm_lb115:;
                    gm_b16_8 t787;
                    GM_ST(void *, (uint8_t *)&t787, (void *)(gm_sn6sqlran7h3r));
                    GM_ST(uintptr_t, (uint8_t *)&t787 + 8, (uintptr_t)(6));
                    gm_trqfvmjtt4sij t788 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t787, &t788);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb114:;
                {
                    uint8_t t789 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t790 = (uint8_t)(t789 == ((uint8_t)49ULL));
                    if (!t790) {
                        goto gm_lb117;
                    }
                    memmove(((uint8_t *)&F.l101), ((((uint8_t *)t128) + 8) + 8), 56);
                    void * t791 = (void *)((uint8_t *)&F.l1);
                    gm_tvul7d1mdc2eq t792;
                    gm_f_ident_sx_kd0v352((void *)&t792, &(*(gm_t08olq97rscn3 *)((uint8_t *)&F.l101)));
                    gm_tftb1odc739t5 t793 = gm_f_push_mjiecfq(t791, &t792);
                    gm_l153 = t793;
                    uint8_t t794 = (uint8_t)!((((uint8_t *)&gm_l153))[0] != 0);
                    if (!t794) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l153) + 1, gm_srck6bk8q21r07v, 485, 13);
                    {
                        {
                            uint8_t t795 = (uint8_t)(((((uint8_t *)&F.l101) + 32))[16] != 0);
                            if (!t795) {
                                goto gm_lb119;
                            }
                            gm_b16_8 t796 = GM_LD(gm_b16_8, (((uint8_t *)&F.l101) + 32));
                            gm_l102 = t796;
                            gm_trqfvmjtt4sij t797;
                            gm_f_list_ccsq5cl((void *)&t797, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l103 = t797;
                            gm_b16_8 t798 = gm_l102;
                            gm_l104 = t798;
                            gm_l105 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t799 = gm_l105;
                                    uint64_t t800 = GM_LD(uint64_t, (((uint8_t *)&gm_l104) + 8));
                                    uint8_t t801 = (t799 < t800);
                                    if (!t801) {
                                        goto gm_lb120;
                                    }
                                    {
                                        void * t802 = GM_LD(void *, ((uint8_t *)&gm_l104));
                                        uint64_t t803 = GM_LD(uint64_t, (((uint8_t *)&gm_l104) + 8));
                                        uint64_t t804 = gm_l105;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l106), (((uint8_t *)t802) + (uintptr_t)t804 * 144), 144);
                                                void * t805 = (void *)((uint8_t *)&F.l103);
                                                void * t806 = (void *)((uint8_t *)&F.l106);
                                                gm_tvul7d1mdc2eq t807;
                                                gm_f_dump_arg_9iv8pdv((void *)&t807, (void *)NULL, cx, NULL, t806);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_tftb1odc739t5 t808 = gm_f_push_mjiecfq(t805, &t807);
                                                gm_l154 = t808;
                                                uint8_t t809 = (uint8_t)!((((uint8_t *)&gm_l154))[0] != 0);
                                                if (!t809) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l154) + 1, gm_srck6bk8q21r07v, 488, 31);
                                                goto gm_lb123;
                                            }
gm_lb124:;
                                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 488, 17);
                                        }
gm_lb123:;
                                    }
gm_lb122:;
                                    uint64_t t810 = gm_l105;
                                    uint64_t r811; bool o812 = __builtin_add_overflow(t810, ((uint64_t)1ULL), &r811);
                                    uint64_t t813 = r811;
                                    gm_l105 = t813;
                                }
                            }
gm_lb120:;
                            void * t814 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t815;
                            GM_ST(void *, (uint8_t *)&t815, (void *)(gm_s20t5qopq8n9u));
                            GM_ST(uintptr_t, (uint8_t *)&t815 + 8, (uintptr_t)(4));
                            gm_tvul7d1mdc2eq t816;
                            gm_f_node_dm1r8dn((void *)&t816, (void *)NULL, cx, NULL, t815, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l103)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t817 = gm_f_push_mjiecfq(t814, &t816);
                            gm_l155 = t817;
                            uint8_t t818 = (uint8_t)!((((uint8_t *)&gm_l155))[0] != 0);
                            if (!t818) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l155) + 1, gm_srck6bk8q21r07v, 489, 17);
                            goto gm_lb118;
                        }
gm_lb119:;
                    }
gm_lb118:;
                    gm_b16_8 t819;
                    GM_ST(void *, (uint8_t *)&t819, (void *)(gm_s3t7gv740qicd));
                    GM_ST(uintptr_t, (uint8_t *)&t819 + 8, (uintptr_t)(9));
                    gm_trqfvmjtt4sij t820 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
                    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t819, &t820);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb117:;
            }
            {
                {
                    uint8_t t821 = GM_LD(uint8_t, ((((uint8_t *)t128) + 8) + 0));
                    uint8_t t822 = (uint8_t)(t821 == ((uint8_t)50ULL));
                    if (!t822) {
                        goto gm_lb125;
                    }
                    gm_b16_8 t823;
                    GM_ST(void *, (uint8_t *)&t823, (void *)(gm_s79s3h98matgr));
                    GM_ST(uintptr_t, (uint8_t *)&t823 + 8, (uintptr_t)(5));
                    gm_tvul7d1mdc2eq t824;
                    GM_ST(uint8_t, (uint8_t *)&t824 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t824 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t824 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t824 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t824 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t824 + 8), t823);
                    memmove(gm_out, ((uint8_t *)&t824), 32);
                    goto gm_fdone;
                }
gm_lb125:;
                gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 347, 5);
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_dump_file_cno3fos(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_dump_file_cno3fos F;
    void * gm_l0; /* f */
    void * gm_l1; /* out */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l6;
    gm_tvul7d1mdc2eq gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_trqfvmjtt4sij t7;
    gm_f_list_ccsq5cl((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t7;
    void * t8 = gm_l0;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)t8));
    gm_l3 = t9;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l4;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t15 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t13) + (uintptr_t)t15 * 160), 160);
                        void * t16 = (void *)((uint8_t *)&F.l2);
                        void * t17 = (void *)((uint8_t *)&F.l5);
                        gm_tvul7d1mdc2eq t18;
                        gm_f_dump_item_bs3lq5m((void *)&t18, (void *)NULL, cx, NULL, t17);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t19 = gm_f_push_mjiecfq(t16, &t18);
                        gm_l6 = t19;
                        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srck6bk8q21r07v, 19, 27);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 19, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t21 = gm_l4;
            uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
            uint64_t t24 = r22;
            gm_l4 = t24;
        }
    }
gm_lb1:;
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sl5jkfh6104lp));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(4));
    gm_tvul7d1mdc2eq t26;
    gm_f_bnode_hu9fd4q((void *)&t26, (void *)NULL, cx, NULL, t25, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t26;
    void * t27 = (void *)((uint8_t *)&gm_l7);
    void * t28 = gm_l1;
    gm_b0_1 t29 = gm_f_render_7k3fqtk((void *)NULL, cx, NULL, t27, ((int64_t)0LL), t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t30 = gm_l1;
    gm_tftb1odc739t5 t31 = gm_f_push_t83rg2m(t30, ((uint8_t)10ULL));
    gm_l8 = t31;
    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srck6bk8q21r07v, 21, 5);
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
inline __attribute__((aligned(4))) void gm_f_dump_fn_lit_tchlprc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_fn_lit_tchlprc F;
    void * gm_l0; /* f */
    gm_tvul7d1mdc2eq gm_l2; /* w */
    void * gm_l4; /* e */
    GM_STACK_CHECK(&F, 383u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4eh5qkgoio6s gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tvul7d1mdc2eq gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_trqfvmjtt4sij t7;
    gm_f_list_ccsq5cl((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t7;
    {
        {
            void * t8 = gm_l0;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 72));
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
            gm_f_dump_constraints_68aa75n((void *)&gm_l5, (void *)NULL, cx, NULL, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l5))[32] != 0);
            if (!t11) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l2), ((uint8_t *)&gm_l5), 32);
            void * t12 = (void *)((uint8_t *)&F.l1);
            gm_tftb1odc739t5 t13 = gm_f_push_mjiecfq(t12, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l2)));
            gm_l6 = t13;
            uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srck6bk8q21r07v, 581, 42);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t15 = (void *)((uint8_t *)&F.l1);
    void * t16 = gm_l0;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 88) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
            if (!t18) {
                goto gm_lb4;
            }
            memmove(((uint8_t *)&F.l3), ((((uint8_t *)t16) + 88) + 8), 40);
            void * t19 = (void *)((uint8_t *)&F.l3);
            gm_f_dump_block_5utjqg0((void *)&gm_l7, (void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb3;
        }
gm_lb4:;
        {
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t16) + 88) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)1ULL));
            if (!t21) {
                goto gm_lb5;
            }
            void * t22 = GM_LD(void *, ((((uint8_t *)t16) + 88) + 8));
            gm_l4 = t22;
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sm8obe455p2d9));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(2));
            void * t24 = gm_l4;
            gm_tvul7d1mdc2eq t25;
            gm_f_dump_expr_l3bbdps((void *)&t25, (void *)NULL, cx, NULL, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node1_gpsjtpc((void *)&gm_l7, (void *)NULL, cx, NULL, t23, &t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb3;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 582, 15);
    }
gm_lb3:;
    gm_tftb1odc739t5 t26 = gm_f_push_mjiecfq(t15, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l7)));
    gm_l8 = t26;
    uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srck6bk8q21r07v, 582, 5);
    gm_b16_8 t28;
    GM_ST(void *, (uint8_t *)&t28, (void *)(gm_s579fb7l5l6df));
    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(2));
    void * t29 = gm_l0;
    void * t30 = (void *)((uint8_t *)t29);
    gm_trqfvmjtt4sij t31 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_dump_fn_sig_4ip3qer((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t28, t30, &t31);
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
inline __attribute__((aligned(4))) void gm_f_dump_fn_sig_4ip3qer(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, const void * a2) {
    struct gm_fr_dump_fn_sig_4ip3qer F;
    gm_b16_8 gm_l0; /* head */
    void * gm_l1; /* sig */
    void * gm_l4; /* r */
    gm_b16_8 gm_l5; /* e */
    GM_STACK_CHECK(&F, 564u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_trqfvmjtt4sij t6;
    gm_f_list_ccsq5cl((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t6;
    void * t7 = (void *)((uint8_t *)&F.l3);
    void * t8 = gm_l1;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)t8));
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    gm_tvul7d1mdc2eq t11;
    gm_f_dump_params_vfgaabj((void *)&t11, (void *)NULL, cx, NULL, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t12 = gm_f_push_mjiecfq(t7, &t11);
    gm_l6 = t12;
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srck6bk8q21r07v, 564, 5);
    void * t14 = gm_l1;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t14) + 16));
    if (t15) {
        void * t16 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sqpev88g6capg));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(8));
        gm_tvul7d1mdc2eq t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t18 + 8), t17);
        gm_tftb1odc739t5 t19 = gm_f_push_mjiecfq(t16, &t18);
        gm_l7 = t19;
        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srck6bk8q21r07v, 565, 23);
    }
    {
        {
            void * t21 = gm_l1;
            uint8_t t22 = (uint8_t)(GM_LD(void *, (((uint8_t *)t21) + 24)) != 0);
            if (!t22) {
                goto gm_lb2;
            }
            void * t23 = GM_LD(void *, (((uint8_t *)t21) + 24));
            gm_l4 = t23;
            void * t24 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s1k3lfare8sei));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(3));
            void * t26 = gm_l4;
            gm_tvul7d1mdc2eq t27;
            gm_f_dump_expr_l3bbdps((void *)&t27, (void *)NULL, cx, NULL, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t28;
            gm_f_node1_gpsjtpc((void *)&t28, (void *)NULL, cx, NULL, t25, &t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t29 = gm_f_push_mjiecfq(t24, &t28);
            gm_l8 = t29;
            uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srck6bk8q21r07v, 566, 23);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t31 = gm_l1;
            uint8_t t32 = (uint8_t)(((((uint8_t *)t31) + 32))[16] != 0);
            if (!t32) {
                goto gm_lb4;
            }
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t31) + 32));
            gm_l5 = t33;
            void * t34 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t35 = gm_l5;
            gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
            gm_tvul7d1mdc2eq t37;
            gm_f_dump_effects_4fqcm0g((void *)&t37, (void *)NULL, cx, NULL, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t38 = gm_f_push_mjiecfq(t34, &t37);
            gm_l9 = t38;
            uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 567, 27);
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t40 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
    gm_tftb1odc739t5 t43 = gm_f_extend_i72a9oi(t40, t42);
    gm_l10 = t43;
    uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srck6bk8q21r07v, 568, 5);
    gm_b16_8 t45 = gm_l0;
    gm_trqfvmjtt4sij t46 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l3));
    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t45, &t46);
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
inline __attribute__((aligned(4))) void gm_f_dump_foreign_member_vrht12f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_foreign_member_vrht12f F;
    void * gm_l0; /* m */
    gm_t08olq97rscn3 gm_l2; /* n */
    gm_t08olq97rscn3 gm_l4; /* n */
    void * gm_l5; /* ty */
    GM_STACK_CHECK(&F, 552u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_trqfvmjtt4sij t5;
    gm_f_list_ccsq5cl((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t5;
    void * t6 = (void *)((uint8_t *)&F.l1);
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t7));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_b0_1 t10 = gm_f_push_attrs_a5l439c((void *)NULL, cx, NULL, t6, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t11 = gm_l0;
    {
        {
            uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t11) + 16) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)0ULL));
            if (!t13) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t11) + 16) + 8), 32);
            memmove(((uint8_t *)&F.l3), (((((uint8_t *)t11) + 16) + 8) + 32), 72);
            void * t14 = (void *)((uint8_t *)&F.l1);
            gm_tvul7d1mdc2eq t15;
            gm_f_ident_sx_kd0v352((void *)&t15, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)));
            gm_tftb1odc739t5 t16 = gm_f_push_mjiecfq(t14, &t15);
            gm_l6 = t16;
            uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srck6bk8q21r07v, 683, 13);
            void * t18 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s252dj7ek002d));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(7));
            void * t20 = (void *)((uint8_t *)&F.l3);
            gm_trqfvmjtt4sij t21;
            gm_f_list_ccsq5cl((void *)&t21, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t22;
            gm_f_dump_fn_sig_4ip3qer((void *)&t22, (void *)NULL, cx, NULL, t19, t20, &t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t23 = gm_f_push_mjiecfq(t18, &t22);
            gm_l7 = t23;
            uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srck6bk8q21r07v, 684, 13);
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_se31bb9e7icn2));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(10));
            gm_trqfvmjtt4sij t26 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t25, &t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t11) + 16) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)1ULL));
            if (!t28) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t11) + 16) + 8), 32);
            void * t29 = GM_LD(void *, (((((uint8_t *)t11) + 16) + 8) + 32));
            gm_l5 = t29;
            void * t30 = (void *)((uint8_t *)&F.l1);
            gm_tvul7d1mdc2eq t31;
            gm_f_ident_sx_kd0v352((void *)&t31, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l4)));
            gm_tftb1odc739t5 t32 = gm_f_push_mjiecfq(t30, &t31);
            gm_l8 = t32;
            uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srck6bk8q21r07v, 688, 13);
            void * t34 = (void *)((uint8_t *)&F.l1);
            void * t35 = gm_l5;
            gm_tvul7d1mdc2eq t36;
            gm_f_dump_expr_l3bbdps((void *)&t36, (void *)NULL, cx, NULL, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t37 = gm_f_push_mjiecfq(t34, &t36);
            gm_l9 = t37;
            uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 689, 13);
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_saufuk3rs2i2m));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(14));
            gm_trqfvmjtt4sij t40 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t39, &t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 681, 5);
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
inline __attribute__((aligned(4))) void gm_f_dump_handler_decl_htmrmsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_handler_decl_htmrmsf F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l2; /* p */
    gm_b16_8 gm_l3; /* e */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 668u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_trqfvmjtt4sij t11;
    gm_f_list_ccsq5cl((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t11;
    {
        {
            void * t12 = gm_l0;
            uint8_t t13 = (uint8_t)((((uint8_t *)t12))[16] != 0);
            if (!t13) {
                goto gm_lb2;
            }
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)t12));
            gm_l2 = t14;
            void * t15 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t16 = gm_l2;
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            gm_tvul7d1mdc2eq t18;
            gm_f_dump_params_vfgaabj((void *)&t18, (void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t19 = gm_f_push_mjiecfq(t15, &t18);
            gm_l7 = t19;
            uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srck6bk8q21r07v, 233, 24);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t21 = (void *)((uint8_t *)&F.l1);
    void * t22 = gm_l0;
    void * t23 = GM_LD(void *, (((uint8_t *)t22) + 24));
    gm_tvul7d1mdc2eq t24;
    gm_f_dump_expr_l3bbdps((void *)&t24, (void *)NULL, cx, NULL, t23);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t25 = gm_f_push_mjiecfq(t21, &t24);
    gm_l8 = t25;
    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srck6bk8q21r07v, 234, 5);
    void * t27 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t28;
    GM_ST(void *, (uint8_t *)&t28, (void *)(gm_scc8p0a9eachg));
    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(3));
    void * t29 = gm_l0;
    void * t30 = GM_LD(void *, (((uint8_t *)t29) + 32));
    gm_tvul7d1mdc2eq t31;
    gm_f_dump_expr_l3bbdps((void *)&t31, (void *)NULL, cx, NULL, t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tvul7d1mdc2eq t32;
    gm_f_node1_gpsjtpc((void *)&t32, (void *)NULL, cx, NULL, t28, &t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t33 = gm_f_push_mjiecfq(t27, &t32);
    gm_l9 = t33;
    uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 235, 5);
    {
        {
            void * t35 = gm_l0;
            uint8_t t36 = (uint8_t)(((((uint8_t *)t35) + 40))[16] != 0);
            if (!t36) {
                goto gm_lb4;
            }
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)t35) + 40));
            gm_l3 = t37;
            void * t38 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t39 = gm_l3;
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            gm_tvul7d1mdc2eq t41;
            gm_f_dump_effects_4fqcm0g((void *)&t41, (void *)NULL, cx, NULL, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t42 = gm_f_push_mjiecfq(t38, &t41);
            gm_l10 = t42;
            uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srck6bk8q21r07v, 236, 25);
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t44 = gm_l0;
    gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)t44) + 64));
    gm_l4 = t45;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t46 = gm_l5;
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t48 = (t46 < t47);
            if (!t48) {
                goto gm_lb5;
            }
            {
                void * t49 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t51 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t49) + (uintptr_t)t51 * 72), 72);
                        void * t52 = (void *)((uint8_t *)&F.l1);
                        void * t53 = (void *)((uint8_t *)&F.l6);
                        gm_tvul7d1mdc2eq t54;
                        gm_f_dump_clause_kje51q9((void *)&t54, (void *)NULL, cx, NULL, t53);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t55 = gm_f_push_mjiecfq(t52, &t54);
                        gm_l11 = t55;
                        uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srck6bk8q21r07v, 237, 26);
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 237, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t57 = gm_l5;
            uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
            uint64_t t60 = r58;
            gm_l5 = t60;
        }
    }
gm_lb5:;
    gm_b16_8 t61;
    GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sf3ii7gekd9ll));
    GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(12));
    gm_trqfvmjtt4sij t62 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t61, &t62);
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
__attribute__((aligned(4))) void gm_f_dump_if_3qo5nul(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_if_3qo5nul F;
    void * gm_l0; /* i */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t08olq97rscn3 gm_l7; /* n */
    void * gm_l8; /* body */
    GM_STACK_CHECK(&F, 713u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_trqfvmjtt4sij t11;
    gm_f_list_ccsq5cl((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t11;
    void * t12 = gm_l0;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)t12));
    gm_l2 = t13;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t14 = gm_l3;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                void * t17 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t19 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t17) + (uintptr_t)t19 * 72), 72);
                        gm_trqfvmjtt4sij t20;
                        gm_f_list_ccsq5cl((void *)&t20, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l5 = t20;
                        void * t21 = (void *)((uint8_t *)&F.l5);
                        void * t22 = (void *)((uint8_t *)&F.l4);
                        gm_tvul7d1mdc2eq t23;
                        gm_f_dump_cond_cu0s815((void *)&t23, (void *)NULL, cx, NULL, t22);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t24 = gm_f_push_mjiecfq(t21, &t23);
                        gm_l9 = t24;
                        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 520, 9);
                        void * t26 = (void *)((uint8_t *)&F.l5);
                        void * t27 = (void *)(((uint8_t *)&F.l4) + 32);
                        gm_tvul7d1mdc2eq t28;
                        gm_f_dump_block_5utjqg0((void *)&t28, (void *)NULL, cx, NULL, t27);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t29 = gm_f_push_mjiecfq(t26, &t28);
                        gm_l10 = t29;
                        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srck6bk8q21r07v, 521, 9);
                        void * t31 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t32;
                        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s0jho24omhfui));
                        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(6));
                        gm_tvul7d1mdc2eq t33;
                        gm_f_bnode_hu9fd4q((void *)&t33, (void *)NULL, cx, NULL, t32, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l5)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t34 = gm_f_push_mjiecfq(t31, &t33);
                        gm_l11 = t34;
                        uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srck6bk8q21r07v, 522, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 518, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t36 = gm_l3;
            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
            uint64_t t39 = r37;
            gm_l3 = t39;
        }
    }
gm_lb1:;
    void * t40 = gm_l0;
    {
        {
            uint8_t t41 = (uint8_t)(((((uint8_t *)t40) + 16))[48] != 0);
            if (t41) {
                goto gm_lb7;
            }
            goto gm_lb6;
        }
gm_lb7:;
        {
            uint8_t t42 = (uint8_t)(((((uint8_t *)t40) + 16))[48] != 0);
            if (!t42) {
                goto gm_lb8;
            }
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t40) + 16) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)0ULL));
            if (!t44) {
                goto gm_lb8;
            }
            memmove(((uint8_t *)&F.l6), ((((uint8_t *)t40) + 16) + 8), 40);
            void * t45 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s9es2tos1bhfp));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(4));
            void * t47 = (void *)((uint8_t *)&F.l6);
            gm_tvul7d1mdc2eq t48;
            gm_f_dump_block_5utjqg0((void *)&t48, (void *)NULL, cx, NULL, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t49;
            gm_f_node1_gpsjtpc((void *)&t49, (void *)NULL, cx, NULL, t46, &t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t50 = gm_f_push_mjiecfq(t45, &t49);
            gm_l12 = t50;
            uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srck6bk8q21r07v, 526, 22);
            goto gm_lb6;
        }
gm_lb8:;
        {
            uint8_t t52 = (uint8_t)(((((uint8_t *)t40) + 16))[48] != 0);
            if (!t52) {
                goto gm_lb9;
            }
            uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t40) + 16) + 0));
            uint8_t t54 = (uint8_t)(t53 == ((uint8_t)1ULL));
            if (!t54) {
                goto gm_lb9;
            }
            memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t40) + 16) + 8), 32);
            void * t55 = GM_LD(void *, (((((uint8_t *)t40) + 16) + 8) + 32));
            gm_l8 = t55;
            void * t56 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sjjc564tiep5s));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(12));
            gm_tvul7d1mdc2eq t58;
            gm_f_ident_sx_kd0v352((void *)&t58, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)));
            void * t59 = gm_l8;
            gm_tvul7d1mdc2eq t60;
            gm_f_dump_expr_l3bbdps((void *)&t60, (void *)NULL, cx, NULL, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t61;
            gm_f_node2_b4qpqat((void *)&t61, (void *)NULL, cx, NULL, t57, &t58, &t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t62 = gm_f_push_mjiecfq(t56, &t61);
            gm_l13 = t62;
            uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srck6bk8q21r07v, 527, 30);
            goto gm_lb6;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 524, 5);
    }
gm_lb6:;
    gm_b16_8 t64;
    GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sl9nvct67or45));
    GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(2));
    gm_trqfvmjtt4sij t65 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t64, &t65);
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
__attribute__((aligned(4))) void gm_f_dump_item_bs3lq5m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_item_bs3lq5m F;
    void * gm_l0; /* item */
    gm_b16_8 gm_l2; /* vis */
    gm_b16_8 gm_l3; /* doc */
    void * gm_l6; /* h */
    void * gm_l8; /* e */
    GM_STACK_CHECK(&F, 741u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tvul7d1mdc2eq gm_l12;
    gm_b16_8 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_trqfvmjtt4sij t12;
    gm_f_list_ccsq5cl((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t12;
    {
        {
            void * t13 = gm_l0;
            gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t13) + 56));
            gm_f_visibility_u2ipb12((void *)&gm_l9, t14);
            uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
            if (!t15) {
                goto gm_lb2;
            }
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
            gm_l2 = t16;
            void * t17 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t18 = gm_l2;
            gm_tvul7d1mdc2eq t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 8), t18);
            gm_tftb1odc739t5 t20 = gm_f_push_mjiecfq(t17, &t19);
            gm_l10 = t20;
            uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srck6bk8q21r07v, 204, 38);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t22 = gm_l0;
            uint8_t t23 = (uint8_t)(((((uint8_t *)t22) + 16))[16] != 0);
            if (!t23) {
                goto gm_lb4;
            }
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 16));
            gm_l3 = t24;
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s8rsdirj4dmun));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(3));
            gm_b16_8 t27 = gm_l3;
            gm_tvul7d1mdc2eq t28;
            gm_f_atom_of_bytes_dj32elq((void *)&t28, (void *)NULL, cx, NULL, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t29;
            gm_f_node1_gpsjtpc((void *)&t29, (void *)NULL, cx, NULL, t26, &t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t30 = gm_f_push_mjiecfq(t25, &t29);
            gm_l11 = t30;
            uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srck6bk8q21r07v, 205, 26);
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t32 = (void *)((uint8_t *)&F.l1);
    void * t33 = gm_l0;
    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t33) + 40));
    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
    gm_b0_1 t36 = gm_f_push_attrs_a5l439c((void *)NULL, cx, NULL, t32, t35);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t37 = (void *)((uint8_t *)&F.l1);
    void * t38 = gm_l0;
    {
        {
            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t38) + 64) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)0ULL));
            if (!t40) {
                goto gm_lb6;
            }
            memmove(((uint8_t *)&F.l4), ((((uint8_t *)t38) + 64) + 8), 64);
            void * t41 = (void *)((uint8_t *)&F.l4);
            gm_f_dump_binding_b27qkdk((void *)&gm_l12, (void *)NULL, cx, NULL, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb5;
        }
gm_lb6:;
        {
            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t38) + 64) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)1ULL));
            if (!t43) {
                goto gm_lb7;
            }
            memmove(((uint8_t *)&F.l5), ((((uint8_t *)t38) + 64) + 8), 64);
            void * t44 = (void *)((uint8_t *)&F.l5);
            gm_f_dump_when_items_16tgm3i((void *)&gm_l12, (void *)NULL, cx, NULL, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb5;
        }
gm_lb7:;
        {
            uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t38) + 64) + 0));
            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)2ULL));
            if (!t46) {
                goto gm_lb8;
            }
            void * t47 = GM_LD(void *, ((((uint8_t *)t38) + 64) + 8));
            gm_l6 = t47;
            void * t48 = gm_l6;
            gm_f_dump_handler_decl_htmrmsf((void *)&gm_l12, (void *)NULL, cx, NULL, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb5;
        }
gm_lb8:;
        {
            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t38) + 64) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)3ULL));
            if (!t50) {
                goto gm_lb9;
            }
            memmove(((uint8_t *)&F.l7), ((((uint8_t *)t38) + 64) + 8), 88);
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 80));
            if (t51) {
                gm_b16_8 t52;
                GM_ST(void *, (uint8_t *)&t52, (void *)(gm_srfagufvviad7));
                GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(5));
                gm_l13 = t52;
            } else {
                gm_b16_8 t53;
                GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s9r64t1166q1f));
                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(4));
                gm_l13 = t53;
            }
            gm_b16_8 t54 = gm_l13;
            gm_tvul7d1mdc2eq t55;
            gm_f_str_lit_a2k13t3((void *)&t55, (void *)NULL, cx, NULL, &(*(gm_tibupt4dmcqai *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t56 = (void *)(((uint8_t *)&F.l7) + 40);
            gm_tvul7d1mdc2eq t57;
            gm_f_dump_block_5utjqg0((void *)&t57, (void *)NULL, cx, NULL, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node2_b4qpqat((void *)&gm_l12, (void *)NULL, cx, NULL, t54, &t55, &t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb5;
        }
gm_lb9:;
        {
            uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t38) + 64) + 0));
            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)4ULL));
            if (!t59) {
                goto gm_lb10;
            }
            void * t60 = GM_LD(void *, ((((uint8_t *)t38) + 64) + 8));
            gm_l8 = t60;
            gm_b16_8 t61;
            GM_ST(void *, (uint8_t *)&t61, (void *)(gm_snas2pfnujj1i));
            GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(14));
            void * t62 = gm_l8;
            gm_tvul7d1mdc2eq t63;
            gm_f_dump_expr_l3bbdps((void *)&t63, (void *)NULL, cx, NULL, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node1_gpsjtpc((void *)&gm_l12, (void *)NULL, cx, NULL, t61, &t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb5;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 207, 12);
    }
gm_lb5:;
    gm_tftb1odc739t5 t64 = gm_f_push_mjiecfq(t37, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l12)));
    gm_l14 = t64;
    uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srck6bk8q21r07v, 207, 5);
    gm_b16_8 t66;
    GM_ST(void *, (uint8_t *)&t66, (void *)(gm_srltb43nuabaq));
    GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(4));
    gm_trqfvmjtt4sij t67 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t66, &t67);
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
inline __attribute__((aligned(4))) void gm_f_dump_items_atcioo9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_dump_items_atcioo9 F;
    gm_b16_8 gm_l0; /* label */
    gm_b16_8 gm_l1; /* items */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 278u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l6;
    gm_trqfvmjtt4sij t7;
    gm_f_list_ccsq5cl((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t7;
    gm_b16_8 t8 = gm_l1;
    gm_l3 = t8;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l4;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t14 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t12) + (uintptr_t)t14 * 160), 160);
                        void * t15 = (void *)((uint8_t *)&F.l2);
                        void * t16 = (void *)((uint8_t *)&F.l5);
                        gm_tvul7d1mdc2eq t17;
                        gm_f_dump_item_bs3lq5m((void *)&t17, (void *)NULL, cx, NULL, t16);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t18 = gm_f_push_mjiecfq(t15, &t17);
                        gm_l6 = t18;
                        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srck6bk8q21r07v, 219, 25);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 219, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t20 = gm_l4;
            uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
            uint64_t t23 = r21;
            gm_l4 = t23;
        }
    }
gm_lb1:;
    gm_b16_8 t24 = gm_l0;
    gm_trqfvmjtt4sij t25 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
    gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t24, &t25);
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
__attribute__((aligned(4))) void gm_f_dump_param_kr39os0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_param_kr39os0 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t08olq97rscn3 gm_l4; /* n */
    void * gm_l6; /* t */
    void * gm_l7; /* d */
    GM_STACK_CHECK(&F, 779u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_trqfvmjtt4sij t11;
    gm_f_list_ccsq5cl((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t11;
    void * t12 = gm_l0;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)t12));
    gm_l2 = t13;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t14 = gm_l3;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                void * t17 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t19 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t17) + (uintptr_t)t19 * 32), 32);
                        void * t20 = (void *)((uint8_t *)&F.l1);
                        gm_tvul7d1mdc2eq t21;
                        gm_f_ident_sx_kd0v352((void *)&t21, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l4)));
                        gm_tftb1odc739t5 t22 = gm_f_push_mjiecfq(t20, &t21);
                        gm_l8 = t22;
                        uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                        if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srck6bk8q21r07v, 546, 24);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 546, 5);
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
    gm_trqfvmjtt4sij t28;
    gm_f_list_ccsq5cl((void *)&t28, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t28;
    void * t29 = (void *)((uint8_t *)&F.l5);
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sm56lhobvb6g5));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(5));
    gm_tvul7d1mdc2eq t31;
    gm_f_node_dm1r8dn((void *)&t31, (void *)NULL, cx, NULL, t30, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t32 = gm_f_push_mjiecfq(t29, &t31);
    gm_l9 = t32;
    uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 548, 5);
    void * t34 = gm_l0;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)t34) + 16));
    if (t35) {
        void * t36 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_ssqg2h6hrn099));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(8));
        gm_tvul7d1mdc2eq t38;
        GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t38 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t38 + 8), t37);
        gm_tftb1odc739t5 t39 = gm_f_push_mjiecfq(t36, &t38);
        gm_l10 = t39;
        uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
        if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srck6bk8q21r07v, 549, 21);
    }
    void * t41 = gm_l0;
    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)t41) + 17));
    if (t42) {
        void * t43 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sm8si2u0a1u17));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(4));
        gm_tvul7d1mdc2eq t45;
        GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t45 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t45 + 8), t44);
        gm_tftb1odc739t5 t46 = gm_f_push_mjiecfq(t43, &t45);
        gm_l11 = t46;
        uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
        if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srck6bk8q21r07v, 550, 17);
    }
    {
        {
            void * t48 = gm_l0;
            uint8_t t49 = (uint8_t)(GM_LD(void *, (((uint8_t *)t48) + 24)) != 0);
            if (!t49) {
                goto gm_lb7;
            }
            void * t50 = GM_LD(void *, (((uint8_t *)t48) + 24));
            gm_l6 = t50;
            void * t51 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sqbhgmmp8kn4m));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(2));
            void * t53 = gm_l6;
            gm_tvul7d1mdc2eq t54;
            gm_f_dump_expr_l3bbdps((void *)&t54, (void *)NULL, cx, NULL, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t55;
            gm_f_node1_gpsjtpc((void *)&t55, (void *)NULL, cx, NULL, t52, &t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t56 = gm_f_push_mjiecfq(t51, &t55);
            gm_l12 = t56;
            uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srck6bk8q21r07v, 551, 20);
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    {
        {
            void * t58 = gm_l0;
            uint8_t t59 = (uint8_t)(GM_LD(void *, (((uint8_t *)t58) + 32)) != 0);
            if (!t59) {
                goto gm_lb9;
            }
            void * t60 = GM_LD(void *, (((uint8_t *)t58) + 32));
            gm_l7 = t60;
            void * t61 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t62;
            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_ses9qtp6kdd22));
            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(7));
            void * t63 = gm_l7;
            gm_tvul7d1mdc2eq t64;
            gm_f_dump_expr_l3bbdps((void *)&t64, (void *)NULL, cx, NULL, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t65;
            gm_f_node1_gpsjtpc((void *)&t65, (void *)NULL, cx, NULL, t62, &t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t66 = gm_f_push_mjiecfq(t61, &t65);
            gm_l13 = t66;
            uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srck6bk8q21r07v, 552, 25);
            goto gm_lb8;
        }
gm_lb9:;
    }
gm_lb8:;
    gm_b16_8 t68;
    GM_ST(void *, (uint8_t *)&t68, (void *)(gm_skissj10nclhr));
    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(5));
    gm_trqfvmjtt4sij t69 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l5));
    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t68, &t69);
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
inline __attribute__((aligned(4))) void gm_f_dump_params_vfgaabj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dump_params_vfgaabj F;
    gm_b16_8 gm_l0; /* params */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 262u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l5;
    gm_trqfvmjtt4sij t7;
    gm_f_list_ccsq5cl((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t7;
    gm_b16_8 t8 = gm_l0;
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
                        void * t15 = (void *)((uint8_t *)&F.l1);
                        void * t16 = (void *)((uint8_t *)&F.l4);
                        gm_tvul7d1mdc2eq t17;
                        gm_f_dump_param_kr39os0((void *)&t17, (void *)NULL, cx, NULL, t16);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t18 = gm_f_push_mjiecfq(t15, &t17);
                        gm_l5 = t18;
                        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srck6bk8q21r07v, 558, 23);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 558, 5);
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
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sfkmkcagsnqnb));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(6));
    gm_trqfvmjtt4sij t25 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t24, &t25);
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
inline __attribute__((aligned(4))) void gm_f_dump_pat_arg_ai8l7kq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_pat_arg_ai8l7kq F;
    void * gm_l0; /* a */
    gm_t08olq97rscn3 gm_l1; /* n */
    GM_STACK_CHECK(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = (uint8_t)((((uint8_t *)t4))[32] != 0);
            if (!t5) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l1), ((uint8_t *)t4), 32);
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s19ndlkgdtviu));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(5));
            gm_tvul7d1mdc2eq t7;
            gm_f_ident_sx_kd0v352((void *)&t7, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l1)));
            void * t8 = gm_l0;
            void * t9 = (void *)(((uint8_t *)t8) + 40);
            gm_tvul7d1mdc2eq t10;
            gm_f_dump_pattern_q6u5c24((void *)&t10, (void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t6, &t7, &t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t11 = gm_l0;
    void * t12 = (void *)(((uint8_t *)t11) + 40);
    gm_f_dump_pattern_q6u5c24((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t12);
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
__attribute__((aligned(4))) void gm_f_dump_pattern_q6u5c24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_pattern_q6u5c24 F;
    void * gm_l0; /* p */
    gm_t08olq97rscn3 gm_l2; /* id */
    gm_t08olq97rscn3 gm_l3; /* id */
    void * gm_l4; /* e */
    void * gm_l5; /* lo */
    void * gm_l6; /* hi */
    uint8_t gm_l7; /* inclusive */
    void * gm_l8; /* inner */
    gm_tagc77imt2chm gm_l9; /* pp */
    gm_b16_8 gm_l10; /* xs */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l15; /* args */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    GM_STACK_CHECK(&F, 1395u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_trqfvmjtt4sij t24;
    gm_f_list_ccsq5cl((void *)&t24, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t24;
    void * t25 = gm_l0;
    {
        {
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
            if (!t27) {
                goto gm_lb2;
            }
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_s3brj349ts8fs));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(8));
            gm_tvul7d1mdc2eq t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t29 + 8), t28);
            memmove(gm_out, ((uint8_t *)&t29), 32);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)1ULL));
            if (!t31) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t25) + 8) + 8), 32);
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_shmknkannurll));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(4));
            gm_tvul7d1mdc2eq t33;
            gm_f_ident_sx_kd0v352((void *)&t33, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)));
            gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t32, &t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)2ULL));
            if (!t35) {
                goto gm_lb4;
            }
            memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t25) + 8) + 8), 32);
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_smjq2fi49hdnf));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(10));
            gm_tvul7d1mdc2eq t37;
            gm_f_ident_sx_kd0v352((void *)&t37, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l3)));
            gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t36, &t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb4:;
        {
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)3ULL));
            if (!t39) {
                goto gm_lb5;
            }
            void * t40 = GM_LD(void *, ((((uint8_t *)t25) + 8) + 8));
            gm_l4 = t40;
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_shs3nem6t7u8g));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(3));
            void * t42 = gm_l4;
            gm_tvul7d1mdc2eq t43;
            gm_f_dump_expr_l3bbdps((void *)&t43, (void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t41, &t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb5:;
        {
            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)4ULL));
            if (!t45) {
                goto gm_lb6;
            }
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_suctgno4l10a5));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(8));
            gm_tvul7d1mdc2eq t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t47 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t47 + 8), t46);
            memmove(gm_out, ((uint8_t *)&t47), 32);
            goto gm_fdone;
        }
gm_lb6:;
        {
            uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            uint8_t t49 = (uint8_t)(t48 == ((uint8_t)5ULL));
            if (!t49) {
                goto gm_lb7;
            }
            void * t50 = GM_LD(void *, ((((uint8_t *)t25) + 8) + 8));
            gm_l5 = t50;
            void * t51 = GM_LD(void *, (((((uint8_t *)t25) + 8) + 8) + 8));
            gm_l6 = t51;
            uint8_t t52 = GM_LD(uint8_t, (((((uint8_t *)t25) + 8) + 8) + 16));
            gm_l7 = t52;
            void * t53 = (void *)((uint8_t *)&F.l1);
            void * t54 = gm_l5;
            gm_tvul7d1mdc2eq t55;
            gm_f_dump_expr_l3bbdps((void *)&t55, (void *)NULL, cx, NULL, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t56 = gm_f_push_mjiecfq(t53, &t55);
            gm_l19 = t56;
            uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srck6bk8q21r07v, 706, 13);
            void * t58 = (void *)((uint8_t *)&F.l1);
            void * t59 = gm_l6;
            gm_tvul7d1mdc2eq t60;
            gm_f_dump_expr_l3bbdps((void *)&t60, (void *)NULL, cx, NULL, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t61 = gm_f_push_mjiecfq(t58, &t60);
            gm_l20 = t61;
            uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t62) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srck6bk8q21r07v, 707, 13);
            uint8_t t63 = gm_l7;
            if (t63) {
                void * t64 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t65;
                GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s3ia9jm7npe6t));
                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(9));
                gm_tvul7d1mdc2eq t66;
                GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t66 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t65);
                gm_tftb1odc739t5 t67 = gm_f_push_mjiecfq(t64, &t66);
                gm_l21 = t67;
                uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srck6bk8q21r07v, 708, 28);
            }
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sl4ns6hobicht));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(5));
            gm_trqfvmjtt4sij t70 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t69, &t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb7:;
        {
            uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            uint8_t t72 = (uint8_t)(t71 == ((uint8_t)6ULL));
            if (!t72) {
                goto gm_lb8;
            }
            void * t73 = GM_LD(void *, ((((uint8_t *)t25) + 8) + 8));
            gm_l8 = t73;
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_shhr1hlq1ethc));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(3));
            void * t75 = gm_l8;
            gm_tvul7d1mdc2eq t76;
            gm_f_dump_pattern_q6u5c24((void *)&t76, (void *)NULL, cx, NULL, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t74, &t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb8:;
        {
            uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            uint8_t t78 = (uint8_t)(t77 == ((uint8_t)7ULL));
            if (!t78) {
                goto gm_lb9;
            }
            memmove(((uint8_t *)&gm_l9), ((((uint8_t *)t25) + 8) + 8), 32);
            void * t79 = (void *)((uint8_t *)&F.l1);
            void * t80 = GM_LD(void *, ((uint8_t *)&gm_l9));
            gm_tvul7d1mdc2eq t81;
            gm_f_dump_expr_l3bbdps((void *)&t81, (void *)NULL, cx, NULL, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t82 = gm_f_push_mjiecfq(t79, &t81);
            gm_l22 = t82;
            uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
            if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srck6bk8q21r07v, 713, 13);
            {
                {
                    uint8_t t84 = (uint8_t)(((((uint8_t *)&gm_l9) + 8))[16] != 0);
                    if (!t84) {
                        goto gm_lb11;
                    }
                    gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                    gm_l10 = t85;
                    gm_trqfvmjtt4sij t86;
                    gm_f_list_ccsq5cl((void *)&t86, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l11 = t86;
                    gm_b16_8 t87 = gm_l10;
                    gm_l12 = t87;
                    gm_l13 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t88 = gm_l13;
                            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                            uint8_t t90 = (t88 < t89);
                            if (!t90) {
                                goto gm_lb12;
                            }
                            {
                                void * t91 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint64_t t93 = gm_l13;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l14), (((uint8_t *)t91) + (uintptr_t)t93 * 120), 120);
                                        void * t94 = (void *)((uint8_t *)&F.l11);
                                        void * t95 = (void *)((uint8_t *)&F.l14);
                                        gm_tvul7d1mdc2eq t96;
                                        gm_f_dump_pat_arg_ai8l7kq((void *)&t96, (void *)NULL, cx, NULL, t95);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t97 = gm_f_push_mjiecfq(t94, &t96);
                                        gm_l23 = t97;
                                        uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                        if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srck6bk8q21r07v, 716, 31);
                                        goto gm_lb15;
                                    }
gm_lb16:;
                                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 716, 17);
                                }
gm_lb15:;
                            }
gm_lb14:;
                            uint64_t t99 = gm_l13;
                            uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                            uint64_t t102 = r100;
                            gm_l13 = t102;
                        }
                    }
gm_lb12:;
                    void * t103 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t104;
                    GM_ST(void *, (uint8_t *)&t104, (void *)(gm_s20t5qopq8n9u));
                    GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(4));
                    gm_tvul7d1mdc2eq t105;
                    gm_f_node_dm1r8dn((void *)&t105, (void *)NULL, cx, NULL, t104, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l11)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t106 = gm_f_push_mjiecfq(t103, &t105);
                    gm_l24 = t106;
                    uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                    if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srck6bk8q21r07v, 717, 17);
                    goto gm_lb10;
                }
gm_lb11:;
            }
gm_lb10:;
            gm_b16_8 t108;
            GM_ST(void *, (uint8_t *)&t108, (void *)(gm_smimi08sfrg0n));
            GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(4));
            gm_trqfvmjtt4sij t109 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t108, &t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb9:;
        {
            uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
            uint8_t t111 = (uint8_t)(t110 == ((uint8_t)8ULL));
            if (!t111) {
                goto gm_lb17;
            }
            gm_b16_8 t112 = GM_LD(gm_b16_8, ((((uint8_t *)t25) + 8) + 8));
            gm_l15 = t112;
            gm_b16_8 t113 = gm_l15;
            gm_l16 = t113;
            gm_l17 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t114 = gm_l17;
                    uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint8_t t116 = (t114 < t115);
                    if (!t116) {
                        goto gm_lb18;
                    }
                    {
                        void * t117 = GM_LD(void *, ((uint8_t *)&gm_l16));
                        uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                        uint64_t t119 = gm_l17;
                        {
                            {
                                memmove(((uint8_t *)&F.l18), (((uint8_t *)t117) + (uintptr_t)t119 * 120), 120);
                                void * t120 = (void *)((uint8_t *)&F.l1);
                                void * t121 = (void *)((uint8_t *)&F.l18);
                                gm_tvul7d1mdc2eq t122;
                                gm_f_dump_pat_arg_ai8l7kq((void *)&t122, (void *)NULL, cx, NULL, t121);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t123 = gm_f_push_mjiecfq(t120, &t122);
                                gm_l25 = t123;
                                uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srck6bk8q21r07v, 722, 29);
                                goto gm_lb21;
                            }
gm_lb22:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 722, 13);
                        }
gm_lb21:;
                    }
gm_lb20:;
                    uint64_t t125 = gm_l17;
                    uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                    uint64_t t128 = r126;
                    gm_l17 = t128;
                }
            }
gm_lb18:;
            gm_b16_8 t129;
            GM_ST(void *, (uint8_t *)&t129, (void *)(gm_sk83con0mdsfe));
            GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(5));
            gm_trqfvmjtt4sij t130 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t129, &t130);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb17:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 699, 5);
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
__attribute__((aligned(4))) void gm_f_dump_stmt_tfqg5dm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_stmt_tfqg5dm F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* attrs */
    gm_b16_8 gm_l4; /* places */
    gm_tjsp7bhdn2e8j gm_l5; /* op */
    void * gm_l6; /* value */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    void * gm_l12; /* e */
    uint8_t gm_l13; /* is_err */
    void * gm_l14; /* body */
    void * gm_l15; /* w */
    gm_t08olq97rscn3 gm_l18; /* l */
    void * gm_l19; /* e */
    void * gm_l21; /* value */
    void * gm_l22; /* fallback */
    GM_STACK_CHECK(&F, 1704u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_b16_8 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    void * t19 = gm_l0;
    {
        {
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)0ULL));
            if (!t21) {
                goto gm_lb2;
            }
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)t19) + 8) + 8));
            gm_l1 = t22;
            memmove(((uint8_t *)&F.l2), (((((uint8_t *)t19) + 8) + 8) + 16), 64);
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t24 = ((t23 == ((uint64_t)0ULL)));
            if (t24) {
                void * t25 = (void *)((uint8_t *)&F.l2);
                gm_f_dump_binding_b27qkdk((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            gm_trqfvmjtt4sij t26;
            gm_f_list_ccsq5cl((void *)&t26, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l3 = t26;
            void * t27 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t28 = gm_l1;
            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
            gm_b0_1 t30 = gm_f_push_attrs_a5l439c((void *)NULL, cx, NULL, t27, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t31 = (void *)((uint8_t *)&F.l3);
            void * t32 = (void *)((uint8_t *)&F.l2);
            gm_tvul7d1mdc2eq t33;
            gm_f_dump_binding_b27qkdk((void *)&t33, (void *)NULL, cx, NULL, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t34 = gm_f_push_mjiecfq(t31, &t33);
            gm_l25 = t34;
            uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
            if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srck6bk8q21r07v, 286, 13);
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_spl4qnmq2arsc));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(12));
            gm_trqfvmjtt4sij t37 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l3));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t36, &t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)1ULL));
            if (!t39) {
                goto gm_lb3;
            }
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((((uint8_t *)t19) + 8) + 8));
            gm_l4 = t40;
            gm_tjsp7bhdn2e8j t41 = GM_LD(gm_tjsp7bhdn2e8j, (((((uint8_t *)t19) + 8) + 8) + 16));
            gm_l5 = t41;
            void * t42 = GM_LD(void *, (((((uint8_t *)t19) + 8) + 8) + 24));
            gm_l6 = t42;
            gm_trqfvmjtt4sij t43;
            gm_f_list_ccsq5cl((void *)&t43, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l7 = t43;
            gm_b16_8 t44 = gm_l4;
            gm_l8 = t44;
            gm_l9 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t45 = gm_l9;
                    uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint8_t t47 = (t45 < t46);
                    if (!t47) {
                        goto gm_lb4;
                    }
                    {
                        void * t48 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint64_t t50 = gm_l9;
                        {
                            {
                                memmove(((uint8_t *)&F.l10), (((uint8_t *)t48) + (uintptr_t)t50 * 88), 88);
                                void * t51 = (void *)((uint8_t *)&F.l7);
                                void * t52 = (void *)((uint8_t *)&F.l10);
                                gm_tvul7d1mdc2eq t53;
                                gm_f_dump_expr_l3bbdps((void *)&t53, (void *)NULL, cx, NULL, t52);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t54 = gm_f_push_mjiecfq(t51, &t53);
                                gm_l26 = t54;
                                uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srck6bk8q21r07v, 291, 31);
                                goto gm_lb7;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 291, 13);
                        }
gm_lb7:;
                    }
gm_lb6:;
                    uint64_t t56 = gm_l9;
                    uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                    uint64_t t59 = r57;
                    gm_l9 = t59;
                }
            }
gm_lb4:;
            gm_trqfvmjtt4sij t60;
            gm_f_list_ccsq5cl((void *)&t60, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l11 = t60;
            void * t61 = (void *)((uint8_t *)&F.l11);
            gm_tjsp7bhdn2e8j t62 = gm_l5;
            gm_b16_8 t63 = gm_f_assign_op_name_8crhefq((void *)NULL, cx, NULL, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t64 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t64 + 8), t63);
            gm_tftb1odc739t5 t65 = gm_f_push_mjiecfq(t61, &t64);
            gm_l27 = t65;
            uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
            if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srck6bk8q21r07v, 293, 13);
            void * t67 = (void *)((uint8_t *)&F.l11);
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s0l81imssm78p));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(6));
            gm_tvul7d1mdc2eq t69;
            gm_f_node_dm1r8dn((void *)&t69, (void *)NULL, cx, NULL, t68, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t70 = gm_f_push_mjiecfq(t67, &t69);
            gm_l28 = t70;
            uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
            if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srck6bk8q21r07v, 294, 13);
            void * t72 = (void *)((uint8_t *)&F.l11);
            void * t73 = gm_l6;
            gm_tvul7d1mdc2eq t74;
            gm_f_dump_expr_l3bbdps((void *)&t74, (void *)NULL, cx, NULL, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t75 = gm_f_push_mjiecfq(t72, &t74);
            gm_l29 = t75;
            uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
            if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srck6bk8q21r07v, 295, 13);
            gm_b16_8 t77;
            GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sugq7dl55ocon));
            GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(6));
            gm_trqfvmjtt4sij t78 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l11));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t77, &t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t80 = (uint8_t)(t79 == ((uint8_t)2ULL));
            if (!t80) {
                goto gm_lb9;
            }
            void * t81 = GM_LD(void *, ((((uint8_t *)t19) + 8) + 8));
            gm_l12 = t81;
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sk5s72mtujop8));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(4));
            void * t83 = gm_l12;
            gm_tvul7d1mdc2eq t84;
            gm_f_dump_expr_l3bbdps((void *)&t84, (void *)NULL, cx, NULL, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t82, &t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb9:;
        {
            uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t86 = (uint8_t)(t85 == ((uint8_t)3ULL));
            if (!t86) {
                goto gm_lb10;
            }
            uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 8));
            gm_l13 = t87;
            void * t88 = GM_LD(void *, (((((uint8_t *)t19) + 8) + 8) + 8));
            gm_l14 = t88;
            uint8_t t89 = gm_l13;
            if (t89) {
                gm_b16_8 t90;
                GM_ST(void *, (uint8_t *)&t90, (void *)(gm_ser8ip7vtbv6c));
                GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(8));
                gm_l30 = t90;
            } else {
                gm_b16_8 t91;
                GM_ST(void *, (uint8_t *)&t91, (void *)(gm_sllcm1pqagh28));
                GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(5));
                gm_l30 = t91;
            }
            gm_b16_8 t92 = gm_l30;
            void * t93 = gm_l14;
            gm_tvul7d1mdc2eq t94;
            gm_f_dump_stmt_tfqg5dm((void *)&t94, (void *)NULL, cx, NULL, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t92, &t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb10:;
        {
            uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t96 = (uint8_t)(t95 == ((uint8_t)4ULL));
            if (!t96) {
                goto gm_lb11;
            }
            void * t97 = GM_LD(void *, ((((uint8_t *)t19) + 8) + 8));
            gm_l15 = t97;
            void * t98 = gm_l15;
            gm_f_dump_when_stmt_hdq14ev((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb11:;
        {
            uint8_t t99 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t100 = (uint8_t)(t99 == ((uint8_t)5ULL));
            if (!t100) {
                goto gm_lb12;
            }
            memmove(((uint8_t *)&F.l16), ((((uint8_t *)t19) + 8) + 8), 112);
            gm_trqfvmjtt4sij t101;
            gm_f_list_ccsq5cl((void *)&t101, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l17 = t101;
            {
                {
                    uint8_t t102 = (uint8_t)((((uint8_t *)&F.l16))[32] != 0);
                    if (!t102) {
                        goto gm_lb14;
                    }
                    memmove(((uint8_t *)&gm_l18), ((uint8_t *)&F.l16), 32);
                    void * t103 = (void *)((uint8_t *)&F.l17);
                    gm_b16_8 t104;
                    GM_ST(void *, (uint8_t *)&t104, (void *)(gm_su137hja0ebcd));
                    GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(5));
                    gm_tvul7d1mdc2eq t105;
                    gm_f_ident_sx_kd0v352((void *)&t105, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l18)));
                    gm_tvul7d1mdc2eq t106;
                    gm_f_node1_gpsjtpc((void *)&t106, (void *)NULL, cx, NULL, t104, &t105);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t107 = gm_f_push_mjiecfq(t103, &t106);
                    gm_l31 = t107;
                    uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                    if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srck6bk8q21r07v, 303, 31);
                    goto gm_lb13;
                }
gm_lb14:;
            }
gm_lb13:;
            void * t109 = (void *)((uint8_t *)&F.l17);
            void * t110 = (void *)(((uint8_t *)&F.l16) + 40);
            gm_tvul7d1mdc2eq t111;
            gm_f_dump_cond_cu0s815((void *)&t111, (void *)NULL, cx, NULL, t110);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t112 = gm_f_push_mjiecfq(t109, &t111);
            gm_l32 = t112;
            uint8_t t113 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
            if (!t113) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srck6bk8q21r07v, 304, 13);
            void * t114 = (void *)((uint8_t *)&F.l17);
            void * t115 = (void *)(((uint8_t *)&F.l16) + 72);
            gm_tvul7d1mdc2eq t116;
            gm_f_dump_block_5utjqg0((void *)&t116, (void *)NULL, cx, NULL, t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t117 = gm_f_push_mjiecfq(t114, &t116);
            gm_l33 = t117;
            uint8_t t118 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
            if (!t118) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srck6bk8q21r07v, 305, 13);
            gm_b16_8 t119;
            GM_ST(void *, (uint8_t *)&t119, (void *)(gm_s5rv6umk4akl0));
            GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(5));
            gm_trqfvmjtt4sij t120 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l17));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t119, &t120);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb12:;
        {
            uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t122 = (uint8_t)(t121 == ((uint8_t)6ULL));
            if (!t122) {
                goto gm_lb15;
            }
            void * t123 = GM_LD(void *, ((((uint8_t *)t19) + 8) + 8));
            gm_l19 = t123;
            gm_b16_8 t124;
            GM_ST(void *, (uint8_t *)&t124, (void *)(gm_si1m7q6ia0hto));
            GM_ST(uintptr_t, (uint8_t *)&t124 + 8, (uintptr_t)(6));
            void * t125 = gm_l19;
            gm_tvul7d1mdc2eq t126;
            gm_f_dump_expr_l3bbdps((void *)&t126, (void *)NULL, cx, NULL, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t124, &t126);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb15:;
        {
            uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t128 = (uint8_t)(t127 == ((uint8_t)7ULL));
            if (!t128) {
                goto gm_lb16;
            }
            memmove(((uint8_t *)&F.l20), ((((uint8_t *)t19) + 8) + 8), 64);
            void * t129 = GM_LD(void *, (((((uint8_t *)t19) + 8) + 8) + 64));
            gm_l21 = t129;
            void * t130 = GM_LD(void *, (((((uint8_t *)t19) + 8) + 8) + 72));
            gm_l22 = t130;
            gm_trqfvmjtt4sij t131;
            gm_f_list_ccsq5cl((void *)&t131, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l23 = t131;
            void * t132 = (void *)((uint8_t *)&F.l23);
            void * t133 = (void *)((uint8_t *)&F.l20);
            gm_tvul7d1mdc2eq t134;
            gm_f_dump_pattern_q6u5c24((void *)&t134, (void *)NULL, cx, NULL, t133);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t135 = gm_f_push_mjiecfq(t132, &t134);
            gm_l34 = t135;
            uint8_t t136 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
            if (!t136) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_srck6bk8q21r07v, 311, 13);
            void * t137 = (void *)((uint8_t *)&F.l23);
            void * t138 = gm_l21;
            gm_tvul7d1mdc2eq t139;
            gm_f_dump_expr_l3bbdps((void *)&t139, (void *)NULL, cx, NULL, t138);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t140 = gm_f_push_mjiecfq(t137, &t139);
            gm_l35 = t140;
            uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
            if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_srck6bk8q21r07v, 312, 13);
            void * t142 = (void *)((uint8_t *)&F.l23);
            void * t143 = gm_l22;
            gm_tvul7d1mdc2eq t144;
            gm_f_dump_expr_l3bbdps((void *)&t144, (void *)NULL, cx, NULL, t143);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t145 = gm_f_push_mjiecfq(t142, &t144);
            gm_l36 = t145;
            uint8_t t146 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
            if (!t146) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srck6bk8q21r07v, 313, 13);
            gm_b16_8 t147;
            GM_ST(void *, (uint8_t *)&t147, (void *)(gm_s283bs8hsleb3));
            GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(8));
            gm_trqfvmjtt4sij t148 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l23));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t147, &t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb16:;
        {
            uint8_t t149 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t150 = (uint8_t)(t149 == ((uint8_t)8ULL));
            if (!t150) {
                goto gm_lb17;
            }
            memmove(((uint8_t *)&F.l24), ((((uint8_t *)t19) + 8) + 8), 40);
            void * t151 = (void *)((uint8_t *)&F.l24);
            gm_f_dump_block_5utjqg0((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb17:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 281, 5);
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
inline __attribute__((aligned(4))) void gm_f_dump_struct_lit_n9dqlnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_struct_lit_n9dqlnm F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* p */
    gm_tvul7d1mdc2eq gm_l3; /* w */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 506u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l7;
    gm_t4eh5qkgoio6s gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_trqfvmjtt4sij t11;
    gm_f_list_ccsq5cl((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t11;
    {
        {
            void * t12 = gm_l0;
            uint8_t t13 = (uint8_t)((((uint8_t *)t12))[16] != 0);
            if (!t13) {
                goto gm_lb2;
            }
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)t12));
            gm_l2 = t14;
            void * t15 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t16 = gm_l2;
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            gm_tvul7d1mdc2eq t18;
            gm_f_dump_params_vfgaabj((void *)&t18, (void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t19 = gm_f_push_mjiecfq(t15, &t18);
            gm_l7 = t19;
            uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srck6bk8q21r07v, 591, 24);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t21 = gm_l0;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 24));
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
            gm_f_dump_constraints_68aa75n((void *)&gm_l8, (void *)NULL, cx, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = (uint8_t)((((uint8_t *)&gm_l8))[32] != 0);
            if (!t24) {
                goto gm_lb4;
            }
            memmove(((uint8_t *)&gm_l3), ((uint8_t *)&gm_l8), 32);
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_tftb1odc739t5 t26 = gm_f_push_mjiecfq(t25, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l3)));
            gm_l9 = t26;
            uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 592, 42);
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t28 = gm_l0;
    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 40));
    gm_l4 = t29;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t30 = gm_l5;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t32 = (t30 < t31);
            if (!t32) {
                goto gm_lb5;
            }
            {
                void * t33 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t35 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t33) + (uintptr_t)t35 * 80), 80);
                        void * t36 = (void *)((uint8_t *)&F.l1);
                        void * t37 = (void *)((uint8_t *)&F.l6);
                        gm_tvul7d1mdc2eq t38;
                        gm_f_dump_struct_member_q034tdn((void *)&t38, (void *)NULL, cx, NULL, t37);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t39 = gm_f_push_mjiecfq(t36, &t38);
                        gm_l10 = t39;
                        uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srck6bk8q21r07v, 593, 26);
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 593, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t41 = gm_l5;
            uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
            uint64_t t44 = r42;
            gm_l5 = t44;
        }
    }
gm_lb5:;
    gm_b16_8 t45;
    GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sc49g68j2cekh));
    GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(6));
    gm_trqfvmjtt4sij t46 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t45, &t46);
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
__attribute__((aligned(4))) void gm_f_dump_struct_member_q034tdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_struct_member_q034tdn F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l3; /* vis */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t08olq97rscn3 gm_l7; /* n */
    void * gm_l8; /* d */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l14; /* members */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    GM_STACK_CHECK(&F, 1427u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l19;
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
    gm_tftb1odc739t5 gm_l30;
    gm_trqfvmjtt4sij t26;
    gm_f_list_ccsq5cl((void *)&t26, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t26;
    void * t27 = gm_l0;
    {
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)t27) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)0ULL));
            if (!t29) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), (((uint8_t *)t27) + 8), 72);
            void * t30 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            gm_b0_1 t33 = gm_f_push_attrs_a5l439c((void *)NULL, cx, NULL, t30, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    gm_t9qkh0rsooc8d t34 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 16));
                    gm_f_visibility_u2ipb12((void *)&gm_l19, t34);
                    uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
                    if (!t35) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
                    gm_l3 = t36;
                    void * t37 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t38 = gm_l3;
                    gm_tvul7d1mdc2eq t39;
                    GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t39 + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t39 + 8), t38);
                    gm_tftb1odc739t5 t40 = gm_f_push_mjiecfq(t37, &t39);
                    gm_l20 = t40;
                    uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                    if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srck6bk8q21r07v, 602, 43);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 17));
            if (t42) {
                void * t43 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t44;
                GM_ST(void *, (uint8_t *)&t44, (void *)(gm_smu48daue2qai));
                GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(5));
                gm_tvul7d1mdc2eq t45;
                GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t45 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t45 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t45 + 8), t44);
                gm_tftb1odc739t5 t46 = gm_f_push_mjiecfq(t43, &t45);
                gm_l21 = t46;
                uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srck6bk8q21r07v, 603, 29);
            }
            gm_trqfvmjtt4sij t48;
            gm_f_list_ccsq5cl((void *)&t48, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l4 = t48;
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
            gm_l5 = t49;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t50 = gm_l6;
                    uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t52 = (t50 < t51);
                    if (!t52) {
                        goto gm_lb5;
                    }
                    {
                        void * t53 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t55 = gm_l6;
                        {
                            {
                                memmove(((uint8_t *)&gm_l7), (((uint8_t *)t53) + (uintptr_t)t55 * 32), 32);
                                void * t56 = (void *)((uint8_t *)&F.l4);
                                gm_tvul7d1mdc2eq t57;
                                gm_f_ident_sx_kd0v352((void *)&t57, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)));
                                gm_tftb1odc739t5 t58 = gm_f_push_mjiecfq(t56, &t57);
                                gm_l22 = t58;
                                uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                                if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srck6bk8q21r07v, 605, 32);
                                goto gm_lb8;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 605, 13);
                        }
gm_lb8:;
                    }
gm_lb7:;
                    uint64_t t60 = gm_l6;
                    uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                    uint64_t t63 = r61;
                    gm_l6 = t63;
                }
            }
gm_lb5:;
            void * t64 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sm56lhobvb6g5));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(5));
            gm_tvul7d1mdc2eq t66;
            gm_f_node_dm1r8dn((void *)&t66, (void *)NULL, cx, NULL, t65, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t67 = gm_f_push_mjiecfq(t64, &t66);
            gm_l23 = t67;
            uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
            if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srck6bk8q21r07v, 606, 13);
            void * t69 = (void *)((uint8_t *)&F.l1);
            void * t70 = GM_LD(void *, (((uint8_t *)&F.l2) + 40));
            gm_tvul7d1mdc2eq t71;
            gm_f_dump_expr_l3bbdps((void *)&t71, (void *)NULL, cx, NULL, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t72 = gm_f_push_mjiecfq(t69, &t71);
            gm_l24 = t72;
            uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
            if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srck6bk8q21r07v, 607, 13);
            {
                {
                    uint8_t t74 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l2) + 48)) != 0);
                    if (!t74) {
                        goto gm_lb11;
                    }
                    void * t75 = GM_LD(void *, (((uint8_t *)&F.l2) + 48));
                    gm_l8 = t75;
                    void * t76 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t77;
                    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_ses9qtp6kdd22));
                    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(7));
                    void * t78 = gm_l8;
                    gm_tvul7d1mdc2eq t79;
                    gm_f_dump_expr_l3bbdps((void *)&t79, (void *)NULL, cx, NULL, t78);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tvul7d1mdc2eq t80;
                    gm_f_node1_gpsjtpc((void *)&t80, (void *)NULL, cx, NULL, t77, &t79);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t81 = gm_f_push_mjiecfq(t76, &t80);
                    gm_l25 = t81;
                    uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                    if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srck6bk8q21r07v, 608, 33);
                    goto gm_lb10;
                }
gm_lb11:;
            }
gm_lb10:;
            gm_b16_8 t83;
            GM_ST(void *, (uint8_t *)&t83, (void *)(gm_s1jboc8lkvac0));
            GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(5));
            gm_trqfvmjtt4sij t84 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t83, &t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)t27) + 0));
            uint8_t t86 = (uint8_t)(t85 == ((uint8_t)1ULL));
            if (!t86) {
                goto gm_lb12;
            }
            memmove(((uint8_t *)&F.l9), (((uint8_t *)t27) + 8), 64);
            void * t87 = (void *)((uint8_t *)&F.l1);
            void * t88 = GM_LD(void *, ((uint8_t *)&F.l9));
            gm_tvul7d1mdc2eq t89;
            gm_f_dump_expr_l3bbdps((void *)&t89, (void *)NULL, cx, NULL, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t90 = gm_f_push_mjiecfq(t87, &t89);
            gm_l26 = t90;
            uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
            if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srck6bk8q21r07v, 612, 13);
            gm_trqfvmjtt4sij t92;
            gm_f_list_ccsq5cl((void *)&t92, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l10 = t92;
            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 8));
            gm_l11 = t93;
            gm_l12 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t94 = gm_l12;
                    uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint8_t t96 = (t94 < t95);
                    if (!t96) {
                        goto gm_lb13;
                    }
                    {
                        void * t97 = GM_LD(void *, ((uint8_t *)&gm_l11));
                        uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                        uint64_t t99 = gm_l12;
                        {
                            {
                                memmove(((uint8_t *)&F.l13), (((uint8_t *)t97) + (uintptr_t)t99 * 80), 80);
                                void * t100 = (void *)((uint8_t *)&F.l10);
                                void * t101 = (void *)((uint8_t *)&F.l13);
                                gm_tvul7d1mdc2eq t102;
                                gm_f_dump_struct_member_q034tdn((void *)&t102, (void *)NULL, cx, NULL, t101);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t103 = gm_f_push_mjiecfq(t100, &t102);
                                gm_l27 = t103;
                                uint8_t t104 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                if (!t104) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srck6bk8q21r07v, 614, 31);
                                goto gm_lb16;
                            }
gm_lb17:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 614, 13);
                        }
gm_lb16:;
                    }
gm_lb15:;
                    uint64_t t105 = gm_l12;
                    uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                    uint64_t t108 = r106;
                    gm_l12 = t108;
                }
            }
gm_lb13:;
            void * t109 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t110;
            GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sofbtko7t8taa));
            GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(4));
            gm_tvul7d1mdc2eq t111;
            gm_f_bnode_hu9fd4q((void *)&t111, (void *)NULL, cx, NULL, t110, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l10)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t112 = gm_f_push_mjiecfq(t109, &t111);
            gm_l28 = t112;
            uint8_t t113 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
            if (!t113) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srck6bk8q21r07v, 615, 13);
            {
                {
                    uint8_t t114 = (uint8_t)(((((uint8_t *)&F.l9) + 24))[16] != 0);
                    if (!t114) {
                        goto gm_lb19;
                    }
                    gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 24));
                    gm_l14 = t115;
                    gm_trqfvmjtt4sij t116;
                    gm_f_list_ccsq5cl((void *)&t116, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l15 = t116;
                    gm_b16_8 t117 = gm_l14;
                    gm_l16 = t117;
                    gm_l17 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t118 = gm_l17;
                            uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                            uint8_t t120 = (t118 < t119);
                            if (!t120) {
                                goto gm_lb20;
                            }
                            {
                                void * t121 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                uint64_t t123 = gm_l17;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l18), (((uint8_t *)t121) + (uintptr_t)t123 * 80), 80);
                                        void * t124 = (void *)((uint8_t *)&F.l15);
                                        void * t125 = (void *)((uint8_t *)&F.l18);
                                        gm_tvul7d1mdc2eq t126;
                                        gm_f_dump_struct_member_q034tdn((void *)&t126, (void *)NULL, cx, NULL, t125);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t127 = gm_f_push_mjiecfq(t124, &t126);
                                        gm_l29 = t127;
                                        uint8_t t128 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                        if (!t128) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srck6bk8q21r07v, 618, 36);
                                        goto gm_lb23;
                                    }
gm_lb24:;
                                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 618, 17);
                                }
gm_lb23:;
                            }
gm_lb22:;
                            uint64_t t129 = gm_l17;
                            uint64_t r130; bool o131 = __builtin_add_overflow(t129, ((uint64_t)1ULL), &r130);
                            uint64_t t132 = r130;
                            gm_l17 = t132;
                        }
                    }
gm_lb20:;
                    void * t133 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t134;
                    GM_ST(void *, (uint8_t *)&t134, (void *)(gm_s9es2tos1bhfp));
                    GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(4));
                    gm_tvul7d1mdc2eq t135;
                    gm_f_bnode_hu9fd4q((void *)&t135, (void *)NULL, cx, NULL, t134, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l15)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t136 = gm_f_push_mjiecfq(t133, &t135);
                    gm_l30 = t136;
                    uint8_t t137 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                    if (!t137) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srck6bk8q21r07v, 619, 17);
                    goto gm_lb18;
                }
gm_lb19:;
            }
gm_lb18:;
            gm_b16_8 t138;
            GM_ST(void *, (uint8_t *)&t138, (void *)(gm_s11b339fv56g1));
            GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(12));
            gm_trqfvmjtt4sij t139 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
            gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t138, &t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 599, 5);
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
inline __attribute__((aligned(4))) void gm_f_dump_tuple_elem_bul8rgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_tuple_elem_bul8rgi F;
    void * gm_l0; /* t */
    gm_t08olq97rscn3 gm_l1; /* n */
    GM_STACK_CHECK(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = (uint8_t)((((uint8_t *)t4))[32] != 0);
            if (!t5) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l1), ((uint8_t *)t4), 32);
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s19ndlkgdtviu));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(5));
            gm_tvul7d1mdc2eq t7;
            gm_f_ident_sx_kd0v352((void *)&t7, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l1)));
            void * t8 = gm_l0;
            void * t9 = (void *)(((uint8_t *)t8) + 40);
            gm_tvul7d1mdc2eq t10;
            gm_f_dump_expr_l3bbdps((void *)&t10, (void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t6, &t7, &t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t11 = gm_l0;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t11) + 128));
    if (t12) {
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s80ddhqa1o9i9));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(6));
        void * t14 = gm_l0;
        void * t15 = (void *)(((uint8_t *)t14) + 40);
        gm_tvul7d1mdc2eq t16;
        gm_f_dump_expr_l3bbdps((void *)&t16, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t13, &t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    void * t17 = gm_l0;
    void * t18 = (void *)(((uint8_t *)t17) + 40);
    gm_f_dump_expr_l3bbdps((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t18);
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
inline __attribute__((aligned(4))) void gm_f_dump_union_lit_leu6qp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_union_lit_leu6qp2 F;
    void * gm_l0; /* u */
    void * gm_l2; /* t */
    gm_b16_8 gm_l3; /* p */
    gm_tvul7d1mdc2eq gm_l4; /* w */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 632u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_t4eh5qkgoio6s gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_trqfvmjtt4sij t13;
    gm_f_list_ccsq5cl((void *)&t13, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t13;
    {
        {
            void * t14 = gm_l0;
            uint8_t t15 = (uint8_t)(GM_LD(void *, ((uint8_t *)t14)) != 0);
            if (!t15) {
                goto gm_lb2;
            }
            void * t16 = GM_LD(void *, ((uint8_t *)t14));
            gm_l2 = t16;
            void * t17 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s420tm9ua7ggg));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
            void * t19 = gm_l2;
            gm_tvul7d1mdc2eq t20;
            gm_f_dump_expr_l3bbdps((void *)&t20, (void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t21;
            gm_f_node1_gpsjtpc((void *)&t21, (void *)NULL, cx, NULL, t18, &t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t22 = gm_f_push_mjiecfq(t17, &t21);
            gm_l8 = t22;
            uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srck6bk8q21r07v, 628, 26);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t24 = gm_l0;
            uint8_t t25 = (uint8_t)(((((uint8_t *)t24) + 8))[16] != 0);
            if (!t25) {
                goto gm_lb4;
            }
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 8));
            gm_l3 = t26;
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t28 = gm_l3;
            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
            gm_tvul7d1mdc2eq t30;
            gm_f_dump_params_vfgaabj((void *)&t30, (void *)NULL, cx, NULL, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t31 = gm_f_push_mjiecfq(t27, &t30);
            gm_l9 = t31;
            uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 629, 24);
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    {
        {
            void * t33 = gm_l0;
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t33) + 32));
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
            gm_f_dump_constraints_68aa75n((void *)&gm_l10, (void *)NULL, cx, NULL, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l10))[32] != 0);
            if (!t36) {
                goto gm_lb6;
            }
            memmove(((uint8_t *)&gm_l4), ((uint8_t *)&gm_l10), 32);
            void * t37 = (void *)((uint8_t *)&F.l1);
            gm_tftb1odc739t5 t38 = gm_f_push_mjiecfq(t37, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l4)));
            gm_l11 = t38;
            uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srck6bk8q21r07v, 630, 42);
            goto gm_lb5;
        }
gm_lb6:;
    }
gm_lb5:;
    void * t40 = gm_l0;
    gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)t40) + 48));
    gm_l5 = t41;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t42 = gm_l6;
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t44 = (t42 < t43);
            if (!t44) {
                goto gm_lb7;
            }
            {
                void * t45 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t47 = gm_l6;
                {
                    {
                        memmove(((uint8_t *)&F.l7), (((uint8_t *)t45) + (uintptr_t)t47 * 72), 72);
                        void * t48 = (void *)((uint8_t *)&F.l1);
                        void * t49 = (void *)((uint8_t *)&F.l7);
                        gm_tvul7d1mdc2eq t50;
                        gm_f_dump_union_member_4lopa45((void *)&t50, (void *)NULL, cx, NULL, t49);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t51 = gm_f_push_mjiecfq(t48, &t50);
                        gm_l12 = t51;
                        uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srck6bk8q21r07v, 631, 26);
                        goto gm_lb10;
                    }
gm_lb11:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 631, 5);
                }
gm_lb10:;
            }
gm_lb9:;
            uint64_t t53 = gm_l6;
            uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
            uint64_t t56 = r54;
            gm_l6 = t56;
        }
    }
gm_lb7:;
    gm_b16_8 t57;
    GM_ST(void *, (uint8_t *)&t57, (void *)(gm_s8p1h96ji9kh0));
    GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(5));
    gm_trqfvmjtt4sij t58 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t57, &t58);
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
__attribute__((aligned(4))) void gm_f_dump_union_member_4lopa45(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_union_member_4lopa45 F;
    void * gm_l0; /* m */
    void * gm_l3; /* p */
    void * gm_l4; /* x */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l10; /* members */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    GM_STACK_CHECK(&F, 1041u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_trqfvmjtt4sij t21;
    gm_f_list_ccsq5cl((void *)&t21, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t21;
    void * t22 = gm_l0;
    {
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t22) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
            if (!t24) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), (((uint8_t *)t22) + 8), 64);
            void * t25 = (void *)((uint8_t *)&F.l1);
            gm_tvul7d1mdc2eq t26;
            gm_f_ident_sx_kd0v352((void *)&t26, &(*(gm_t08olq97rscn3 *)((uint8_t *)&F.l2)));
            gm_tftb1odc739t5 t27 = gm_f_push_mjiecfq(t25, &t26);
            gm_l15 = t27;
            uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
            if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srck6bk8q21r07v, 639, 13);
            {
                {
                    uint8_t t29 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l2) + 32)) != 0);
                    if (!t29) {
                        goto gm_lb4;
                    }
                    void * t30 = GM_LD(void *, (((uint8_t *)&F.l2) + 32));
                    gm_l3 = t30;
                    void * t31 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t32;
                    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sd9regb2pbgv1));
                    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(7));
                    void * t33 = gm_l3;
                    gm_tvul7d1mdc2eq t34;
                    gm_f_dump_expr_l3bbdps((void *)&t34, (void *)NULL, cx, NULL, t33);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tvul7d1mdc2eq t35;
                    gm_f_node1_gpsjtpc((void *)&t35, (void *)NULL, cx, NULL, t32, &t34);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t36 = gm_f_push_mjiecfq(t31, &t35);
                    gm_l16 = t36;
                    uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                    if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srck6bk8q21r07v, 640, 35);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            {
                {
                    uint8_t t38 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l2) + 40)) != 0);
                    if (!t38) {
                        goto gm_lb6;
                    }
                    void * t39 = GM_LD(void *, (((uint8_t *)&F.l2) + 40));
                    gm_l4 = t39;
                    void * t40 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t41;
                    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s5qkai093c1q0));
                    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(5));
                    void * t42 = gm_l4;
                    gm_tvul7d1mdc2eq t43;
                    gm_f_dump_expr_l3bbdps((void *)&t43, (void *)NULL, cx, NULL, t42);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tvul7d1mdc2eq t44;
                    gm_f_node1_gpsjtpc((void *)&t44, (void *)NULL, cx, NULL, t41, &t43);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t45 = gm_f_push_mjiecfq(t40, &t44);
                    gm_l17 = t45;
                    uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                    if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srck6bk8q21r07v, 641, 33);
                    goto gm_lb5;
                }
gm_lb6:;
            }
gm_lb5:;
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_spj9t9b8lqlpp));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(7));
            gm_trqfvmjtt4sij t48 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t47, &t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)t22) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)1ULL));
            if (!t50) {
                goto gm_lb7;
            }
            memmove(((uint8_t *)&F.l5), (((uint8_t *)t22) + 8), 64);
            void * t51 = (void *)((uint8_t *)&F.l1);
            void * t52 = GM_LD(void *, ((uint8_t *)&F.l5));
            gm_tvul7d1mdc2eq t53;
            gm_f_dump_expr_l3bbdps((void *)&t53, (void *)NULL, cx, NULL, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t54 = gm_f_push_mjiecfq(t51, &t53);
            gm_l18 = t54;
            uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srck6bk8q21r07v, 645, 13);
            gm_trqfvmjtt4sij t56;
            gm_f_list_ccsq5cl((void *)&t56, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l6 = t56;
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 8));
            gm_l7 = t57;
            gm_l8 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t58 = gm_l8;
                    uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint8_t t60 = (t58 < t59);
                    if (!t60) {
                        goto gm_lb8;
                    }
                    {
                        void * t61 = GM_LD(void *, ((uint8_t *)&gm_l7));
                        uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        uint64_t t63 = gm_l8;
                        {
                            {
                                memmove(((uint8_t *)&F.l9), (((uint8_t *)t61) + (uintptr_t)t63 * 72), 72);
                                void * t64 = (void *)((uint8_t *)&F.l6);
                                void * t65 = (void *)((uint8_t *)&F.l9);
                                gm_tvul7d1mdc2eq t66;
                                gm_f_dump_union_member_4lopa45((void *)&t66, (void *)NULL, cx, NULL, t65);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t67 = gm_f_push_mjiecfq(t64, &t66);
                                gm_l19 = t67;
                                uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srck6bk8q21r07v, 647, 31);
                                goto gm_lb11;
                            }
gm_lb12:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 647, 13);
                        }
gm_lb11:;
                    }
gm_lb10:;
                    uint64_t t69 = gm_l8;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                    uint64_t t72 = r70;
                    gm_l8 = t72;
                }
            }
gm_lb8:;
            void * t73 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sofbtko7t8taa));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(4));
            gm_tvul7d1mdc2eq t75;
            gm_f_bnode_hu9fd4q((void *)&t75, (void *)NULL, cx, NULL, t74, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l6)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t76 = gm_f_push_mjiecfq(t73, &t75);
            gm_l20 = t76;
            uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srck6bk8q21r07v, 648, 13);
            {
                {
                    uint8_t t78 = (uint8_t)(((((uint8_t *)&F.l5) + 24))[16] != 0);
                    if (!t78) {
                        goto gm_lb14;
                    }
                    gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
                    gm_l10 = t79;
                    gm_trqfvmjtt4sij t80;
                    gm_f_list_ccsq5cl((void *)&t80, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l11 = t80;
                    gm_b16_8 t81 = gm_l10;
                    gm_l12 = t81;
                    gm_l13 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t82 = gm_l13;
                            uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                            uint8_t t84 = (t82 < t83);
                            if (!t84) {
                                goto gm_lb15;
                            }
                            {
                                void * t85 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint64_t t87 = gm_l13;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l14), (((uint8_t *)t85) + (uintptr_t)t87 * 72), 72);
                                        void * t88 = (void *)((uint8_t *)&F.l11);
                                        void * t89 = (void *)((uint8_t *)&F.l14);
                                        gm_tvul7d1mdc2eq t90;
                                        gm_f_dump_union_member_4lopa45((void *)&t90, (void *)NULL, cx, NULL, t89);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t91 = gm_f_push_mjiecfq(t88, &t90);
                                        gm_l21 = t91;
                                        uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                        if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srck6bk8q21r07v, 651, 36);
                                        goto gm_lb18;
                                    }
gm_lb19:;
                                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 651, 17);
                                }
gm_lb18:;
                            }
gm_lb17:;
                            uint64_t t93 = gm_l13;
                            uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                            uint64_t t96 = r94;
                            gm_l13 = t96;
                        }
                    }
gm_lb15:;
                    void * t97 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t98;
                    GM_ST(void *, (uint8_t *)&t98, (void *)(gm_s9es2tos1bhfp));
                    GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(4));
                    gm_tvul7d1mdc2eq t99;
                    gm_f_bnode_hu9fd4q((void *)&t99, (void *)NULL, cx, NULL, t98, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l11)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t100 = gm_f_push_mjiecfq(t97, &t99);
                    gm_l22 = t100;
                    uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                    if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srck6bk8q21r07v, 652, 17);
                    goto gm_lb13;
                }
gm_lb14:;
            }
gm_lb13:;
            gm_b16_8 t102;
            GM_ST(void *, (uint8_t *)&t102, (void *)(gm_s11b339fv56g1));
            GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(12));
            gm_trqfvmjtt4sij t103 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
            gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t102, &t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 637, 5);
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
inline __attribute__((aligned(4))) void gm_f_dump_when_items_16tgm3i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_when_items_16tgm3i F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l2; /* e */
    GM_STACK_CHECK(&F, 400u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_trqfvmjtt4sij t4;
    gm_f_list_ccsq5cl((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t4;
    void * t5 = (void *)((uint8_t *)&F.l1);
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    gm_tvul7d1mdc2eq t8;
    gm_f_dump_expr_l3bbdps((void *)&t8, (void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t9 = gm_f_push_mjiecfq(t5, &t8);
    gm_l3 = t9;
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srck6bk8q21r07v, 225, 5);
    void * t11 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sofbtko7t8taa));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(4));
    void * t13 = gm_l0;
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 8));
    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
    gm_tvul7d1mdc2eq t16;
    gm_f_dump_items_atcioo9((void *)&t16, (void *)NULL, cx, NULL, t12, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t17 = gm_f_push_mjiecfq(t11, &t16);
    gm_l4 = t17;
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srck6bk8q21r07v, 226, 5);
    {
        {
            void * t19 = gm_l0;
            uint8_t t20 = (uint8_t)(((((uint8_t *)t19) + 24))[16] != 0);
            if (!t20) {
                goto gm_lb2;
            }
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t19) + 24));
            gm_l2 = t21;
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s9es2tos1bhfp));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
            gm_b16_8 t24 = gm_l2;
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
            gm_tvul7d1mdc2eq t26;
            gm_f_dump_items_atcioo9((void *)&t26, (void *)NULL, cx, NULL, t23, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t27 = gm_f_push_mjiecfq(t22, &t26);
            gm_l5 = t27;
            uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srck6bk8q21r07v, 227, 23);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sgmcjv273u32m));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(10));
    gm_trqfvmjtt4sij t30 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t29, &t30);
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
inline __attribute__((aligned(4))) void gm_f_dump_when_stmt_hdq14ev(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_when_stmt_hdq14ev F;
    void * gm_l0; /* w */
    void * gm_l3; /* inner */
    GM_STACK_CHECK(&F, 459u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_trqfvmjtt4sij t6;
    gm_f_list_ccsq5cl((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t6;
    void * t7 = (void *)((uint8_t *)&F.l1);
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    gm_tvul7d1mdc2eq t10;
    gm_f_dump_expr_l3bbdps((void *)&t10, (void *)NULL, cx, NULL, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t11 = gm_f_push_mjiecfq(t7, &t10);
    gm_l4 = t11;
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srck6bk8q21r07v, 322, 5);
    void * t13 = (void *)((uint8_t *)&F.l1);
    void * t14 = gm_l0;
    void * t15 = (void *)(((uint8_t *)t14) + 8);
    gm_tvul7d1mdc2eq t16;
    gm_f_dump_block_5utjqg0((void *)&t16, (void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t17 = gm_f_push_mjiecfq(t13, &t16);
    gm_l5 = t17;
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srck6bk8q21r07v, 323, 5);
    void * t19 = gm_l0;
    {
        {
            uint8_t t20 = (uint8_t)(((((uint8_t *)t19) + 48))[48] != 0);
            if (t20) {
                goto gm_lb2;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t21 = (uint8_t)(((((uint8_t *)t19) + 48))[48] != 0);
            if (!t21) {
                goto gm_lb3;
            }
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t19) + 48) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)0ULL));
            if (!t23) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&F.l2), ((((uint8_t *)t19) + 48) + 8), 40);
            void * t24 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s9es2tos1bhfp));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(4));
            void * t26 = (void *)((uint8_t *)&F.l2);
            gm_tvul7d1mdc2eq t27;
            gm_f_dump_block_5utjqg0((void *)&t27, (void *)NULL, cx, NULL, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t28;
            gm_f_node1_gpsjtpc((void *)&t28, (void *)NULL, cx, NULL, t25, &t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t29 = gm_f_push_mjiecfq(t24, &t28);
            gm_l6 = t29;
            uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srck6bk8q21r07v, 326, 22);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t31 = (uint8_t)(((((uint8_t *)t19) + 48))[48] != 0);
            if (!t31) {
                goto gm_lb4;
            }
            uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t19) + 48) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)1ULL));
            if (!t33) {
                goto gm_lb4;
            }
            void * t34 = GM_LD(void *, ((((uint8_t *)t19) + 48) + 8));
            gm_l3 = t34;
            void * t35 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s9es2tos1bhfp));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(4));
            void * t37 = gm_l3;
            gm_tvul7d1mdc2eq t38;
            gm_f_dump_when_stmt_hdq14ev((void *)&t38, (void *)NULL, cx, NULL, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t39;
            gm_f_node1_gpsjtpc((void *)&t39, (void *)NULL, cx, NULL, t36, &t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t40 = gm_f_push_mjiecfq(t35, &t39);
            gm_l7 = t40;
            uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srck6bk8q21r07v, 327, 25);
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 324, 5);
    }
gm_lb1:;
    gm_b16_8 t42;
    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s4dis7c905evg));
    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(9));
    gm_trqfvmjtt4sij t43 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t42, &t43);
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
__attribute__((aligned(4))) uint8_t gm_f_fits_mco2o3v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fits_mco2o3v F;
    void * gm_l0; /* sx */
    void * gm_l1; /* budget */
    gm_b16_8 gm_l2; /* s */
    gm_b16_8 gm_l3; /* items */
    uint8_t gm_l4; /* brk */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tvul7d1mdc2eq gm_l7; /* item */
    GM_STACK_CHECK(&F, 423u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t12 = gm_l0;
    {
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t12) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)0ULL));
            if (!t14) {
                goto gm_lb2;
            }
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 8));
            gm_l2 = t15;
            void * t16 = gm_l1;
            void * t17 = gm_l1;
            int64_t t18 = GM_LD(int64_t, ((uint8_t *)t17));
            gm_b16_8 t19 = gm_l2;
            uint64_t t20 = gm_f_char_count_76unqru(t19);
            int64_t t21 = (int64_t)t20;
            int64_t r22; bool o23 = __builtin_sub_overflow(t18, t21, &r22);
            if (o23) gm_trap("integer overflow in `-`", gm_srck6bk8q21r07v, 66, 13);
            int64_t t24 = r22;
            GM_ST(int64_t, ((uint8_t *)t16), t24);
            void * t25 = gm_l1;
            int64_t t26 = GM_LD(int64_t, ((uint8_t *)t25));
            uint8_t t27 = (t26 >= ((int64_t)0LL));
            F.ret = t27;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)t12) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
            if (!t29) {
                goto gm_lb3;
            }
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 8));
            gm_l3 = t30;
            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 16));
            gm_l4 = t31;
            {
                {
                    uint8_t t32 = gm_l4;
                    if (!t32) {
                        goto gm_lb5;
                    }
                    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t34 = (t33 > ((uint64_t)1ULL));
                    if (!t34) {
                        goto gm_lb5;
                    }
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
gm_lb5:;
            }
gm_lb4:;
            void * t35 = gm_l1;
            void * t36 = gm_l1;
            int64_t t37 = GM_LD(int64_t, ((uint8_t *)t36));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t39 = (t38 > ((uint64_t)1ULL) ? t38 : ((uint64_t)1ULL));
            int64_t t40 = (int64_t)t39;
            int64_t r41; bool o42 = __builtin_add_overflow(((int64_t)2LL), t40, &r41);
            if (o42) gm_trap("integer overflow in `+`", gm_srck6bk8q21r07v, 71, 24);
            int64_t t43 = r41;
            int64_t r44; bool o45 = __builtin_sub_overflow(t43, ((int64_t)1LL), &r44);
            if (o45) gm_trap("integer overflow in `-`", gm_srck6bk8q21r07v, 71, 24);
            int64_t t46 = r44;
            int64_t r47; bool o48 = __builtin_sub_overflow(t37, t46, &r47);
            if (o48) gm_trap("integer overflow in `-`", gm_srck6bk8q21r07v, 71, 13);
            int64_t t49 = r47;
            GM_ST(int64_t, ((uint8_t *)t35), t49);
            void * t50 = gm_l1;
            int64_t t51 = GM_LD(int64_t, ((uint8_t *)t50));
            uint8_t t52 = (t51 < ((int64_t)0LL));
            if (t52) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_b16_8 t53 = gm_l3;
            gm_l5 = t53;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t54 = gm_l6;
                    uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t56 = (t54 < t55);
                    if (!t56) {
                        goto gm_lb6;
                    }
                    {
                        void * t57 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t59 = gm_l6;
                        {
                            {
                                memmove(((uint8_t *)&gm_l7), (((uint8_t *)t57) + (uintptr_t)t59 * 32), 32);
                                void * t60 = (void *)((uint8_t *)&gm_l7);
                                void * t61 = gm_l1;
                                uint8_t t62 = gm_f_fits_mco2o3v(t60, t61);
                                uint8_t t63 = (uint8_t)!t62;
                                if (t63) {
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
                                goto gm_lb9;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 73, 13);
                        }
gm_lb9:;
                    }
gm_lb8:;
                    uint64_t t64 = gm_l6;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    uint64_t t67 = r65;
                    gm_l6 = t67;
                }
            }
gm_lb6:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 64, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_flat_hstd39j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_flat_hstd39j F;
    void * gm_l0; /* sx */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* s */
    gm_b16_8 gm_l3; /* items */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_tvul7d1mdc2eq gm_l7; /* item */
    GM_STACK_CHECK(&F, 466u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_toho9hmgj287v gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    void * t10 = gm_l0;
    {
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t10) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)0ULL));
            if (!t12) {
                goto gm_lb2;
            }
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 8));
            gm_l2 = t13;
            void * t14 = gm_l1;
            gm_b16_8 t15 = gm_l2;
            gm_tftb1odc739t5 t16 = gm_f_extend_0cs85gs(t14, t15);
            gm_l8 = t16;
            uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srck6bk8q21r07v, 83, 21);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t10) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)1ULL));
            if (!t19) {
                goto gm_lb3;
            }
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 8));
            gm_l3 = t20;
            void * t21 = gm_l1;
            gm_tftb1odc739t5 t22 = gm_f_push_t83rg2m(t21, ((uint8_t)40ULL));
            gm_l9 = t22;
            uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srck6bk8q21r07v, 85, 13);
            gm_b16_8 t24 = gm_l3;
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
            gm_l4 = t25;
            gm_l5 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t26 = gm_l5;
                    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint8_t t28 = (t26 < t27);
                    if (!t28) {
                        goto gm_lb4;
                    }
                    {
                        uint64_t t29 = gm_l5;
                        void * t30 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t32 = gm_l5;
                        gm_tvul7d1mdc2eq t33 = GM_LD(gm_tvul7d1mdc2eq, (((uint8_t *)t30) + (uintptr_t)t32 * 32));
                        gm_toho9hmgj287v t34;
                        GM_ST(uint64_t, ((uint8_t *)&t34 + 0), t29);
                        GM_ST(gm_tvul7d1mdc2eq, ((uint8_t *)&t34 + 8), t33);
                        gm_l10 = t34;
                        {
                            {
                                uint64_t t35 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                                gm_l6 = t35;
                                memmove(((uint8_t *)&gm_l7), (((uint8_t *)&gm_l10) + 8), 32);
                                uint64_t t36 = gm_l6;
                                uint8_t t37 = (t36 > ((uint64_t)0ULL));
                                if (t37) {
                                    void * t38 = gm_l1;
                                    gm_tftb1odc739t5 t39 = gm_f_push_t83rg2m(t38, ((uint8_t)32ULL));
                                    gm_l11 = t39;
                                    uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                    if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srck6bk8q21r07v, 87, 28);
                                }
                                void * t41 = (void *)((uint8_t *)&gm_l7);
                                void * t42 = gm_l1;
                                gm_b0_1 t43 = gm_f_flat_hstd39j((void *)NULL, cx, NULL, t41, t42);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb7;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 86, 13);
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
            void * t48 = gm_l1;
            gm_tftb1odc739t5 t49 = gm_f_push_t83rg2m(t48, ((uint8_t)41ULL));
            gm_l12 = t49;
            uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srck6bk8q21r07v, 90, 13);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 82, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_float_suffix_7g3bh9c(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_float_suffix_7g3bh9c F;
    gm_t9qkh0rsooc8d gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 119u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_shnig5tqtdedi));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(3));
            gm_l1 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)1ULL));
            if (!t10) {
                goto gm_lb3;
            }
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sm9e81djuncnu));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(3));
            gm_l1 = t11;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)2ULL));
            if (!t13) {
                goto gm_lb4;
            }
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sqqajgod2bb4o));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(3));
            gm_l1 = t14;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 151, 56);
    }
gm_lb1:;
    gm_b16_8 t15 = gm_l1;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_ident_sx_kd0v352(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_tvul7d1mdc2eq gm_f_ident_sx_kd0v352_c(gm_t08olq97rscn3 a0) { gm_tvul7d1mdc2eq gm_r; gm_f_ident_sx_kd0v352(&gm_r, &a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_make_list_cicccjr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint8_t a2) {
    struct gm_fr_make_list_cicccjr F;
    gm_b16_8 gm_l0; /* head */
    uint8_t gm_l2; /* brk */
    GM_STACK_CHECK(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_trqfvmjtt4sij t2;
    gm_f_list_ccsq5cl((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t2;
    void * t3 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t4 = gm_l0;
    gm_tvul7d1mdc2eq t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_tftb1odc739t5 t6 = gm_f_push_mjiecfq(t3, &t5);
    gm_l4 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srck6bk8q21r07v, 42, 5);
    void * t8 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)a1));
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    gm_tftb1odc739t5 t11 = gm_f_extend_i72a9oi(t8, t10);
    gm_l5 = t11;
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srck6bk8q21r07v, 43, 5);
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    uint8_t t14 = gm_l2;
    gm_th2lqbo77pvsc t15;
    GM_ST(uint8_t, (uint8_t *)&t15 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t15 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t15 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t13);
    GM_ST(uint8_t, ((uint8_t *)&t15 + 16), t14);
    gm_tvul7d1mdc2eq t16;
    GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)1ULL));
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t16 + 8), t15);
    memmove(gm_out, ((uint8_t *)&t16), 32);
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
extern __attribute__((aligned(4))) void gm_f_node1_gpsjtpc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_node2_b4qpqat(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, const void * a2) {
    struct gm_fr_node2_b4qpqat F;
    gm_b16_8 gm_l0; /* head */
    gm_tvul7d1mdc2eq gm_l1; /* x */
    gm_tvul7d1mdc2eq gm_l2; /* y */
    GM_STACK_CHECK(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_tftb1odc739t5 gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_trqfvmjtt4sij t2;
    gm_f_list_ccsq5cl((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t2;
    void * t3 = (void *)((uint8_t *)&F.l3);
    gm_tftb1odc739t5 t4 = gm_f_push_mjiecfq(t3, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l1)));
    gm_l4 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srck6bk8q21r07v, 57, 5);
    void * t6 = (void *)((uint8_t *)&F.l3);
    gm_tftb1odc739t5 t7 = gm_f_push_mjiecfq(t6, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l2)));
    gm_l5 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srck6bk8q21r07v, 58, 5);
    gm_b16_8 t9 = gm_l0;
    gm_trqfvmjtt4sij t10 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l3));
    gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t9, &t10);
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
extern __attribute__((aligned(4))) void gm_f_node_dm1r8dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_attrs_a5l439c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_push_attrs_a5l439c F;
    void * gm_l0; /* v */
    gm_b16_8 gm_l1; /* attrs */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 174u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l5;
    gm_b16_8 t7 = gm_l1;
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
                void * t11 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t13 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t11) + (uintptr_t)t13 * 72), 72);
                        void * t14 = gm_l0;
                        void * t15 = (void *)((uint8_t *)&F.l4);
                        gm_tvul7d1mdc2eq t16;
                        gm_f_dump_attr_qdd7k06((void *)&t16, (void *)NULL, cx, NULL, t15);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t17 = gm_f_push_mjiecfq(t14, &t16);
                        gm_l5 = t17;
                        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srck6bk8q21r07v, 190, 22);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 190, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t19 = gm_l3;
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            uint64_t t22 = r20;
            gm_l3 = t22;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_render_7k3fqtk(void *env, gm_node *cx, gm_node *inst, void * a0, int64_t a1, void * a2) {
    struct gm_fr_render_7k3fqtk F;
    void * gm_l0; /* sx */
    int64_t gm_l1; /* indent */
    void * gm_l2; /* out */
    int64_t gm_l3; /* budget */
    gm_b16_8 gm_l4; /* items */
    uint8_t gm_l5; /* brk */
    int64_t gm_l6; /* col */
    uint8_t gm_l7; /* inline */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_tvul7d1mdc2eq gm_l11; /* item */
    int64_t gm_l12; /* room */
    int64_t gm_l14; /* <tmp> */
    int64_t gm_l15; /* <tmp> */
    GM_STACK_CHECK(&F, 870u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l16;
    gm_toho9hmgj287v gm_l17;
    uint8_t gm_l18;
    uint8_t gm_l19;
    uint8_t gm_l20;
    uint8_t gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    int64_t t17 = GM_LD(int64_t, ((uint8_t *)gm_cddg9214lam));
    int64_t t18 = gm_l1;
    int64_t r19; bool o20 = __builtin_sub_overflow(t17, t18, &r19);
    if (o20) gm_trap("integer overflow in `-`", gm_srck6bk8q21r07v, 96, 15);
    int64_t t21 = r19;
    gm_l3 = t21;
    void * t22 = gm_l0;
    void * t23 = (void *)((uint8_t *)&gm_l3);
    uint8_t t24 = gm_f_fits_mco2o3v(t22, t23);
    if (t24) {
        void * t25 = gm_l0;
        void * t26 = gm_l2;
        gm_b0_1 t27 = gm_f_flat_hstd39j((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            void * t28 = gm_l0;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)t28) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)1ULL));
            if (!t30) {
                goto gm_lb2;
            }
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 8));
            gm_l4 = t31;
            uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 16));
            gm_l5 = t32;
            void * t33 = gm_l2;
            gm_tftb1odc739t5 t34 = gm_f_push_t83rg2m(t33, ((uint8_t)40ULL));
            gm_l16 = t34;
            uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
            if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srck6bk8q21r07v, 104, 5);
            int64_t t36 = gm_l1;
            int64_t r37; bool o38 = __builtin_add_overflow(t36, ((int64_t)1LL), &r37);
            if (o38) gm_trap("integer overflow in `+`", gm_srck6bk8q21r07v, 105, 12);
            int64_t t39 = r37;
            gm_l6 = t39;
            gm_l7 = ((uint8_t)1);
            gm_b16_8 t40 = gm_l4;
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
            gm_l8 = t41;
            gm_l9 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t42 = gm_l9;
                    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint8_t t44 = (t42 < t43);
                    if (!t44) {
                        goto gm_lb3;
                    }
                    {
                        uint64_t t45 = gm_l9;
                        void * t46 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint64_t t48 = gm_l9;
                        gm_tvul7d1mdc2eq t49 = GM_LD(gm_tvul7d1mdc2eq, (((uint8_t *)t46) + (uintptr_t)t48 * 32));
                        gm_toho9hmgj287v t50;
                        GM_ST(uint64_t, ((uint8_t *)&t50 + 0), t45);
                        GM_ST(gm_tvul7d1mdc2eq, ((uint8_t *)&t50 + 8), t49);
                        gm_l17 = t50;
                        {
                            {
                                uint64_t t51 = GM_LD(uint64_t, ((uint8_t *)&gm_l17));
                                gm_l10 = t51;
                                memmove(((uint8_t *)&gm_l11), (((uint8_t *)&gm_l17) + 8), 32);
                                uint8_t t52 = gm_l7;
                                if (t52) {
                                    int64_t t53 = GM_LD(int64_t, ((uint8_t *)gm_cddg9214lam));
                                    int64_t t54 = gm_l6;
                                    int64_t r55; bool o56 = __builtin_sub_overflow(t53, t54, &r55);
                                    if (o56) gm_trap("integer overflow in `-`", gm_srck6bk8q21r07v, 109, 21);
                                    int64_t t57 = r55;
                                    int64_t r58; bool o59 = __builtin_sub_overflow(t57, ((int64_t)1LL), &r58);
                                    if (o59) gm_trap("integer overflow in `-`", gm_srck6bk8q21r07v, 109, 21);
                                    int64_t t60 = r58;
                                    gm_l12 = t60;
                                    {
                                        {
                                            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                                            uint8_t t62 = (uint8_t)(t61 == ((uint8_t)0ULL));
                                            if (!t62) {
                                                goto gm_lb9;
                                            }
                                            gm_l19 = ((uint8_t)1);
                                            goto gm_lb8;
                                        }
gm_lb9:;
                                        {
                                            gm_l19 = ((uint8_t)0);
                                            goto gm_lb8;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 110, 16);
                                    }
gm_lb8:;
                                    uint8_t t63 = gm_l19;
                                    if (t63) {
                                        gm_l18 = ((uint8_t)1);
                                    } else {
                                        uint8_t t64 = gm_l5;
                                        uint8_t t65 = (uint8_t)!t64;
                                        if (t65) {
                                            uint64_t t66 = gm_l10;
                                            uint8_t t67 = (t66 > ((uint64_t)0ULL));
                                            gm_l21 = t67;
                                        } else {
                                            gm_l21 = ((uint8_t)0);
                                        }
                                        uint8_t t68 = gm_l21;
                                        if (t68) {
                                            void * t69 = (void *)((uint8_t *)&gm_l11);
                                            void * t70 = (void *)((uint8_t *)&gm_l12);
                                            uint8_t t71 = gm_f_fits_mco2o3v(t69, t70);
                                            gm_l20 = t71;
                                        } else {
                                            gm_l20 = ((uint8_t)0);
                                        }
                                        uint8_t t72 = gm_l20;
                                        gm_l18 = t72;
                                    }
                                    uint8_t t73 = gm_l18;
                                    if (t73) {
                                        gm_trqfvmjtt4sij t74;
                                        gm_f_list_uvfu1k1((void *)&t74, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l13 = t74;
                                        void * t75 = (void *)((uint8_t *)&gm_l11);
                                        void * t76 = (void *)((uint8_t *)&F.l13);
                                        gm_b0_1 t77 = gm_f_flat_hstd39j((void *)NULL, cx, NULL, t75, t76);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint64_t t78 = gm_l10;
                                        uint8_t t79 = (t78 > ((uint64_t)0ULL));
                                        if (t79) {
                                            void * t80 = gm_l2;
                                            gm_tftb1odc739t5 t81 = gm_f_push_t83rg2m(t80, ((uint8_t)32ULL));
                                            gm_l22 = t81;
                                            uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                                            if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srck6bk8q21r07v, 114, 21);
                                            int64_t t83 = gm_l6;
                                            int64_t r84; bool o85 = __builtin_add_overflow(t83, ((int64_t)1LL), &r84);
                                            if (o85) gm_trap("integer overflow in `+`", gm_srck6bk8q21r07v, 115, 21);
                                            int64_t t86 = r84;
                                            gm_l6 = t86;
                                        }
                                        int64_t t87 = gm_l6;
                                        gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                                        gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
                                        uint64_t t90 = gm_f_char_count_76unqru(t89);
                                        int64_t t91 = (int64_t)t90;
                                        int64_t r92; bool o93 = __builtin_add_overflow(t87, t91, &r92);
                                        if (o93) gm_trap("integer overflow in `+`", gm_srck6bk8q21r07v, 117, 17);
                                        int64_t t94 = r92;
                                        gm_l6 = t94;
                                        void * t95 = gm_l2;
                                        gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                                        gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
                                        gm_tftb1odc739t5 t98 = gm_f_extend_0cs85gs(t95, t97);
                                        gm_l23 = t98;
                                        uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                        if (!t99) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srck6bk8q21r07v, 118, 17);
                                        goto gm_lb5;
                                    }
                                    gm_l7 = ((uint8_t)0);
                                }
                                void * t100 = gm_l2;
                                gm_tftb1odc739t5 t101 = gm_f_push_t83rg2m(t100, ((uint8_t)10ULL));
                                gm_l24 = t101;
                                uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                if (!t102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srck6bk8q21r07v, 123, 9);
                                gm_l15 = ((int64_t)0LL);
                                int64_t t103 = gm_l1;
                                int64_t r104; bool o105 = __builtin_add_overflow(t103, ((int64_t)2LL), &r104);
                                if (o105) gm_trap("integer overflow in `+`", gm_srck6bk8q21r07v, 124, 21);
                                int64_t t106 = r104;
                                gm_l14 = t106;
                                {
                                    for (;;) {
                                        int64_t t107 = gm_l15;
                                        int64_t t108 = gm_l14;
                                        uint8_t t109 = (t107 < t108);
                                        if (!t109) {
                                            goto gm_lb11;
                                        }
                                        {
                                            {
                                                {
                                                    void * t110 = gm_l2;
                                                    gm_tftb1odc739t5 t111 = gm_f_push_t83rg2m(t110, ((uint8_t)32ULL));
                                                    gm_l25 = t111;
                                                    uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                                    if (!t112) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srck6bk8q21r07v, 124, 34);
                                                    goto gm_lb14;
                                                }
gm_lb15:;
                                                gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 124, 9);
                                            }
gm_lb14:;
                                        }
gm_lb13:;
                                        int64_t t113 = gm_l15;
                                        int64_t r114; bool o115 = __builtin_add_overflow(t113, ((int64_t)1LL), &r114);
                                        int64_t t116 = r114;
                                        gm_l15 = t116;
                                    }
                                }
gm_lb11:;
                                void * t117 = (void *)((uint8_t *)&gm_l11);
                                int64_t t118 = gm_l1;
                                int64_t r119; bool o120 = __builtin_add_overflow(t118, ((int64_t)2LL), &r119);
                                if (o120) gm_trap("integer overflow in `+`", gm_srck6bk8q21r07v, 125, 23);
                                int64_t t121 = r119;
                                void * t122 = gm_l2;
                                gm_b0_1 t123 = gm_f_render_7k3fqtk((void *)NULL, cx, NULL, t117, t121, t122);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 107, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t124 = gm_l9;
                    uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                    uint64_t t127 = r125;
                    gm_l9 = t127;
                }
            }
gm_lb3:;
            void * t128 = gm_l2;
            gm_tftb1odc739t5 t129 = gm_f_push_t83rg2m(t128, ((uint8_t)41ULL));
            gm_l26 = t129;
            uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
            if (!t130) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srck6bk8q21r07v, 127, 5);
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
inline __attribute__((aligned(4))) void gm_f_str_lit_a2k13t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_str_lit_a2k13t3 F;
    gm_tibupt4dmcqai gm_l0; /* s */
    gm_b16_8 gm_l1; /* head */
    GM_STACK_CHECK(&F, 206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_b16_8 gm_l2;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 16) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s6pilvbjelm5p));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(3));
            gm_l2 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 16) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)1ULL));
            if (!t10) {
                goto gm_lb3;
            }
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sltau19rt9o73));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(7));
            gm_l2 = t11;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 16) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)2ULL));
            if (!t13) {
                goto gm_lb4;
            }
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_squ5nrnqo0b0l));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(5));
            gm_l2 = t14;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 143, 13);
    }
gm_lb1:;
    gm_b16_8 t15 = gm_l2;
    gm_l1 = t15;
    gm_b16_8 t16 = gm_l1;
    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
    gm_tvul7d1mdc2eq t19;
    gm_f_atom_of_bytes_dj32elq((void *)&t19, (void *)NULL, cx, NULL, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t16, &t19);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unop_name_j99tcv9(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unop_name_j99tcv9 F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)0ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_seb76g152eseg));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(3));
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
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sgs4qi36v3o5v));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(7));
            gm_l1 = t14;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)2ULL));
            if (!t16) {
                goto gm_lb4;
            }
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sukutl224cgkm));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(7));
            gm_l1 = t17;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)3ULL));
            if (!t19) {
                goto gm_lb5;
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sn581p3lbgrjg));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(8));
            gm_l1 = t20;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)4ULL));
            if (!t22) {
                goto gm_lb6;
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s4fd35uidvgkn));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(3));
            gm_l1 = t23;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)5ULL));
            if (!t25) {
                goto gm_lb7;
            }
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_scvhpklutvpjg));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(3));
            gm_l1 = t26;
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 164, 40);
    }
gm_lb1:;
    gm_b16_8 t27 = gm_l1;
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_visibility_u2ipb12(void *gm_out, gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visibility_u2ipb12 F;
    gm_t9qkh0rsooc8d gm_l0; /* vis */
    GM_STACK_CHECK_SHALLOW(&F, 135u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l1;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_th2lqbo77pvsc t8;
            memset(&t8, 0, sizeof t8);
            gm_l1 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)1ULL));
            if (!t10) {
                goto gm_lb3;
            }
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_shofa8pdml9fb));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(12));
            gm_th2lqbo77pvsc t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t12), t11);
            ((uint8_t *)&t12)[16] = 1;
            gm_l1 = t12;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)2ULL));
            if (!t14) {
                goto gm_lb4;
            }
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sjqs7q3ostqbm));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(3));
            gm_th2lqbo77pvsc t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[16] = 1;
            gm_l1 = t16;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 196, 49);
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&gm_l1), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_visibility_u2ipb12_c(gm_t9qkh0rsooc8d a0) { gm_th2lqbo77pvsc gm_r; gm_f_visibility_u2ipb12(&gm_r, a0); return gm_r; }
