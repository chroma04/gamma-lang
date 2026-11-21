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
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_b16_8 f3; } gm_t26d2uc07m5o2;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tj2m9dbh1ekv6 f0; uint8_t f1; uint8_t pe[3]; } gm_t46lmulm8gaj3;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; gm_tl874t2atnlh3 f3; } gm_tis7ttivmj935;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t f3; uint8_t pe[3]; } gm_tkrkmsnb6m1b7;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tllos8achgacq;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t f3; uint8_t pe[2]; } gm_tppdoi1slt2pr;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint32_t f2; uint8_t f3; uint8_t pe[7]; } gm_tpvqh65i5mn94;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_tl874t2atnlh3 f2; gm_b16_8 f3; } gm_tt384v6n8ojib;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_tt7dbdq4n1egc;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t26d2uc07m5o2) == 48 && _Alignof(gm_t26d2uc07m5o2) == 8 && offsetof(gm_t26d2uc07m5o2, f0) == 0 && offsetof(gm_t26d2uc07m5o2, f1) == 16 && offsetof(gm_t26d2uc07m5o2, f2) == 28 && offsetof(gm_t26d2uc07m5o2, f3) == 32, "layout of `(name: string, span: Span, vis: Visibility, what: string)`");
_Static_assert(sizeof(gm_t46lmulm8gaj3) == 12 && _Alignof(gm_t46lmulm8gaj3) == 4 && offsetof(gm_t46lmulm8gaj3, f0) == 0 && offsetof(gm_t46lmulm8gaj3, f1) == 8, "layout of `?IntSuffix`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tis7ttivmj935) == 56 && _Alignof(gm_tis7ttivmj935) == 8 && offsetof(gm_tis7ttivmj935, f0) == 0 && offsetof(gm_tis7ttivmj935, f1) == 16 && offsetof(gm_tis7ttivmj935, f2) == 32 && offsetof(gm_tis7ttivmj935, f3) == 44, "layout of `(span: Span, name: string, local: Span, origin: Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tppdoi1slt2pr v0; gm_t08olq97rscn3 v1; gm_tt384v6n8ojib v2; gm_tt7dbdq4n1egc v3; gm_toceffb697p0u v4; gm_tl874t2atnlh3 v5; gm_t26d2uc07m5o2 v6; gm_tllos8achgacq v7; gm_tkrkmsnb6m1b7 v8; gm_tis7ttivmj935 v9; gm_tis7ttivmj935 v10; } f1; } gm_tj5f8vajf35o7;
_Static_assert(sizeof(gm_tkrkmsnb6m1b7) == 48 && _Alignof(gm_tkrkmsnb6m1b7) == 8 && offsetof(gm_tkrkmsnb6m1b7, f0) == 0 && offsetof(gm_tkrkmsnb6m1b7, f1) == 16 && offsetof(gm_tkrkmsnb6m1b7, f2) == 32 && offsetof(gm_tkrkmsnb6m1b7, f3) == 44, "layout of `(span: Span, name: string, catch_all: Span, is_result: bool)`");
_Static_assert(sizeof(gm_tllos8achgacq) == 64 && _Alignof(gm_tllos8achgacq) == 8 && offsetof(gm_tllos8achgacq, f0) == 0 && offsetof(gm_tllos8achgacq, f1) == 16 && offsetof(gm_tllos8achgacq, f2) == 32 && offsetof(gm_tllos8achgacq, f3) == 48, "layout of `(span: Span, name: string, local: Span, why: string)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tppdoi1slt2pr) == 32 && _Alignof(gm_tppdoi1slt2pr) == 8 && offsetof(gm_tppdoi1slt2pr, f0) == 0 && offsetof(gm_tppdoi1slt2pr, f1) == 16 && offsetof(gm_tppdoi1slt2pr, f2) == 28 && offsetof(gm_tppdoi1slt2pr, f3) == 29, "layout of `(name: string, span: Span, param: bool, assigned: bool)`");
_Static_assert(sizeof(gm_tpvqh65i5mn94) == 40 && _Alignof(gm_tpvqh65i5mn94) == 8 && offsetof(gm_tpvqh65i5mn94, f0) == 0 && offsetof(gm_tpvqh65i5mn94, f1) == 16 && offsetof(gm_tpvqh65i5mn94, f2) == 28 && offsetof(gm_tpvqh65i5mn94, f3) == 32, "layout of `PresentBinding`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tt384v6n8ojib) == 56 && _Alignof(gm_tt384v6n8ojib) == 8 && offsetof(gm_tt384v6n8ojib, f0) == 0 && offsetof(gm_tt384v6n8ojib, f1) == 16 && offsetof(gm_tt384v6n8ojib, f2) == 28 && offsetof(gm_tt384v6n8ojib, f3) == 40, "layout of `(name: string, span: Span, item: Span, what: string)`");
_Static_assert(sizeof(gm_tt7dbdq4n1egc) == 28 && _Alignof(gm_tt7dbdq4n1egc) == 4 && offsetof(gm_tt7dbdq4n1egc, f0) == 0 && offsetof(gm_tt7dbdq4n1egc, f1) == 12 && offsetof(gm_tt7dbdq4n1egc, f2) == 24, "layout of `(span: Span, operand: Span, same: bool)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tj5f8vajf35o7) == 72 && _Alignof(gm_tj5f8vajf35o7) == 8 && offsetof(gm_tj5f8vajf35o7, f0) == 0 && offsetof(gm_tj5f8vajf35o7, f1) == 8, "layout of `Finding`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
struct gm_fr_empty_slice_behvl9q {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ko2vag0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mcu2r7e_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_catch_all_mouipl8 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_empty_suloehk {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_6canco6_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_cugp4sg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_names_absent_below_61ta3qt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_names_absent_fajuepv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_pop_d9v3irs {
    gm_frame h;
    gm_t46lmulm8gaj3 ret;
};
struct gm_fr_present_bindings_j3olvoa {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_present_only_bindings_ug2i6dq {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* found */
    gm_trqfvmjtt4sij l12; /* path */
    gm_tpvqh65i5mn94 l15; /* b */
    gm_b0_1 ret;
};
struct gm_fr_push_f21bjke_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rqlbv1e {
    gm_frame h;
    gm_tj5f8vajf35o7 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_tvkoabt {
    gm_frame h;
    gm_tpvqh65i5mn94 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2rchp81_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3n5udep {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_l10rc7v {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
__attribute__((aligned(4))) void gm_f_dupe_4ml4aot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_behvl9q(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ko2vag0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mcu2r7e_2(void);
__attribute__((aligned(4))) uint8_t gm_f_is_catch_all_mouipl8(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_empty_suloehk(void * a0);
inline __attribute__((aligned(4))) void gm_f_list_6canco6_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_cugp4sg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_names_absent_below_61ta3qt(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_names_absent_fajuepv(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t46lmulm8gaj3 gm_f_pop_d9v3irs(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_present_bindings_j3olvoa(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_present_only_bindings_ug2i6dq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f21bjke_2(void * a0, gm_tj2m9dbh1ekv6 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rqlbv1e(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_tvkoabt(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2rchp81_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3n5udep(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_l10rc7v(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
extern uint8_t gm_k5u3oseesd7[40] __attribute__((aligned(8)));
extern const char gm_src27ouot20tc5m[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_behvl9q(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_behvl9q F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ko2vag0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ko2vag0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mcu2r7e_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mcu2r7e_2 F;
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
inline __attribute__((aligned(4))) void gm_f_list_6canco6_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6canco6_2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_mcu2r7e_2();
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
inline __attribute__((aligned(4))) void gm_f_list_cugp4sg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_cugp4sg F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ko2vag0();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t46lmulm8gaj3 gm_f_pop_d9v3irs(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_d9v3irs F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_tj2m9dbh1ekv6 gm_l2; /* x */
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
        gm_t46lmulm8gaj3 t6;
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
    gm_tj2m9dbh1ekv6 t14 = GM_LD(gm_tj2m9dbh1ekv6, (((uint8_t *)t8) + (uintptr_t)t13 * 8));
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
    gm_tj2m9dbh1ekv6 t27 = gm_l2;
    gm_t46lmulm8gaj3 t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 10, 0);
    GM_ST(gm_tj2m9dbh1ekv6, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[8] = 1;
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f21bjke_2(void * a0, gm_tj2m9dbh1ekv6 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_f21bjke_2 F;
    void * gm_l0; /* l */
    gm_tj2m9dbh1ekv6 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_mcu2r7e_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_2rchp81_2(t8, ((uint64_t)1ULL), t10);
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
    gm_tj2m9dbh1ekv6 t18 = gm_l1;
    GM_ST(gm_tj2m9dbh1ekv6, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rqlbv1e(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rqlbv1e F;
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
        gm_b16_8 t9 = gm_f_empty_slice_behvl9q();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_3n5udep(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 72), ((uint8_t *)&F.l1), 72);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_tvkoabt(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_tvkoabt F;
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
        gm_b16_8 t9 = gm_f_empty_slice_ko2vag0();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_l10rc7v(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2rchp81_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2rchp81_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3n5udep(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3n5udep F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_l10rc7v(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_l10rc7v F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_catch_all_mouipl8(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_catch_all_mouipl8 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* alts */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    void * gm_l4; /* a */
    GM_STACK_CHECK(&F, 178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t14 = gm_l0;
    {
        {
            {
                {
                    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
                    uint8_t t16 = (uint8_t)(t15 == ((uint8_t)0ULL));
                    if (!t16) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)1ULL));
                    if (!t18) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)10ULL));
            if (!t20) {
                goto gm_lb6;
            }
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((((uint8_t *)t14) + 8) + 8));
            gm_l1 = t21;
            gm_b16_8 t22 = gm_l1;
            gm_l2 = t22;
            gm_l3 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t23 = gm_l3;
                    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint8_t t25 = (t23 < t24);
                    if (!t25) {
                        goto gm_lb7;
                    }
                    {
                        void * t26 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t28 = gm_l3;
                        {
                            {
                                void * t29 = GM_LD(void *, (((uint8_t *)t26) + (uintptr_t)t28 * 8));
                                gm_l4 = t29;
                                void * t30 = gm_l4;
                                uint8_t t31 = gm_f_is_catch_all_mouipl8(t30);
                                if (t31) {
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto gm_lb10;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 72, 13);
                        }
gm_lb10:;
                    }
gm_lb9:;
                    uint64_t t32 = gm_l3;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l3 = t35;
                }
            }
gm_lb7:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb6:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 69, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_suloehk(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_empty_suloehk F;
    void * gm_l0; /* e */
    void * gm_l1; /* b */
    void * gm_l2; /* t */
    gm_b16_8 gm_l3; /* xs */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t8 = gm_l0;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)46ULL));
            if (!t10) {
                goto gm_lb2;
            }
            void * t11 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
            gm_l1 = t11;
            void * t12 = gm_l1;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
            uint8_t t14 = (t13 > ((uint64_t)0ULL));
            if (t14) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            {
                {
                    void * t15 = gm_l1;
                    uint8_t t16 = (uint8_t)(GM_LD(void *, (((uint8_t *)t15) + 16)) != 0);
                    if (!t16) {
                        goto gm_lb4;
                    }
                    void * t17 = GM_LD(void *, (((uint8_t *)t15) + 16));
                    gm_l2 = t17;
                    void * t18 = gm_l2;
                    uint8_t t19 = gm_f_is_empty_suloehk(t18);
                    F.ret = t19;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)35ULL));
            if (!t21) {
                goto gm_lb5;
            }
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)t8) + 8) + 8));
            gm_l3 = t22;
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t24 = ((t23 == ((uint64_t)0ULL)));
            F.ret = t24;
            goto gm_fret;
        }
gm_lb5:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 57, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_names_absent_below_61ta3qt(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_names_absent_below_61ta3qt F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* path */
    GM_STACK_CHECK(&F, 299u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    uint8_t gm_l3;
    void * t5 = gm_l0;
    uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
    uint8_t t7 = (uint8_t)((t6 == ((uint8_t)0ULL)));
    gm_tb38v5s9j1r32 t8 = GM_LD(gm_tb38v5s9j1r32, (((uint8_t *)t5) + 8));
    gm_tb38v5s9j1r32 t9;
    memset(&t9, 0, sizeof t9);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)0ULL));
    if (t7) {
        gm_l2 = ((uint8_t)1);
    } else {
        void * t10 = gm_l0;
        {
            {
                uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
                uint8_t t12 = (uint8_t)(t11 == ((uint8_t)1ULL));
                if (!t12) {
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
            gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 165, 27);
        }
gm_lb1:;
        uint8_t t13 = gm_l3;
        gm_l2 = t13;
    }
    uint8_t t14 = gm_l2;
    if (t14) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    void * t15 = gm_l0;
    gm_b16_8 t16 = gm_l1;
    uint8_t t17 = gm_f_names_absent_fajuepv(t15, t16);
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_names_absent_fajuepv(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_names_absent_fajuepv F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* path */
    gm_tj2m9dbh1ekv6 gm_l2; /* step */
    gm_b16_8 gm_l3; /* rest */
    gm_b16_8 gm_l4; /* alts */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    void * gm_l7; /* q */
    uint32_t gm_l8; /* v */
    uint32_t gm_l9; /* variant */
    void * gm_l10; /* payload */
    void * gm_l11; /* q */
    uint32_t gm_l12; /* k */
    gm_b16_8 gm_l13; /* fs */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l16; /* f */
    void * gm_l17; /* q */
    void * gm_l18; /* q */
    void * gm_l19; /* q */
    GM_STACK_CHECK(&F, 1532u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l20;
    uint8_t gm_l21;
    uint8_t gm_l22;
    uint8_t gm_l23;
    uint8_t gm_l24;
    uint8_t gm_l25;
    uint8_t gm_l26;
    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t41 = ((t40 == ((uint64_t)0ULL)));
    if (t41) {
        void * t42 = gm_l0;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
        uint8_t t44 = (uint8_t)((t43 == ((uint8_t)4ULL)));
        gm_tb38v5s9j1r32 t45 = GM_LD(gm_tb38v5s9j1r32, (((uint8_t *)t42) + 8));
        gm_tb38v5s9j1r32 t46;
        memset(&t46, 0, sizeof t46);
        GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)4ULL));
        if (t44) {
            gm_l22 = ((uint8_t)1);
        } else {
            void * t47 = gm_l0;
            {
                {
                    uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t47) + 8) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)7ULL));
                    if (!t49) {
                        goto gm_lb2;
                    }
                    gm_l23 = ((uint8_t)1);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    gm_l23 = ((uint8_t)0);
                    goto gm_lb1;
                }
gm_lb3:;
                gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 123, 50);
            }
gm_lb1:;
            uint8_t t50 = gm_l23;
            gm_l22 = t50;
        }
        uint8_t t51 = gm_l22;
        if (t51) {
            gm_l21 = ((uint8_t)1);
        } else {
            void * t52 = gm_l0;
            uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t52) + 8) + 0));
            uint8_t t54 = (uint8_t)((t53 == ((uint8_t)0ULL)));
            gm_tb38v5s9j1r32 t55 = GM_LD(gm_tb38v5s9j1r32, (((uint8_t *)t52) + 8));
            gm_tb38v5s9j1r32 t56;
            memset(&t56, 0, sizeof t56);
            GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)0ULL));
            gm_l21 = t54;
        }
        uint8_t t57 = gm_l21;
        if (t57) {
            gm_l20 = ((uint8_t)1);
        } else {
            void * t58 = gm_l0;
            {
                {
                    uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t58) + 8) + 0));
                    uint8_t t60 = (uint8_t)(t59 == ((uint8_t)1ULL));
                    if (!t60) {
                        goto gm_lb5;
                    }
                    gm_l24 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    gm_l24 = ((uint8_t)0);
                    goto gm_lb4;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 123, 90);
            }
gm_lb4:;
            uint8_t t61 = gm_l24;
            gm_l20 = t61;
        }
        uint8_t t62 = gm_l20;
        F.ret = t62;
        goto gm_fret;
    }
    void * t63 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t64)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t64, gm_src27ouot20tc5m, 124, 13);
    gm_tj2m9dbh1ekv6 t65 = GM_LD(gm_tj2m9dbh1ekv6, (((uint8_t *)t63) + (uintptr_t)((uint64_t)0ULL) * 8));
    gm_l2 = t65;
    void * t66 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t68 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t69 = (uintptr_t)t67;
    if (t68 > t69 || t69 > (uintptr_t)t67) gm_trap_slice(t68, t69, t67, gm_src27ouot20tc5m, 125, 13);
    uint64_t t70 = ((uintptr_t)t67) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t71;
    GM_ST(void *, (uint8_t *)&t71, (void *)(((uint8_t *)t66 + (uintptr_t)((uint64_t)1ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(t70));
    gm_l3 = t71;
    {
        {
            void * t72 = gm_l0;
            uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t72) + 8) + 0));
            uint8_t t74 = (uint8_t)(t73 == ((uint8_t)10ULL));
            if (!t74) {
                goto gm_lb8;
            }
            gm_b16_8 t75 = GM_LD(gm_b16_8, ((((uint8_t *)t72) + 8) + 8));
            gm_l4 = t75;
            gm_b16_8 t76 = gm_l4;
            gm_l5 = t76;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t77 = gm_l6;
                    uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t79 = (t77 < t78);
                    if (!t79) {
                        goto gm_lb9;
                    }
                    {
                        void * t80 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t82 = gm_l6;
                        {
                            {
                                void * t83 = GM_LD(void *, (((uint8_t *)t80) + (uintptr_t)t82 * 8));
                                gm_l7 = t83;
                                void * t84 = gm_l7;
                                gm_b16_8 t85 = gm_l1;
                                uint8_t t86 = gm_f_names_absent_fajuepv(t84, t85);
                                if (t86) {
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto gm_lb12;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 127, 9);
                        }
gm_lb12:;
                    }
gm_lb11:;
                    uint64_t t87 = gm_l6;
                    uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                    uint64_t t90 = r88;
                    gm_l6 = t90;
                }
            }
