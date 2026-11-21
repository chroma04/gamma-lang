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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
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
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; void * f3; } gm_tl93vjpcesj48;
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl93vjpcesj48) == 48 && _Alignof(gm_tl93vjpcesj48) == 8 && offsetof(gm_tl93vjpcesj48, f0) == 0 && offsetof(gm_tl93vjpcesj48, f1) == 8 && offsetof(gm_tl93vjpcesj48, f2) == 24 && offsetof(gm_tl93vjpcesj48, f3) == 40, "layout of `NestedFn`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t08olq97rscn3 f1; } gm_tp9ftj4p31ha1;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tj44oivvogvtl) == 40 && _Alignof(gm_tj44oivvogvtl) == 8 && offsetof(gm_tj44oivvogvtl, f0) == 0 && offsetof(gm_tj44oivvogvtl, f1) == 24, "layout of `Performed`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tp9ftj4p31ha1) == 40 && _Alignof(gm_tp9ftj4p31ha1) == 8 && offsetof(gm_tp9ftj4p31ha1, f0) == 0 && offsetof(gm_tp9ftj4p31ha1, f1) == 8, "layout of `(fn_id: u32, name: Ident)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
typedef struct { gm_tddtjfggdat0d f0; uint8_t f1; uint8_t pe[7]; } gm_tt775pork2bs8;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_tt775pork2bs8) == 56 && _Alignof(gm_tt775pork2bs8) == 8 && offsetof(gm_tt775pork2bs8, f0) == 0 && offsetof(gm_tt775pork2bs8, f1) == 48, "layout of `?FnTy`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
struct gm_fr_block_q09sn1p {
    gm_frame h;
    void * ret;
};
struct gm_fr_check_no_self_reference_4ra5lcu {
    gm_frame h;
    uint32_t l1; /* fn_id */
    uint8_t l5; /* refers */
    gm_tl93vjpcesj48 l8; /* n */
    gm_b0_1 ret;
};
struct gm_fr_effect_has_param_args_oketd14 {
    gm_frame h;
    gm_toa2vhe3l4hfr l4; /* a */
    uint8_t ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dq2isu2 {
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
struct gm_fr_empty_slice_jvoelso {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qmcc4fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vhibpt8 {
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
struct gm_fr_extend_74ac4hb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_3e7faf9 {
    gm_frame h;
    gm_tsth9qu1dv0g3 l2; /* callee */
    gm_b0_1 ret;
};
struct gm_fr_fn_literal_fiu78lb {
    gm_frame h;
    gm_t1s75i08qtptd l3; /* name */
    gm_tt775pork2bs8 l10; /* expected */
    gm_tddtjfggdat0d l13; /* f */
    gm_tddtjfggdat0d l14; /* f */
    gm_tf2orqcrphmek l17; /* p */
    gm_tddtjfggdat0d l21; /* f */
    gm_trqfvmjtt4sij l23; /* open_effects */
    gm_tddtjfggdat0d l25; /* f */
    gm_trqfvmjtt4sij l26; /* kept */
    gm_trqfvmjtt4sij l40; /* param_ids */
    gm_t5v0c8ia6a5mo l43; /* p */
    gm_tk9n3vlila9qg l47; /* frame */
    gm_tj44oivvogvtl l52; /* p */
    gm_trqfvmjtt4sij l60; /* all */
    gm_trqfvmjtt4sij l61; /* ptys */
    gm_t5v0c8ia6a5mo l64; /* p */
    gm_tddtjfggdat0d l66; /* f */
    gm_tddtjfggdat0d l67; /* fty */
    void * ret;
};
struct gm_fr_fn_meq413i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_26002qo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_57u5svl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8c2oop8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9lchiud {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c8qq9n9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e79cvtl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h4jcodi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hgrco1l {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vrv37j5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_infer_open_effect_47fpti4 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* found */
    gm_tj44oivvogvtl l9; /* p */
    gm_trqfvmjtt4sij l14; /* names */
    gm_trqfvmjtt4sij l18; /* all */
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_generic_jrl2kf9 {
    gm_frame h;
    gm_tf2orqcrphmek l3; /* p */
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
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
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
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_literal_body_5lm01a5 {
    gm_frame h;
    gm_t79rbdv4rafe5 l6; /* b */
    void * ret;
};
struct gm_fr_literal_params_n5511im {
    gm_frame h;
    gm_tt775pork2bs8 l2; /* expected */
    gm_trqfvmjtt4sij l3; /* params */
    gm_tf2orqcrphmek l6; /* p */
    gm_tddtjfggdat0d l15; /* f */
    gm_tddtjfggdat0d l16; /* f */
    gm_b16_8 ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_t2b6tm8c0vgo1 l1; /* kind */
    void * ret;
};
struct gm_fr_nested_fn_binding_0cl95ol {
    gm_frame h;
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_nested_generic_fn_q4qqrgj {
    gm_frame h;
    gm_trcmmd12cefb1 ret;
};
struct gm_fr_new_literal_fn_oqjp4lg {
    gm_frame h;
    uint32_t ret;
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
struct gm_fr_push_3onuaru {
    gm_frame h;
    gm_tj44oivvogvtl l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bu2hcel {
    gm_frame h;
    gm_tp9ftj4p31ha1 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d1r4h19 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_jd7smj6 {
    gm_frame h;
    gm_tl93vjpcesj48 l1; /* x */
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
struct gm_fr_push_p9c61k8 {
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
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_7sj41hq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_9ibjpen {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_kqobl40 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_m244brh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_s9eh08e {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_td8s0op {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_reject_returned_closure_1ouocor {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_route_performed_pruu3np {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* own */
    gm_tj44oivvogvtl l8; /* p */
    gm_b16_8 ret;
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
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_ohk62mc {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_r3r7qdh {
    gm_frame h;
    void * ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_visit_e3tpkkh {
    gm_frame h;
    gm_closure l1; /* f */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_no_self_reference_4ra5lcu(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_declare_fn_6piolnr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) void * gm_f_declare_temps_a7fc14v(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_effect_has_param_args_oketd14(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dq2isu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jvoelso(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vhibpt8(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_enter_fn_vo5kel1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_exit_fn_kj6vbu8(void *gm_out, void * a0);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_74ac4hb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_finish_block_body_4vol3tv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_3e7faf9(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_fn_literal_fiu78lb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, const void * a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meq413i(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_format_26002qo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_2bh0ost(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_57u5svl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8c2oop8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9lchiud(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
inline __attribute__((aligned(4))) void gm_f_format_c8qq9n9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_hgrco1l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_vrv37j5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_infer_open_effect_47fpti4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, void * a3, uint32_t a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_generic_jrl2kf9(void * a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_literal_body_5lm01a5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint8_t a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_literal_params_n5511im(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_nested_fn_binding_0cl95ol(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_nested_generic_fn_q4qqrgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) uint32_t gm_f_new_literal_fn_oqjp4lg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, uint64_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_tnpifv20irnph a6);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3onuaru(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bu2hcel(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jd7smj6(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p9c61k8(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_recursive_without_ret_48l31rm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9ibjpen(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kqobl40(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m244brh(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s9eh08e(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_td8s0op(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_reject_returned_closure_1ouocor(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_list_uhjlh6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_route_performed_pruu3np(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3, uint8_t a4);
__attribute__((aligned(4))) uint8_t gm_f_set_contains_8v9ep9m(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_undeclared_message_b14iouu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2, void * a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void * gm_f_unsupported_r3r7qdh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_visit_e3tpkkh(void * a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_within_sys_otp5qp6(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s4b8airp3rh69[50];
extern const uint8_t gm_s8mre3qset1hp[60];
extern const uint8_t gm_s93p2i934ld6p[5];
extern const uint8_t gm_sdsd71bjtv81c[98];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sgqep6dpse2vs[64];
extern const uint8_t gm_sgv98i2v4c6ls[93];
extern const uint8_t gm_sig8ahbvm8eb8[10];
extern const uint8_t gm_sjb713eltfa88[112];
extern const uint8_t gm_skfniirr0r2gc[63];
extern const uint8_t gm_skmfilbqoc8lo[87];
extern const uint8_t gm_smv96er1uehm8[94];
extern const uint8_t gm_sn1j4m1p1cnm1[83];
extern const uint8_t gm_southrlg4k72m[6];
extern const uint8_t gm_sqa5rf17c57e4[5];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcl43af23eher3[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srdq954rpedg3[93];
extern const uint8_t gm_ss7dlauv50bt8[81];
extern const uint8_t gm_ssa5e93p23oqi[90];
extern const gm_tmpl gm_tpl2arnv1ofugei;
extern const gm_tmpl gm_tpl6lpvgmmqlalm;
extern const gm_tmpl gm_tpll8q455cp9mhi;
extern const gm_tmpl gm_tplm2m182mt5ljj;
extern const gm_tmpl gm_tplp93m8c6u3nrp;
extern const gm_tmpl gm_tpls5kpq7mskrde;
extern const gm_tmpl gm_tplsbqgr9k4itq0;
extern const gm_tmpl gm_tplsgm949s33v8m;
extern const gm_tmpl gm_tplvk1pmcp9tr4p;
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_block_q09sn1p F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* value */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    uint8_t t4 = gm_l2;
    gm_tkn42u7na3t81 t5;
    memset(&t5, 0, sizeof t5);
    void * t6 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t2, t3, t4, t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dq2isu2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dq2isu2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jvoelso(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jvoelso F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmcc4fe F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vhibpt8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vhibpt8 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_meq413i(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_meq413i F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_closure t3 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_visit_e3tpkkh *)((&F.h)->up))->l1));
    gm_f_visit_e3tpkkh(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_format_26002qo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_26002qo F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smv96er1uehm8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(93));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpls5kpq7mskrde), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_57u5svl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_57u5svl F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssa5e93p23oqi));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(89));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6lpvgmmqlalm), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8c2oop8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8c2oop8 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjb713eltfa88));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(111));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl2arnv1ofugei), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9lchiud(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_9lchiud F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t2lqu8jsj511d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8mre3qset1hp));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(59));
    gm_f_format_2bh0ost((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplp93m8c6u3nrp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h4jcodi F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqa5rf17c57e4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsbqgr9k4itq0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_hgrco1l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hgrco1l F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skmfilbqoc8lo));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(86));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsgm949s33v8m), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vrv37j5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vrv37j5 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgqep6dpse2vs));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(63));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvk1pmcp9tr4p), t3);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3onuaru(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3onuaru F;
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
        gm_b16_8 t9 = gm_f_empty_slice_vhibpt8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_s9eh08e(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bu2hcel(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bu2hcel F;
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
        gm_b16_8 t9 = gm_f_empty_slice_dq2isu2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_9ibjpen(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jd7smj6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_jd7smj6 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_jvoelso();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_kqobl40(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p9c61k8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_p9c61k8 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_qmcc4fe();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_td8s0op(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9ibjpen(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_9ibjpen F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kqobl40(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_kqobl40 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s9eh08e(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_s9eh08e F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_td8s0op(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_td8s0op F;
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
inline __attribute__((aligned(4))) void * gm_f_unit_ohk62mc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_unit_ohk62mc F;
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
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) void gm_f_visit_e3tpkkh(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visit_e3tpkkh F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_closure t2 = F.l1;
    void * t3 = gm_l0;
    gm_closure t4 = t2;
    gm_node *ccx5 = t4.cx;
    void * t6 = t4.code;
    gm_b0_1 t7;
    if (GM_PTR_TAGGED(t6)) {
        ((void (*)(void *))GM_PTR_UNTAG(t6))(t3);
    } else {
        t7 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t6)(t4.env, ccx5, NULL, t3);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t8 = gm_l0;
    gm_closure t9 = ((gm_closure){(void *)gm_f_fn_meq413i, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t8, t9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_no_self_reference_4ra5lcu(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_check_no_self_reference_4ra5lcu F;
    void * gm_l0; /* cx */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* name */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 355u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l9;
    F.l5 = ((uint8_t)0);
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 704));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 704) + 8));
    uint64_t t10 = gm_l2;
    void * t11 = gm_l0;
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t11) + 704) + 8));
    uint64_t t13 = (uintptr_t)t10;
    uint64_t t14 = (uintptr_t)t12;
    if (t13 > t14 || t14 > (uintptr_t)t9) gm_trap_slice(t13, t14, t9, gm_srcl43af23eher3, 287, 14);
    uint64_t t15 = ((uintptr_t)t12) - (uintptr_t)t10;
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t8 + (uintptr_t)t10 * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(t15));
    gm_l6 = t16;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t18 = gm_l7;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t20 = (t18 < t19);
            if (!t20) {
                goto gm_lb1;
            }
            {
                void * t21 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t23 = gm_l7;
                {
                    {
                        memmove(((uint8_t *)&F.l8), (((uint8_t *)t21) + (uintptr_t)t23 * 48), 48);
                        void * t24 = GM_LD(void *, (((uint8_t *)&F.l8) + 40));
                        gm_closure t25 = ((gm_closure){(void *)gm_f_fn_3e7faf9, (void *)&F.h, cx});
                        gm_f_visit_e3tpkkh(t24, t25);
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 287, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t26 = gm_l7;
            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
            uint64_t t29 = r27;
            gm_l7 = t29;
        }
    }
    goto gm_ls30;
gm_ll17:;
    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb1; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb3; }
    }
    goto gm_lad;
gm_ls30:;
gm_lb1:;
    uint8_t t31 = F.l5;
    if (t31) {
        void * t32 = gm_l0;
        gm_tl874t2atnlh3 t33 = gm_l4;
        gm_b16_8 t34 = gm_l3;
        gm_tnpifv20irnph t35;
        GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t34);
        gm_f_format_hgrco1l((void *)&gm_l9, (void *)NULL, cx, NULL, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcl43af23eher3, 296, 32);
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        gm_b0_1 t39 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t32, t33, t38);
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
inline __attribute__((aligned(4))) uint8_t gm_f_effect_has_param_args_oketd14(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_has_param_args_oketd14 F;
    void * gm_l0; /* tys */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t88rcucg1b7uk gm_l5; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t9 = gm_l1;
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)t9) + 8));
    gm_l2 = t10;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t11 = gm_l3;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                void * t14 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t16 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t14) + (uintptr_t)t16 * 40), 40);
                        {
                            {
                                uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                                uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
                                if (!t18) {
                                    goto gm_lb7;
                                }
                                gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
                                gm_l5 = t19;
                                void * t20 = gm_l0;
                                gm_t88rcucg1b7uk t21 = gm_l5;
                                uint8_t t22 = gm_f_has_param_u9t0efs(t20, GM_CONV(gm_csguankko4lmhd, t21));
                                if (t22) {
                                    F.ret = ((uint8_t)1);
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
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 243, 5);
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
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3e7faf9(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_3e7faf9 F;
    void * gm_l0; /* e */
    uint32_t gm_l1; /* f */
    uint32_t gm_l3; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t8 = gm_l0;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)26ULL));
            if (!t10) {
                goto gm_lb2;
            }
            uint32_t t11 = GM_LD(uint32_t, ((((uint8_t *)t8) + 8) + 8));
            gm_l1 = t11;
            uint32_t t12 = gm_l1;
            uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_check_no_self_reference_4ra5lcu *)((&F.h)->up))->l1));
            uint8_t t14 = ((t12 == t13));
            if (t14) {
                GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_check_no_self_reference_4ra5lcu *)((&F.h)->up))->l5), ((uint8_t)1));
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)34ULL));
            if (!t16) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&F.l2), ((((uint8_t *)t8) + 8) + 8), 40);
            {
                {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
                    if (!t18) {
                        goto gm_lb5;
                    }
                    uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 8));
                    gm_l3 = t19;
                    uint32_t t20 = gm_l3;
                    uint32_t t21 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_check_no_self_reference_4ra5lcu *)((&F.h)->up))->l1));
                    uint8_t t22 = ((t20 == t21));
                    if (!t22) {
                        goto gm_lb5;
                    }
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_check_no_self_reference_4ra5lcu *)((&F.h)->up))->l5), ((uint8_t)1));
                    goto gm_lb4;
                }