gm_lb9:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb8:;
    }
gm_lb7:;
    void * t91 = gm_l0;
    uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t91) + 8) + 0));
    uint8_t t93 = (uint8_t)((t92 == ((uint8_t)0ULL)));
    gm_tb38v5s9j1r32 t94 = GM_LD(gm_tb38v5s9j1r32, (((uint8_t *)t91) + 8));
    gm_tb38v5s9j1r32 t95;
    memset(&t95, 0, sizeof t95);
    GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)0ULL));
    if (t93) {
        gm_l25 = ((uint8_t)1);
    } else {
        void * t96 = gm_l0;
        {
            {
                uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t96) + 8) + 0));
                uint8_t t98 = (uint8_t)(t97 == ((uint8_t)1ULL));
                if (!t98) {
                    goto gm_lb15;
                }
                gm_l26 = ((uint8_t)1);
                goto gm_lb14;
            }
gm_lb15:;
            {
                gm_l26 = ((uint8_t)0);
                goto gm_lb14;
            }
gm_lb16:;
            gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 132, 27);
        }
gm_lb14:;
        uint8_t t99 = gm_l26;
        gm_l25 = t99;
    }
    uint8_t t100 = gm_l25;
    if (t100) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    {
        {
            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t102 = (uint8_t)(t101 == ((uint8_t)0ULL));
            if (!t102) {
                goto gm_lb18;
            }
            uint32_t t103 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            gm_l8 = t103;
            {
                {
                    void * t104 = gm_l0;
                    uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t104) + 8) + 0));
                    uint8_t t106 = (uint8_t)(t105 == ((uint8_t)8ULL));
                    if (!t106) {
                        goto gm_lb20;
                    }
                    uint32_t t107 = GM_LD(uint32_t, ((((uint8_t *)t104) + 8) + 8));
                    gm_l9 = t107;
                    void * t108 = GM_LD(void *, (((((uint8_t *)t104) + 8) + 8) + 8));
                    gm_l10 = t108;
                    {
                        {
                            uint8_t t109 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l10)) != 0);
                            if (!t109) {
                                goto gm_lb22;
                            }
                            void * t110 = GM_LD(void *, ((uint8_t *)&gm_l10));
                            gm_l11 = t110;
                            uint32_t t111 = gm_l9;
                            uint32_t t112 = gm_l8;
                            uint8_t t113 = ((t111 == t112));
                            if (t113) {
                                void * t114 = gm_l11;
                                gm_b16_8 t115 = gm_l3;
                                uint8_t t116 = gm_f_names_absent_below_61ta3qt(t114, t115);
                                F.ret = t116;
                                goto gm_fret;
                            }
                            goto gm_lb21;
                        }
gm_lb22:;
                    }
gm_lb21:;
                    goto gm_lb19;
                }
gm_lb20:;
            }
gm_lb19:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb18:;
        {
            uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t118 = (uint8_t)(t117 == ((uint8_t)1ULL));
            if (!t118) {
                goto gm_lb23;
            }
            uint32_t t119 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            gm_l12 = t119;
            {
                {
                    void * t120 = gm_l0;
                    uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t120) + 8) + 0));
                    uint8_t t122 = (uint8_t)(t121 == ((uint8_t)9ULL));
                    if (!t122) {
                        goto gm_lb25;
                    }
                    gm_b16_8 t123 = GM_LD(gm_b16_8, ((((uint8_t *)t120) + 8) + 8));
                    gm_l13 = t123;
                    gm_b16_8 t124 = gm_l13;
                    gm_l14 = t124;
                    gm_l15 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t125 = gm_l15;
                            uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                            uint8_t t127 = (t125 < t126);
                            if (!t127) {
                                goto gm_lb26;
                            }
                            {
                                void * t128 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                uint64_t t130 = gm_l15;
                                {
                                    {
                                        gm_ttggq7ng4kp3p t131 = GM_LD(gm_ttggq7ng4kp3p, (((uint8_t *)t128) + (uintptr_t)t130 * 16));
                                        gm_l16 = t131;
                                        uint32_t t132 = GM_LD(uint32_t, ((uint8_t *)&gm_l16));
                                        uint32_t t133 = gm_l12;
                                        uint8_t t134 = ((t132 == t133));
                                        if (t134) {
                                            void * t135 = GM_LD(void *, (((uint8_t *)&gm_l16) + 8));
                                            gm_b16_8 t136 = gm_l3;
                                            uint8_t t137 = gm_f_names_absent_below_61ta3qt(t135, t136);
                                            F.ret = t137;
                                            goto gm_fret;
                                        }
                                        goto gm_lb29;
                                    }
gm_lb30:;
                                    gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 144, 17);
                                }