gm_lb5:;
            }
gm_lb4:;
            goto gm_lb1;
        }
gm_lb3:;
        {
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 289, 13);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_fn_literal_fiu78lb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2, const void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_fn_literal_fiu78lb F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lit */
    gm_tkn42u7na3t81 gm_l2; /* expect */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_tkn42u7na3t81 gm_l6; /* target */
    gm_t88rcucg1b7uk gm_l7; /* t0 */
    gm_t88rcucg1b7uk gm_l8; /* t */
    gm_t88rcucg1b7uk gm_l9; /* inner */
    uint8_t gm_l11; /* wants_pointer */
    gm_t88rcucg1b7uk gm_l12; /* t */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l18; /* params */
    gm_tkn42u7na3t81 gm_l19; /* ret_known */
    void * gm_l20; /* r */
    gm_tnpifv20irnph gm_l22; /* effects */
    gm_b16_8 gm_l24; /* list_ */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_tiilmfd16v3fc gm_l29; /* e */
    uint8_t gm_l30; /* iterator_arg */
    uint8_t gm_l31; /* infer_ret */
    uint8_t gm_l32; /* infer_block */
    gm_t88rcucg1b7uk gm_l33; /* ret */
    gm_t08olq97rscn3 gm_l34; /* n */
    gm_b16_8 gm_l35; /* fn_name */
    uint32_t gm_l36; /* fn_id */
    gm_t08olq97rscn3 gm_l37; /* n */
    uint8_t gm_l38; /* open */
    uint64_t gm_l39; /* nested_start */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    uint32_t gm_l44; /* id */
    void * gm_l45; /* body */
    gm_t88rcucg1b7uk gm_l46; /* final_ret */
    uint8_t gm_l48; /* yields */
    uint32_t gm_l49; /* y */
    gm_b16_8 gm_l50; /* <tmp> */
    uint64_t gm_l51; /* <tmp> */
    gm_t88rcucg1b7uk gm_l53; /* ty */
    gm_b16_8 gm_l54; /* <tmp> */
    uint64_t gm_l55; /* <tmp> */
    uint32_t gm_l56; /* id */
    uint8_t gm_l57; /* undeclared_is_error */
    gm_b16_8 gm_l58; /* own */
    uint8_t gm_l59; /* carries_context */
    gm_b16_8 gm_l62; /* <tmp> */
    uint64_t gm_l63; /* <tmp> */
    uint8_t gm_l65; /* win64 */
    gm_t08olq97rscn3 gm_l68; /* n */
    uint8_t gm_l69; /* expects_fn */
    gm_t88rcucg1b7uk gm_l70; /* t */
    gm_t88rcucg1b7uk gm_l71; /* ty */
    GM_STACK_CHECK(&F, 5137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&F.l3, a3, sizeof F.l3);
    gm_l4 = a4;
    uint8_t gm_l72;
    gm_t53at80pfdus3 gm_l73;
    uint8_t gm_l74;
    gm_t53at80pfdus3 gm_l75;
    gm_t53at80pfdus3 gm_l76;
    uint8_t gm_l77;
    uint8_t gm_l78;
    gm_tftb1odc739t5 gm_l79;
    gm_tftb1odc739t5 gm_l80;
    gm_tnpifv20irnph gm_l81;
    uint8_t gm_l82;
    uint8_t gm_l83;
    uint8_t gm_l84;
    uint8_t gm_l85;
    uint8_t gm_l86;
    uint8_t gm_l87;
    uint8_t gm_l88;
    gm_t88rcucg1b7uk gm_l89;
    gm_b16_8 gm_l90;
    uint8_t gm_l91;
    uint8_t gm_l92;
    uint8_t gm_l93;
    gm_tftb1odc739t5 gm_l94;
    gm_tftb1odc739t5 gm_l95;
    gm_tabb710d9mq2e gm_l96;
    uint8_t gm_l97;
    uint8_t gm_l98;
    uint8_t gm_l99;
    uint8_t gm_l100;
    uint8_t gm_l101;
    gm_tftb1odc739t5 gm_l102;
    gm_tftb1odc739t5 gm_l103;
    gm_tftb1odc739t5 gm_l104;
    gm_tftb1odc739t5 gm_l105;
    uint8_t gm_l106;
    gm_t53at80pfdus3 gm_l107;
    gm_t88rcucg1b7uk gm_l108;
    uint8_t gm_l109;
    {
        void * t97 = gm_l0;
        void * t98 = GM_LD(void *, ((uint8_t *)t97));
        void * t99 = (void *)(((uint8_t *)t98) + 8);
        gm_l5 = t99;
        {
            {
                {
                    {
                        uint8_t t100 = (uint8_t)((((uint8_t *)&F.l3))[32] != 0);
                        if (t100) {
                            goto gm_lb4;
                        }
                        gm_l72 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_l72 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 8, 8);
                }
gm_lb3:;
                uint8_t t101 = gm_l72;
                if (!t101) {
                    goto gm_lb2;
                }
                void * t102 = gm_l1;
                uint8_t t103 = gm_f_is_generic_jrl2kf9(t102);
                if (!t103) {
                    goto gm_lb2;
                }
                void * t104 = gm_l0;
                void * t105 = gm_l1;
                gm_tl874t2atnlh3 t106 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t105) + 136));
                gm_b16_8 t107;
                GM_ST(void *, (uint8_t *)&t107, (void *)(gm_sgv98i2v4c6ls));
                GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(92));
                gm_b0_1 t108 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t104, t106, t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t109 = gm_l0;
                void * t110 = GM_LD(void *, ((uint8_t *)t109));
                gm_tl874t2atnlh3 t111 = gm_l4;
                void * t112 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t110, t111);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t112;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
        gm_tkn42u7na3t81 t113;
        memset(&t113, 0, sizeof t113);
        gm_l6 = t113;
        {
            {
                uint8_t t114 = (uint8_t)((((uint8_t *)&gm_l2))[4] != 0);
                if (!t114) {
                    goto gm_lb7;
                }
                gm_t88rcucg1b7uk t115 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
                gm_l7 = t115;
                void * t116 = gm_l0;
                void * t117 = (void *)(((uint8_t *)t116) + 96);
                void * t118 = gm_l0;
                void * t119 = GM_LD(void *, ((uint8_t *)t118));
                gm_t88rcucg1b7uk t120 = gm_l7;
                gm_t88rcucg1b7uk t121 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t117, t119, t120);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t121;
                {
                    {
                        void * t122 = gm_l5;
                        gm_t88rcucg1b7uk t123 = gm_l8;
                        gm_f_kind_jcnt4hm((void *)&gm_l73, t122, GM_CONV(gm_csguankko4lmhd, t123));
                        uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l73) + 0));
                        uint8_t t125 = (uint8_t)(t124 == ((uint8_t)13ULL));
                        if (!t125) {
                            goto gm_lb9;
                        }
                        gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l73) + 8));
                        gm_l9 = t126;
                        void * t127 = gm_l5;
                        gm_t88rcucg1b7uk t128 = gm_l9;
                        gm_f_kind_jcnt4hm((void *)&gm_l75, t127, GM_CONV(gm_csguankko4lmhd, t128));
                        {
                            {
                                uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&gm_l75) + 0));
                                uint8_t t130 = (uint8_t)(t129 == ((uint8_t)16ULL));
                                if (!t130) {
                                    goto gm_lb11;
                                }
                                gm_l74 = ((uint8_t)1);
                                goto gm_lb10;
                            }
gm_lb11:;
                            {
                                gm_l74 = ((uint8_t)0);
                                goto gm_lb10;
                            }
gm_lb12:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 16, 48);
                        }
gm_lb10:;
                        uint8_t t131 = gm_l74;
                        if (!t131) {
                            goto gm_lb9;
                        }
                        gm_t88rcucg1b7uk t132 = gm_l9;
                        gm_l8 = t132;
                        goto gm_lb8;
                    }
gm_lb9:;
                }
gm_lb8:;
                gm_t88rcucg1b7uk t133 = gm_l8;
                gm_tkn42u7na3t81 t134;
                GM_ST(uint8_t, (uint8_t *)&t134 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t134 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t134), t133);
                ((uint8_t *)&t134)[4] = 1;
                gm_l6 = t134;
                goto gm_lb6;
            }
gm_lb7:;
        }
gm_lb6:;
    }
    {
        gm_tt775pork2bs8 t135;
        memset(&t135, 0, sizeof t135);
        F.l10 = t135;
        gm_l11 = ((uint8_t)0);
        {
            {
                uint8_t t136 = (uint8_t)((((uint8_t *)&gm_l6))[4] != 0);
                if (!t136) {
                    goto gm_lb14;
                }
                gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l6));
                gm_l12 = t137;
                void * t138 = gm_l5;
                gm_t88rcucg1b7uk t139 = gm_l12;
                gm_f_kind_jcnt4hm((void *)&gm_l76, t138, GM_CONV(gm_csguankko4lmhd, t139));
                {
                    {
                        uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&gm_l76) + 0));
                        uint8_t t141 = (uint8_t)(t140 == ((uint8_t)15ULL));
                        if (!t141) {
                            goto gm_lb16;
                        }
                        memmove(((uint8_t *)&F.l13), (((uint8_t *)&gm_l76) + 8), 48);
                        gm_tddtjfggdat0d t142 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l13));
                        gm_tt775pork2bs8 t143;
                        GM_ST(uint8_t, (uint8_t *)&t143 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t143 + 50, 0);
                        GM_ST(uint32_t, (uint8_t *)&t143 + 52, 0);
                        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t143), t142);
                        ((uint8_t *)&t143)[48] = 1;
                        F.l10 = t143;
                        goto gm_lb15;
                    }