gm_lb29:;
                            }
gm_lb28:;
                            uint64_t t138 = gm_l15;
                            uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                            uint64_t t141 = r139;
                            gm_l15 = t141;
                        }
                    }
gm_lb26:;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
gm_lb25:;
            }
gm_lb24:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb23:;
        {
            uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t143 = (uint8_t)(t142 == ((uint8_t)2ULL));
            if (!t143) {
                goto gm_lb31;
            }
            {
                {
                    void * t144 = gm_l0;
                    uint8_t t145 = GM_LD(uint8_t, ((((uint8_t *)t144) + 8) + 0));
                    uint8_t t146 = (uint8_t)(t145 == ((uint8_t)5ULL));
                    if (!t146) {
                        goto gm_lb33;
                    }
                    void * t147 = GM_LD(void *, ((((uint8_t *)t144) + 8) + 8));
                    gm_l17 = t147;
                    void * t148 = gm_l17;
                    gm_b16_8 t149 = gm_l3;
                    uint8_t t150 = gm_f_names_absent_below_61ta3qt(t148, t149);
                    F.ret = t150;
                    goto gm_fret;
                }
gm_lb33:;
            }
gm_lb32:;
            {
                {
                    void * t151 = gm_l0;
                    uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)t151) + 8) + 0));
                    uint8_t t153 = (uint8_t)(t152 == ((uint8_t)6ULL));
                    if (!t153) {
                        goto gm_lb35;
                    }
                    void * t154 = GM_LD(void *, ((((uint8_t *)t151) + 8) + 8));
                    gm_l18 = t154;
                    void * t155 = gm_l18;
                    gm_b16_8 t156 = gm_l3;
                    uint8_t t157 = gm_f_names_absent_below_61ta3qt(t155, t156);
                    F.ret = t157;
                    goto gm_fret;
                }