gm_lb16:;
                    {
                        uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&gm_l76) + 0));
                        uint8_t t145 = (uint8_t)(t144 == ((uint8_t)16ULL));
                        if (!t145) {
                            goto gm_lb17;
                        }
                        memmove(((uint8_t *)&F.l14), (((uint8_t *)&gm_l76) + 8), 48);
                        gm_tddtjfggdat0d t146 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l14));
                        gm_tt775pork2bs8 t147;
                        GM_ST(uint8_t, (uint8_t *)&t147 + 49, 0);
                        GM_ST(uint16_t, (uint8_t *)&t147 + 50, 0);
                        GM_ST(uint32_t, (uint8_t *)&t147 + 52, 0);
                        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t147), t146);
                        ((uint8_t *)&t147)[48] = 1;
                        F.l10 = t147;
                        gm_l11 = ((uint8_t)1);
                        goto gm_lb15;
                    }
gm_lb17:;
                    {
                        goto gm_lb15;
                    }
gm_lb18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 22, 9);
                }
gm_lb15:;
                goto gm_lb13;
            }
gm_lb14:;
        }
gm_lb13:;
        void * t148 = gm_l1;
        gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)t148));
        gm_l15 = t149;
        gm_l16 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t150 = gm_l16;
                uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                uint8_t t152 = (t150 < t151);
                if (!t152) {
                    goto gm_lb19;
                }
                {
                    void * t153 = GM_LD(void *, ((uint8_t *)&gm_l15));
                    uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    uint64_t t155 = gm_l16;
                    {
                        {
                            memmove(((uint8_t *)&F.l17), (((uint8_t *)t153) + (uintptr_t)t155 * 56), 56);
                            uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 17));
                            if (t156) {
                                gm_l77 = ((uint8_t)1);
                            } else {
                                {
                                    {
                                        uint8_t t157 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l17) + 32)) != 0);
                                        if (t157) {
                                            goto gm_lb25;
                                        }
                                        gm_l78 = ((uint8_t)0);
                                        goto gm_lb24;
                                    }
gm_lb25:;
                                    {
                                        gm_l78 = ((uint8_t)1);
                                        goto gm_lb24;
                                    }
gm_lb26:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 32, 22);
                                }
gm_lb24:;
                                uint8_t t158 = gm_l78;
                                gm_l77 = t158;
                            }
                            uint8_t t159 = gm_l77;
                            if (t159) {
                                void * t160 = gm_l0;
                                gm_tl874t2atnlh3 t161 = gm_l4;
                                gm_b16_8 t162;
                                GM_ST(void *, (uint8_t *)&t162, (void *)(gm_s4b8airp3rh69));
                                GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(49));
                                void * t163 = gm_f_unsupported_r3r7qdh((void *)NULL, cx, NULL, t160, t161, t162);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t163;
                                goto gm_fret;
                            }
                            goto gm_lb22;
                        }
gm_lb23:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 31, 5);
                    }
gm_lb22:;
                }
gm_lb21:;
                uint64_t t164 = gm_l16;
                uint64_t r165; bool o166 = __builtin_add_overflow(t164, ((uint64_t)1ULL), &r165);
                uint64_t t167 = r165;
                gm_l16 = t167;
            }
        }
gm_lb19:;
    }
    {
        void * t168 = gm_l0;
        void * t169 = gm_l1;
        gm_b16_8 t170 = gm_f_literal_params_n5511im((void *)NULL, cx, NULL, t168, t169, &(*(gm_tt775pork2bs8 *)((uint8_t *)&F.l10)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t170;
        gm_tkn42u7na3t81 t171;
        memset(&t171, 0, sizeof t171);
        gm_l19 = t171;
        {
            {
                void * t172 = gm_l1;
                uint8_t t173 = (uint8_t)(GM_LD(void *, (((uint8_t *)t172) + 24)) != 0);
                if (!t173) {
                    goto gm_lb28;
                }
                void * t174 = GM_LD(void *, (((uint8_t *)t172) + 24));
                gm_l20 = t174;
                void * t175 = gm_l0;
                void * t176 = gm_l20;
                gm_t88rcucg1b7uk t177 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t175, t176);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t178;
                GM_ST(uint8_t, (uint8_t *)&t178 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t178 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t178), t177);
                ((uint8_t *)&t178)[4] = 1;
                gm_l19 = t178;
                goto gm_lb27;
            }
gm_lb28:;
            {
                {
                    uint8_t t179 = (uint8_t)((((uint8_t *)&F.l10))[48] != 0);
                    if (!t179) {
                        goto gm_lb30;
                    }
                    memmove(((uint8_t *)&F.l21), ((uint8_t *)&F.l10), 48);
                    void * t180 = gm_l5;
                    gm_t88rcucg1b7uk t181 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                    uint8_t t182 = gm_f_has_param_u9t0efs(t180, GM_CONV(gm_csguankko4lmhd, t181));
                    uint8_t t183 = (uint8_t)!t182;
                    if (!t183) {
                        goto gm_lb30;
                    }
                    gm_t88rcucg1b7uk t184 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l21) + 16));
                    gm_tkn42u7na3t81 t185;
                    GM_ST(uint8_t, (uint8_t *)&t185 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t185 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t185), t184);
                    ((uint8_t *)&t185)[4] = 1;
                    gm_l19 = t185;
                    goto gm_lb29;
                }
gm_lb30:;
            }
gm_lb29:;
        }
gm_lb27:;
        gm_tnpifv20irnph t186 = gm_f_empty_effects_r0iv3pv();
        gm_l22 = t186;
        gm_trqfvmjtt4sij t187;
        gm_f_list_go2qg0b((void *)&t187, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l23 = t187;
        {
            {
                void * t188 = gm_l1;
                uint8_t t189 = (uint8_t)(((((uint8_t *)t188) + 32))[16] != 0);
                if (!t189) {
                    goto gm_lb32;
                }
                gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)t188) + 32));
                gm_l24 = t190;
                void * t191 = gm_l0;
                void * t192 = GM_LD(void *, ((uint8_t *)t191));
                void * t193 = gm_l0;
                uint64_t t194 = GM_LD(uint64_t, (((uint8_t *)t193) + 8));
                gm_b16_8 t195 = gm_l24;
                gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
                gm_tnpifv20irnph t197 = gm_f_resolve_effect_list_uhjlh6o((void *)NULL, cx, NULL, t192, t194, t196);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t197;
                goto gm_lb31;
            }
gm_lb32:;
            {
                {
                    uint8_t t198 = (uint8_t)((((uint8_t *)&F.l10))[48] != 0);
                    if (!t198) {
                        goto gm_lb34;
                    }
                    memmove(((uint8_t *)&F.l25), ((uint8_t *)&F.l10), 48);
                    gm_trqfvmjtt4sij t199;
                    gm_f_list_084fcd6((void *)&t199, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l26 = t199;
                    gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 24));
                    gm_l27 = t200;
                    gm_l28 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t201 = gm_l28;
                            uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                            uint8_t t203 = (t201 < t202);
                            if (!t203) {
                                goto gm_lb35;
                            }
                            {
                                void * t204 = GM_LD(void *, ((uint8_t *)&gm_l27));
                                uint64_t t205 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                                uint64_t t206 = gm_l28;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l29), (((uint8_t *)t204) + (uintptr_t)t206 * 24), 24);
                                        void * t207 = gm_l5;
                                        void * t208 = (void *)((uint8_t *)&gm_l29);
                                        uint8_t t209 = gm_f_effect_has_param_args_oketd14(t207, t208);
                                        if (t209) {
                                            void * t210 = (void *)((uint8_t *)&F.l23);
                                            uint32_t t211 = GM_LD(uint32_t, ((uint8_t *)&gm_l29));
                                            gm_tftb1odc739t5 t212 = gm_f_push_im9d4ro(t210, t211);
                                            gm_l79 = t212;
                                            uint8_t t213 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                                            if (!t213) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 1, gm_srcl43af23eher3, 51, 17);
                                        } else {
                                            void * t214 = (void *)((uint8_t *)&F.l26);
                                            gm_tftb1odc739t5 t215 = gm_f_push_d1r4h19(t214, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l29)));
                                            gm_l80 = t215;
                                            uint8_t t216 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                                            if (!t216) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 1, gm_srcl43af23eher3, 53, 17);
                                        }
                                        goto gm_lb38;
                                    }
gm_lb39:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 49, 9);
                                }
gm_lb38:;
                            }
gm_lb37:;
                            uint64_t t217 = gm_l28;
                            uint64_t r218; bool o219 = __builtin_add_overflow(t217, ((uint64_t)1ULL), &r218);
                            uint64_t t220 = r218;
                            gm_l28 = t220;
                        }
                    }
gm_lb35:;
                    uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)&F.l23) + 8));
                    uint8_t t222 = ((t221 == ((uint64_t)0ULL)));
                    if (t222) {
                        gm_tnpifv20irnph t223 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l25) + 24));
                        gm_l81 = t223;
                    } else {
                        gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
                        gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&t224));
                        gm_tnpifv20irnph t226 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t225);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l81 = t226;
                    }
                    gm_tnpifv20irnph t227 = gm_l81;
                    gm_l22 = t227;
                    goto gm_lb33;
                }
gm_lb34:;
            }
gm_lb33:;
        }
gm_lb31:;
    }
    {
        void * t228 = gm_l1;
        gm_tl874t2atnlh3 t229 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t228) + 136));
        void * t230 = gm_l0;
        gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)t230) + 1248));
        uint8_t t232 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t231) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t231))) + i * 12, ((uint8_t *)&t229))) { t232 = 1; break; }
        uint8_t t233 = (uint8_t)(t232);
        gm_l30 = t233;
        {
            {
                uint8_t t234 = (uint8_t)((((uint8_t *)&gm_l19))[4] != 0);
                if (t234) {
                    goto gm_lb41;
                }
                gm_l84 = ((uint8_t)1);
                goto gm_lb40;
            }
gm_lb41:;
            {
                gm_l84 = ((uint8_t)0);
                goto gm_lb40;
            }
gm_lb42:;
            gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 60, 19);
        }
gm_lb40:;
        uint8_t t235 = gm_l84;
        if (t235) {
            gm_l83 = ((uint8_t)1);
        } else {
            uint8_t t236 = gm_l30;
            gm_l83 = t236;
        }
        uint8_t t237 = gm_l83;
        if (t237) {
            void * t238 = gm_l1;
            {
                {
                    uint8_t t239 = GM_LD(uint8_t, ((((uint8_t *)t238) + 88) + 0));
                    uint8_t t240 = (uint8_t)(t239 == ((uint8_t)1ULL));
                    if (!t240) {
                        goto gm_lb44;
                    }
                    gm_l85 = ((uint8_t)1);
                    goto gm_lb43;
                }
gm_lb44:;
                {
                    gm_l85 = ((uint8_t)0);
                    goto gm_lb43;
                }
gm_lb45:;
                gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 60, 58);
            }
gm_lb43:;
            uint8_t t241 = gm_l85;
            gm_l82 = t241;
        } else {
            gm_l82 = ((uint8_t)0);
        }
        uint8_t t242 = gm_l82;
        gm_l31 = t242;
        {
            {
                uint8_t t243 = (uint8_t)((((uint8_t *)&gm_l19))[4] != 0);
                if (t243) {
                    goto gm_lb47;
                }
                gm_l87 = ((uint8_t)1);
                goto gm_lb46;
            }
gm_lb47:;
            {
                gm_l87 = ((uint8_t)0);
                goto gm_lb46;
            }
gm_lb48:;
            gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 61, 20);
        }
gm_lb46:;
        uint8_t t244 = gm_l87;
        if (t244) {
            void * t245 = gm_l1;
            {
                {
                    uint8_t t246 = GM_LD(uint8_t, ((((uint8_t *)t245) + 88) + 0));
                    uint8_t t247 = (uint8_t)(t246 == ((uint8_t)0ULL));
                    if (!t247) {
                        goto gm_lb50;
                    }
                    gm_l88 = ((uint8_t)1);
                    goto gm_lb49;
                }
gm_lb50:;
                {
                    gm_l88 = ((uint8_t)0);
                    goto gm_lb49;
                }
gm_lb51:;
                gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 61, 42);
            }
gm_lb49:;
            uint8_t t248 = gm_l88;
            gm_l86 = t248;
        } else {
            gm_l86 = ((uint8_t)0);
        }
    }
    {
        uint8_t t249 = gm_l86;
        gm_l32 = t249;
        uint8_t t250 = (uint8_t)((((uint8_t *)&gm_l19))[4] != 0);
        if (t250) {
            gm_t88rcucg1b7uk t251 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l19));
            gm_l89 = t251;
        } else {
            void * t252 = gm_l5;
            gm_t88rcucg1b7uk t253 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t252);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l89 = t253;
        }
        gm_t88rcucg1b7uk t254 = gm_l89;
        gm_l33 = t254;
        {
            {
                uint8_t t255 = (uint8_t)((((uint8_t *)&F.l3))[32] != 0);
                if (!t255) {
                    goto gm_lb53;
                }
                memmove(((uint8_t *)&gm_l34), ((uint8_t *)&F.l3), 32);
                gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l34));
                gm_l90 = t256;
                goto gm_lb52;
            }