gm_lb35:;
            }
gm_lb34:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb31:;
        {
            uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t159 = (uint8_t)(t158 == ((uint8_t)3ULL));
            if (!t159) {
                goto gm_lb36;
            }
            {
                {
                    void * t160 = gm_l0;
                    uint8_t t161 = GM_LD(uint8_t, ((((uint8_t *)t160) + 8) + 0));
                    uint8_t t162 = (uint8_t)(t161 == ((uint8_t)7ULL));
                    if (!t162) {
                        goto gm_lb38;
                    }
                    void * t163 = GM_LD(void *, ((((uint8_t *)t160) + 8) + 8));
                    gm_l19 = t163;
                    void * t164 = gm_l19;
                    gm_b16_8 t165 = gm_l3;
                    uint8_t t166 = gm_f_names_absent_below_61ta3qt(t164, t165);
                    F.ret = t166;
                    goto gm_fret;
                }
gm_lb38:;
            }
gm_lb37:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb36:;
        gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 133, 5);
    }
gm_lb17:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_present_bindings_j3olvoa(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2, void * a3) {
    struct gm_fr_present_bindings_j3olvoa F;
    void * gm_l0; /* p */
    uint8_t gm_l1; /* top */
    void * gm_l2; /* path */
    void * gm_l3; /* out */
    void * gm_l4; /* q */
    void * gm_l5; /* q */
    uint32_t gm_l6; /* l */
    void * gm_l7; /* q */
    uint32_t gm_l8; /* variant */
    void * gm_l9; /* payload */
    void * gm_l10; /* q */
    gm_b16_8 gm_l11; /* fs */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l14; /* f */
    gm_b16_8 gm_l15; /* alts */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    void * gm_l18; /* q */
    GM_STACK_CHECK(&F, 925u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l19;
    uint8_t gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    void * t29 = gm_l0;
    {
        {
            {
                {
                    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)5ULL));
                    if (!t31) {
                        goto gm_lb4;
                    }
                    void * t32 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
                    gm_l4 = t32;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)6ULL));
                    if (!t34) {
                        goto gm_lb5;
                    }
                    void * t35 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
                    gm_l4 = t35;
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            {
                {
                    void * t36 = gm_l4;
                    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t38 = (uint8_t)(t37 == ((uint8_t)1ULL));
                    if (!t38) {
                        goto gm_lb7;
                    }
                    uint32_t t39 = GM_LD(uint32_t, ((((uint8_t *)t36) + 8) + 8));
                    gm_l6 = t39;
                    uint8_t t40 = gm_l1;
                    uint8_t t41 = (uint8_t)!t40;
                    if (t41) {
                        void * t42 = gm_l3;
                        void * t43 = gm_l2;
                        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)t43));
                        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                        gm_f_dupe_4ml4aot((void *)&gm_l19, (void *)NULL, cx, NULL, t45, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k5u3oseesd7)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                        if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_src27ouot20tc5m, 88, 51);
                        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                        void * t48 = gm_l4;
                        gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t48) + 120));
                        uint32_t t50 = gm_l6;
                        void * t51 = gm_l0;
                        {
                            {
                                uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t51) + 8) + 0));
                                uint8_t t53 = (uint8_t)(t52 == ((uint8_t)6ULL));
                                if (!t53) {
                                    goto gm_lb9;
                                }
                                gm_l20 = ((uint8_t)1);
                                goto gm_lb8;
                            }
gm_lb9:;
                            {
                                gm_l20 = ((uint8_t)0);
                                goto gm_lb8;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 88, 105);
                        }
gm_lb8:;
                        uint8_t t54 = gm_l20;
                        gm_tpvqh65i5mn94 t55;
                        GM_ST(uint8_t, (uint8_t *)&t55 + 33, 0);
                        GM_ST(uint16_t, (uint8_t *)&t55 + 34, 0);
                        GM_ST(uint32_t, (uint8_t *)&t55 + 36, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t47);
                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t55 + 16), t49);
                        GM_ST(uint32_t, ((uint8_t *)&t55 + 28), t50);
                        GM_ST(uint8_t, ((uint8_t *)&t55 + 32), t54);
                        gm_tftb1odc739t5 t56 = gm_f_push_tvkoabt(t42, &t55);
                        gm_l21 = t56;
                        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                        if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_src27ouot20tc5m, 88, 21);
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
                    goto gm_lb6;
                }