gm_lb53:;
            gm_b16_8 t257;
            GM_ST(void *, (uint8_t *)&t257, (void *)(gm_s93p2i934ld6p));
            GM_ST(uintptr_t, (uint8_t *)&t257 + 8, (uintptr_t)(4));
            gm_l90 = t257;
        }
gm_lb52:;
        gm_b16_8 t258 = gm_l90;
        gm_l35 = t258;
        void * t259 = gm_l0;
        void * t260 = GM_LD(void *, ((uint8_t *)t259));
        gm_b16_8 t261 = gm_l35;
        gm_tl874t2atnlh3 t262 = gm_l4;
        void * t263 = gm_l0;
        uint64_t t264 = GM_LD(uint64_t, (((uint8_t *)t263) + 8));
        gm_b16_8 t265 = gm_l18;
        gm_t88rcucg1b7uk t266 = gm_l33;
        gm_tnpifv20irnph t267 = gm_l22;
        uint32_t t268 = gm_f_new_literal_fn_oqjp4lg((void *)NULL, cx, NULL, t260, t261, t262, t264, t265, t266, t267);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l36 = t268;
        {
            {
                uint8_t t269 = (uint8_t)((((uint8_t *)&F.l3))[32] != 0);
                if (!t269) {
                    goto gm_lb55;
                }
                memmove(((uint8_t *)&gm_l37), ((uint8_t *)&F.l3), 32);
                void * t270 = gm_l0;
                gm_b16_8 t271 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l37));
                uint32_t t272 = gm_l36;
                gm_tl874t2atnlh3 t273 = gm_l4;
                gm_t930q5l38hdap t274;
                memset(&t274, 0, sizeof t274);
                GM_ST(uint8_t, ((uint8_t *)&t274 + 0), ((uint8_t)3ULL));
                GM_ST(uint32_t, ((uint8_t *)&t274 + 8), t272);
                gm_b0_1 t275 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t270, t271, &t274, t273);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb54;
            }
gm_lb55:;
        }
gm_lb54:;
        void * t276 = gm_l0;
        uint32_t t277 = gm_l36;
        gm_t88rcucg1b7uk t278 = gm_l33;
        gm_tnpifv20irnph t279 = gm_l22;
        gm_b0_1 t280 = gm_f_enter_fn_vo5kel1((void *)NULL, cx, NULL, t276, t277, t278, t279);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t281 = gm_l0;
        uint8_t t282 = gm_l32;
        GM_ST(uint8_t, (((uint8_t *)t281) + 744), t282);
        {
            {
                uint8_t t283 = (uint8_t)((((uint8_t *)&F.l3))[32] != 0);
                if (t283) {
                    goto gm_lb57;
                }
                gm_l92 = ((uint8_t)0);
                goto gm_lb56;
            }
gm_lb57:;
            {
                gm_l92 = ((uint8_t)1);
                goto gm_lb56;
            }
gm_lb58:;
            gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 69, 13);
        }
gm_lb56:;
    }
    {
        uint8_t t284 = gm_l92;
        if (t284) {
            uint8_t t285 = gm_l31;
            if (t285) {
                gm_l93 = ((uint8_t)1);
            } else {
                uint8_t t286 = gm_l32;
                gm_l93 = t286;
            }
            uint8_t t287 = gm_l93;
            gm_l91 = t287;
        } else {
            gm_l91 = ((uint8_t)0);
        }
        uint8_t t288 = gm_l91;
        gm_l38 = t288;
        uint8_t t289 = gm_l38;
        if (t289) {
            void * t290 = gm_l0;
            void * t291 = (void *)(((uint8_t *)t290) + 760);
            uint32_t t292 = gm_l36;
            gm_tftb1odc739t5 t293 = gm_f_push_im9d4ro(t291, t292);
            gm_l94 = t293;
            uint8_t t294 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
            if (!t294) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l94) + 1, gm_srcl43af23eher3, 70, 15);
        }
        void * t295 = gm_l0;
        uint64_t t296 = GM_LD(uint64_t, ((((uint8_t *)t295) + 704) + 8));
        gm_l39 = t296;
        gm_trqfvmjtt4sij t297;
        gm_f_list_go2qg0b((void *)&t297, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l40 = t297;
        gm_b16_8 t298 = gm_l18;
        gm_l41 = t298;
        gm_l42 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t299 = gm_l42;
                uint64_t t300 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                uint8_t t301 = (t299 < t300);
                if (!t301) {
                    goto gm_lb59;
                }
                {
                    void * t302 = GM_LD(void *, ((uint8_t *)&gm_l41));
                    uint64_t t303 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                    uint64_t t304 = gm_l42;
                    {
                        {
                            memmove(((uint8_t *)&F.l43), (((uint8_t *)t302) + (uintptr_t)t304 * 56), 56);
                            void * t305 = gm_l0;
                            gm_b16_8 t306 = GM_LD(gm_b16_8, ((uint8_t *)&F.l43));
                            gm_t88rcucg1b7uk t307 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l43) + 16));
                            gm_tl874t2atnlh3 t308 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l43) + 20));
                            uint32_t t309 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t305, t306, t307, ((uint8_t)0), t308);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l44 = t309;
                            gm_b16_8 t310 = GM_LD(gm_b16_8, ((uint8_t *)&F.l43));
                            gm_b16_8 t311;
                            GM_ST(void *, (uint8_t *)&t311, (void *)(gm_sfdbfrv46lihd));
                            GM_ST(uintptr_t, (uint8_t *)&t311 + 8, (uintptr_t)(1));
                            uint8_t t312 = (!gm_eq0iqiubfm7e(((uint8_t *)&t310), ((uint8_t *)&t311)));
                            if (t312) {
                                void * t313 = gm_l0;
                                gm_b16_8 t314 = GM_LD(gm_b16_8, ((uint8_t *)&F.l43));
                                uint32_t t315 = gm_l44;
                                gm_tl874t2atnlh3 t316 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l43) + 20));
                                gm_t930q5l38hdap t317;
                                memset(&t317, 0, sizeof t317);
                                GM_ST(uint8_t, ((uint8_t *)&t317 + 0), ((uint8_t)0ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t317 + 8), t315);
                                gm_b0_1 t318 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t313, t314, &t317, t316);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            void * t319 = (void *)((uint8_t *)&F.l40);
                            uint32_t t320 = gm_l44;
                            gm_tftb1odc739t5 t321 = gm_f_push_im9d4ro(t319, t320);
                            gm_l95 = t321;
                            uint8_t t322 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
                            if (!t322) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l95) + 1, gm_srcl43af23eher3, 78, 9);
                            goto gm_lb62;
                        }
gm_lb63:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 75, 5);
                    }
gm_lb62:;
                }
gm_lb61:;
                uint64_t t323 = gm_l42;
                uint64_t r324; bool o325 = __builtin_add_overflow(t323, ((uint64_t)1ULL), &r324);
                uint64_t t326 = r324;
                gm_l42 = t326;
            }
        }
gm_lb59:;
    }
    {
        void * t327 = gm_l0;
        void * t328 = gm_l0;
        void * t329 = gm_l1;
        void * t330 = (void *)(((uint8_t *)t329) + 88);
        gm_t88rcucg1b7uk t331 = gm_l33;
        uint8_t t332 = gm_l31;
        void * t333 = gm_f_literal_body_5lm01a5((void *)NULL, cx, NULL, t328, t330, t331, t332);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t334 = gm_f_declare_temps_a7fc14v((void *)NULL, cx, NULL, t327, ((uint64_t)0ULL), t333);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l45 = t334;
        uint8_t t335 = gm_l38;
        if (t335) {
            void * t336 = gm_l0;
            void * t337 = (void *)(((uint8_t *)t336) + 760);
            gm_t9805qtonbsag t338 = gm_f_pop_570g79n(t337);
        }
        void * t339 = gm_l0;
        gm_t88rcucg1b7uk t340 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t339) + 168));
        gm_l46 = t340;
        void * t341 = gm_l0;
        void * t342 = gm_l1;
        gm_t88rcucg1b7uk t343 = gm_l46;
        gm_b0_1 t344 = gm_f_reject_returned_closure_1ouocor((void *)NULL, cx, NULL, t341, t342, t343);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t345 = gm_l0;
        gm_f_exit_fn_kj6vbu8((void *)&F.l47, t345);
        uint8_t t346 = gm_l30;
        if (t346) {
            gm_l48 = ((uint8_t)0);
            {
                {
                    void * t347 = gm_l0;
                    void * t348 = GM_LD(void *, ((uint8_t *)t347));
                    uint8_t t349 = (uint8_t)(((((uint8_t *)t348) + 2076))[4] != 0);
                    if (!t349) {
                        goto gm_lb65;
                    }
                    uint32_t t350 = GM_LD(uint32_t, (((uint8_t *)t348) + 2076));
                    gm_l49 = t350;
                    gm_b16_8 t351 = GM_LD(gm_b16_8, (((uint8_t *)&F.l47) + 16));
                    gm_l50 = t351;
                    gm_l51 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t352 = gm_l51;
                            uint64_t t353 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                            uint8_t t354 = (t352 < t353);
                            if (!t354) {
                                goto gm_lb66;
                            }
                            {
                                void * t355 = GM_LD(void *, ((uint8_t *)&gm_l50));
                                uint64_t t356 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                                uint64_t t357 = gm_l51;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l52), (((uint8_t *)t355) + (uintptr_t)t357 * 40), 40);
                                        uint32_t t358 = GM_LD(uint32_t, ((uint8_t *)&F.l52));
                                        uint32_t t359 = gm_l49;
                                        uint8_t t360 = ((t358 == t359));
                                        if (t360) {
                                            gm_l48 = ((uint8_t)1);
                                        }
                                        goto gm_lb69;
                                    }
gm_lb70:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 88, 13);
                                }
gm_lb69:;
                            }
gm_lb68:;
                            uint64_t t361 = gm_l51;
                            uint64_t r362; bool o363 = __builtin_add_overflow(t361, ((uint64_t)1ULL), &r362);
                            uint64_t t364 = r362;
                            gm_l51 = t364;
                        }
                    }
gm_lb66:;
                    goto gm_lb64;
                }
gm_lb65:;
            }
gm_lb64:;
            uint8_t t365 = gm_l48;
            uint8_t t366 = (uint8_t)!t365;
            if (t366) {
                void * t367 = gm_l0;
                void * t368 = (void *)(((uint8_t *)t367) + 96);
                void * t369 = gm_l0;
                void * t370 = GM_LD(void *, ((uint8_t *)t369));
                gm_t88rcucg1b7uk t371 = gm_l46;
                gm_t88rcucg1b7uk t372 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t368, t370, t371);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l53 = t372;
                void * t373 = gm_l5;
                gm_t88rcucg1b7uk t374 = gm_l53;
                uint8_t t375 = gm_f_is_error_8k27gd1(t373, GM_CONV(gm_csguankko4lmhd, t374));
                uint8_t t376 = (uint8_t)!t375;
                if (t376) {
                    void * t377 = gm_l0;
                    gm_tl874t2atnlh3 t378 = gm_l4;
                    void * t379 = gm_l0;
                    gm_t88rcucg1b7uk t380 = gm_l53;
                    gm_b16_8 t381 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t379, t380);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t382;
                    GM_ST(gm_b16_8, ((uint8_t *)&t382 + 0), t381);
                    gm_f_format_8c2oop8((void *)&gm_l96, (void *)NULL, cx, NULL, t382);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t383 = (uint8_t)!((((uint8_t *)&gm_l96))[0] != 0);
                    if (!t383) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l96) + 8, gm_srcl43af23eher3, 95, 32);
                    gm_b16_8 t384 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l96) + 8));
                    gm_b16_8 t385 = GM_LD(gm_b16_8, ((uint8_t *)&t384));
                    gm_b0_1 t386 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t377, t378, t385);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t387 = gm_l0;
                void * t388 = GM_LD(void *, ((uint8_t *)t387));
                gm_tl874t2atnlh3 t389 = gm_l4;
                void * t390 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t388, t389);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t390;
                goto gm_fret;
            }
        }
    }
    {
        gm_b16_8 t391 = GM_LD(gm_b16_8, ((uint8_t *)&F.l23));
        gm_l54 = t391;
        gm_l55 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t392 = gm_l55;
                uint64_t t393 = GM_LD(uint64_t, (((uint8_t *)&gm_l54) + 8));
                uint8_t t394 = (t392 < t393);
                if (!t394) {
                    goto gm_lb71;
                }
                {
                    void * t395 = GM_LD(void *, ((uint8_t *)&gm_l54));
                    uint64_t t396 = GM_LD(uint64_t, (((uint8_t *)&gm_l54) + 8));
                    uint64_t t397 = gm_l55;
                    {
                        {
                            uint32_t t398 = GM_LD(uint32_t, (((uint8_t *)t395) + (uintptr_t)t397 * 4));
                            gm_l56 = t398;
                            void * t399 = gm_l0;
                            uint32_t t400 = gm_l56;
                            gm_b16_8 t401 = GM_LD(gm_b16_8, (((uint8_t *)&F.l47) + 16));
                            gm_b16_8 t402 = GM_LD(gm_b16_8, ((uint8_t *)&t401));
                            void * t403 = (void *)((uint8_t *)&gm_l22);
                            uint32_t t404 = gm_l36;
                            gm_tl874t2atnlh3 t405 = gm_l4;
                            uint8_t t406 = gm_f_infer_open_effect_47fpti4((void *)NULL, cx, NULL, t399, t400, t402, t403, t404, t405);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t407 = (uint8_t)!t406;
                            if (t407) {
                                void * t408 = gm_l0;
                                void * t409 = GM_LD(void *, ((uint8_t *)t408));
                                gm_tl874t2atnlh3 t410 = gm_l4;
                                void * t411 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t409, t410);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t411;
                                goto gm_fret;
                            }
                            goto gm_lb74;
                        }
gm_lb75:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 100, 5);
                    }
gm_lb74:;
                }
gm_lb73:;
                uint64_t t412 = gm_l55;
                uint64_t r413; bool o414 = __builtin_add_overflow(t412, ((uint64_t)1ULL), &r413);
                uint64_t t415 = r413;
                gm_l55 = t415;
            }
        }
gm_lb71:;
        uint8_t t416 = gm_l31;
        if (t416) {
            gm_l97 = ((uint8_t)1);
        } else {
            uint8_t t417 = gm_l32;
            gm_l97 = t417;
        }
        uint8_t t418 = gm_l97;
        if (t418) {
            void * t419 = gm_l0;
            void * t420 = GM_LD(void *, ((uint8_t *)t419));
            void * t421 = GM_LD(void *, (((uint8_t *)t420) + 344));
            uint64_t t422 = GM_LD(uint64_t, ((((uint8_t *)t420) + 344) + 8));
            uint32_t t423 = gm_l36;
            uint64_t t424 = (uint64_t)t423;
            if ((uintptr_t)t424 >= (uintptr_t)(t422)) gm_trap_index((uintptr_t)t424, t422, gm_srcl43af23eher3, 103, 35);
            gm_t88rcucg1b7uk t425 = gm_l46;
            GM_ST(gm_t88rcucg1b7uk, ((((uint8_t *)t421) + (uintptr_t)t424 * 136) + 56), t425);
        }
        {
            {
                uint8_t t426 = (uint8_t)((((uint8_t *)&F.l3))[32] != 0);
                if (t426) {
                    goto gm_lb77;
                }
                gm_l99 = ((uint8_t)0);
                goto gm_lb76;
            }
gm_lb77:;
            {
                gm_l99 = ((uint8_t)1);
                goto gm_lb76;
            }
gm_lb78:;
            gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 104, 28);
        }
gm_lb76:;
        uint8_t t427 = gm_l99;
        if (t427) {
            uint8_t t428 = GM_LD(uint8_t, (((uint8_t *)&F.l47) + 32));
            uint8_t t429 = (uint8_t)!t428;
            gm_l98 = t429;
        } else {
            gm_l98 = ((uint8_t)0);
        }
    }
    {
        uint8_t t430 = gm_l98;
        gm_l57 = t430;
        void * t431 = gm_l0;
        gm_b16_8 t432 = GM_LD(gm_b16_8, (((uint8_t *)&F.l47) + 16));
        gm_b16_8 t433 = GM_LD(gm_b16_8, ((uint8_t *)&t432));
        void * t434 = (void *)((uint8_t *)&gm_l22);
        uint8_t t435 = gm_l11;
        if (t435) {
            {
                {
                    uint8_t t436 = (uint8_t)((((uint8_t *)&F.l3))[32] != 0);
                    if (t436) {
                        goto gm_lb80;
                    }
                    gm_l101 = ((uint8_t)1);
                    goto gm_lb79;
                }
gm_lb80:;
                {
                    gm_l101 = ((uint8_t)0);
                    goto gm_lb79;
                }
gm_lb81:;
                gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 105, 76);
            }
gm_lb79:;
            uint8_t t437 = gm_l101;
            gm_l100 = t437;
        } else {
            gm_l100 = ((uint8_t)0);
        }
        uint8_t t438 = gm_l100;
        uint8_t t439 = gm_l57;
        gm_b16_8 t440 = gm_f_route_performed_pruu3np((void *)NULL, cx, NULL, t431, t433, t434, t438, t439);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l58 = t440;
        uint64_t t441 = GM_LD(uint64_t, (((uint8_t *)&gm_l58) + 8));
        uint8_t t442 = (t441 > ((uint64_t)0ULL));
        gm_l59 = t442;
        uint8_t t443 = gm_l59;
        if (t443) {
            gm_trqfvmjtt4sij t444;
            gm_f_list_084fcd6((void *)&t444, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l60 = t444;
            void * t445 = (void *)((uint8_t *)&F.l60);
            gm_b16_8 t446 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
            gm_tftb1odc739t5 t447 = gm_f_extend_74ac4hb(t445, t446);
            gm_l102 = t447;
            uint8_t t448 = (uint8_t)!((((uint8_t *)&gm_l102))[0] != 0);
            if (!t448) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l102) + 1, gm_srcl43af23eher3, 111, 9);
            void * t449 = (void *)((uint8_t *)&F.l60);
            gm_b16_8 t450 = gm_l58;
            gm_b16_8 t451 = GM_LD(gm_b16_8, ((uint8_t *)&t450));
            gm_tftb1odc739t5 t452 = gm_f_extend_74ac4hb(t449, t451);
            gm_l103 = t452;
            uint8_t t453 = (uint8_t)!((((uint8_t *)&gm_l103))[0] != 0);
            if (!t453) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l103) + 1, gm_srcl43af23eher3, 112, 9);
            void * t454 = gm_l0;
            void * t455 = GM_LD(void *, ((uint8_t *)t454));
            void * t456 = GM_LD(void *, (((uint8_t *)t455) + 344));
            uint64_t t457 = GM_LD(uint64_t, ((((uint8_t *)t455) + 344) + 8));
            uint32_t t458 = gm_l36;
            uint64_t t459 = (uint64_t)t458;
            if ((uintptr_t)t459 >= (uintptr_t)(t457)) gm_trap_index((uintptr_t)t459, t457, gm_srcl43af23eher3, 113, 9);
            gm_b16_8 t460 = GM_LD(gm_b16_8, ((uint8_t *)&F.l60));
            gm_b16_8 t461 = GM_LD(gm_b16_8, ((uint8_t *)&t460));
            gm_tnpifv20irnph t462 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t461);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_tnpifv20irnph, ((((uint8_t *)t456) + (uintptr_t)t459 * 136) + 64), t462);
        }
        uint8_t t463 = GM_LD(uint8_t, (((uint8_t *)&F.l47) + 32));
        if (t463) {
            void * t464 = gm_l0;
            void * t465 = GM_LD(void *, ((uint8_t *)t464));
            void * t466 = GM_LD(void *, (((uint8_t *)t465) + 384));
            uint64_t t467 = GM_LD(uint64_t, ((((uint8_t *)t465) + 384) + 8));
            uint32_t t468 = gm_l36;
            uint64_t t469 = (uint64_t)t468;
            if ((uintptr_t)t469 >= (uintptr_t)(t467)) gm_trap_index((uintptr_t)t469, t467, gm_srcl43af23eher3, 115, 25);
            void * t470 = gm_l0;
            gm_t9805qtonbsag t471 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t470) + 400));
            GM_ST(gm_t9805qtonbsag, ((((uint8_t *)t466) + (uintptr_t)t469 * 64) + 48), t471);
        }
    }
    {
        void * t472 = gm_l0;
        void * t473 = (void *)(((uint8_t *)t472) + 704);
        uint32_t t474 = gm_l36;
        gm_b16_8 t475 = GM_LD(gm_b16_8, ((uint8_t *)&F.l40));
        gm_b16_8 t476 = GM_LD(gm_b16_8, ((uint8_t *)&F.l47));
        void * t477 = gm_l45;
        gm_tl93vjpcesj48 t478;
        GM_ST(uint32_t, (uint8_t *)&t478 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t478 + 0), t474);
        GM_ST(gm_b16_8, ((uint8_t *)&t478 + 8), t475);
        GM_ST(gm_b16_8, ((uint8_t *)&t478 + 24), t476);
        GM_ST(void *, ((uint8_t *)&t478 + 40), t477);
        gm_tftb1odc739t5 t479 = gm_f_push_jd7smj6(t473, &t478);
        gm_l104 = t479;
        uint8_t t480 = (uint8_t)!((((uint8_t *)&gm_l104))[0] != 0);
        if (!t480) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l104) + 1, gm_srcl43af23eher3, 116, 5);
        gm_trqfvmjtt4sij t481;
        gm_f_list_nof7vfj((void *)&t481, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l61 = t481;
        gm_b16_8 t482 = gm_l18;
        gm_l62 = t482;
        gm_l63 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t483 = gm_l63;
                uint64_t t484 = GM_LD(uint64_t, (((uint8_t *)&gm_l62) + 8));
                uint8_t t485 = (t483 < t484);
                if (!t485) {
                    goto gm_lb82;
                }
                {
                    void * t486 = GM_LD(void *, ((uint8_t *)&gm_l62));
                    uint64_t t487 = GM_LD(uint64_t, (((uint8_t *)&gm_l62) + 8));
                    uint64_t t488 = gm_l63;
                    {
                        {
                            memmove(((uint8_t *)&F.l64), (((uint8_t *)t486) + (uintptr_t)t488 * 56), 56);
                            void * t489 = (void *)((uint8_t *)&F.l61);
                            gm_t88rcucg1b7uk t490 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l64) + 16));
                            gm_tftb1odc739t5 t491 = gm_f_push_k3l9nic(t489, GM_CONV(gm_csguankko4lmhd, t490));
                            gm_l105 = t491;
                            uint8_t t492 = (uint8_t)!((((uint8_t *)&gm_l105))[0] != 0);
                            if (!t492) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l105) + 1, gm_srcl43af23eher3, 118, 23);
                            goto gm_lb85;
                        }
gm_lb86:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 118, 5);
                    }
gm_lb85:;
                }
gm_lb84:;
                uint64_t t493 = gm_l63;
                uint64_t r494; bool o495 = __builtin_add_overflow(t493, ((uint64_t)1ULL), &r494);
                uint64_t t496 = r494;
                gm_l63 = t496;
            }
        }
gm_lb82:;
        gm_l65 = ((uint8_t)0);
        {
            {
                uint8_t t497 = (uint8_t)((((uint8_t *)&F.l10))[48] != 0);
                if (!t497) {
                    goto gm_lb88;
                }
                memmove(((uint8_t *)&F.l66), ((uint8_t *)&F.l10), 48);
                uint8_t t498 = gm_l11;
                if (!t498) {
                    goto gm_lb88;
                }
                uint8_t t499 = GM_LD(uint8_t, (((uint8_t *)&F.l47) + 32));
                uint8_t t500 = (uint8_t)!t499;
                if (!t500) {
                    goto gm_lb88;
                }
                uint8_t t501 = GM_LD(uint8_t, (((uint8_t *)&F.l66) + 40));
                gm_l65 = t501;
                goto gm_lb87;
            }
gm_lb88:;
        }
gm_lb87:;
        uint8_t t502 = gm_l65;
        if (t502) {
            void * t503 = gm_l0;
            void * t504 = GM_LD(void *, ((uint8_t *)t503));
            void * t505 = (void *)((uint8_t *)&gm_l22);
            uint8_t t506 = gm_f_within_sys_otp5qp6((void *)NULL, cx, NULL, t504, t505);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t506) {
                void * t507 = gm_l0;
                void * t508 = GM_LD(void *, ((uint8_t *)t507));
                void * t509 = GM_LD(void *, (((uint8_t *)t508) + 344));
                uint64_t t510 = GM_LD(uint64_t, ((((uint8_t *)t508) + 344) + 8));
                uint32_t t511 = gm_l36;
                uint64_t t512 = (uint64_t)t511;
                if ((uintptr_t)t512 >= (uintptr_t)(t510)) gm_trap_index((uintptr_t)t512, t510, gm_srcl43af23eher3, 124, 13);
                GM_ST(uint8_t, ((((uint8_t *)t509) + (uintptr_t)t512 * 136) + 128), ((uint8_t)1));
            } else {
                void * t513 = gm_l0;
                gm_tl874t2atnlh3 t514 = gm_l4;
                gm_b16_8 t515;
                GM_ST(void *, (uint8_t *)&t515, (void *)(gm_srdq954rpedg3));
                GM_ST(uintptr_t, (uint8_t *)&t515 + 8, (uintptr_t)(92));
                gm_b0_1 t516 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t513, t514, t515);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
    }
    {
        gm_b16_8 t517 = GM_LD(gm_b16_8, ((uint8_t *)&F.l61));
        gm_b16_8 t518 = GM_LD(gm_b16_8, ((uint8_t *)&t517));
        gm_t88rcucg1b7uk t519 = gm_l46;
        gm_tnpifv20irnph t520 = gm_l22;
        uint8_t t521 = gm_l65;
        gm_tddtjfggdat0d t522;
        GM_ST(uint32_t, (uint8_t *)&t522 + 20, 0);
        GM_ST(uint8_t, (uint8_t *)&t522 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t522 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t522 + 44, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t522 + 0), t518);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t522 + 16), t519);
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t522 + 24), t520);
        GM_ST(uint8_t, ((uint8_t *)&t522 + 40), t521);
        F.l67 = t522;
        uint8_t t523 = GM_LD(uint8_t, (((uint8_t *)&F.l47) + 32));
        if (t523) {
            uint8_t t524 = gm_l11;
            if (t524) {
                void * t525 = gm_l0;
                gm_tl874t2atnlh3 t526 = gm_l4;
                gm_b16_8 t527;
                GM_ST(void *, (uint8_t *)&t527, (void *)(gm_sn1j4m1p1cnm1));
                GM_ST(uintptr_t, (uint8_t *)&t527 + 8, (uintptr_t)(82));
                gm_b0_1 t528 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t525, t526, t527);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t529 = gm_l0;
                void * t530 = GM_LD(void *, ((uint8_t *)t529));
                gm_tl874t2atnlh3 t531 = gm_l4;
                void * t532 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t530, t531);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t532;
                goto gm_fret;
            }
            {
                {
                    uint8_t t533 = (uint8_t)((((uint8_t *)&F.l3))[32] != 0);
                    if (!t533) {
                        goto gm_lb90;
                    }
                    memmove(((uint8_t *)&gm_l68), ((uint8_t *)&F.l3), 32);
                    void * t534 = gm_l0;
                    uint32_t t535 = gm_l36;
                    uint64_t t536 = gm_l39;
                    gm_b16_8 t537 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l68));
                    gm_tl874t2atnlh3 t538 = gm_l4;
                    gm_b0_1 t539 = gm_f_check_no_self_reference_4ra5lcu((void *)NULL, cx, NULL, t534, t535, t536, t537, t538);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb89;
                }