gm_lb7:;
            }
gm_lb6:;
            void * t58 = gm_l2;
            gm_tj2m9dbh1ekv6 t59;
            GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)2ULL));
            gm_tftb1odc739t5 t60 = gm_f_push_f21bjke_2(t58, t59);
            gm_l22 = t60;
            uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
            if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_src27ouot20tc5m, 92, 13);
            void * t62 = gm_l4;
            uint8_t t63 = gm_l1;
            void * t64 = gm_l2;
            void * t65 = gm_l3;
            gm_b0_1 t66 = gm_f_present_bindings_j3olvoa((void *)NULL, cx, NULL, t62, t63, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t67 = gm_l2;
            gm_t46lmulm8gaj3 t68 = gm_f_pop_d9v3irs(t67);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t70 = (uint8_t)(t69 == ((uint8_t)7ULL));
            if (!t70) {
                goto gm_lb11;
            }
            void * t71 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
            gm_l7 = t71;
            void * t72 = gm_l2;
            gm_tj2m9dbh1ekv6 t73;
            GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)3ULL));
            gm_tftb1odc739t5 t74 = gm_f_push_f21bjke_2(t72, t73);
            gm_l23 = t74;
            uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
            if (!t75) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_src27ouot20tc5m, 97, 13);
            void * t76 = gm_l7;
            uint8_t t77 = gm_l1;
            void * t78 = gm_l2;
            void * t79 = gm_l3;
            gm_b0_1 t80 = gm_f_present_bindings_j3olvoa((void *)NULL, cx, NULL, t76, t77, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t81 = gm_l2;
            gm_t46lmulm8gaj3 t82 = gm_f_pop_d9v3irs(t81);
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t84 = (uint8_t)(t83 == ((uint8_t)8ULL));
            if (!t84) {
                goto gm_lb12;
            }
            uint32_t t85 = GM_LD(uint32_t, ((((uint8_t *)t29) + 8) + 8));
            gm_l8 = t85;
            void * t86 = GM_LD(void *, (((((uint8_t *)t29) + 8) + 8) + 8));
            gm_l9 = t86;
            {
                {
                    uint8_t t87 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l9)) != 0);
                    if (!t87) {
                        goto gm_lb14;
                    }
                    void * t88 = GM_LD(void *, ((uint8_t *)&gm_l9));
                    gm_l10 = t88;
                    void * t89 = gm_l2;
                    uint32_t t90 = gm_l8;
                    gm_tj2m9dbh1ekv6 t91;
                    GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t91 + 4), t90);
                    gm_tftb1odc739t5 t92 = gm_f_push_f21bjke_2(t89, t91);
                    gm_l24 = t92;
                    uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                    if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src27ouot20tc5m, 102, 13);
                    void * t94 = gm_l10;
                    void * t95 = gm_l2;
                    void * t96 = gm_l3;
                    gm_b0_1 t97 = gm_f_present_bindings_j3olvoa((void *)NULL, cx, NULL, t94, ((uint8_t)0), t95, t96);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t98 = gm_l2;
                    gm_t46lmulm8gaj3 t99 = gm_f_pop_d9v3irs(t98);
                    goto gm_lb13;
                }
gm_lb14:;
            }
gm_lb13:;
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t100 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t101 = (uint8_t)(t100 == ((uint8_t)9ULL));
            if (!t101) {
                goto gm_lb15;
            }
            gm_b16_8 t102 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 8) + 8));
            gm_l11 = t102;
            gm_b16_8 t103 = gm_l11;
            gm_l12 = t103;
            gm_l13 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t104 = gm_l13;
                    uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                    uint8_t t106 = (t104 < t105);
                    if (!t106) {
                        goto gm_lb16;
                    }
                    {
                        void * t107 = GM_LD(void *, ((uint8_t *)&gm_l12));
                        uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                        uint64_t t109 = gm_l13;
                        {
                            {
                                gm_ttggq7ng4kp3p t110 = GM_LD(gm_ttggq7ng4kp3p, (((uint8_t *)t107) + (uintptr_t)t109 * 16));
                                gm_l14 = t110;
                                void * t111 = gm_l2;
                                uint32_t t112 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
                                gm_tj2m9dbh1ekv6 t113;
                                GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)1ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t113 + 4), t112);
                                gm_tftb1odc739t5 t114 = gm_f_push_f21bjke_2(t111, t113);
                                gm_l25 = t114;
                                uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_src27ouot20tc5m, 108, 17);
                                void * t116 = GM_LD(void *, (((uint8_t *)&gm_l14) + 8));
                                void * t117 = gm_l2;
                                void * t118 = gm_l3;
                                gm_b0_1 t119 = gm_f_present_bindings_j3olvoa((void *)NULL, cx, NULL, t116, ((uint8_t)0), t117, t118);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t120 = gm_l2;
                                gm_t46lmulm8gaj3 t121 = gm_f_pop_d9v3irs(t120);
                                goto gm_lb19;
                            }
gm_lb20:;
                            gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 107, 13);
                        }
gm_lb19:;
                    }
gm_lb18:;
                    uint64_t t122 = gm_l13;
                    uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                    uint64_t t125 = r123;
                    gm_l13 = t125;
                }
            }
gm_lb16:;
            goto gm_lb1;
        }