gm_lb90:;
            }
gm_lb89:;
            void * t540 = gm_l5;
            gm_tddtjfggdat0d t541 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l67));
            gm_t53at80pfdus3 t542;
            GM_ST(uint8_t, (uint8_t *)&t542 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t542 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t542 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t542 + 0), ((uint8_t)15ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t542 + 8), t541);
            gm_t88rcucg1b7uk t543 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t540, &t542);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t544 = gm_l36;
            gm_tl874t2atnlh3 t545 = gm_l4;
            gm_t2b6tm8c0vgo1 t546;
            memset(&t546, 0, sizeof t546);
            GM_ST(uint8_t, ((uint8_t *)&t546 + 0), ((uint8_t)27ULL));
            GM_ST(uint32_t, ((uint8_t *)&t546 + 8), t544);
            void * t547 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t543, &t546, t545);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t547;
            goto gm_fret;
        }
        gm_l69 = ((uint8_t)0);
        {
            {
                uint8_t t548 = (uint8_t)((((uint8_t *)&gm_l2))[4] != 0);
                if (!t548) {
                    goto gm_lb92;
                }
                gm_t88rcucg1b7uk t549 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
                gm_l70 = t549;
                void * t550 = gm_l5;
                gm_t88rcucg1b7uk t551 = gm_l70;
                gm_f_kind_jcnt4hm((void *)&gm_l107, t550, GM_CONV(gm_csguankko4lmhd, t551));
                {
                    {
                        uint8_t t552 = GM_LD(uint8_t, (((uint8_t *)&gm_l107) + 0));
                        uint8_t t553 = (uint8_t)(t552 == ((uint8_t)15ULL));
                        if (!t553) {
                            goto gm_lb94;
                        }
                        gm_l106 = ((uint8_t)1);
                        goto gm_lb93;
                    }
gm_lb94:;
                    {
                        gm_l106 = ((uint8_t)0);
                        goto gm_lb93;
                    }
gm_lb95:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 139, 24);
                }
gm_lb93:;
                uint8_t t554 = gm_l106;
                if (!t554) {
                    goto gm_lb92;
                }
                gm_l69 = ((uint8_t)1);
                goto gm_lb91;
            }
gm_lb92:;
        }
gm_lb91:;
    }
    {
        uint8_t t555 = gm_l59;
        if (t555) {
            gm_l109 = ((uint8_t)1);
        } else {
            uint8_t t556 = gm_l69;
            gm_l109 = t556;
        }
        uint8_t t557 = gm_l109;
        if (t557) {
            void * t558 = gm_l5;
            gm_tddtjfggdat0d t559 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l67));
            gm_t53at80pfdus3 t560;
            GM_ST(uint8_t, (uint8_t *)&t560 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t560 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t560 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t560 + 0), ((uint8_t)15ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t560 + 8), t559);
            gm_t88rcucg1b7uk t561 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t558, &t560);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l108 = t561;
        } else {
            void * t562 = gm_l5;
            gm_tddtjfggdat0d t563 = GM_LD(gm_tddtjfggdat0d, ((uint8_t *)&F.l67));
            gm_t53at80pfdus3 t564;
            GM_ST(uint8_t, (uint8_t *)&t564 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t564 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t564 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t564 + 0), ((uint8_t)16ULL));
            GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t564 + 8), t563);
            gm_t88rcucg1b7uk t565 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t562, &t564);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l108 = t565;
        }
        gm_t88rcucg1b7uk t566 = gm_l108;
        gm_l71 = t566;
        gm_t88rcucg1b7uk t567 = gm_l71;
        uint32_t t568 = gm_l36;
        gm_tl874t2atnlh3 t569 = gm_l4;
        gm_t2b6tm8c0vgo1 t570;
        memset(&t570, 0, sizeof t570);
        GM_ST(uint8_t, ((uint8_t *)&t570 + 0), ((uint8_t)26ULL));
        GM_ST(uint32_t, ((uint8_t *)&t570 + 8), t568);
        void * t571 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t567, &t570, t569);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t571;
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
__attribute__((aligned(4))) uint8_t gm_f_infer_open_effect_47fpti4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, void * a3, uint32_t a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_infer_open_effect_47fpti4 F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* id */
    gm_b16_8 gm_l2; /* performed */
    void * gm_l3; /* effects */
    uint32_t gm_l4; /* fn_id */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint8_t gm_l10; /* seen */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_tiilmfd16v3fc gm_l13; /* x */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tiilmfd16v3fc gm_l17; /* e */
    GM_STACK_CHECK(&F, 1083u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tftb1odc739t5 gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_trqfvmjtt4sij t17;
    gm_f_list_084fcd6((void *)&t17, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t17;
    gm_b16_8 t18 = gm_l2;
    gm_l7 = t18;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t19 = gm_l8;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                void * t22 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t24 = gm_l8;
                {
                    {
                        memmove(((uint8_t *)&F.l9), (((uint8_t *)t22) + (uintptr_t)t24 * 40), 40);
                        uint32_t t25 = GM_LD(uint32_t, ((uint8_t *)&F.l9));
                        uint32_t t26 = gm_l1;
                        uint8_t t27 = (!(t25 == t26));
                        if (t27) {
                            goto gm_lb3;
                        }
                        gm_l10 = ((uint8_t)0);
                        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                        gm_l11 = t28;
                        gm_l12 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t29 = gm_l12;
                                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                uint8_t t31 = (t29 < t30);
                                if (!t31) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t32 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                    uint64_t t34 = gm_l12;
                                    {
                                        {
                                            memmove(((uint8_t *)&gm_l13), (((uint8_t *)t32) + (uintptr_t)t34 * 24), 24);
                                            void * t35 = (void *)((uint8_t *)&gm_l13);
                                            void * t36 = (void *)((uint8_t *)&F.l9);
                                            int32_t t37 = gm_f_cmp_ref_8ntrqll(t35, t36);
                                            uint8_t t38 = ((t37 == ((int32_t)0LL)));
                                            if (t38) {
                                                gm_l10 = ((uint8_t)1);
                                            }
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 258, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t39 = gm_l12;
                                uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                                uint64_t t42 = r40;
                                gm_l12 = t42;
                            }
                        }
gm_lb6:;
                        uint8_t t43 = gm_l10;
                        uint8_t t44 = (uint8_t)!t43;
                        if (t44) {
                            void * t45 = (void *)((uint8_t *)&F.l6);
                            gm_tftb1odc739t5 t46 = gm_f_push_d1r4h19(t45, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&F.l9)));
                            gm_l19 = t46;
                            uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                            if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcl43af23eher3, 261, 23);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 255, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t48 = gm_l8;
            uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
            uint64_t t51 = r49;
            gm_l8 = t51;
        }
    }
gm_lb1:;
    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
    uint8_t t53 = (t52 > ((uint64_t)1ULL));
    if (t53) {
        gm_trqfvmjtt4sij t54;
        gm_f_list_epvducg((void *)&t54, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l14 = t54;
        gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_l15 = t55;
        gm_l16 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t56 = gm_l16;
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                uint8_t t58 = (t56 < t57);
                if (!t58) {
                    goto gm_lb11;
                }
                {
                    void * t59 = GM_LD(void *, ((uint8_t *)&gm_l15));
                    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    uint64_t t61 = gm_l16;
                    {
                        {
                            memmove(((uint8_t *)&gm_l17), (((uint8_t *)t59) + (uintptr_t)t61 * 24), 24);
                            void * t62 = (void *)((uint8_t *)&F.l14);
                            void * t63 = gm_l0;
                            void * t64 = GM_LD(void *, ((uint8_t *)t63));
                            void * t65 = (void *)(((uint8_t *)t64) + 8);
                            void * t66 = (void *)((uint8_t *)&gm_l17);
                            gm_b16_8 t67 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t65, t66);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t68;
                            GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t67);
                            gm_f_format_h4jcodi((void *)&gm_l20, (void *)NULL, cx, NULL, t68);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcl43af23eher3, 265, 43);
                            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                            gm_tftb1odc739t5 t72 = gm_f_push_3ef746b(t62, t71);
                            gm_l21 = t72;
                            uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                            if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcl43af23eher3, 265, 32);
                            goto gm_lb14;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 265, 9);
                    }
gm_lb14:;
                }
gm_lb13:;
                uint64_t t74 = gm_l16;
                uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                uint64_t t77 = r75;
                gm_l16 = t77;
            }
        }
gm_lb11:;
        void * t78 = gm_l0;
        gm_tl874t2atnlh3 t79 = gm_l5;
        gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
        gm_b16_8 t82;
        GM_ST(void *, (uint8_t *)&t82, (void *)(gm_southrlg4k72m));
        GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(5));
        gm_f_join_sfknljb((void *)&gm_l22, (void *)NULL, cx, NULL, t81, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
        if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srcl43af23eher3, 266, 128);
        gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
        gm_tnpifv20irnph t85;
        GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t84);
        gm_f_format_57u5svl((void *)&gm_l23, (void *)NULL, cx, NULL, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
        if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_srcl43af23eher3, 266, 24);
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
        gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
        gm_b0_1 t89 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t78, t79, t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
    uint8_t t91 = ((t90 == ((uint64_t)1ULL)));
    if (t91) {
        gm_trqfvmjtt4sij t92;
        gm_f_list_084fcd6((void *)&t92, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l18 = t92;
        void * t93 = (void *)((uint8_t *)&F.l18);
        void * t94 = gm_l3;
        gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)t94));
        gm_tftb1odc739t5 t96 = gm_f_extend_74ac4hb(t93, t95);
        gm_l24 = t96;
        uint8_t t97 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
        if (!t97) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcl43af23eher3, 271, 9);
        void * t98 = (void *)((uint8_t *)&F.l18);
        void * t99 = GM_LD(void *, ((uint8_t *)&F.l6));
        uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t100)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t100, gm_srcl43af23eher3, 272, 18);
        gm_tftb1odc739t5 t101 = gm_f_push_d1r4h19(t98, &(*(gm_tiilmfd16v3fc *)(((uint8_t *)t99) + (uintptr_t)((uint64_t)0ULL) * 24)));
        gm_l25 = t101;
        uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
        if (!t102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcl43af23eher3, 272, 9);
        void * t103 = gm_l3;
        gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
        gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
        gm_tnpifv20irnph t106 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)t103), t106);
        void * t107 = gm_l0;
        void * t108 = GM_LD(void *, ((uint8_t *)t107));
        void * t109 = GM_LD(void *, (((uint8_t *)t108) + 344));
        uint64_t t110 = GM_LD(uint64_t, ((((uint8_t *)t108) + 344) + 8));
        uint32_t t111 = gm_l4;
        uint64_t t112 = (uint64_t)t111;
        if ((uintptr_t)t112 >= (uintptr_t)(t110)) gm_trap_index((uintptr_t)t112, t110, gm_srcl43af23eher3, 274, 9);
        void * t113 = gm_l3;
        gm_tnpifv20irnph t114 = GM_LD(gm_tnpifv20irnph, ((uint8_t *)t113));
        GM_ST(gm_tnpifv20irnph, ((((uint8_t *)t109) + (uintptr_t)t112 * 136) + 64), t114);
    }
    F.ret = ((uint8_t)1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_generic_jrl2kf9(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_generic_jrl2kf9 F;
    void * gm_l0; /* lit */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    void * gm_l4; /* t */
    gm_t08olq97rscn3 gm_l5; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 225u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t9 = gm_l0;
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t9) + 72) + 8));
    uint8_t t11 = (t10 > ((uint64_t)0ULL));
    if (t11) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    void * t12 = gm_l0;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)t12));
    gm_l1 = t13;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t14 = gm_l2;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                void * t17 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t19 = gm_l2;
                {
                    {
                        memmove(((uint8_t *)&F.l3), (((uint8_t *)t17) + (uintptr_t)t19 * 56), 56);
                        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 16));
                        if (t20) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        {
                            {
                                uint8_t t21 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l3) + 24)) != 0);
                                if (!t21) {
                                    goto gm_lb7;
                                }
                                void * t22 = GM_LD(void *, (((uint8_t *)&F.l3) + 24));
                                gm_l4 = t22;
                                void * t23 = gm_l4;
                                uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t23) + 8) + 0));
                                uint8_t t25 = (uint8_t)(t24 == ((uint8_t)6ULL));
                                if (!t25) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&gm_l5), ((((uint8_t *)t23) + 8) + 8), 32);
                                gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                                gm_b16_8 t27;
                                GM_ST(void *, (uint8_t *)&t27, (void *)(gm_s2737i1iq57nf));
                                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(4));
                                uint8_t t28 = (gm_eq0iqiubfm7e(((uint8_t *)&t26), ((uint8_t *)&t27)));
                                if (!t28) {
                                    goto gm_lb7;
                                }
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 149, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t29 = gm_l2;
            uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
            uint64_t t32 = r30;
            gm_l2 = t32;
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
inline __attribute__((aligned(4))) void * gm_f_literal_body_5lm01a5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint8_t a3) {
    struct gm_fr_literal_body_5lm01a5 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* body */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    uint8_t gm_l3; /* infer_ret */
    void * gm_l4; /* e */
    void * gm_l5; /* b */
    void * gm_l7; /* block */
    GM_STACK_CHECK(&F, 328u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t88rcucg1b7uk gm_l8;
    void * t5 = gm_l1;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)1ULL));
            if (!t7) {
                goto gm_lb2;
            }
            void * t8 = GM_LD(void *, (((uint8_t *)t5) + 8));
            gm_l4 = t8;
            uint8_t t9 = gm_l3;
            if (t9) {
                void * t10 = gm_l0;
                void * t11 = gm_l4;
                gm_tkn42u7na3t81 t12;
                memset(&t12, 0, sizeof t12);
                void * t13 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t10, t11, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l5 = t13;
                void * t14 = gm_l0;
                void * t15 = gm_l5;
                gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t15));
                GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)t14) + 168), t16);
                void * t17 = gm_l5;
                F.ret = t17;
                goto gm_fret;
            }
            void * t18 = gm_l0;
            void * t19 = gm_l0;
            void * t20 = gm_l4;
            gm_t88rcucg1b7uk t21 = gm_l2;
            gm_tkn42u7na3t81 t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t22), t21);
            ((uint8_t *)&t22)[4] = 1;
            void * t23 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t19, t20, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t24 = gm_l2;
            void * t25 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t18, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t25;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)t5) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
            if (!t27) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&F.l6), (((uint8_t *)t5) + 8), 40);
            void * t28 = gm_l0;
            void * t29 = (void *)((uint8_t *)&F.l6);
            void * t30 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t28, t29, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t30;
            void * t31 = gm_l0;
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t31) + 744));
            if (t32) {
                void * t33 = gm_l0;
                void * t34 = gm_l0;
                uint8_t t35 = (uint8_t)(((((uint8_t *)t34) + 748))[4] != 0);
                if (t35) {
                    gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t34) + 748));
                    gm_l8 = t36;
                } else {
                    void * t37 = gm_l0;
                    void * t38 = GM_LD(void *, ((uint8_t *)t37));
                    void * t39 = (void *)(((uint8_t *)t38) + 8);
                    gm_t88rcucg1b7uk t40 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t39);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l8 = t40;
                }
                gm_t88rcucg1b7uk t41 = gm_l8;
                GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)t33) + 168), t41);
                void * t42 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t42) + 744), ((uint8_t)0));
            }
            void * t43 = gm_l0;
            void * t44 = gm_l7;
            void * t45 = (void *)((uint8_t *)&F.l6);
            void * t46 = gm_f_finish_block_body_4vol3tv((void *)NULL, cx, NULL, t43, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t46;
            goto gm_fret;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 187, 5);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_literal_params_n5511im(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_literal_params_n5511im F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lit */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tkn42u7na3t81 gm_l7; /* annotated */
    void * gm_l8; /* t */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_t08olq97rscn3 gm_l11; /* n */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* ty */
    gm_t88rcucg1b7uk gm_l14; /* t */
    GM_STACK_CHECK(&F, 950u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_tabb710d9mq2e gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_trqfvmjtt4sij t20;
    gm_f_list_af74s0q((void *)&t20, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t20;
    void * t21 = gm_l1;
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)t21));
    gm_l4 = t22;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t23 = gm_l5;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t25 = (t23 < t24);
            if (!t25) {
                goto gm_lb1;
            }
            {
                void * t26 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t28 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t26) + (uintptr_t)t28 * 56), 56);
                        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 16));
                        if (t29) {
                            void * t30 = gm_l0;
                            gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l6) + 40));
                            gm_b16_8 t32;
                            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_skfniirr0r2gc));
                            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(62));
                            gm_b0_1 t33 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t30, t31, t32);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        gm_tkn42u7na3t81 t34;
                        memset(&t34, 0, sizeof t34);
                        gm_l7 = t34;
                        {
                            {
                                uint8_t t35 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l6) + 24)) != 0);
                                if (!t35) {
                                    goto gm_lb7;
                                }
                                void * t36 = GM_LD(void *, (((uint8_t *)&F.l6) + 24));
                                gm_l8 = t36;
                                void * t37 = gm_l0;
                                void * t38 = gm_l8;
                                gm_t88rcucg1b7uk t39 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t37, t38);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tkn42u7na3t81 t40;
                                GM_ST(uint8_t, (uint8_t *)&t40 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t40 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t40), t39);
                                ((uint8_t *)&t40)[4] = 1;
                                gm_l7 = t40;
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                        gm_l9 = t41;
                        gm_l10 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t42 = gm_l10;
                                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                uint8_t t44 = (t42 < t43);
                                if (!t44) {
                                    goto gm_lb8;
                                }
                                {
                                    void * t45 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                    uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                    uint64_t t47 = gm_l10;
                                    {
                                        {
                                            memmove(((uint8_t *)&gm_l11), (((uint8_t *)t45) + (uintptr_t)t47 * 32), 32);
                                            uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                            gm_l12 = t48;
                                            void * t49 = gm_l0;
                                            void * t50 = GM_LD(void *, ((uint8_t *)t49));
                                            void * t51 = (void *)(((uint8_t *)t50) + 8);
                                            gm_t88rcucg1b7uk t52 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t51);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l13 = t52;
                                            {
                                                {
                                                    uint8_t t53 = (uint8_t)((((uint8_t *)&gm_l7))[4] != 0);
                                                    if (!t53) {
                                                        goto gm_lb14;
                                                    }
                                                    gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l7));
                                                    gm_l14 = t54;
                                                    gm_t88rcucg1b7uk t55 = gm_l14;
                                                    gm_l13 = t55;
                                                    goto gm_lb13;
                                                }
gm_lb14:;
                                                {
                                                    {
                                                        uint8_t t56 = (uint8_t)((((uint8_t *)&F.l2))[48] != 0);
                                                        if (!t56) {
                                                            goto gm_lb16;
                                                        }
                                                        memmove(((uint8_t *)&F.l15), ((uint8_t *)&F.l2), 48);
                                                        uint64_t t57 = gm_l12;
                                                        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
                                                        uint8_t t59 = (t57 < t58);
                                                        if (!t59) {
                                                            goto gm_lb16;
                                                        }
                                                        void * t60 = gm_l0;
                                                        void * t61 = GM_LD(void *, ((uint8_t *)t60));
                                                        void * t62 = (void *)(((uint8_t *)t61) + 8);
                                                        void * t63 = GM_LD(void *, ((uint8_t *)&F.l15));
                                                        uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
                                                        uint64_t t65 = gm_l12;
                                                        gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t63) + (uintptr_t)t65 * 4));
                                                        uint8_t t67 = gm_f_has_param_u9t0efs(t62, GM_CONV(gm_csguankko4lmhd, t66));
                                                        uint8_t t68 = (uint8_t)!t67;
                                                        if (!t68) {
                                                            goto gm_lb16;
                                                        }
                                                        void * t69 = GM_LD(void *, ((uint8_t *)&F.l15));
                                                        uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
                                                        uint64_t t71 = gm_l12;
                                                        gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t69) + (uintptr_t)t71 * 4));
                                                        gm_l13 = t72;
                                                        goto gm_lb15;
                                                    }
gm_lb16:;
                                                    void * t73 = gm_l0;
                                                    gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l11) + 16));
                                                    gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                                                    gm_tnpifv20irnph t76;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t76 + 0), t75);
                                                    gm_f_format_vrv37j5((void *)&gm_l17, (void *)NULL, cx, NULL, t76);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                                    if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcl43af23eher3, 174, 34);
                                                    gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                                                    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                                                    gm_b0_1 t80 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t73, t74, t79);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
gm_lb15:;
                                            }
gm_lb13:;
                                            void * t81 = (void *)((uint8_t *)&F.l3);
                                            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                                            gm_t88rcucg1b7uk t83 = gm_l13;
                                            gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l11) + 16));
                                            gm_t9805qtonbsag t85;
                                            memset(&t85, 0, sizeof t85);
                                            gm_t5v0c8ia6a5mo t86;
                                            GM_ST(uint8_t, (uint8_t *)&t86 + 49, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t86 + 50, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t86 + 52, 0);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t82);
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t86 + 16), t83);
                                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t86 + 20), t84);
                                            GM_ST(void *, ((uint8_t *)&t86 + 32), ((void *)0));
                                            GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t86 + 40), t85);
                                            GM_ST(uint8_t, ((uint8_t *)&t86 + 48), ((uint8_t)0));
                                            gm_tftb1odc739t5 t87 = gm_f_push_o8u01sh(t81, &t86);
                                            gm_l18 = t87;
                                            uint8_t t88 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                            if (!t88) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcl43af23eher3, 176, 13);
                                            goto gm_lb11;
                                        }
gm_lb12:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 164, 9);
                                    }
gm_lb11:;
                                }
gm_lb10:;
                                uint64_t t89 = gm_l10;
                                uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                                uint64_t t92 = r90;
                                gm_l10 = t92;
                            }
                        }
gm_lb8:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 160, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t93 = gm_l5;
            uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
            uint64_t t96 = r94;
            gm_l5 = t96;
        }
    }
gm_lb1:;
    {
        {
            uint8_t t97 = (uint8_t)((((uint8_t *)&F.l2))[48] != 0);
            if (!t97) {
                goto gm_lb18;
            }
            memmove(((uint8_t *)&F.l16), ((uint8_t *)&F.l2), 48);
            uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
            uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
            uint8_t t100 = (!(t98 == t99));
            if (!t100) {
                goto gm_lb18;
            }
            void * t101 = gm_l0;
            void * t102 = gm_l1;
            gm_tl874t2atnlh3 t103 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t102) + 56));
            uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
            uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
            gm_t2lqu8jsj511d t106;
            GM_ST(uint64_t, ((uint8_t *)&t106 + 0), t104);
            GM_ST(uint64_t, ((uint8_t *)&t106 + 8), t105);
            gm_f_format_9lchiud((void *)&gm_l19, (void *)NULL, cx, NULL, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srcl43af23eher3, 180, 32);
            gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
            gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
            gm_b0_1 t110 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t101, t103, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb17;
        }
gm_lb18:;
    }