gm_lb15:;
        {
            uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            uint8_t t127 = (uint8_t)(t126 == ((uint8_t)10ULL));
            if (!t127) {
                goto gm_lb21;
            }
            gm_b16_8 t128 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 8) + 8));
            gm_l15 = t128;
            gm_b16_8 t129 = gm_l15;
            gm_l16 = t129;
            gm_l17 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t130 = gm_l17;
                    uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint8_t t132 = (t130 < t131);
                    if (!t132) {
                        goto gm_lb22;
                    }
                    {
                        void * t133 = GM_LD(void *, ((uint8_t *)&gm_l16));
                        uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                        uint64_t t135 = gm_l17;
                        {
                            {
                                void * t136 = GM_LD(void *, (((uint8_t *)t133) + (uintptr_t)t135 * 8));
                                gm_l18 = t136;
                                void * t137 = gm_l18;
                                uint8_t t138 = gm_l1;
                                void * t139 = gm_l2;
                                void * t140 = gm_l3;
                                gm_b0_1 t141 = gm_f_present_bindings_j3olvoa((void *)NULL, cx, NULL, t137, t138, t139, t140);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb25;
                            }
gm_lb26:;
                            gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 114, 13);
                        }
gm_lb25:;
                    }
gm_lb24:;
                    uint64_t t142 = gm_l17;
                    uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                    uint64_t t145 = r143;
                    gm_l17 = t145;
                }
            }
gm_lb22:;
            goto gm_lb1;
        }
gm_lb21:;
        {
            goto gm_lb1;
        }
gm_lb27:;
        gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 84, 5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_present_only_bindings_ug2i6dq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_present_only_bindings_ug2i6dq F;
    void * gm_l0; /* def */
    gm_b16_8 gm_l1; /* arms */
    void * gm_l2; /* out */
    void * gm_l3; /* catch_all */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    void * gm_l7; /* ca */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint8_t gm_l16; /* handled */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* j */
    gm_b16_8 gm_l20; /* name */
    GM_STACK_CHECK(&F, 1030u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l21;
    void * gm_l22;
    uint8_t gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_l3 = ((void *)0);
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    gm_l4 = t32;
    {
        for (;;) {
            uint64_t t33 = gm_l5;
            uint64_t t34 = gm_l4;
            uint8_t t35 = (t33 < t34);
            if (!t35) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t36 = gm_l5;
                        gm_l6 = t36;
                        {
                            {
                                void * t37 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                uint64_t t39 = gm_l6;
                                {
                                    {
                                        uint8_t t40 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t37) + (uintptr_t)t39 * 24) + 8)) != 0);
                                        if (t40) {
                                            goto gm_lb9;
                                        }
                                        gm_l21 = ((uint8_t)1);
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        gm_l21 = ((uint8_t)0);
                                        goto gm_lb8;
                                    }
gm_lb10:;
                                    gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 31, 12);
                                }
gm_lb8:;
                                uint8_t t41 = gm_l21;
                                if (!t41) {
                                    goto gm_lb7;
                                }
                                void * t42 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                uint64_t t44 = gm_l6;
                                void * t45 = GM_LD(void *, (((uint8_t *)t42) + (uintptr_t)t44 * 24));
                                uint8_t t46 = gm_f_is_catch_all_mouipl8(t45);
                                if (!t46) {
                                    goto gm_lb7;
                                }
                                void * t47 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                uint64_t t49 = gm_l6;
                                void * t50 = (void *)(((uint8_t *)t47) + (uintptr_t)t49 * 24);
                                gm_l3 = t50;
                                goto gm_lb1;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 30, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t51 = gm_l5;
            uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
            uint64_t t54 = r52;
            gm_l5 = t54;
        }
    }