gm_lb17:;
    gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    F.ret = t111;
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
__attribute__((aligned(4))) void gm_f_nested_fn_binding_0cl95ol(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_nested_fn_binding_0cl95ol F;
    void * gm_l0; /* cx */
    gm_t08olq97rscn3 gm_l1; /* name */
    void * gm_l2; /* lit */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* v */
    uint32_t gm_l5; /* id */
    void * gm_l6; /* scope */
    uint64_t gm_l7; /* j */
    uint32_t gm_l8; /* f */
    GM_STACK_CHECK(&F, 960u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l9;
    void * t9 = gm_l2;
    uint8_t t10 = gm_f_is_generic_jrl2kf9(t9);
    if (t10) {
        void * t11 = gm_l0;
        void * t12 = gm_l2;
        gm_tl874t2atnlh3 t13 = gm_l3;
        gm_t08olq97rscn3 t14 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a1));
        gm_f_nested_generic_fn_q4qqrgj((void *)&(*(gm_trcmmd12cefb1 *)gm_out), (void *)NULL, cx, NULL, t11, &t14, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    void * t15 = gm_l0;
    void * t16 = gm_l2;
    gm_tkn42u7na3t81 t17;
    memset(&t17, 0, sizeof t17);
    gm_t08olq97rscn3 t18 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a1));
    gm_t1s75i08qtptd t19;
    GM_ST(uint8_t, (uint8_t *)&t19 + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&t19 + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&t19 + 36, 0);
    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t19), t18);
    ((uint8_t *)&t19)[32] = 1;
    gm_tl874t2atnlh3 t20 = gm_l3;
    void * t21 = gm_f_fn_literal_fiu78lb((void *)NULL, cx, NULL, t15, t16, t17, &t19, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t21;
    {
        {
            void * t22 = gm_l4;
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)27ULL));
            if (!t24) {
                goto gm_lb2;
            }
            void * t25 = gm_l0;
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)a1));
            void * t27 = gm_l4;
            gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t27));
            gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
            uint32_t t30 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t25, t26, t28, ((uint8_t)0), t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t30;
            void * t31 = gm_l0;
            void * t32 = GM_LD(void *, (((uint8_t *)t31) + 56));
            uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t31) + 56) + 8));
            void * t34 = gm_l0;
            uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)t34) + 56) + 8));
            uint64_t r36; bool o37 = __builtin_sub_overflow(t35, ((uint64_t)1ULL), &r36);
            if (o37) gm_trap("integer overflow in `-`", gm_srcl43af23eher3, 319, 35);
            uint64_t t38 = r36;
            if ((uintptr_t)t38 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t38, t33, gm_srcl43af23eher3, 319, 19);
            void * t39 = (void *)(((uint8_t *)t32) + (uintptr_t)t38 * 40);
            gm_l6 = t39;
            void * t40 = gm_l6;
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)t40) + 8));
            gm_l7 = t41;
            {
                for (;;) {
                    uint64_t t42 = gm_l7;
                    uint8_t t43 = (t42 > ((uint64_t)0ULL));
                    if (!t43) {
                        goto gm_lb3;
                    }
                    {
                        uint64_t t44 = gm_l7;
                        uint64_t r45; bool o46 = __builtin_sub_overflow(t44, ((uint64_t)1ULL), &r45);
                        if (o46) gm_trap("integer overflow in `-`", gm_srcl43af23eher3, 322, 13);
                        uint64_t t47 = r45;
                        gm_l7 = t47;
                        void * t48 = gm_l6;
                        void * t49 = GM_LD(void *, ((uint8_t *)t48));
                        uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)t48) + 8));
                        uint64_t t51 = gm_l7;
                        if ((uintptr_t)t51 >= (uintptr_t)(t50)) gm_trap_index((uintptr_t)t51, t50, gm_srcl43af23eher3, 323, 16);
                        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)t49) + (uintptr_t)t51 * 224));
                        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)a1));
                        uint8_t t54 = (gm_eq0iqiubfm7e(((uint8_t *)&t52), ((uint8_t *)&t53)));
                        if (t54) {
                            void * t55 = gm_l6;
                            void * t56 = GM_LD(void *, ((uint8_t *)t55));
                            uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t55) + 8));
                            uint64_t t58 = gm_l7;
                            if ((uintptr_t)t58 >= (uintptr_t)(t57)) gm_trap_index((uintptr_t)t58, t57, gm_srcl43af23eher3, 324, 17);
                            uint32_t t59 = gm_l5;
                            gm_t930q5l38hdap t60;
                            memset(&t60, 0, sizeof t60);
                            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)0ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t60 + 8), t59);
                            GM_ST(gm_t930q5l38hdap, ((((uint8_t *)t56) + (uintptr_t)t58 * 224) + 16), t60);
                            goto gm_lb3;
                        }
                    }
gm_lb5:;
                }
            }
gm_lb3:;
            uint32_t t61 = gm_l5;
            void * t62 = gm_l4;
            gm_ttggq7ng4kp3p t63;
            GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t63 + 0), t61);
            GM_ST(void *, ((uint8_t *)&t63 + 8), t62);
            gm_trcmmd12cefb1 t64;
            GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t64 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)0ULL));
            GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t64 + 8), t63);
            memmove(gm_out, ((uint8_t *)&t64), 32);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t65 = gm_l4;
            uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t65) + 8) + 0));
            uint8_t t67 = (uint8_t)(t66 == ((uint8_t)26ULL));
            if (!t67) {
                goto gm_lb7;
            }
            uint32_t t68 = GM_LD(uint32_t, ((((uint8_t *)t65) + 8) + 8));
            gm_l8 = t68;
            void * t69 = gm_l0;
            void * t70 = GM_LD(void *, ((uint8_t *)t69));
            void * t71 = (void *)(((uint8_t *)t70) + 1728);
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)a1));
            uint32_t t73 = gm_l8;
            gm_tommf4j9upomc t74;
            GM_ST(uint32_t, (uint8_t *)&t74 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t74 + 0), t72);
            GM_ST(uint32_t, ((uint8_t *)&t74 + 16), t73);
            gm_tftb1odc739t5 t75 = gm_f_push_p9c61k8(t71, &t74);
            gm_l9 = t75;
            uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcl43af23eher3, 330, 30);
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    void * t77 = gm_l0;
    void * t78 = GM_LD(void *, ((uint8_t *)t77));
    gm_tl874t2atnlh3 t79 = gm_l3;
    void * t80 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t78, t79);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trcmmd12cefb1 t81;
    GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t81 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t81 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)4ULL));
    GM_ST(void *, ((uint8_t *)&t81 + 8), t80);
    memmove(gm_out, ((uint8_t *)&t81), 32);
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
inline __attribute__((aligned(4))) void gm_f_nested_generic_fn_q4qqrgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_nested_generic_fn_q4qqrgj F;
    void * gm_l0; /* cx */
    gm_t08olq97rscn3 gm_l1; /* name */
    void * gm_l2; /* lit */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* f */
    GM_STACK_CHECK(&F, 771u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t6qooom8qa2do gm_l5;
    gm_tftb1odc739t5 gm_l6;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            void * t6 = gm_l0;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 8));
            void * t8 = gm_l2;
            gm_t08olq97rscn3 t9 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a1));
            gm_f_declare_fn_6piolnr((void *)&gm_l5, (void *)NULL, cx, NULL, t5, t7, &t9, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)5ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
            gm_l4 = t12;
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, ((uint8_t *)t13));
            void * t15 = (void *)(((uint8_t *)t14) + 2392);
            uint32_t t16 = gm_l4;
            gm_t08olq97rscn3 t17 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a1));
            gm_tp9ftj4p31ha1 t18;
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t18 + 0), t16);
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t18 + 8), t17);
            gm_tftb1odc739t5 t19 = gm_f_push_bu2hcel(t15, &t18);
            gm_l6 = t19;
            uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcl43af23eher3, 305, 9);
            void * t21 = gm_l0;
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)a1));
            uint32_t t23 = gm_l4;
            gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
            gm_t930q5l38hdap t25;
            memset(&t25, 0, sizeof t25);
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)3ULL));
            GM_ST(uint32_t, ((uint8_t *)&t25 + 8), t23);
            gm_b0_1 t26 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t21, t22, &t25, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t27 = gm_l0;
    void * t28 = GM_LD(void *, ((uint8_t *)t27));
    gm_tl874t2atnlh3 t29 = gm_l3;
    void * t30 = gm_f_unit_ohk62mc((void *)NULL, cx, NULL, t28, t29);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trcmmd12cefb1 t31;
    GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t31 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)4ULL));
    GM_ST(void *, ((uint8_t *)&t31 + 8), t30);
    memmove(gm_out, ((uint8_t *)&t31), 32);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_recursive_without_ret_48l31rm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_reject_returned_closure_1ouocor(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_reject_returned_closure_1ouocor F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lit */
    gm_t88rcucg1b7uk gm_l2; /* final_ret */
    gm_tl874t2atnlh3 gm_l3; /* at */
    void * gm_l4; /* r */
    void * gm_l5; /* e */
    GM_STACK_CHECK(&F, 303u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l6;
    gm_t53at80pfdus3 gm_l7;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    void * t11 = (void *)(((uint8_t *)t10) + 8);
    void * t12 = gm_l0;
    void * t13 = (void *)(((uint8_t *)t12) + 96);
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    gm_t88rcucg1b7uk t16 = gm_l2;
    gm_t88rcucg1b7uk t17 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t13, t15, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_kind_jcnt4hm((void *)&gm_l7, t11, GM_CONV(gm_csguankko4lmhd, t17));
    {
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)15ULL));
            if (!t19) {
                goto gm_lb2;
            }
            gm_l6 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l6 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 210, 8);
    }
gm_lb1:;
    uint8_t t20 = gm_l6;
    if (t20) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t21 = gm_l1;
    gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 56));
    gm_l3 = t22;
    {
        {
            void * t23 = gm_l1;
            uint8_t t24 = (uint8_t)(GM_LD(void *, (((uint8_t *)t23) + 24)) != 0);
            if (!t24) {
                goto gm_lb5;
            }
            void * t25 = GM_LD(void *, (((uint8_t *)t23) + 24));
            gm_l4 = t25;
            void * t26 = gm_l4;
            gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t26) + 72));
            gm_l3 = t27;
            goto gm_lb4;
        }
gm_lb5:;
        {
            {
                void * t28 = gm_l1;
                uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t28) + 88) + 0));
                uint8_t t30 = (uint8_t)(t29 == ((uint8_t)1ULL));
                if (!t30) {
                    goto gm_lb7;
                }
                void * t31 = GM_LD(void *, ((((uint8_t *)t28) + 88) + 8));
                gm_l5 = t31;
                void * t32 = gm_l5;
                gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 72));
                gm_l3 = t33;
                goto gm_lb6;
            }
gm_lb7:;
        }
gm_lb6:;
    }
gm_lb4:;
    void * t34 = gm_l0;
    gm_tl874t2atnlh3 t35 = gm_l3;
    gm_b16_8 t36;
    GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sdsd71bjtv81c));
    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(97));
    gm_b0_1 t37 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t34, t35, t36);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_route_performed_pruu3np(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3, uint8_t a4) {
    struct gm_fr_route_performed_pruu3np F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* performed */
    void * gm_l2; /* effects */
    uint8_t gm_l3; /* pointer_literal */
    uint8_t gm_l4; /* undeclared_is_error */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 509u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_th2lqbo77pvsc gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_trqfvmjtt4sij t7;
    gm_f_list_084fcd6((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t7;
    gm_b16_8 t8 = gm_l1;
    gm_l6 = t8;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l7;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t14 = gm_l7;
                {
                    {
                        memmove(((uint8_t *)&F.l8), (((uint8_t *)t12) + (uintptr_t)t14 * 40), 40);
                        void * t15 = gm_l2;
                        void * t16 = (void *)((uint8_t *)&F.l8);
                        uint8_t t17 = gm_f_set_contains_8v9ep9m(t15, t16);
                        if (t17) {
                            goto gm_lb3;
                        }
                        uint8_t t18 = gm_l3;
                        if (t18) {
                            void * t19 = gm_l0;
                            gm_tl874t2atnlh3 t20 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 24));
                            void * t21 = gm_l0;
                            void * t22 = GM_LD(void *, ((uint8_t *)t21));
                            void * t23 = (void *)(((uint8_t *)t22) + 8);
                            void * t24 = (void *)((uint8_t *)&F.l8);
                            gm_b16_8 t25 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t23, t24);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t26;
                            GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
                            gm_f_format_26002qo((void *)&gm_l9, (void *)NULL, cx, NULL, t26);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                            if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcl43af23eher3, 228, 30);
                            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                            gm_b0_1 t30 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t19, t20, t29);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb3;
                        }
                        void * t31 = (void *)((uint8_t *)&F.l5);
                        gm_tftb1odc739t5 t32 = gm_f_push_d1r4h19(t31, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&F.l8)));
                        gm_l10 = t32;
                        uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcl43af23eher3, 231, 9);
                        uint8_t t34 = gm_l4;
                        if (t34) {
                            void * t35 = gm_l0;
                            gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 24));
                            void * t37 = gm_l0;
                            void * t38 = (void *)((uint8_t *)&F.l8);
                            gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 24));
                            void * t40 = gm_l2;
                            gm_f_undeclared_message_b14iouu((void *)&gm_l11, (void *)NULL, cx, NULL, t37, t38, t39, t40);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                            gm_b0_1 t42 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t35, t36, t41);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        } else {
                            void * t43 = gm_l0;
                            void * t44 = (void *)(((uint8_t *)t43) + 192);
                            gm_tftb1odc739t5 t45 = gm_f_push_3onuaru(t44, &(*(gm_tj44oivvogvtl *)((uint8_t *)&F.l8)));
                            gm_l12 = t45;
                            uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                            if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcl43af23eher3, 235, 13);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcl43af23eher3, 225, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t47 = gm_l7;
            uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
            uint64_t t50 = r48;
            gm_l7 = t50;
        }
    }
gm_lb1:;
    gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    F.ret = t51;
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