gm_lb1:;
    uint8_t t55 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
    if (t55) {
        void * t56 = GM_LD(void *, ((uint8_t *)&gm_l3));
        gm_l22 = t56;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t57 = gm_l22;
    gm_l7 = t57;
    void * t58 = gm_l7;
    void * t59 = GM_LD(void *, (((uint8_t *)t58) + 16));
    uint8_t t60 = gm_f_is_empty_suloehk(t59);
    if (t60) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    gm_l9 = ((uint64_t)0ULL);
    uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    gm_l8 = t61;
    {
        for (;;) {
            uint64_t t62 = gm_l9;
            uint64_t t63 = gm_l8;
            uint8_t t64 = (t62 < t63);
            if (!t64) {
                goto gm_lb11;
            }
            {
                {
                    {
                        uint64_t t65 = gm_l9;
                        gm_l10 = t65;
                        gm_trqfvmjtt4sij t66;
                        gm_f_list_cugp4sg((void *)&t66, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l11 = t66;
                        gm_trqfvmjtt4sij t67;
                        gm_f_list_6canco6_2((void *)&t67, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l12 = t67;
                        void * t68 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t70 = gm_l10;
                        void * t71 = GM_LD(void *, (((uint8_t *)t68) + (uintptr_t)t70 * 24));
                        void * t72 = (void *)((uint8_t *)&F.l12);
                        void * t73 = (void *)((uint8_t *)&F.l11);
                        gm_b0_1 t74 = gm_f_present_bindings_j3olvoa((void *)NULL, cx, NULL, t71, ((uint8_t)1), t72, t73);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                        gm_l13 = t75;
                        gm_l14 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t76 = gm_l14;
                                uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                uint8_t t78 = (t76 < t77);
                                if (!t78) {
                                    goto gm_lb16;
                                }
                                {
                                    void * t79 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                    uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                    uint64_t t81 = gm_l14;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l15), (((uint8_t *)t79) + (uintptr_t)t81 * 40), 40);
                                            gm_l16 = ((uint8_t)0);
                                            gm_l18 = ((uint64_t)0ULL);
                                            uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                            gm_l17 = t82;
                                            {
                                                for (;;) {
                                                    uint64_t t83 = gm_l18;
                                                    uint64_t t84 = gm_l17;
                                                    uint8_t t85 = (t83 < t84);
                                                    if (!t85) {
                                                        goto gm_lb21;
                                                    }
                                                    {
                                                        {
                                                            {
                                                                uint64_t t86 = gm_l18;
                                                                gm_l19 = t86;
                                                                {
                                                                    {
                                                                        uint64_t t87 = gm_l19;
                                                                        uint64_t t88 = gm_l10;
                                                                        uint8_t t89 = (!(t87 == t88));
                                                                        if (!t89) {
                                                                            goto gm_lb27;
                                                                        }
                                                                        void * t90 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                                                        uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                                                        uint64_t t92 = gm_l19;
                                                                        {
                                                                            {
                                                                                uint8_t t93 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t90) + (uintptr_t)t92 * 24) + 8)) != 0);
                                                                                if (t93) {
                                                                                    goto gm_lb29;
                                                                                }
                                                                                gm_l23 = ((uint8_t)1);
                                                                                goto gm_lb28;
                                                                            }
gm_lb29:;
                                                                            {
                                                                                gm_l23 = ((uint8_t)0);
                                                                                goto gm_lb28;
                                                                            }
gm_lb30:;
                                                                            gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 46, 35);
                                                                        }
gm_lb28:;
                                                                        uint8_t t94 = gm_l23;
                                                                        if (!t94) {
                                                                            goto gm_lb27;
                                                                        }
                                                                        void * t95 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                                                        uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                                                        uint64_t t97 = gm_l19;
                                                                        void * t98 = GM_LD(void *, (((uint8_t *)t95) + (uintptr_t)t97 * 24));
                                                                        uint8_t t99 = gm_f_is_catch_all_mouipl8(t98);
                                                                        uint8_t t100 = (uint8_t)!t99;
                                                                        if (!t100) {
                                                                            goto gm_lb27;
                                                                        }
                                                                        void * t101 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                                                        uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                                                        uint64_t t103 = gm_l19;
                                                                        void * t104 = GM_LD(void *, (((uint8_t *)t101) + (uintptr_t)t103 * 24));
                                                                        gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
                                                                        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                                                                        uint8_t t107 = gm_f_names_absent_fajuepv(t104, t106);
                                                                        if (!t107) {
                                                                            goto gm_lb27;
                                                                        }
                                                                        gm_l16 = ((uint8_t)1);
                                                                        goto gm_lb26;
                                                                    }
gm_lb27:;
                                                                }
gm_lb26:;
                                                                goto gm_lb24;
                                                            }
gm_lb25:;
                                                            gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 45, 13);
                                                        }
gm_lb24:;
                                                    }
gm_lb23:;
                                                    uint64_t t108 = gm_l18;
                                                    uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                                                    uint64_t t111 = r109;
                                                    gm_l18 = t111;
                                                }
                                            }
gm_lb21:;
                                            uint8_t t112 = gm_l16;
                                            uint8_t t113 = (uint8_t)!t112;
                                            if (t113) {
                                                void * t114 = gm_l0;
                                                void * t115 = GM_LD(void *, (((uint8_t *)t114) + 24));
                                                uint64_t t116 = GM_LD(uint64_t, ((((uint8_t *)t114) + 24) + 8));
                                                uint32_t t117 = GM_LD(uint32_t, (((uint8_t *)&F.l15) + 28));
                                                uint64_t t118 = (uint64_t)t117;
                                                if ((uintptr_t)t118 >= (uintptr_t)(t116)) gm_trap_index((uintptr_t)t118, t116, gm_src27ouot20tc5m, 49, 25);
                                                gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)t115) + (uintptr_t)t118 * 40));
                                                gm_l20 = t119;
                                                void * t120 = gm_l2;
                                                gm_tl874t2atnlh3 t121 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l15) + 16));
                                                gm_b16_8 t122 = gm_l20;
                                                void * t123 = gm_l7;
                                                void * t124 = GM_LD(void *, ((uint8_t *)t123));
                                                gm_tl874t2atnlh3 t125 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t124) + 120));
                                                uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&F.l15) + 32));
                                                gm_tkrkmsnb6m1b7 t127;
                                                GM_ST(uint32_t, (uint8_t *)&t127 + 12, 0);
                                                GM_ST(uint8_t, (uint8_t *)&t127 + 45, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t127 + 46, 0);
                                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t127 + 0), t121);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t127 + 16), t122);
                                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t127 + 32), t125);
                                                GM_ST(uint8_t, ((uint8_t *)&t127 + 44), t126);
                                                gm_tj5f8vajf35o7 t128;
                                                GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t128 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t128 + 56, 0);
                                                GM_ST(uint64_t, (uint8_t *)&t128 + 64, 0);
                                                GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)8ULL));
                                                GM_ST(gm_tkrkmsnb6m1b7, ((uint8_t *)&t128 + 8), t127);
                                                gm_tftb1odc739t5 t129 = gm_f_push_rqlbv1e(t120, &t128);
                                                gm_l24 = t129;
                                                uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                                if (!t130) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src27ouot20tc5m, 50, 17);
                                            }
                                            goto gm_lb19;
                                        }
gm_lb20:;
                                        gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 43, 9);
                                    }
gm_lb19:;
                                }
gm_lb18:;
                                uint64_t t131 = gm_l14;
                                uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
                                uint64_t t134 = r132;
                                gm_l14 = t134;
                            }
                        }
gm_lb16:;
                        goto gm_lb14;
                    }
gm_lb15:;
                    gm_trap("no match arm matched the scrutinee", gm_src27ouot20tc5m, 39, 5);
                }
gm_lb14:;
            }
gm_lb13:;
            uint64_t t135 = gm_l9;
            uint64_t r136; bool o137 = __builtin_add_overflow(t135, ((uint64_t)1ULL), &r136);
            uint64_t t138 = r136;
            gm_l9 = t138;
        }
    }
gm_lb11:;
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
