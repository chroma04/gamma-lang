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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
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
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
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
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(base: ^Expr, index: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
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
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
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
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { uint64_t f0; gm_tf1bjg81u0glv f1; } gm_td38of7a349nj;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
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
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tbgd14esf0jvi v1; } f1; } gm_ts5t31j0bugq1;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_ts5t31j0bugq1 f0; uint8_t f1; uint8_t pe[7]; } gm_tck5nas0nf5v8;
_Static_assert(sizeof(gm_td38of7a349nj) == 32 && _Alignof(gm_td38of7a349nj) == 8 && offsetof(gm_td38of7a349nj, f0) == 0 && offsetof(gm_td38of7a349nj, f1) == 8, "layout of `(i: usize, x: IterPre)`");
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
typedef struct { gm_tbci5ospk5nnn f0; uint8_t f1; uint8_t pe[7]; } gm_thrdpodrs8pgt;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts5t31j0bugq1) == 40 && _Alignof(gm_ts5t31j0bugq1) == 8 && offsetof(gm_ts5t31j0bugq1, f0) == 0 && offsetof(gm_ts5t31j0bugq1, f1) == 8, "layout of `Packed`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tck5nas0nf5v8) == 48 && _Alignof(gm_tck5nas0nf5v8) == 8 && offsetof(gm_tck5nas0nf5v8, f0) == 0 && offsetof(gm_tck5nas0nf5v8, f1) == 40, "layout of `?Packed`");
typedef struct { uint64_t f0; gm_toa2vhe3l4hfr f1; } gm_tdr8bh2bm3fc8;
_Static_assert(sizeof(gm_thrdpodrs8pgt) == 56 && _Alignof(gm_thrdpodrs8pgt) == 8 && offsetof(gm_thrdpodrs8pgt, f0) == 0 && offsetof(gm_thrdpodrs8pgt, f1) == 48, "layout of `?Int`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_t2b6tm8c0vgo1;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tdr8bh2bm3fc8) == 48 && _Alignof(gm_tdr8bh2bm3fc8) == 8 && offsetof(gm_tdr8bh2bm3fc8, f0) == 0 && offsetof(gm_tdr8bh2bm3fc8, f1) == 8, "layout of `(i: usize, x: GenericArg)`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
_Static_assert(sizeof(gm_t2b6tm8c0vgo1) == 64 && _Alignof(gm_t2b6tm8c0vgo1) == 8 && offsetof(gm_t2b6tm8c0vgo1, f0) == 0 && offsetof(gm_t2b6tm8c0vgo1, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_t2b6tm8c0vgo1 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tcgrjq9toscke;
typedef struct { uint64_t f0; gm_tc3oht4ksth9d f1; } gm_tkppg5stlt438;
typedef struct { uint64_t f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tsdklcaprgv9r;
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
_Static_assert(sizeof(gm_tcgrjq9toscke) == 88 && _Alignof(gm_tcgrjq9toscke) == 8 && offsetof(gm_tcgrjq9toscke, f0) == 0 && offsetof(gm_tcgrjq9toscke, f1) == 8 && offsetof(gm_tcgrjq9toscke, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_tkppg5stlt438) == 96 && _Alignof(gm_tkppg5stlt438) == 8 && offsetof(gm_tkppg5stlt438, f0) == 0 && offsetof(gm_tkppg5stlt438, f1) == 8, "layout of `(i: usize, x: Expr)`");
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_tsdklcaprgv9r) == 104 && _Alignof(gm_tsdklcaprgv9r) == 8 && offsetof(gm_tsdklcaprgv9r, f0) == 0 && offsetof(gm_tsdklcaprgv9r, f1) == 8 && offsetof(gm_tsdklcaprgv9r, f2) == 96, "layout of `ArgSource`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_bind_available_effects_uav06po {
    gm_frame h;
    gm_toa2vhe3l4hfr l13; /* x */
    gm_b0_1 ret;
};
struct gm_fr_bind_literal_fallbacks_r2021qp {
    gm_frame h;
    void * l0; /* cx */
    void * l1; /* sig */
    void * l2; /* binds */
    uint8_t l11; /* mentions */
    gm_trqfvmjtt4sij l13; /* found */
    gm_b0_1 ret;
};
struct gm_fr_bind_unit_defaults_htn8poj {
    gm_frame h;
    gm_t5v0c8ia6a5mo l8; /* p */
    gm_b0_1 ret;
};
struct gm_fr_check_generic_constraint_6138rrh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_clearly_value_f7icrt5 {
    gm_frame h;
    gm_t930q5l38hdap l3; /* b */
    uint8_t ret;
};
struct gm_fr_coerce_generic_args_9ald24q {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* out */
    gm_trqfvmjtt4sij l6; /* slot */
    gm_t5v0c8ia6a5mo l10; /* p */
    gm_td7pkamr64l0c ret;
};
struct gm_fr_comptime_arg_vtq6sop {
    gm_frame h;
    gm_t930q5l38hdap l4; /* b */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_denotes_type_jgfet92 {
    gm_frame h;
    gm_tet9guhev5r4u l4; /* i */
    uint8_t ret;
};
struct gm_fr_effect_args_name_jgklot1 {
    gm_frame h;
    void * l0; /* s */
    uint32_t l2; /* pid */
    uint8_t l3; /* found */
    gm_toa2vhe3l4hfr l6; /* a */
    uint8_t ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_3l1ts9q {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_4l4svrs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
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
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
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
struct gm_fr_empty_slice_v5cv86p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
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
struct gm_fr_extend_nkql463 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_bfunt71 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dgpelc4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_dink6tf {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_hitalag {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pjcgmfa {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_qtpj77g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tqlm0ij {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_uualn6s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_3ifsvek {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_41hm2he {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5k1v0hr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7u7i1da {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8tl219j {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9n2qn66 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d2d7bae {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dvcvfgj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_itam0lj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kj30rlk {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ldnd7h1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_m4otrtu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_obibpjh {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qk20moi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qv493su {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r57fu0b {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s1gurul {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vns89mr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_generic_call_2tlofdi {
    gm_frame h;
    gm_tqqhoivahirr0 l5; /* sig */
    gm_t5v0c8ia6a5mo l10; /* p */
    gm_t5v0c8ia6a5mo l15; /* p */
    gm_trqfvmjtt4sij l16; /* named */
    gm_t7te2tdpebbt4 l19; /* a */
    gm_trqfvmjtt4sij l21; /* slot */
    gm_t5v0c8ia6a5mo l25; /* p */
    gm_ts5t31j0bugq1 l26; /* packed */
    gm_t7te2tdpebbt4 l32; /* a */
    gm_trqfvmjtt4sij l33; /* stmts */
    void * ret;
};
struct gm_fr_generic_call_packed_h7ssi9n {
    gm_frame h;
    void * l0; /* cx */
    gm_tqqhoivahirr0 l5; /* sig */
    gm_t5v0c8ia6a5mo l11; /* p */
    gm_trqfvmjtt4sij l12; /* named */
    gm_trqfvmjtt4sij l13; /* positional */
    gm_t7te2tdpebbt4 l16; /* a */
    gm_tc3oht4ksth9d l18; /* e */
    gm_trqfvmjtt4sij l20; /* targets */
    gm_t5v0c8ia6a5mo l24; /* p */
    gm_t5v0c8ia6a5mo l29; /* p */
    gm_trqfvmjtt4sij l30; /* binds */
    gm_trqfvmjtt4sij l31; /* literal_args */
    gm_trqfvmjtt4sij l33; /* values */
    gm_trqfvmjtt4sij l36; /* sources */
    gm_tc3oht4ksth9d l40; /* e */
    gm_t7te2tdpebbt4 l43; /* a */
    gm_tc3oht4ksth9d l45; /* e */
    gm_trqfvmjtt4sij l47; /* source_order */
    gm_tsdklcaprgv9r l50; /* src */
    gm_trqfvmjtt4sij l51; /* iter_pre */
    gm_trqfvmjtt4sij l59; /* fixed */
    gm_t5v0c8ia6a5mo l68; /* p */
    uint8_t l73; /* unbound */
    gm_t5v0c8ia6a5mo l77; /* q */
    gm_t0mfir12leg2g l78; /* lit */
    gm_tc3oht4ksth9d l79; /* l */
    gm_tc3oht4ksth9d l80; /* arg */
    uint8_t l82; /* mentions_free */
    gm_trqfvmjtt4sij l84; /* found */
    gm_trqfvmjtt4sij l90; /* map */
    gm_trqfvmjtt4sij l95; /* written */
    gm_tsdklcaprgv9r l99; /* src */
    gm_td7pkamr64l0c l100; /* coerced */
    gm_trqfvmjtt4sij l102; /* slot */
    void * ret;
};
struct gm_fr_generic_fn_from_expected_ok2k7go {
    gm_frame h;
    gm_tddtjfggdat0d l5; /* want */
    gm_tqqhoivahirr0 l6; /* sig */
    gm_trqfvmjtt4sij l7; /* runtime */
    gm_t5v0c8ia6a5mo l10; /* p */
    gm_trqfvmjtt4sij l11; /* binds */
    gm_trqfvmjtt4sij l16; /* targs */
    void * ret;
};
struct gm_fr_generic_type_args_mj9hh1l {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* targs */
    gm_t5v0c8ia6a5mo l19; /* p */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_given_by_name_pd5ma4t {
    gm_frame h;
    gm_t7te2tdpebbt4 l3; /* a */
    uint8_t ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_inferable_param_f80lrdi {
    gm_frame h;
    void * l0; /* s */
    uint32_t l3; /* pid */
    gm_t5v0c8ia6a5mo l10; /* q */
    uint32_t l11; /* all */
    uint32_t l12; /* computed */
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_instantiation_vgsqc8f {
    gm_frame h;
    gm_b16_8 l2; /* args */
    gm_t5v0c8ia6a5mo l8; /* p */
    gm_trqfvmjtt4sij l9; /* targets */
    gm_t5v0c8ia6a5mo l13; /* p */
    gm_t5v0c8ia6a5mo l20; /* p */
    gm_tc3oht4ksth9d l24; /* e */
    gm_t5v0c8ia6a5mo l25; /* p */
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
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_89e90o6 {
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
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_lnmdod1 {
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
struct gm_fr_list_p2dh8s2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq {
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
struct gm_fr_list_vp1djv1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_location_function_thau9ff {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_mentions_error_37knu7r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_t2b6tm8c0vgo1 l1; /* kind */
    void * ret;
};
struct gm_fr_no_inference_from_fn_klvrs5n {
    gm_frame h;
    gm_t4gut2c9fj1kt l3; /* d */
    gm_t4gut2c9fj1kt l5; /* d */
    gm_tcgrjq9toscke l9; /* el */
    gm_b0_1 ret;
};
struct gm_fr_normalize_value_fgo7ofj {
    gm_frame h;
    gm_tbci5ospk5nnn l10; /* i */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_order_generic_args_m8gcs0c {
    gm_frame h;
    gm_b16_8 l5; /* source_order */
    gm_trqfvmjtt4sij l6; /* pre */
    gm_b16_8 ret;
};
struct gm_fr_param_constraints_2k5fnnl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_param_default_h93ci1m {
    gm_frame h;
    void * ret;
};
struct gm_fr_param_kind_a0aikmb {
    gm_frame h;
    gm_tnnjeerolos0a ret;
};
struct gm_fr_param_name_rbnop5l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_0ee93e4 {
    gm_frame h;
    gm_tsdklcaprgv9r l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_14olj4s {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_cl1fihl {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tcgrjq9toscke l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_h69n5fi {
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
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
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
struct gm_fr_regrow_51kssak {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_dv63scj {
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
struct gm_fr_regrow_j4iu2lk {
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
struct gm_fr_regrow_q44geqn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_cannot_infer_tor1q4u {
    gm_frame h;
    gm_t5v0c8ia6a5mo l11; /* p */
    gm_t5v0c8ia6a5mo l19; /* q */
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_same_int_value_ngrqsok {
    gm_frame h;
    gm_tbci5ospk5nnn l5; /* i */
    gm_tbci5ospk5nnn l6; /* j */
    uint8_t ret;
};
struct gm_fr_set_bind_nsjnlvq {
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
struct gm_fr_show_int_ub6npub {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* v */
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
struct gm_fr_source_location_nc1oola {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* fields */
    void * ret;
};
struct gm_fr_source_position_taggjb0 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_span_text_sv6qv18 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_stable_sort_by_3nevrtc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_stable_sort_by_spa1mja {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_too_many_generic_args_bs99n7s {
    gm_frame h;
    gm_t5v0c8ia6a5mo l13; /* p */
    gm_b0_1 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_walk_v858co7 {
    gm_frame h;
    void * l0; /* s */
    gm_closure l2; /* f */
    gm_t53at80pfdus3 l3; /* k */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_arg_value_gavegdu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_array_as_slice_gvflfg2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_available_7okjiog(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_available_effects_uav06po(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_literal_fallbacks_r2021qp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_unit_defaults_htn8poj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bound_to_5c11t0h(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_check_generic_constraint_6138rrh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) uint8_t gm_f_clearly_value_f7icrt5(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_coerce_generic_args_9ald24q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_arg_vtq6sop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_value_l5orc93(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_constraint_name_ombut3o(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void * gm_f_default_value_lo723gi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) uint8_t gm_f_denotes_type_jgfet92(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_effect_args_name_jgklot1(void * a0, void * a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3l1ts9q(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4l4svrs(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_v5cv86p(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_finish_call_kjknkfg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_dgpelc4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_dink6tf(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_hitalag(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_fn_name_at_5o2j1h9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_pjcgmfa(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_qtpj77g(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void * gm_f_fn_ref_aqs2fl4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tqlm0ij(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_uualn6s(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_for_each_component_o5cuh7c(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_format_3ifsvek(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_41hm2he(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5k1v0hr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_7u7i1da(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8tl219j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9n2qn66(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_d2d7bae(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_dvcvfgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_h5s5tmr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_itam0lj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_kj30rlk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ldnd7h1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_m4otrtu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_obibpjh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qk20moi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qv493su(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_r57fu0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rkil4k4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s1gurul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_vns89mr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void * gm_f_generic_call_2tlofdi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_generic_call_packed_h7ssi9n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_generic_fn_from_expected_ok2k7go(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_generic_type_args_mj9hh1l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_given_by_name_pd5ma4t(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_inferable_param_f80lrdi(void * a0, void * a1, uint64_t a2);
__attribute__((aligned(4))) uint32_t gm_f_instantiate_35r5pb2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_int_key_dmcraup(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_const_ref_arg_s0f37he(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_instantiation_vgsqc8f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_site_8sr7vam(void * a0);
__attribute__((aligned(4))) void gm_f_iterator_arg_r0if28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_iterator_call_as_value_00i5mch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_key_int_pqscuvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_89e90o6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_lnmdod1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p2dh8s2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_location_function_thau9ff(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_match_params_6vndkud(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, void * a3);
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_error_37knu7r(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_merge_sort_3cnr3mo(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_merge_sort_fp6fkii(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_no_inference_from_fn_klvrs5n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_normalize_value_fgo7ofj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_not_first_class_dve75oi(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_order_generic_args_m8gcs0c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_pack_rest_args_cknfrfl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_constraints_2k5fnnl(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_param_default_h93ci1m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, void * a3, gm_t88rcucg1b7uk a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_param_index_78k4iut(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0ee93e4(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_14olj4s(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_cl1fihl(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_record_effects_0eel5vu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_51kssak(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_dv63scj(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q44geqn(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_remove_iqb0btt(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_cannot_infer_tor1q4u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_effect_arg_4h6q4qv(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_ty_uk6c1lp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_same_int_value_ngrqsok(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_satisfies_p0n2er8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_second_class_part_k6i54mk(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_set_bind_nsjnlvq(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_source_location_nc1oola(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_source_location_ty_slllard(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint64_t gm_f_source_position_taggjb0(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_span_text_sv6qv18(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_spread_generic_args_8qtof2f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_stable_sort_by_3nevrtc(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_stable_sort_by_spa1mja(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_too_many_generic_args_bs99n7s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_b16_8 a3, gm_b16_8 a4, uint64_t a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_name_oqtujpn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_param_3av4r8i(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
extern uint8_t gm_kg80t7hdccb[40] __attribute__((aligned(8)));
extern uint8_t gm_kieqgrgghc5[40] __attribute__((aligned(8)));
extern uint8_t gm_ko7k0ku4679[40] __attribute__((aligned(8)));
extern const uint8_t gm_s14lpmr3v2vk3[57];
extern const uint8_t gm_s1cbu3jc19u4t[36];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s26emr7fhbmjr[22];
extern const uint8_t gm_s27tmg3h56aru[53];
extern const uint8_t gm_s2pmug7pm7rdg[31];
extern const uint8_t gm_s6kdis17b15fm[114];
extern const uint8_t gm_s8pjuqfuormq3[51];
extern const uint8_t gm_s9h42skj8t388[90];
extern const uint8_t gm_sa9e77lq9q04p[16];
extern const uint8_t gm_savtbnmf9bjnr[17];
extern const uint8_t gm_sb7685j14iksa[41];
extern const uint8_t gm_sb7jq1p8shp96[113];
extern const uint8_t gm_sett0rbmg4jjf[60];
extern const uint8_t gm_sf7ldnukv79cp[24];
extern const uint8_t gm_sg47f6qrh6jrs[32];
extern const uint8_t gm_sg58j44mq0qb5[61];
extern const uint8_t gm_sg7ronlh5k7js[84];
extern const uint8_t gm_sgj7bt7nru3s1[27];
extern const uint8_t gm_sjjcm6bafumir[47];
extern const uint8_t gm_slbgk65sat4bm[8];
extern const uint8_t gm_smq842qg8d7u6[119];
extern const uint8_t gm_so1us53sh0cre[44];
extern const uint8_t gm_sonq8sf0vvvvr[127];
extern const uint8_t gm_spgfbquq93iqp[43];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src7h8dni243k2v[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srckg5f2c0p31ur[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcrorgjlg51brd[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srqr27qbutlf7[78];
extern const uint8_t gm_srv4cua6q51nq[5];
extern const uint8_t gm_sseb1k7dlt7ge[67];
extern const uint8_t gm_stl5olh2jfr0d[40];
extern const gm_tmpl gm_tpl06vq09a1bq44;
extern const gm_tmpl gm_tpl0e4rd6ag9tqa;
extern const gm_tmpl gm_tpl0n7kmejjk8it;
extern const gm_tmpl gm_tpl0rlavm9oqi24;
extern const gm_tmpl gm_tpl1o8uodh0pvjc;
extern const gm_tmpl gm_tpl5g0hj2vn6uah;
extern const gm_tmpl gm_tpl7r51aeh92b80;
extern const gm_tmpl gm_tpl82nt5tav1er7;
extern const gm_tmpl gm_tplablvqji1bpu7;
extern const gm_tmpl gm_tplbn5t5stpvru2;
extern const gm_tmpl gm_tplfdsuiujvavc8;
extern const gm_tmpl gm_tplhonv8g349dv9;
extern const gm_tmpl gm_tplidajav2hoc6k;
extern const gm_tmpl gm_tplj0t0s73b2v4j;
extern const gm_tmpl gm_tpllir4kiseuk2b;
extern const gm_tmpl gm_tplnevoink17fov;
extern const gm_tmpl gm_tplrq1qj3euarkk;
extern const gm_tmpl gm_tplvle6f1rmlmjh;
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
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b) {
    if (a[8] != b[8]) return false;
    return !a[8] || gm_eq6groq37ram(a, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq467d5cm1d5(a, b);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_emit_dph5v1e F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3l1ts9q(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3l1ts9q F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 104)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4l4svrs(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_4l4svrs F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_v5cv86p(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_v5cv86p F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l2;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        int32_t t9 = gm_f_mag_cmp_s39veq6(t6, t8);
        uint8_t t10 = ((t9 == ((int32_t)0LL)));
        gm_l2 = t10;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t11 = gm_l2;
    F.ret = t11;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_nkql463 F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_h08har5_2(t12, t13, t14);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_bfunt71 F;
    gm_t88rcucg1b7uk gm_l0; /* c */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_walk_v858co7 *)((&F.h)->up))->l0));
    gm_t88rcucg1b7uk t3 = gm_l0;
    gm_closure t4 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_walk_v858co7 *)((&F.h)->up))->l2));
    gm_f_walk_v858co7(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_dgpelc4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_dgpelc4 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_order_generic_args_m8gcs0c *)((&F.h)->up))->l5));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)t3));
    uint64_t t5 = gm_f_source_position_taggjb0(t2, t4);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_order_generic_args_m8gcs0c *)((&F.h)->up))->l5));
    void * t7 = gm_l1;
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)t7));
    uint64_t t9 = gm_f_source_position_taggjb0(t6, t8);
    uint8_t t10 = (t5 < t9);
    F.ret = t10;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_dink6tf(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_dink6tf F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 37u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 96));
    void * t4 = gm_l1;
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t4) + 96));
    uint8_t t6 = (t3 < t5);
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tqlm0ij(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_tqlm0ij F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 147u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_effect_args_name_jgklot1 *)((&F.h)->up))->l0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l0;
    gm_t53at80pfdus3 t5;
    gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_effect_args_name_jgklot1 *)((&F.h)->up))->l2));
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
    uint8_t t8 = (uint8_t)(t7 == ((uint8_t)19ULL));
    if (t8) {
        uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&t5) + 8));
        t8 = (uint8_t)(t9 == t6);
    }
    uint8_t t10 = (uint8_t)(t8);
    gm_t53at80pfdus3 t11;
    memset(&t11, 0, sizeof t11);
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)19ULL));
    GM_ST(uint32_t, ((uint8_t *)&t11 + 8), t6);
    if (t10) {
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_effect_args_name_jgklot1 *)((&F.h)->up))->l3), ((uint8_t)1));
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_3ifsvek(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3ifsvek F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg58j44mq0qb5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(60));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplablvqji1bpu7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_41hm2he(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_41hm2he F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s9h42skj8t388));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(89));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_h5s5tmr((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfdsuiujvavc8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5k1v0hr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_5k1v0hr F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_savtbnmf9bjnr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvle6f1rmlmjh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7u7i1da(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7u7i1da F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sett0rbmg4jjf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(59));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplidajav2hoc6k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_8tl219j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8tl219j F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srqr27qbutlf7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(77));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl82nt5tav1er7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9n2qn66(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_9n2qn66 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sg47f6qrh6jrs));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(31));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5g0hj2vn6uah), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_d2d7bae(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_d2d7bae F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s14lpmr3v2vk3));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(56));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0rlavm9oqi24), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_dvcvfgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dvcvfgj F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tf1bjg81u0glv gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_so1us53sh0cre));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(43));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_rkil4k4((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnevoink17fov), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_itam0lj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_itam0lj F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb7685j14iksa));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhonv8g349dv9), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_kj30rlk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kj30rlk F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjjcm6bafumir));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(46));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl06vq09a1bq44), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ldnd7h1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ldnd7h1 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb7jq1p8shp96));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(112));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj0t0s73b2v4j), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_m4otrtu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_m4otrtu F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smq842qg8d7u6));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(118));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0n7kmejjk8it), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_obibpjh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_obibpjh F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_spgfbquq93iqp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(42));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllir4kiseuk2b), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qv493su(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qv493su F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s27tmg3h56aru));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrq1qj3euarkk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_r57fu0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_r57fu0b F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s2pmug7pm7rdg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(30));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7r51aeh92b80), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_s1gurul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_s1gurul F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sonq8sf0vvvvr));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(126));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0e4rd6ag9tqa), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_vns89mr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vns89mr F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sf7ldnukv79cp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(23));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1o8uodh0pvjc), t2);
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
inline __attribute__((aligned(4))) void gm_f_list_89e90o6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_89e90o6 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_4l4svrs();
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
inline __attribute__((aligned(4))) void gm_f_list_lnmdod1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_lnmdod1 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_3l1ts9q();
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
inline __attribute__((aligned(4))) void gm_f_list_p2dh8s2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_p2dh8s2 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_v5cv86p();
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
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_error_37knu7r(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mentions_error_37knu7r F;
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
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 255, 62);
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
    uint8_t t8 = (uint8_t)(t7 & ((uint8_t)4ULL));
    uint8_t t9 = (!(t8 == ((uint8_t)0ULL)));
    F.ret = t9;
    goto gm_fret;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_constraints_2k5fnnl(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_constraints_2k5fnnl F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 1128));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1128) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srckg5f2c0p31ur, 252, 78);
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + (uintptr_t)t6 * 48) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    F.ret = t8;
    goto gm_fret;
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
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_a0aikmb(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_kind_a0aikmb F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 1128));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1128) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srckg5f2c0p31ur, 249, 62);
    gm_tnnjeerolos0a t7 = GM_LD(gm_tnnjeerolos0a, (((uint8_t *)t3) + (uintptr_t)t6 * 48));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_name_rbnop5l F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 256));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 256) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 290, 61);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
    F.ret = t7;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0ee93e4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0ee93e4 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_3l1ts9q();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_q44geqn(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 104), ((uint8_t *)a1), 104);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 104)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_14olj4s(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_14olj4s F;
    void * gm_l0; /* l */
    gm_tf1bjg81u0glv gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_v5cv86p();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_51kssak(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_cl1fihl(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_cl1fihl F;
    void * gm_l0; /* l */
    gm_thsh8t5u55gps gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_4l4svrs();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_dv63scj(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 20), ((uint8_t *)a1), 20);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 20)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_51kssak(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_51kssak F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_dv63scj(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_dv63scj F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)20ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q44geqn(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_q44geqn F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)104ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_show_int_ub6npub F;
    GM_STACK_CHECK(&F, 146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tabb710d9mq2e gm_l1;
    uint64_t t2 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
    uint8_t t3 = (t2 <= ((uint64_t)512ULL));
    if (t3) {
        gm_b16_8 t4 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        F.ret = t5;
        goto gm_fret;
    }
    uint64_t t6 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
    gm_t4ce0jd3tdro4 t7;
    GM_ST(uint64_t, ((uint8_t *)&t7 + 0), t6);
    gm_f_format_5k1v0hr((void *)&gm_l1, (void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_src7h8dni243k2v, 68, 12);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_span_text_sv6qv18(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_text_sv6qv18 F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t7 = gm_f_file_o15fqku(t5, t6);
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 16));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 16) + 8));
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t11 = (uint64_t)t10;
    uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t13 = (uint64_t)t12;
    uint64_t t14 = (uintptr_t)t11;
    uint64_t t15 = (uintptr_t)t13;
    if (t14 > t15 || t15 > (uintptr_t)t9) gm_trap_slice(t14, t15, t9, gm_srcrorgjlg51brd, 723, 60);
    uint64_t t16 = ((uintptr_t)t13) - (uintptr_t)t11;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t8 + (uintptr_t)t11 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16));
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_stable_sort_by_3nevrtc(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stable_sort_by_3nevrtc F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = gm_l1;
    gm_f_merge_sort_3cnr3mo(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_stable_sort_by_spa1mja(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stable_sort_by_spa1mja F;
    gm_b16_8 gm_l0; /* s */
    gm_closure gm_l1; /* less */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = gm_l1;
    gm_f_merge_sort_fp6fkii(t2, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1ULL));
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
inline __attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_walk_v858co7 F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    F.l2 = a2;
    gm_closure t2 = F.l2;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_closure t4 = t2;
    gm_node *ccx5 = t4.cx;
    void * t6 = t4.code;
    gm_b0_1 t7;
    if (GM_PTR_TAGGED(t6)) {
        ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t6))(GM_CONV(gm_csguankko4lmhd, t3));
    } else {
        t7 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t6)(t4.env, ccx5, NULL, t3);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t8 = F.l0;
    void * t9 = (void *)(((uint8_t *)t8) + 8);
    gm_t88rcucg1b7uk t10 = gm_l1;
    gm_t53at80pfdus3 t11;
    gm_f_kind_jcnt4hm((void *)&t11, t9, GM_CONV(gm_csguankko4lmhd, t10));
    F.l3 = t11;
    void * t12 = (void *)((uint8_t *)&F.l3);
    gm_closure t13 = ((gm_closure){(void *)gm_f_fn_bfunt71, (void *)&F.h, cx});
    gm_f_for_each_component_o5cuh7c(t12, t13);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_available_effects_uav06po(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_bind_available_effects_uav06po F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    void * gm_l2; /* binds */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* pid */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tiilmfd16v3fc gm_l8; /* e */
    gm_b16_8 gm_l9; /* avail */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l14; /* xt */
    gm_t88rcucg1b7uk gm_l15; /* yt */
    GM_STACK_CHECK(&F, 834u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l16;
    uint8_t gm_l17;
    gm_tkn42u7na3t81 gm_l18;
    gm_tdr8bh2bm3fc8 gm_l19;
    void * t22 = gm_l1;
    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 80));
    gm_l3 = t23;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t24 = gm_l4;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t26 = (t24 < t25);
            if (!t26) {
                goto gm_lb1;
            }
            {
                void * t27 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t29 = gm_l4;
                {
                    {
                        uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)t27) + (uintptr_t)t29 * 4));
                        gm_l5 = t30;
                        void * t31 = gm_l2;
                        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)t31));
                        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                        uint32_t t34 = gm_l5;
                        gm_tkn42u7na3t81 t35 = gm_f_bound_to_5c11t0h(t33, t34);
                        gm_l18 = t35;
                        {
                            {
                                uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l18))[4] != 0);
                                if (t36) {
                                    goto gm_lb7;
                                }
                                gm_l17 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l17 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 343, 12);
                        }
gm_lb6:;
                        uint8_t t37 = gm_l17;
                        if (t37) {
                            gm_l16 = ((uint8_t)1);
                        } else {
                            void * t38 = gm_l0;
                            void * t39 = GM_LD(void *, ((uint8_t *)t38));
                            uint32_t t40 = gm_l5;
                            gm_tnnjeerolos0a t41 = gm_f_param_kind_a0aikmb(t39, t40);
                            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
                            uint8_t t43 = (uint8_t)(!(t42 == ((uint8_t)1ULL)));
                            gm_tnnjeerolos0a t44;
                            GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)1ULL));
                            gm_l16 = t43;
                        }
                        uint8_t t45 = gm_l16;
                        if (t45) {
                            goto gm_lb3;
                        }
                        void * t46 = gm_l1;
                        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t46) + 64));
                        gm_l6 = t47;
                        gm_l7 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t48 = gm_l7;
                                uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint8_t t50 = (t48 < t49);
                                if (!t50) {
                                    goto gm_lb9;
                                }
                                {
                                    void * t51 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    uint64_t t53 = gm_l7;
                                    {
                                        {
                                            memmove(((uint8_t *)&gm_l8), (((uint8_t *)t51) + (uintptr_t)t53 * 24), 24);
                                            void * t54 = gm_l0;
                                            void * t55 = GM_LD(void *, ((uint8_t *)t54));
                                            void * t56 = (void *)((uint8_t *)&gm_l8);
                                            uint32_t t57 = gm_l5;
                                            uint8_t t58 = gm_f_effect_args_name_jgklot1(t55, t56, t57);
                                            uint8_t t59 = (uint8_t)!t58;
                                            if (t59) {
                                                goto gm_lb11;
                                            }
                                            void * t60 = gm_l0;
                                            uint32_t t61 = GM_LD(uint32_t, ((uint8_t *)&gm_l8));
                                            gm_b16_8 t62 = gm_f_available_7okjiog((void *)NULL, cx, NULL, t60, t61);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l9 = t62;
                                            uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                            uint8_t t64 = (!(t63 == ((uint64_t)1ULL)));
                                            if (t64) {
                                                goto gm_lb11;
                                            }
                                            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                                            gm_l10 = t65;
                                            gm_l11 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t66 = gm_l11;
                                                    uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                    uint8_t t68 = (t66 < t67);
                                                    if (!t68) {
                                                        goto gm_lb14;
                                                    }
                                                    {
                                                        uint64_t t69 = gm_l11;
                                                        void * t70 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                                        uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                        uint64_t t72 = gm_l11;
                                                        gm_toa2vhe3l4hfr t73 = GM_LD(gm_toa2vhe3l4hfr, (((uint8_t *)t70) + (uintptr_t)t72 * 40));
                                                        gm_tdr8bh2bm3fc8 t74;
                                                        GM_ST(uint64_t, ((uint8_t *)&t74 + 0), t69);
                                                        GM_ST(gm_toa2vhe3l4hfr, ((uint8_t *)&t74 + 8), t73);
                                                        gm_l19 = t74;
                                                        {
                                                            {
                                                                uint64_t t75 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
                                                                gm_l12 = t75;
                                                                memmove(((uint8_t *)&F.l13), (((uint8_t *)&gm_l19) + 8), 40);
                                                                {
                                                                    {
                                                                        uint64_t t76 = gm_l12;
                                                                        void * t77 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                                                        uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t78)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t78, gm_srcrorgjlg51brd, 349, 24);
                                                                        uint64_t t79 = GM_LD(uint64_t, (((((uint8_t *)t77) + (uintptr_t)((uint64_t)0ULL) * 24) + 8) + 8));
                                                                        uint8_t t80 = (t76 < t79);
                                                                        if (!t80) {
                                                                            goto gm_lb20;
                                                                        }
                                                                        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                                                                        uint8_t t82 = (uint8_t)(t81 == ((uint8_t)0ULL));
                                                                        if (!t82) {
                                                                            goto gm_lb20;
                                                                        }
                                                                        gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 8));
                                                                        gm_l14 = t83;
                                                                        void * t84 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                                                        uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t85)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t85, gm_srcrorgjlg51brd, 349, 78);
                                                                        void * t86 = GM_LD(void *, ((((uint8_t *)t84) + (uintptr_t)((uint64_t)0ULL) * 24) + 8));
                                                                        uint64_t t87 = GM_LD(uint64_t, (((((uint8_t *)t84) + (uintptr_t)((uint64_t)0ULL) * 24) + 8) + 8));
                                                                        uint64_t t88 = gm_l12;
                                                                        if ((uintptr_t)t88 >= (uintptr_t)(t87)) gm_trap_index((uintptr_t)t88, t87, gm_srcrorgjlg51brd, 349, 78);
                                                                        uint8_t t89 = GM_LD(uint8_t, ((((uint8_t *)t86) + (uintptr_t)t88 * 40) + 0));
                                                                        uint8_t t90 = (uint8_t)(t89 == ((uint8_t)0ULL));
                                                                        if (!t90) {
                                                                            goto gm_lb20;
                                                                        }
                                                                        gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t86) + (uintptr_t)t88 * 40) + 8));
                                                                        gm_l15 = t91;
                                                                        void * t92 = gm_l0;
                                                                        void * t93 = GM_LD(void *, ((uint8_t *)t92));
                                                                        gm_t88rcucg1b7uk t94 = gm_l14;
                                                                        gm_t88rcucg1b7uk t95 = gm_l15;
                                                                        void * t96 = gm_l2;
                                                                        gm_b0_1 t97 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t93, t94, t95, t96);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        goto gm_lb19;
                                                                    }
gm_lb20:;
                                                                }
gm_lb19:;
                                                                goto gm_lb17;
                                                            }
gm_lb18:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 348, 13);
                                                        }
gm_lb17:;
                                                    }
gm_lb16:;
                                                    uint64_t t98 = gm_l11;
                                                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                                                    uint64_t t101 = r99;
                                                    gm_l11 = t101;
                                                }
                                            }
gm_lb14:;
                                            goto gm_lb12;
                                        }
gm_lb13:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 344, 9);
                                    }
gm_lb12:;
                                }
gm_lb11:;
                                uint64_t t102 = gm_l7;
                                uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                                uint64_t t105 = r103;
                                gm_l7 = t105;
                            }
                        }
gm_lb9:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 342, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t106 = gm_l4;
            uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
            uint64_t t109 = r107;
            gm_l4 = t109;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_literal_fallbacks_r2021qp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_bind_literal_fallbacks_r2021qp F;
    gm_b16_8 gm_l3; /* literal_args */
    uint8_t gm_l4; /* any_free */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* g */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_thsh8t5u55gps gm_l10; /* la */
    gm_t88rcucg1b7uk gm_l12; /* t */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l16; /* b */
    GM_STACK_CHECK(&F, 698u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    F.l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l17;
    gm_tkn42u7na3t81 gm_l18;
    uint8_t gm_l19;
    gm_tnnjeerolos0a gm_l20;
    uint8_t gm_l21;
    gm_tkn42u7na3t81 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_l4 = ((uint8_t)0);
    void * t28 = F.l1;
    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 80));
    gm_l5 = t29;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t31 = gm_l6;
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t33 = (t31 < t32);
            if (!t33) {
                goto gm_lb1;
            }
            {
                void * t34 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t36 = gm_l6;
                {
                    {
                        uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)t34) + (uintptr_t)t36 * 4));
                        gm_l7 = t37;
                        {
                            {
                                void * t38 = F.l2;
                                gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)t38));
                                gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
                                uint32_t t41 = gm_l7;
                                gm_tkn42u7na3t81 t42 = gm_f_bound_to_5c11t0h(t40, t41);
                                gm_l18 = t42;
                                {
                                    {
                                        uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l18))[4] != 0);
                                        if (t43) {
                                            goto gm_lb9;
                                        }
                                        gm_l17 = ((uint8_t)1);
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        gm_l17 = ((uint8_t)0);
                                        goto gm_lb8;
                                    }
gm_lb10:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 307, 12);
                                }
gm_lb8:;
                                uint8_t t44 = gm_l17;
                                if (!t44) {
                                    goto gm_lb7;
                                }
                                void * t45 = F.l0;
                                void * t46 = GM_LD(void *, ((uint8_t *)t45));
                                uint32_t t47 = gm_l7;
                                gm_tnnjeerolos0a t48 = gm_f_param_kind_a0aikmb(t46, t47);
                                gm_l20 = t48;
                                {
                                    {
                                        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                                        uint8_t t50 = (uint8_t)(t49 == ((uint8_t)2ULL));
                                        if (!t50) {
                                            goto gm_lb12;
                                        }
                                        gm_l19 = ((uint8_t)0);
                                        goto gm_lb11;
                                    }
gm_lb12:;
                                    {
                                        gm_l19 = ((uint8_t)1);
                                        goto gm_lb11;
                                    }
gm_lb13:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 307, 54);
                                }
gm_lb11:;
                                uint8_t t51 = gm_l19;
                                if (!t51) {
                                    goto gm_lb7;
                                }
                                gm_l4 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 306, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t52 = gm_l6;
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            uint64_t t55 = r53;
            gm_l6 = t55;
        }
    }
gm_lb1:;
    uint8_t t56 = gm_l4;
    if (t56) {
        gm_b16_8 t57 = gm_l3;
        gm_l8 = t57;
        gm_l9 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t59 = gm_l9;
                uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint8_t t61 = (t59 < t60);
                if (!t61) {
                    goto gm_lb14;
                }
                {
                    void * t62 = GM_LD(void *, ((uint8_t *)&gm_l8));
                    uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t64 = gm_l9;
                    {
                        {
                            memmove(((uint8_t *)&gm_l10), (((uint8_t *)t62) + (uintptr_t)t64 * 20), 20);
                            F.l11 = ((uint8_t)0);
                            void * t65 = F.l0;
                            void * t66 = GM_LD(void *, ((uint8_t *)t65));
                            gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l10));
                            gm_closure t68 = ((gm_closure){(void *)gm_f_fn_uualn6s, (void *)&F.h, cx});
                            gm_f_walk_v858co7(t66, GM_CONV(gm_csguankko4lmhd, t67), t68);
                            if (gm_unw.kind) {
                                goto gm_ll58;
                            }
                            uint8_t t69 = F.l11;
                            uint8_t t70 = (uint8_t)!t69;
                            if (t70) {
                                goto gm_lb16;
                            }
                            void * t71 = F.l0;
                            gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 4));
                            gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l10) + 8));
                            gm_t88rcucg1b7uk t74 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t71, t72, t73);
                            if (gm_unw.kind) {
                                goto gm_ll58;
                            }
                            gm_l12 = t74;
                            gm_trqfvmjtt4sij t75;
                            gm_f_list_oalq03d((void *)&t75, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll58;
                            }
                            F.l13 = t75;
                            void * t76 = F.l0;
                            void * t77 = GM_LD(void *, ((uint8_t *)t76));
                            gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l10));
                            gm_t88rcucg1b7uk t79 = gm_l12;
                            void * t80 = (void *)((uint8_t *)&F.l13);
                            gm_b0_1 t81 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t77, t78, t79, t80);
                            if (gm_unw.kind) {
                                goto gm_ll58;
                            }
                            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                            gm_l14 = t82;
                            gm_l15 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t84 = gm_l15;
                                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                    uint8_t t86 = (t84 < t85);
                                    if (!t86) {
                                        goto gm_lb19;
                                    }
                                    {
                                        void * t87 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                        uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                        uint64_t t89 = gm_l15;
                                        {
                                            {
                                                gm_tplpbqf3duvn3 t90 = GM_LD(gm_tplpbqf3duvn3, (((uint8_t *)t87) + (uintptr_t)t89 * 8));
                                                gm_l16 = t90;
                                                void * t91 = F.l2;
                                                gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)t91));
                                                gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                                                uint32_t t94 = GM_LD(uint32_t, ((uint8_t *)&gm_l16));
                                                gm_tkn42u7na3t81 t95 = gm_f_bound_to_5c11t0h(t93, t94);
                                                gm_l22 = t95;
                                                {
                                                    {
                                                        uint8_t t96 = (uint8_t)((((uint8_t *)&gm_l22))[4] != 0);
                                                        if (t96) {
                                                            goto gm_lb25;
                                                        }
                                                        gm_l21 = ((uint8_t)1);
                                                        goto gm_lb24;
                                                    }
gm_lb25:;
                                                    {
                                                        gm_l21 = ((uint8_t)0);
                                                        goto gm_lb24;
                                                    }
gm_lb26:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 320, 20);
                                                }
gm_lb24:;
                                                uint8_t t97 = gm_l21;
                                                if (t97) {
                                                    void * t98 = F.l2;
                                                    gm_tplpbqf3duvn3 t99 = gm_l16;
                                                    gm_tftb1odc739t5 t100 = gm_f_push_h69n5fi(t98, GM_CONV(gm_cs7e9ppsd384bj, t99));
                                                    gm_l23 = t100;
                                                    uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                                    if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcrorgjlg51brd, 320, 66);
                                                }
                                                goto gm_lb22;
                                            }
gm_lb23:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 319, 13);
                                        }
gm_lb22:;
                                    }
gm_lb21:;
                                    uint64_t t102 = gm_l15;
                                    uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                                    uint64_t t105 = r103;
                                    gm_l15 = t105;
                                }
                            }
gm_lb19:;
                            goto gm_lb17;
                        }
gm_lb18:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 310, 9);
                    }
gm_lb17:;
                }
gm_lb16:;
                uint64_t t106 = gm_l9;
                uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                uint64_t t109 = r107;
                gm_l9 = t109;
            }
        }
        goto gm_ls110;
gm_ll58:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb14; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb16; }
        }
        goto gm_lad;
gm_ls110:;
gm_lb14:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_bind_unit_defaults_htn8poj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_bind_unit_defaults_htn8poj F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l2; /* values */
    void * gm_l3; /* binds */
    void * gm_l4; /* tys */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint32_t gm_l9; /* pid */
    void * gm_l10; /* d */
    gm_b16_8 gm_l11; /* elems */
    GM_STACK_CHECK(&F, 690u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tgsvmddj3e85e gm_l12;
    gm_t53at80pfdus3 gm_l13;
    uint8_t gm_l14;
    void * gm_l15;
    uint8_t gm_l16;
    gm_tkn42u7na3t81 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    void * t17 = gm_l0;
    void * t18 = GM_LD(void *, ((uint8_t *)t17));
    void * t19 = (void *)(((uint8_t *)t18) + 8);
    gm_l4 = t19;
    void * t20 = gm_l1;
    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t20) + 40));
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
    gm_l5 = t22;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t23 = gm_l6;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t25 = (t23 < t24);
            if (!t25) {
                goto gm_lb1;
            }
            {
                uint64_t t26 = gm_l6;
                void * t27 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t29 = gm_l6;
                gm_t5v0c8ia6a5mo t30 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t27) + (uintptr_t)t29 * 56));
                gm_tgsvmddj3e85e t31;
                GM_ST(uint64_t, ((uint8_t *)&t31 + 0), t26);
                GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t31 + 8), t30);
                gm_l12 = t31;
                {
                    {
                        uint64_t t32 = GM_LD(uint64_t, ((uint8_t *)&gm_l12));
                        gm_l7 = t32;
                        memmove(((uint8_t *)&F.l8), (((uint8_t *)&gm_l12) + 8), 56);
                        {
                            {
                                void * t33 = gm_l4;
                                gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                                gm_f_kind_jcnt4hm((void *)&gm_l13, t33, GM_CONV(gm_csguankko4lmhd, t34));
                                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)19ULL));
                                if (!t36) {
                                    goto gm_lb7;
                                }
                                uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 8));
                                gm_l9 = t37;
                                void * t38 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint64_t t40 = gm_l7;
                                if ((uintptr_t)t40 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t40, t39, gm_srcrorgjlg51brd, 332, 12);
                                {
                                    {
                                        uint8_t t41 = (uint8_t)(GM_LD(void *, (((uint8_t *)t38) + (uintptr_t)t40 * 8)) != 0);
                                        if (t41) {
                                            goto gm_lb9;
                                        }
                                        gm_l14 = ((uint8_t)0);
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        gm_l14 = ((uint8_t)1);
                                        goto gm_lb8;
                                    }
gm_lb10:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 332, 12);
                                }
gm_lb8:;
                                uint8_t t42 = gm_l14;
                                if (t42) {
                                    goto gm_lb3;
                                }
                                uint8_t t43 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l8) + 32)) != 0);
                                if (t43) {
                                    void * t44 = GM_LD(void *, (((uint8_t *)&F.l8) + 32));
                                    gm_l15 = t44;
                                } else {
                                    goto gm_lb3;
                                }
                                void * t45 = gm_l15;
                                gm_l10 = t45;
                                {
                                    {
                                        void * t46 = gm_l10;
                                        uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
                                        uint8_t t48 = (uint8_t)(t47 == ((uint8_t)10ULL));
                                        if (!t48) {
                                            goto gm_lb12;
                                        }
                                        gm_b16_8 t49 = GM_LD(gm_b16_8, ((((uint8_t *)t46) + 8) + 8));
                                        gm_l11 = t49;
                                        uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                        uint8_t t51 = ((t50 == ((uint64_t)0ULL)));
                                        if (!t51) {
                                            goto gm_lb12;
                                        }
                                        void * t52 = gm_l3;
                                        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)t52));
                                        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                                        uint32_t t55 = gm_l9;
                                        gm_tkn42u7na3t81 t56 = gm_f_bound_to_5c11t0h(t54, t55);
                                        gm_l17 = t56;
                                        {
                                            {
                                                uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l17))[4] != 0);
                                                if (t57) {
                                                    goto gm_lb14;
                                                }
                                                gm_l16 = ((uint8_t)1);
                                                goto gm_lb13;
                                            }
gm_lb14:;
                                            {
                                                gm_l16 = ((uint8_t)0);
                                                goto gm_lb13;
                                            }
gm_lb15:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 334, 59);
                                        }
gm_lb13:;
                                        uint8_t t58 = gm_l16;
                                        if (!t58) {
                                            goto gm_lb12;
                                        }
                                        void * t59 = gm_l3;
                                        uint32_t t60 = gm_l9;
                                        void * t61 = gm_l4;
                                        gm_t88rcucg1b7uk t62 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t61);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tplpbqf3duvn3 t63;
                                        GM_ST(uint32_t, ((uint8_t *)&t63 + 0), t60);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t63 + 4), t62);
                                        gm_tftb1odc739t5 t64 = gm_f_push_h69n5fi(t59, GM_CONV(gm_cs7e9ppsd384bj, t63));
                                        gm_l18 = t64;
                                        uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                        if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcrorgjlg51brd, 334, 101);
                                        goto gm_lb11;
                                    }
gm_lb12:;
                                }
gm_lb11:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 330, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t66 = gm_l6;
            uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
            uint64_t t69 = r67;
            gm_l6 = t69;
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
__attribute__((aligned(4))) uint8_t gm_f_check_generic_constraint_6138rrh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_check_generic_constraint_6138rrh F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l2; /* targs */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* k */
    uint32_t gm_l8; /* pid */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_t88rcucg1b7uk gm_l10; /* part */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l13; /* c */
    GM_STACK_CHECK(&F, 757u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tn68el8d14vku gm_l14;
    gm_tkn42u7na3t81 gm_l15;
    gm_tabb710d9mq2e gm_l16;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    void * t16 = (void *)(((uint8_t *)t15) + 8);
    gm_l4 = t16;
    void * t17 = gm_l1;
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t17) + 80));
    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
    gm_l5 = t19;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t20 = gm_l6;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                uint64_t t23 = gm_l6;
                void * t24 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t26 = gm_l6;
                uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t24) + (uintptr_t)t26 * 4));
                gm_tn68el8d14vku t28;
                GM_ST(uint32_t, (uint8_t *)&t28 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t28 + 0), t23);
                GM_ST(uint32_t, ((uint8_t *)&t28 + 8), t27);
                gm_l14 = t28;
                {
                    {
                        uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                        gm_l7 = t29;
                        uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 8));
                        gm_l8 = t30;
                        void * t31 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t33 = gm_l7;
                        if ((uintptr_t)t33 >= (uintptr_t)(t32)) gm_trap_index((uintptr_t)t33, t32, gm_srcrorgjlg51brd, 438, 14);
                        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t31) + (uintptr_t)t33 * 4));
                        gm_l9 = t34;
                        void * t35 = gm_l4;
                        gm_t88rcucg1b7uk t36 = gm_l9;
                        uint8_t t37 = gm_f_is_error_8k27gd1(t35, GM_CONV(gm_csguankko4lmhd, t36));
                        if (t37) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        {
                            {
                                void * t38 = gm_l0;
                                void * t39 = GM_LD(void *, ((uint8_t *)t38));
                                gm_t88rcucg1b7uk t40 = gm_l9;
                                gm_tkn42u7na3t81 t41 = gm_f_second_class_part_k6i54mk(t39, GM_CONV(gm_csguankko4lmhd, t40));
                                gm_l15 = t41;
                                uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l15))[4] != 0);
                                if (!t42) {
                                    goto gm_lb7;
                                }
                                gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l15));
                                gm_l10 = t43;
                                void * t44 = gm_l0;
                                gm_tl874t2atnlh3 t45 = gm_l3;
                                void * t46 = gm_l0;
                                void * t47 = GM_LD(void *, ((uint8_t *)t46));
                                gm_t88rcucg1b7uk t48 = gm_l9;
                                gm_t88rcucg1b7uk t49 = gm_l10;
                                gm_b16_8 t50 = gm_f_not_first_class_dve75oi((void *)NULL, cx, NULL, t47, t48, t49);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b0_1 t51 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t44, t45, t50);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        void * t52 = gm_l0;
                        void * t53 = GM_LD(void *, ((uint8_t *)t52));
                        uint32_t t54 = gm_l8;
                        gm_b16_8 t55 = gm_f_param_constraints_2k5fnnl(t53, t54);
                        gm_l11 = t55;
                        gm_l12 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t56 = gm_l12;
                                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                uint8_t t58 = (t56 < t57);
                                if (!t58) {
                                    goto gm_lb8;
                                }
                                {
                                    void * t59 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                    uint64_t t61 = gm_l12;
                                    {
                                        {
                                            gm_t9qkh0rsooc8d t62 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t59) + (uintptr_t)t61 * 1));
                                            gm_l13 = t62;
                                            void * t63 = gm_l0;
                                            void * t64 = GM_LD(void *, ((uint8_t *)t63));
                                            gm_t88rcucg1b7uk t65 = gm_l9;
                                            gm_t9qkh0rsooc8d t66 = gm_l13;
                                            uint8_t t67 = gm_f_satisfies_p0n2er8((void *)NULL, cx, NULL, t64, t65, t66);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t68 = (uint8_t)!t67;
                                            if (t68) {
                                                void * t69 = gm_l0;
                                                gm_tl874t2atnlh3 t70 = gm_l3;
                                                void * t71 = gm_l0;
                                                gm_t88rcucg1b7uk t72 = gm_l9;
                                                gm_b16_8 t73 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t71, t72);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_t9qkh0rsooc8d t74 = gm_l13;
                                                gm_b16_8 t75 = gm_f_constraint_name_ombut3o(t74);
                                                void * t76 = gm_l4;
                                                uint32_t t77 = gm_l8;
                                                gm_b16_8 t78 = gm_f_param_name_rbnop5l(t76, t77);
                                                gm_tfeab279c16rr t79;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t79 + 0), t73);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t79 + 16), t75);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t79 + 32), t78);
                                                gm_f_format_d2d7bae((void *)&gm_l16, (void *)NULL, cx, NULL, &t79);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                                if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcrorgjlg51brd, 443, 32);
                                                gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                                                gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                                                gm_b0_1 t83 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t69, t70, t82);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            goto gm_lb11;
                                        }
gm_lb12:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 441, 9);
                                    }
gm_lb11:;
                                }
gm_lb10:;
                                uint64_t t84 = gm_l12;
                                uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                                uint64_t t87 = r85;
                                gm_l12 = t87;
                            }
                        }
gm_lb8:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 437, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t88 = gm_l6;
            uint64_t r89; bool o90 = __builtin_add_overflow(t88, ((uint64_t)1ULL), &r89);
            uint64_t t91 = r89;
            gm_l6 = t91;
        }
    }
gm_lb1:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_clearly_value_f7icrt5(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clearly_value_f7icrt5 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t08olq97rscn3 gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 317u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l4;
    uint8_t gm_l5;
    gm_tqvu78rpi6q1h gm_l6;
    uint8_t gm_l7;
    void * t16 = gm_l1;
    {
        {
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)6ULL));
            if (!t18) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t16) + 8) + 8), 32);
            {
                {
                    void * t19 = gm_l0;
                    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                    gm_f_lookup_local_6f38t7p((void *)&gm_l6, t19, t20);
                    uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l6))[208] != 0);
                    if (!t21) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l6), 208);
                    {
                        {
                            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)5ULL));
                            if (!t23) {
                                goto gm_lb6;
                            }
                            gm_l7 = ((uint8_t)0);
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            gm_l7 = ((uint8_t)1);
                            goto gm_lb5;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 703, 54);
                    }
gm_lb5:;
                    uint8_t t24 = gm_l7;
                    gm_l5 = t24;
                    goto gm_lb3;
                }
gm_lb4:;
                gm_l5 = ((uint8_t)0);
            }
gm_lb3:;
            uint8_t t25 = gm_l5;
            gm_l4 = t25;
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
                    if (!t27) {
                        goto gm_lb10;
                    }
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)1ULL));
                    if (!t29) {
                        goto gm_lb11;
                    }
                    goto gm_lb9;
                }
gm_lb11:;
                {
                    uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)2ULL));
                    if (!t31) {
                        goto gm_lb12;
                    }
                    goto gm_lb9;
                }
gm_lb12:;
                {
                    uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
                    uint8_t t33 = (uint8_t)(t32 == ((uint8_t)3ULL));
                    if (!t33) {
                        goto gm_lb13;
                    }
                    goto gm_lb9;
                }
gm_lb13:;
                goto gm_lb8;
            }
gm_lb9:;
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb8:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb14:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 702, 63);
    }
gm_lb1:;
    uint8_t t34 = gm_l4;
    F.ret = t34;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_coerce_generic_args_9ald24q(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_coerce_generic_args_9ald24q F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l2; /* values */
    gm_b16_8 gm_l3; /* map */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_t88rcucg1b7uk gm_l11; /* pty */
    void * gm_l12; /* v */
    void * gm_l13; /* d */
    GM_STACK_CHECK(&F, 1031u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tgsvmddj3e85e gm_l14;
    gm_tftb1odc739t5 gm_l15;
    uint8_t gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_trqfvmjtt4sij t14;
    gm_f_list_vp1djv1_2((void *)&t14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t14;
    gm_trqfvmjtt4sij t15;
    gm_f_list_5dhgg4i((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t15;
    void * t16 = gm_l1;
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t16) + 40));
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
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
                uint64_t t22 = gm_l8;
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t25 = gm_l8;
                gm_t5v0c8ia6a5mo t26 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t23) + (uintptr_t)t25 * 56));
                gm_tgsvmddj3e85e t27;
                GM_ST(uint64_t, ((uint8_t *)&t27 + 0), t22);
                GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t27 + 8), t26);
                gm_l14 = t27;
                {
                    {
                        uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                        gm_l9 = t28;
                        memmove(((uint8_t *)&F.l10), (((uint8_t *)&gm_l14) + 8), 56);
                        void * t29 = (void *)((uint8_t *)&F.l6);
                        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                        gm_tftb1odc739t5 t31 = gm_f_push_ucgmlo2(t29, t30);
                        gm_l15 = t31;
                        uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                        if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcrorgjlg51brd, 458, 9);
                        {
                            {
                                uint8_t t33 = (uint8_t)(((((uint8_t *)&F.l10) + 40))[4] != 0);
                                if (t33) {
                                    goto gm_lb7;
                                }
                                gm_l16 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l16 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 459, 12);
                        }
gm_lb6:;
                        uint8_t t34 = gm_l16;
                        if (t34) {
                            goto gm_lb3;
                        }
                        void * t35 = gm_l0;
                        void * t36 = GM_LD(void *, ((uint8_t *)t35));
                        gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                        gm_b16_8 t38 = gm_l3;
                        gm_t88rcucg1b7uk t39 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t36, t37, t38);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l11 = t39;
                        {
                            {
                                void * t40 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint64_t t42 = gm_l9;
                                if ((uintptr_t)t42 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t42, t41, gm_srcrorgjlg51brd, 461, 17);
                                uint8_t t43 = (uint8_t)(GM_LD(void *, (((uint8_t *)t40) + (uintptr_t)t42 * 8)) != 0);
                                if (!t43) {
                                    goto gm_lb10;
                                }
                                void * t44 = GM_LD(void *, (((uint8_t *)t40) + (uintptr_t)t42 * 8));
                                gm_l12 = t44;
                                void * t45 = (void *)((uint8_t *)&F.l5);
                                void * t46 = gm_l0;
                                void * t47 = gm_l12;
                                gm_t88rcucg1b7uk t48 = gm_l11;
                                void * t49 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t46, t47, t48);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t50 = gm_f_push_d943vgp_2(t45, &(*(gm_tcgrjq9toscke *)((uint8_t *)t49)));
                                gm_l17 = t50;
                                uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcrorgjlg51brd, 462, 13);
                                goto gm_lb9;
                            }
gm_lb10:;
                            {
                                {
                                    uint8_t t52 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l10) + 32)) != 0);
                                    if (!t52) {
                                        goto gm_lb12;
                                    }
                                    void * t53 = GM_LD(void *, (((uint8_t *)&F.l10) + 32));
                                    gm_l13 = t53;
                                    void * t54 = (void *)((uint8_t *)&F.l5);
                                    void * t55 = gm_l0;
                                    void * t56 = (void *)((uint8_t *)&F.l10);
                                    void * t57 = gm_l1;
                                    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)t57) + 32));
                                    void * t59 = gm_l13;
                                    gm_t88rcucg1b7uk t60 = gm_l11;
                                    gm_tl874t2atnlh3 t61 = gm_l4;
                                    void * t62 = gm_f_param_default_h93ci1m((void *)NULL, cx, NULL, t55, t56, t58, t59, t60, t61);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tftb1odc739t5 t63 = gm_f_push_d943vgp_2(t54, &(*(gm_tcgrjq9toscke *)((uint8_t *)t62)));
                                    gm_l18 = t63;
                                    uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                    if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcrorgjlg51brd, 464, 13);
                                    goto gm_lb11;
                                }
gm_lb12:;
                                void * t65 = gm_l0;
                                gm_tl874t2atnlh3 t66 = gm_l4;
                                gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                gm_tnpifv20irnph t68;
                                GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t67);
                                gm_f_format_qk20moi((void *)&gm_l19, (void *)NULL, cx, NULL, t68);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srcrorgjlg51brd, 466, 28);
                                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                                gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                                gm_b0_1 t72 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t65, t66, t71);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t73 = (void *)((uint8_t *)&F.l5);
                                void * t74 = gm_l0;
                                void * t75 = GM_LD(void *, ((uint8_t *)t74));
                                gm_tl874t2atnlh3 t76 = gm_l4;
                                void * t77 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t75, t76);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t78 = gm_f_push_d943vgp_2(t73, &(*(gm_tcgrjq9toscke *)((uint8_t *)t77)));
                                gm_l20 = t78;
                                uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcrorgjlg51brd, 467, 13);
                            }
gm_lb11:;
                        }
gm_lb9:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 457, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t80 = gm_l8;
            uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
            uint64_t t83 = r81;
            gm_l8 = t83;
        }
    }
gm_lb1:;
    gm_trqfvmjtt4sij t84 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l5));
    gm_trqfvmjtt4sij t85 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l6));
    gm_td7pkamr64l0c t86;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t86 + 0), t84);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t86 + 40), t85);
    memmove(gm_out, ((uint8_t *)&t86), 80);
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_comptime_arg_vtq6sop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2) {
    struct gm_fr_comptime_arg_vtq6sop F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    GM_STACK_CHECK(&F, 504u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tqvu78rpi6q1h gm_l5;
    uint8_t gm_l6;
    gm_tabb710d9mq2e gm_l7;
    {
        {
            void * t11 = gm_l2;
            uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t11) + 8) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)6ULL));
            if (!t13) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t11) + 8) + 8), 32);
            {
                {
                    void * t14 = gm_l0;
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_f_lookup_local_6f38t7p((void *)&gm_l5, t14, t15);
                    uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l5))[208] != 0);
                    if (!t16) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&F.l4), ((uint8_t *)&gm_l5), 208);
                    {
                        {
                            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)3ULL));
                            if (!t18) {
                                goto gm_lb6;
                            }
                            gm_l6 = ((uint8_t)0);
                            goto gm_lb5;
                        }
gm_lb6:;
                        {
                            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
                            if (!t20) {
                                goto gm_lb7;
                            }
                            gm_l6 = ((uint8_t)0);
                            goto gm_lb5;
                        }
gm_lb7:;
                        {
                            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)4ULL));
                            if (!t22) {
                                goto gm_lb8;
                            }
                            gm_l6 = ((uint8_t)0);
                            goto gm_lb5;
                        }
gm_lb8:;
                        {
                            gm_l6 = ((uint8_t)1);
                            goto gm_lb5;
                        }
gm_lb9:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 530, 46);
                    }
gm_lb5:;
                    uint8_t t23 = gm_l6;
                    if (!t23) {
                        goto gm_lb4;
                    }
                    void * t24 = gm_l0;
                    void * t25 = gm_l2;
                    gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 72));
                    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                    gm_tnpifv20irnph t28;
                    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t27);
                    gm_f_format_8tl219j((void *)&gm_l7, (void *)NULL, cx, NULL, t28);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                    if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcrorgjlg51brd, 531, 30);
                    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                    gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
                    gm_b0_1 t32 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t24, t26, t31);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tkn42u7na3t81 t33;
                    memset(&t33, 0, sizeof t33);
                    F.ret = t33;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t34 = gm_l0;
    void * t35 = GM_LD(void *, ((uint8_t *)t34));
    void * t36 = gm_l0;
    uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)t36) + 8));
    gm_t88rcucg1b7uk t38 = gm_l1;
    void * t39 = gm_l2;
    gm_tkn42u7na3t81 t40 = gm_f_comptime_value_l5orc93((void *)NULL, cx, NULL, t35, t37, t38, t39);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t40;
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
__attribute__((aligned(4))) uint8_t gm_f_denotes_type_jgfet92(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_denotes_type_jgfet92 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* inner */
    gm_t08olq97rscn3 gm_l3; /* id */
    GM_STACK_CHECK(&F, 541u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l5;
    gm_tqvu78rpi6q1h gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    gm_tkn42u7na3t81 gm_l9;
    uint8_t gm_l10;
    gm_tkn42u7na3t81 gm_l11;
    uint8_t gm_l12;
    gm_tkn42u7na3t81 gm_l13;
    void * t23 = gm_l1;
    {
        {
            uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t23) + 8) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)9ULL));
            if (!t25) {
                goto gm_lb2;
            }
            void * t26 = GM_LD(void *, ((((uint8_t *)t23) + 8) + 8));
            gm_l2 = t26;
            void * t27 = gm_l0;
            void * t28 = gm_l2;
            uint8_t t29 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t29;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t23) + 8) + 0));
            uint8_t t31 = (uint8_t)(t30 == ((uint8_t)6ULL));
            if (!t31) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t23) + 8) + 8), 32);
            void * t32 = gm_l0;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_f_lookup_local_6f38t7p((void *)&gm_l6, t32, t33);
            {
                {
                    uint8_t t34 = (uint8_t)((((uint8_t *)&gm_l6))[208] != 0);
                    if (t34) {
                        goto gm_lb5;
                    }
                    gm_l5 = ((uint8_t)0);
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    gm_l5 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 713, 16);
            }
gm_lb4:;
            uint8_t t35 = gm_l5;
            if (t35) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t36 = gm_l0;
            void * t37 = GM_LD(void *, ((uint8_t *)t36));
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_tkn42u7na3t81 t39 = gm_f_type_param_3av4r8i(t37, t38);
            gm_l9 = t39;
            {
                {
                    uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l9))[4] != 0);
                    if (t40) {
                        goto gm_lb8;
                    }
                    gm_l8 = ((uint8_t)0);
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    gm_l8 = ((uint8_t)1);
                    goto gm_lb7;
                }
gm_lb9:;
                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 714, 20);
            }
gm_lb7:;
            uint8_t t41 = gm_l8;
            if (t41) {
                gm_l7 = ((uint8_t)1);
            } else {
                void * t42 = gm_l0;
                void * t43 = gm_l1;
                gm_tkn42u7na3t81 t44 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t42, t43);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t44;
                {
                    {
                        uint8_t t45 = (uint8_t)((((uint8_t *)&gm_l11))[4] != 0);
                        if (t45) {
                            goto gm_lb11;
                        }
                        gm_l10 = ((uint8_t)0);
                        goto gm_lb10;
                    }
gm_lb11:;
                    {
                        gm_l10 = ((uint8_t)1);
                        goto gm_lb10;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 714, 60);
                }
gm_lb10:;
                uint8_t t46 = gm_l10;
                gm_l7 = t46;
            }
            uint8_t t47 = gm_l7;
            F.ret = t47;
            goto gm_fret;
        }
gm_lb3:;
        {
            {
                {
                    uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t23) + 8) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)19ULL));
                    if (!t49) {
                        goto gm_lb15;
                    }
                    goto gm_lb14;
                }
gm_lb15:;
                {
                    uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t23) + 8) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)22ULL));
                    if (!t51) {
                        goto gm_lb16;
                    }
                    goto gm_lb14;
                }
gm_lb16:;
                goto gm_lb13;
            }
gm_lb14:;
            void * t52 = gm_l0;
            void * t53 = gm_l1;
            gm_tkn42u7na3t81 t54 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t54;
            {
                {
                    uint8_t t55 = (uint8_t)((((uint8_t *)&gm_l13))[4] != 0);
                    if (t55) {
                        goto gm_lb18;
                    }
                    gm_l12 = ((uint8_t)0);
                    goto gm_lb17;
                }
gm_lb18:;
                {
                    gm_l12 = ((uint8_t)1);
                    goto gm_lb17;
                }
gm_lb19:;
                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 716, 39);
            }
gm_lb17:;
            uint8_t t56 = gm_l12;
            F.ret = t56;
            goto gm_fret;
        }
gm_lb13:;
        {
            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t23) + 8) + 0));
            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)49ULL));
            if (!t58) {
                goto gm_lb20;
            }
            memmove(((uint8_t *)&F.l4), ((((uint8_t *)t23) + 8) + 8), 56);
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
            gm_b16_8 t60;
            GM_ST(void *, (uint8_t *)&t60, (void *)(gm_slbgk65sat4bm));
            GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(7));
            uint8_t t61 = (gm_eq0iqiubfm7e(((uint8_t *)&t59), ((uint8_t *)&t60)));
            F.ret = t61;
            goto gm_fret;
        }
gm_lb20:;
        {
            void * t62 = gm_l0;
            void * t63 = GM_LD(void *, ((uint8_t *)t62));
            void * t64 = gm_l0;
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)t64) + 8));
            void * t66 = gm_l1;
            uint8_t t67 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t63, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t67;
            goto gm_fret;
        }
gm_lb21:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 710, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_effect_args_name_jgklot1(void * a0, void * a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_args_name_jgklot1 F;
    void * gm_l1; /* e */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 175u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    void * t9 = gm_l1;
    uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)t9));
    uint8_t t11 = ((t10 == ((uint32_t)0ULL)));
    if (t11) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    F.l3 = ((uint8_t)0);
    void * t12 = gm_l1;
    gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 8));
    gm_l4 = t13;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t15 = gm_l5;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t20 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t18) + (uintptr_t)t20 * 40), 40);
                        {
                            {
                                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                uint8_t t22 = (uint8_t)(t21 == ((uint8_t)0ULL));
                                if (!t22) {
                                    goto gm_lb7;
                                }
                                gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                                gm_l7 = t23;
                                void * t24 = F.l0;
                                gm_t88rcucg1b7uk t25 = gm_l7;
                                gm_closure t26 = ((gm_closure){(void *)gm_f_fn_tqlm0ij, (void *)&F.h, cx});
                                gm_f_walk_v858co7(t24, GM_CONV(gm_csguankko4lmhd, t25), t26);
                                if (gm_unw.kind) {
                                    goto gm_ll14;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 622, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t27 = gm_l5;
            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
            uint64_t t30 = r28;
            gm_l5 = t30;
        }
    }
    goto gm_ls31;
gm_ll14:;
    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb1; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb3; }
    }
    goto gm_lad;
gm_ls31:;
gm_lb1:;
    uint8_t t32 = F.l3;
    F.ret = t32;
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_dgpelc4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_dink6tf(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hitalag(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_hitalag F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    uint32_t gm_l1; /* q */
    GM_STACK_CHECK_SHALLOW(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t53at80pfdus3 gm_l2;
    {
        {
            void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l0));
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            void * t6 = (void *)(((uint8_t *)t5) + 8);
            gm_t88rcucg1b7uk t7 = gm_l0;
            gm_f_kind_jcnt4hm((void *)&gm_l2, t6, GM_CONV(gm_csguankko4lmhd, t7));
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)19ULL));
            if (!t9) {
                goto gm_lb2;
            }
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
            gm_l1 = t10;
            uint32_t t11 = gm_l1;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l59));
            uint8_t t13 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t12) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t12))) + i * 4, ((uint8_t *)&t11))) { t13 = 1; break; }
            uint8_t t14 = (uint8_t)(!t13);
            if (!t14) {
                goto gm_lb2;
            }
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l82), ((uint8_t)1));
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pjcgmfa(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_pjcgmfa F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    uint32_t gm_l1; /* q */
    GM_STACK_CHECK_SHALLOW(&F, 175u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t53at80pfdus3 gm_l2;
    uint8_t gm_l3;
    gm_tkn42u7na3t81 gm_l4;
    {
        {
            void * t7 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l0));
            void * t8 = GM_LD(void *, ((uint8_t *)t7));
            void * t9 = (void *)(((uint8_t *)t8) + 8);
            gm_t88rcucg1b7uk t10 = gm_l0;
            gm_f_kind_jcnt4hm((void *)&gm_l2, t9, GM_CONV(gm_csguankko4lmhd, t10));
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)19ULL));
            if (!t12) {
                goto gm_lb2;
            }
            uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
            gm_l1 = t13;
            uint32_t t14 = gm_l1;
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l5) + 80));
            uint8_t t16 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t15) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t15))) + i * 4, ((uint8_t *)&t14))) { t16 = 1; break; }
            uint8_t t17 = (uint8_t)(t16);
            if (!t17) {
                goto gm_lb2;
            }
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l30));
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            uint32_t t20 = gm_l1;
            gm_tkn42u7na3t81 t21 = gm_f_bound_to_5c11t0h(t19, t20);
            gm_l4 = t21;
            {
                {
                    uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l4))[4] != 0);
                    if (t22) {
                        goto gm_lb4;
                    }
                    gm_l3 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l3 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 182, 72);
            }
gm_lb3:;
            uint8_t t23 = gm_l3;
            if (!t23) {
                goto gm_lb2;
            }
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_generic_call_packed_h7ssi9n *)((&F.h)->up))->l73), ((uint8_t)1));
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_qtpj77g(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_qtpj77g F;
    gm_t88rcucg1b7uk gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* l */
    gm_t88rcucg1b7uk gm_l2; /* r */
    uint32_t gm_l3; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 427u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t53at80pfdus3 gm_l4;
    void * t6 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l0));
    void * t7 = (void *)(((uint8_t *)t6) + 8);
    gm_t88rcucg1b7uk t8 = gm_l0;
    gm_f_kind_jcnt4hm((void *)&gm_l4, t7, GM_CONV(gm_csguankko4lmhd, t8));
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)22ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l4) + 8) + 4));
            gm_l1 = t11;
            gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l4) + 8) + 8));
            gm_l2 = t12;
            void * t13 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l0));
            void * t14 = (void *)(((uint8_t *)t13) + 8);
            gm_t88rcucg1b7uk t15 = gm_l1;
            gm_t53at80pfdus3 t16;
            gm_f_kind_jcnt4hm((void *)&t16, t14, GM_CONV(gm_csguankko4lmhd, t15));
            uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l3));
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)19ULL));
            if (t19) {
                uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&t16) + 8));
                t19 = (uint8_t)(t20 == t17);
            }
            uint8_t t21 = (uint8_t)(t19);
            gm_t53at80pfdus3 t22;
            memset(&t22, 0, sizeof t22);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)19ULL));
            GM_ST(uint32_t, ((uint8_t *)&t22 + 8), t17);
            if (t21) {
                uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l12));
                uint32_t r24; bool o25 = __builtin_add_overflow(t23, ((uint32_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `+`", gm_srcrorgjlg51brd, 605, 55);
                uint32_t t26 = r24;
                GM_ST(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l12), t26);
            }
            void * t27 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l0));
            void * t28 = (void *)(((uint8_t *)t27) + 8);
            gm_t88rcucg1b7uk t29 = gm_l2;
            gm_t53at80pfdus3 t30;
            gm_f_kind_jcnt4hm((void *)&t30, t28, GM_CONV(gm_csguankko4lmhd, t29));
            uint32_t t31 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l3));
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)19ULL));
            if (t33) {
                uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)&t30) + 8));
                t33 = (uint8_t)(t34 == t31);
            }
            uint8_t t35 = (uint8_t)(t33);
            gm_t53at80pfdus3 t36;
            memset(&t36, 0, sizeof t36);
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)19ULL));
            GM_ST(uint32_t, ((uint8_t *)&t36 + 8), t31);
            if (t35) {
                uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l12));
                uint32_t r38; bool o39 = __builtin_add_overflow(t37, ((uint32_t)1ULL), &r38);
                if (o39) gm_trap("integer overflow in `+`", gm_srcrorgjlg51brd, 606, 55);
                uint32_t t40 = r38;
                GM_ST(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l12), t40);
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)19ULL));
            if (!t42) {
                goto gm_lb3;
            }
            uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
            gm_l3 = t43;
            uint32_t t44 = gm_l3;
            uint32_t t45 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l3));
            uint8_t t46 = ((t44 == t45));
            if (t46) {
                uint32_t t47 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l11));
                uint32_t r48; bool o49 = __builtin_add_overflow(t47, ((uint32_t)1ULL), &r48);
                if (o49) gm_trap("integer overflow in `+`", gm_srcrorgjlg51brd, 608, 44);
                uint32_t t50 = r48;
                GM_ST(uint32_t, ((uint8_t *)&((struct gm_fr_inferable_param_f80lrdi *)((&F.h)->up))->l11), t50);
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 603, 13);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tqlm0ij(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_uualn6s(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_uualn6s F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    uint32_t gm_l1; /* q */
    GM_STACK_CHECK_SHALLOW(&F, 191u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t53at80pfdus3 gm_l2;
    uint8_t gm_l3;
    gm_tkn42u7na3t81 gm_l4;
    {
        {
            void * t7 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_bind_literal_fallbacks_r2021qp *)((&F.h)->up))->l0));
            void * t8 = GM_LD(void *, ((uint8_t *)t7));
            void * t9 = (void *)(((uint8_t *)t8) + 8);
            gm_t88rcucg1b7uk t10 = gm_l0;
            gm_f_kind_jcnt4hm((void *)&gm_l2, t9, GM_CONV(gm_csguankko4lmhd, t10));
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)19ULL));
            if (!t12) {
                goto gm_lb2;
            }
            uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
            gm_l1 = t13;
            uint32_t t14 = gm_l1;
            void * t15 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_bind_literal_fallbacks_r2021qp *)((&F.h)->up))->l1));
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 80));
            uint8_t t17 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t16) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t16))) + i * 4, ((uint8_t *)&t14))) { t17 = 1; break; }
            uint8_t t18 = (uint8_t)(t17);
            if (!t18) {
                goto gm_lb2;
            }
            void * t19 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_bind_literal_fallbacks_r2021qp *)((&F.h)->up))->l2));
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)t19));
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            uint32_t t22 = gm_l1;
            gm_tkn42u7na3t81 t23 = gm_f_bound_to_5c11t0h(t21, t22);
            gm_l4 = t23;
            {
                {
                    uint8_t t24 = (uint8_t)((((uint8_t *)&gm_l4))[4] != 0);
                    if (t24) {
                        goto gm_lb4;
                    }
                    gm_l3 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l3 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 313, 76);
            }
gm_lb3:;
            uint8_t t25 = gm_l3;
            if (!t25) {
                goto gm_lb2;
            }
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_bind_literal_fallbacks_r2021qp *)((&F.h)->up))->l11), ((uint8_t)1));
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_generic_call_2tlofdi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_generic_call_2tlofdi F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* receiver */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_b16_8 gm_l6; /* written */
    uint8_t gm_l7; /* rest */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_tf5srh4mp4b3i gm_l11; /* recv */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_t08olq97rscn3 gm_l20; /* n */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_b16_8 gm_l27; /* a */
    gm_tl874t2atnlh3 gm_l28; /* sp */
    gm_b16_8 gm_l29; /* msg */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    void * gm_l34; /* r */
    void * gm_l35; /* x */
    gm_b16_8 gm_l36; /* spread */
    void * gm_l37; /* call */
    GM_STACK_CHECK(&F, 2206u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l38;
    gm_tgsvmddj3e85e gm_l39;
    uint8_t gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tgsvmddj3e85e gm_l42;
    uint8_t gm_l43;
    uint8_t gm_l44;
    uint8_t gm_l45;
    gm_tftb1odc739t5 gm_l46;
    gm_tck5nas0nf5v8 gm_l47;
    uint8_t gm_l48;
    gm_th2lqbo77pvsc gm_l49;
    gm_b16_8 gm_l50;
    {
        void * t45 = gm_l0;
        void * t46 = GM_LD(void *, ((uint8_t *)t45));
        void * t47 = GM_LD(void *, (((uint8_t *)t46) + 344));
        uint64_t t48 = GM_LD(uint64_t, ((((uint8_t *)t46) + 344) + 8));
        uint32_t t49 = gm_l1;
        uint64_t t50 = (uint64_t)t49;
        if ((uintptr_t)t50 >= (uintptr_t)(t48)) gm_trap_index((uintptr_t)t50, t48, gm_srcrorgjlg51brd, 27, 12);
        memmove(((uint8_t *)&F.l5), (((uint8_t *)t47) + (uintptr_t)t50 * 136), 136);
        gm_b16_8 t51 = gm_l3;
        gm_l6 = t51;
        gm_l7 = ((uint8_t)0);
        gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
        gm_l8 = t52;
        gm_l9 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t53 = gm_l9;
                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint8_t t55 = (t53 < t54);
                if (!t55) {
                    goto gm_lb1;
                }
                {
                    void * t56 = GM_LD(void *, ((uint8_t *)&gm_l8));
                    uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t58 = gm_l9;
                    {
                        {
                            memmove(((uint8_t *)&F.l10), (((uint8_t *)t56) + (uintptr_t)t58 * 56), 56);
                            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 48));
                            if (t59) {
                                gm_l7 = ((uint8_t)1);
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 31, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t60 = gm_l9;
                uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                uint64_t t63 = r61;
                gm_l9 = t63;
            }
        }
gm_lb1:;
        uint8_t t64 = gm_l7;
        if (t64) {
            gm_tf5srh4mp4b3i t65;
            memset(&t65, 0, sizeof t65);
            gm_l11 = t65;
            {
                {
                    uint8_t t66 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
                    if (t66) {
                        goto gm_lb7;
                    }
                    gm_l38 = ((uint8_t)0);
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    gm_l38 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 36, 12);
            }
gm_lb6:;
            uint8_t t67 = gm_l38;
            if (t67) {
                gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                gm_l12 = t69;
                gm_l13 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t70 = gm_l13;
                        uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                        uint8_t t72 = (t70 < t71);
                        if (!t72) {
                            goto gm_lb9;
                        }
                        {
                            uint64_t t73 = gm_l13;
                            void * t74 = GM_LD(void *, ((uint8_t *)&gm_l12));
                            uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                            uint64_t t76 = gm_l13;
                            gm_t5v0c8ia6a5mo t77 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t74) + (uintptr_t)t76 * 56));
                            gm_tgsvmddj3e85e t78;
                            GM_ST(uint64_t, ((uint8_t *)&t78 + 0), t73);
                            GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t78 + 8), t77);
                            gm_l39 = t78;
                            {
                                {
                                    uint64_t t79 = GM_LD(uint64_t, ((uint8_t *)&gm_l39));
                                    gm_l14 = t79;
                                    memmove(((uint8_t *)&F.l15), (((uint8_t *)&gm_l39) + 8), 56);
                                    {
                                        {
                                            uint8_t t80 = (uint8_t)(((((uint8_t *)&F.l15) + 40))[4] != 0);
                                            if (t80) {
                                                goto gm_lb15;
                                            }
                                            gm_l40 = ((uint8_t)1);
                                            goto gm_lb14;
                                        }
gm_lb15:;
                                        {
                                            gm_l40 = ((uint8_t)0);
                                            goto gm_lb14;
                                        }
gm_lb16:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 38, 20);
                                    }
gm_lb14:;
                                    uint8_t t81 = gm_l40;
                                    if (t81) {
                                        uint64_t t82 = gm_l14;
                                        gm_tf5srh4mp4b3i t83;
                                        GM_ST(uint8_t, (uint8_t *)&t83 + 9, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t83 + 10, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t83 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t83), t82);
                                        ((uint8_t *)&t83)[8] = 1;
                                        gm_l11 = t83;
                                        goto gm_lb9;
                                    }
                                    goto gm_lb12;
                                }
gm_lb13:;
                                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 37, 13);
                            }
gm_lb12:;
                        }
gm_lb11:;
                        uint64_t t84 = gm_l13;
                        uint64_t r85; bool o86 = __builtin_add_overflow(t84, ((uint64_t)1ULL), &r85);
                        uint64_t t87 = r85;
                        gm_l13 = t87;
                    }
                }
gm_lb9:;
            }
            gm_trqfvmjtt4sij t88;
            gm_f_list_epvducg((void *)&t88, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l16 = t88;
            gm_b16_8 t89 = gm_l3;
            gm_l17 = t89;
            gm_l18 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t90 = gm_l18;
                    uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint8_t t92 = (t90 < t91);
                    if (!t92) {
                        goto gm_lb17;
                    }
                    {
                        void * t93 = GM_LD(void *, ((uint8_t *)&gm_l17));
                        uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                        uint64_t t95 = gm_l18;
                        {
                            {
                                memmove(((uint8_t *)&F.l19), (((uint8_t *)t93) + (uintptr_t)t95 * 144), 144);
                                {
                                    {
                                        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 0));
                                        uint8_t t97 = (uint8_t)(t96 == ((uint8_t)1ULL));
                                        if (!t97) {
                                            goto gm_lb23;
                                        }
                                        memmove(((uint8_t *)&gm_l20), (((uint8_t *)&F.l19) + 8), 32);
                                        void * t98 = (void *)((uint8_t *)&F.l16);
                                        gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
                                        gm_tftb1odc739t5 t100 = gm_f_push_3ef746b(t98, t99);
                                        gm_l41 = t100;
                                        uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                        if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srcrorgjlg51brd, 46, 41);
                                        goto gm_lb22;
                                    }
gm_lb23:;
                                }
gm_lb22:;
                                goto gm_lb20;
                            }
gm_lb21:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 45, 9);
                        }
gm_lb20:;
                    }
gm_lb19:;
                    uint64_t t102 = gm_l18;
                    uint64_t r103; bool o104 = __builtin_add_overflow(t102, ((uint64_t)1ULL), &r103);
                    uint64_t t105 = r103;
                    gm_l18 = t105;
                }
            }
gm_lb17:;
            gm_trqfvmjtt4sij t106;
            gm_f_list_s83jn2q((void *)&t106, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l21 = t106;
            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
            gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
            gm_l22 = t108;
            gm_l23 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t109 = gm_l23;
                    uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                    uint8_t t111 = (t109 < t110);
                    if (!t111) {
                        goto gm_lb24;
                    }
                    {
                        uint64_t t112 = gm_l23;
                        void * t113 = GM_LD(void *, ((uint8_t *)&gm_l22));
                        uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                        uint64_t t115 = gm_l23;
                        gm_t5v0c8ia6a5mo t116 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t113) + (uintptr_t)t115 * 56));
                        gm_tgsvmddj3e85e t117;
                        GM_ST(uint64_t, ((uint8_t *)&t117 + 0), t112);
                        GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t117 + 8), t116);
                        gm_l42 = t117;
                        {
                            {
                                uint64_t t118 = GM_LD(uint64_t, ((uint8_t *)&gm_l42));
                                gm_l24 = t118;
                                memmove(((uint8_t *)&F.l25), (((uint8_t *)&gm_l42) + 8), 56);
                                void * t119 = (void *)((uint8_t *)&F.l21);
                                gm_tf5srh4mp4b3i t120 = gm_l11;
                                uint64_t t121 = gm_l24;
                                gm_tf5srh4mp4b3i t122;
                                GM_ST(uint8_t, (uint8_t *)&t122 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t122 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t122 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t122), t121);
                                ((uint8_t *)&t122)[8] = 1;
                                uint8_t t123 = (!gm_eq7mri15hk78(((uint8_t *)&t120), ((uint8_t *)&t122)));
                                if (t123) {
                                    void * t124 = gm_l0;
                                    void * t125 = GM_LD(void *, ((uint8_t *)t124));
                                    void * t126 = (void *)((uint8_t *)&F.l5);
                                    uint64_t t127 = gm_l24;
                                    uint8_t t128 = gm_f_inferable_param_f80lrdi(t125, t126, t127);
                                    uint8_t t129 = (uint8_t)!t128;
                                    gm_l45 = t129;
                                } else {
                                    gm_l45 = ((uint8_t)0);
                                }
                                uint8_t t130 = gm_l45;
                                if (t130) {
                                    void * t131 = (void *)((uint8_t *)&F.l25);
                                    uint8_t t132 = gm_f_is_site_8sr7vam(t131);
                                    uint8_t t133 = (uint8_t)!t132;
                                    gm_l44 = t133;
                                } else {
                                    gm_l44 = ((uint8_t)0);
                                }
                                uint8_t t134 = gm_l44;
                                if (t134) {
                                    gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
                                    gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
                                    uint8_t t137 = 0;
                                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t136) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t136))) + i * 16, ((uint8_t *)&t135))) { t137 = 1; break; }
                                    uint8_t t138 = (uint8_t)(!t137);
                                    gm_l43 = t138;
                                } else {
                                    gm_l43 = ((uint8_t)0);
                                }
                                uint8_t t139 = gm_l43;
                                gm_tftb1odc739t5 t140 = gm_f_push_vd1290p(t119, t139);
                                gm_l46 = t140;
                                uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                if (!t141) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_srcrorgjlg51brd, 49, 45);
                                goto gm_lb27;
                            }
gm_lb28:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 49, 9);
                        }
gm_lb27:;
                    }
gm_lb26:;
                    uint64_t t142 = gm_l23;
                    uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                    uint64_t t145 = r143;
                    gm_l23 = t145;
                }
            }
gm_lb24:;
            {
                {
                    gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                    gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
                    gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
                    gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                    gm_b16_8 t150 = gm_l3;
                    gm_tl874t2atnlh3 t151 = gm_l4;
                    gm_f_pack_rest_args_cknfrfl((void *)&gm_l47, (void *)NULL, cx, NULL, t147, t149, t150, t151);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t152 = (uint8_t)((((uint8_t *)&gm_l47))[40] != 0);
                    if (!t152) {
                        goto gm_lb30;
                    }
                    memmove(((uint8_t *)&F.l26), ((uint8_t *)&gm_l47), 40);
                    {
                        {
                            uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                            uint8_t t154 = (uint8_t)(t153 == ((uint8_t)0ULL));
                            if (!t154) {
                                goto gm_lb32;
                            }
                            gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 8));
                            gm_l27 = t155;
                            gm_b16_8 t156 = gm_l27;
                            gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                            gm_l6 = t157;
                            goto gm_lb31;
                        }
gm_lb32:;
                        {
                            uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                            uint8_t t159 = (uint8_t)(t158 == ((uint8_t)1ULL));
                            if (!t159) {
                                goto gm_lb33;
                            }
                            gm_tl874t2atnlh3 t160 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 8));
                            gm_l28 = t160;
                            gm_b16_8 t161 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l26) + 8) + 16));
                            gm_l29 = t161;
                            void * t162 = gm_l0;
                            gm_tl874t2atnlh3 t163 = gm_l28;
                            gm_b16_8 t164 = gm_l29;
                            gm_b0_1 t165 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t162, t163, t164);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t166 = gm_l0;
                            void * t167 = GM_LD(void *, ((uint8_t *)t166));
                            gm_tl874t2atnlh3 t168 = gm_l4;
                            void * t169 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t167, t168);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t169;
                            goto gm_fret;
                        }
gm_lb33:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 51, 13);
                    }
gm_lb31:;
                    goto gm_lb29;
                }
gm_lb30:;
            }
gm_lb29:;
        }
    }
    {
        gm_b16_8 t170 = gm_l6;
        gm_l30 = t170;
        gm_l31 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t171 = gm_l31;
                uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                uint8_t t173 = (t171 < t172);
                if (!t173) {
                    goto gm_lb34;
                }
                {
                    void * t174 = GM_LD(void *, ((uint8_t *)&gm_l30));
                    uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                    uint64_t t176 = gm_l31;
                    {
                        {
                            memmove(((uint8_t *)&F.l32), (((uint8_t *)t174) + (uintptr_t)t176 * 144), 144);
                            {
                                {
                                    uint8_t t177 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 0));
                                    uint8_t t178 = (uint8_t)(t177 == ((uint8_t)2ULL));
                                    if (!t178) {
                                        goto gm_lb40;
                                    }
                                    gm_l48 = ((uint8_t)1);
                                    goto gm_lb39;
                                }
gm_lb40:;
                                {
                                    gm_l48 = ((uint8_t)0);
                                    goto gm_lb39;
                                }
gm_lb41:;
                                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 61, 12);
                            }
gm_lb39:;
                            uint8_t t179 = gm_l48;
                            if (t179) {
                                gm_trqfvmjtt4sij t180;
                                gm_f_list_ir0h3tc_2((void *)&t180, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l33 = t180;
                                gm_l34 = ((void *)0);
                                {
                                    {
                                        uint8_t t181 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
                                        if (!t181) {
                                            goto gm_lb43;
                                        }
                                        void * t182 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                        gm_l35 = t182;
                                        void * t183 = gm_l0;
                                        void * t184 = gm_l35;
                                        void * t185 = (void *)((uint8_t *)&F.l33);
                                        void * t186 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t183, t184, t185);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l34 = t186;
                                        goto gm_lb42;
                                    }
gm_lb43:;
                                }
gm_lb42:;
                                void * t187 = gm_l0;
                                gm_b16_8 t188 = gm_l6;
                                void * t189 = (void *)((uint8_t *)&F.l33);
                                gm_f_spread_generic_args_8qtof2f((void *)&gm_l49, (void *)NULL, cx, NULL, t187, t188, t189);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t190 = (uint8_t)((((uint8_t *)&gm_l49))[16] != 0);
                                if (t190) {
                                    gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l49));
                                    gm_l50 = t191;
                                } else {
                                    void * t192 = gm_l0;
                                    void * t193 = GM_LD(void *, ((uint8_t *)t192));
                                    gm_tl874t2atnlh3 t194 = gm_l4;
                                    void * t195 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t193, t194);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t195;
                                    goto gm_fret;
                                }
                                gm_b16_8 t196 = gm_l50;
                                gm_l36 = t196;
                                void * t197 = gm_l0;
                                uint32_t t198 = gm_l1;
                                void * t199 = gm_l34;
                                gm_b16_8 t200 = gm_l36;
                                gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
                                gm_tl874t2atnlh3 t202 = gm_l4;
                                void * t203 = gm_f_generic_call_packed_h7ssi9n((void *)NULL, cx, NULL, t197, t198, t199, t201, t202);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l37 = t203;
                                void * t204 = gm_l37;
                                gm_t88rcucg1b7uk t205 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t204));
                                gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&F.l33));
                                void * t207 = gm_l37;
                                gm_tl874t2atnlh3 t208 = gm_l4;
                                void * t209 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t205, t206, t207, t208);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t209;
                                goto gm_fret;
                            }
                            goto gm_lb37;
                        }
gm_lb38:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 60, 5);
                    }
gm_lb37:;
                }
gm_lb36:;
                uint64_t t210 = gm_l31;
                uint64_t r211; bool o212 = __builtin_add_overflow(t210, ((uint64_t)1ULL), &r211);
                uint64_t t213 = r211;
                gm_l31 = t213;
            }
        }
gm_lb34:;
    }
    {
        void * t214 = gm_l0;
        uint32_t t215 = gm_l1;
        void * t216 = gm_l2;
        gm_b16_8 t217 = gm_l6;
        gm_tl874t2atnlh3 t218 = gm_l4;
        void * t219 = gm_f_generic_call_packed_h7ssi9n((void *)NULL, cx, NULL, t214, t215, t216, t217, t218);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t219;
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
__attribute__((aligned(4))) void * gm_f_generic_call_packed_h7ssi9n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_generic_call_packed_h7ssi9n F;
    uint32_t gm_l1; /* f */
    void * gm_l2; /* receiver */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l6; /* tys */
    gm_tf5srh4mp4b3i gm_l7; /* recv_index */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t08olq97rscn3 gm_l17; /* n */
    uint8_t gm_l19; /* inst_only */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* i */
    uint8_t gm_l25; /* explicit */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    uint64_t gm_l28; /* i */
    uint8_t gm_l32; /* conflict */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* k */
    gm_b16_8 gm_l41; /* <tmp> */
    uint64_t gm_l42; /* <tmp> */
    gm_t08olq97rscn3 gm_l44; /* nm */
    uint64_t gm_l46; /* i */
    gm_b16_8 gm_l48; /* <tmp> */
    uint64_t gm_l49; /* <tmp> */
    uint64_t gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    uint64_t gm_l54; /* k */
    void * gm_l55; /* src */
    uint32_t gm_l56; /* g */
    uint64_t gm_l57; /* i */
    void * gm_l58; /* r */
    gm_b16_8 gm_l60; /* <tmp> */
    uint64_t gm_l61; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l62; /* b */
    uint64_t gm_l63; /* <tmp> */
    uint64_t gm_l64; /* <tmp> */
    uint64_t gm_l65; /* k */
    uint64_t gm_l66; /* i */
    void * gm_l67; /* e */
    uint32_t gm_l69; /* pid */
    gm_t88rcucg1b7uk gm_l70; /* v */
    gm_t88rcucg1b7uk gm_l71; /* expect */
    uint8_t gm_l72; /* partial */
    gm_b16_8 gm_l74; /* <tmp> */
    uint64_t gm_l75; /* <tmp> */
    uint64_t gm_l76; /* j */
    void * gm_l81; /* v */
    gm_t88rcucg1b7uk gm_l83; /* t */
    gm_b16_8 gm_l85; /* <tmp> */
    uint64_t gm_l86; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l87; /* b */
    gm_t88rcucg1b7uk gm_l88; /* prev */
    gm_b16_8 gm_l89; /* targs */
    gm_b16_8 gm_l91; /* <tmp> */
    uint64_t gm_l92; /* <tmp> */
    uint64_t gm_l93; /* k */
    uint32_t gm_l94; /* g */
    uint64_t gm_l96; /* i */
    gm_b16_8 gm_l97; /* <tmp> */
    uint64_t gm_l98; /* <tmp> */
    void * gm_l101; /* out */
    gm_tnpifv20irnph gm_l103; /* effects */
    gm_b16_8 gm_l104; /* pre */
    uint32_t gm_l105; /* inst */
    void * gm_l106; /* call */
    GM_STACK_CHECK(&F, 9102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l107;
    gm_tgsvmddj3e85e gm_l108;
    uint8_t gm_l109;
    gm_tftb1odc739t5 gm_l110;
    gm_tftb1odc739t5 gm_l111;
    uint8_t gm_l112;
    uint8_t gm_l113;
    gm_tgsvmddj3e85e gm_l114;
    uint8_t gm_l115;
    uint8_t gm_l116;
    uint8_t gm_l117;
    gm_tftb1odc739t5 gm_l118;
    uint8_t gm_l119;
    gm_tgsvmddj3e85e gm_l120;
    gm_tftb1odc739t5 gm_l121;
    gm_tkppg5stlt438 gm_l122;
    gm_tftb1odc739t5 gm_l123;
    gm_tf5srh4mp4b3i gm_l124;
    uint64_t gm_l125;
    gm_tabb710d9mq2e gm_l126;
    gm_tftb1odc739t5 gm_l127;
    gm_tftb1odc739t5 gm_l128;
    uint8_t gm_l129;
    uint8_t gm_l130;
    uint8_t gm_l131;
    gm_tnnjeerolos0a gm_l132;
    uint8_t gm_l133;
    uint8_t gm_l134;
    gm_tftb1odc739t5 gm_l135;
    uint8_t gm_l136;
    gm_tnnjeerolos0a gm_l137;
    gm_tkn42u7na3t81 gm_l138;
    gm_t88rcucg1b7uk gm_l139;
    uint8_t gm_l140;
    gm_t53at80pfdus3 gm_l141;
    uint8_t gm_l142;
    uint8_t gm_l143;
    gm_tgsvmddj3e85e gm_l144;
    uint8_t gm_l145;
    gm_tabb710d9mq2e gm_l146;
    gm_tftb1odc739t5 gm_l147;
    gm_tc3oht4ksth9d gm_l148;
    gm_tkn42u7na3t81 gm_l149;
    uint8_t gm_l150;
    uint8_t gm_l151;
    gm_tftb1odc739t5 gm_l152;
    gm_tkn42u7na3t81 gm_l153;
    gm_t88rcucg1b7uk gm_l154;
    gm_tftb1odc739t5 gm_l155;
    uint8_t gm_l156;
    uint8_t gm_l157;
    gm_tabb710d9mq2e gm_l158;
    gm_th2lqbo77pvsc gm_l159;
    gm_b16_8 gm_l160;
    gm_tn68el8d14vku gm_l161;
    gm_tftb1odc739t5 gm_l162;
    gm_tftb1odc739t5 gm_l163;
    uint8_t gm_l164;
    uint8_t gm_l165;
    gm_tftb1odc739t5 gm_l166;
    {
        void * t154 = F.l0;
        void * t155 = GM_LD(void *, ((uint8_t *)t154));
        void * t156 = GM_LD(void *, (((uint8_t *)t155) + 344));
        uint64_t t157 = GM_LD(uint64_t, ((((uint8_t *)t155) + 344) + 8));
        uint32_t t158 = gm_l1;
        uint64_t t159 = (uint64_t)t158;
        if ((uintptr_t)t159 >= (uintptr_t)(t157)) gm_trap_index((uintptr_t)t159, t157, gm_srcrorgjlg51brd, 77, 12);
        memmove(((uint8_t *)&F.l5), (((uint8_t *)t156) + (uintptr_t)t159 * 136), 136);
        void * t160 = F.l0;
        void * t161 = GM_LD(void *, ((uint8_t *)t160));
        void * t162 = (void *)(((uint8_t *)t161) + 8);
        gm_l6 = t162;
        gm_tf5srh4mp4b3i t163;
        memset(&t163, 0, sizeof t163);
        gm_l7 = t163;
        {
            {
                uint8_t t164 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
                if (t164) {
                    goto gm_lb2;
                }
                gm_l107 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l107 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 80, 8);
        }
gm_lb1:;
        uint8_t t165 = gm_l107;
        if (t165) {
            gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
            gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
            gm_l8 = t167;
            gm_l9 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t169 = gm_l9;
                    uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint8_t t171 = (t169 < t170);
                    if (!t171) {
                        goto gm_lb4;
                    }
                    {
                        uint64_t t172 = gm_l9;
                        void * t173 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint64_t t175 = gm_l9;
                        gm_t5v0c8ia6a5mo t176 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t173) + (uintptr_t)t175 * 56));
                        gm_tgsvmddj3e85e t177;
                        GM_ST(uint64_t, ((uint8_t *)&t177 + 0), t172);
                        GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t177 + 8), t176);
                        gm_l108 = t177;
                        {
                            {
                                uint64_t t178 = GM_LD(uint64_t, ((uint8_t *)&gm_l108));
                                gm_l10 = t178;
                                memmove(((uint8_t *)&F.l11), (((uint8_t *)&gm_l108) + 8), 56);
                                {
                                    {
                                        uint8_t t179 = (uint8_t)(((((uint8_t *)&F.l11) + 40))[4] != 0);
                                        if (t179) {
                                            goto gm_lb10;
                                        }
                                        gm_l109 = ((uint8_t)1);
                                        goto gm_lb9;
                                    }
gm_lb10:;
                                    {
                                        gm_l109 = ((uint8_t)0);
                                        goto gm_lb9;
                                    }
gm_lb11:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 82, 16);
                                }
gm_lb9:;
                                uint8_t t180 = gm_l109;
                                if (t180) {
                                    uint64_t t181 = gm_l10;
                                    gm_tf5srh4mp4b3i t182;
                                    GM_ST(uint8_t, (uint8_t *)&t182 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t182 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t182 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t182), t181);
                                    ((uint8_t *)&t182)[8] = 1;
                                    gm_l7 = t182;
                                    goto gm_lb4;
                                }
                                goto gm_lb7;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 81, 9);
                        }
gm_lb7:;
                    }
gm_lb6:;
                    uint64_t t183 = gm_l9;
                    uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
                    uint64_t t186 = r184;
                    gm_l9 = t186;
                }
            }
gm_lb4:;
        }
    }
    {
        gm_trqfvmjtt4sij t187;
        gm_f_list_epvducg((void *)&t187, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l12 = t187;
        gm_trqfvmjtt4sij t188;
        gm_f_list_vp1djv1((void *)&t188, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t188;
        gm_b16_8 t189 = gm_l3;
        gm_l14 = t189;
        gm_l15 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t191 = gm_l15;
                uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t193 = (t191 < t192);
                if (!t193) {
                    goto gm_lb12;
                }
                {
                    void * t194 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t195 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint64_t t196 = gm_l15;
                    {
                        {
                            memmove(((uint8_t *)&F.l16), (((uint8_t *)t194) + (uintptr_t)t196 * 144), 144);
                            {
                                {
                                    uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                                    uint8_t t198 = (uint8_t)(t197 == ((uint8_t)1ULL));
                                    if (!t198) {
                                        goto gm_lb18;
                                    }
                                    memmove(((uint8_t *)&gm_l17), (((uint8_t *)&F.l16) + 8), 32);
                                    void * t199 = (void *)((uint8_t *)&F.l12);
                                    gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
                                    gm_tftb1odc739t5 t201 = gm_f_push_3ef746b(t199, t200);
                                    gm_l110 = t201;
                                    uint8_t t202 = (uint8_t)!((((uint8_t *)&gm_l110))[0] != 0);
                                    if (!t202) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l110) + 1, gm_srcrorgjlg51brd, 92, 29);
                                    goto gm_lb17;
                                }
gm_lb18:;
                                {
                                    uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                                    uint8_t t204 = (uint8_t)(t203 == ((uint8_t)0ULL));
                                    if (!t204) {
                                        goto gm_lb19;
                                    }
                                    memmove(((uint8_t *)&F.l18), (((uint8_t *)&F.l16) + 8), 88);
                                    void * t205 = (void *)((uint8_t *)&F.l13);
                                    gm_tftb1odc739t5 t206 = gm_f_push_d943vgp(t205, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l18)));
                                    gm_l111 = t206;
                                    uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l111))[0] != 0);
                                    if (!t207) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l111) + 1, gm_srcrorgjlg51brd, 93, 31);
                                    goto gm_lb17;
                                }
gm_lb19:;
                                {
                                    uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&F.l16) + 0));
                                    uint8_t t209 = (uint8_t)(t208 == ((uint8_t)2ULL));
                                    if (!t209) {
                                        goto gm_lb20;
                                    }
                                    goto gm_lb17;
                                }
gm_lb20:;
                                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 91, 9);
                            }
gm_lb17:;
                            goto gm_lb15;
                        }
gm_lb16:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 90, 5);
                    }
gm_lb15:;
                }
gm_lb14:;
                uint64_t t210 = gm_l15;
                uint64_t r211; bool o212 = __builtin_add_overflow(t210, ((uint64_t)1ULL), &r211);
                uint64_t t213 = r211;
                gm_l15 = t213;
            }
        }
gm_lb12:;
        {
            {
                uint8_t t214 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
                if (t214) {
                    goto gm_lb22;
                }
                gm_l113 = ((uint8_t)1);
                goto gm_lb21;
            }
gm_lb22:;
            {
                gm_l113 = ((uint8_t)0);
                goto gm_lb21;
            }
gm_lb23:;
            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 98, 18);
        }
gm_lb21:;
    }
    {
        uint8_t t215 = gm_l113;
        if (t215) {
            void * t216 = F.l0;
            void * t217 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t218 = gm_l3;
            gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
            gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
            uint8_t t221 = gm_f_is_instantiation_vgsqc8f((void *)NULL, cx, NULL, t216, t217, t218, t220);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l112 = t221;
        } else {
            gm_l112 = ((uint8_t)0);
        }
        uint8_t t222 = gm_l112;
        gm_l19 = t222;
        gm_trqfvmjtt4sij t223;
        gm_f_list_5dhgg4i((void *)&t223, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l20 = t223;
        gm_b16_8 t224 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
        gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&t224));
        gm_l21 = t225;
        gm_l22 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t227 = gm_l22;
                uint64_t t228 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                uint8_t t229 = (t227 < t228);
                if (!t229) {
                    goto gm_lb24;
                }
                {
                    uint64_t t230 = gm_l22;
                    void * t231 = GM_LD(void *, ((uint8_t *)&gm_l21));
                    uint64_t t232 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                    uint64_t t233 = gm_l22;
                    gm_t5v0c8ia6a5mo t234 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t231) + (uintptr_t)t233 * 56));
                    gm_tgsvmddj3e85e t235;
                    GM_ST(uint64_t, ((uint8_t *)&t235 + 0), t230);
                    GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t235 + 8), t234);
                    gm_l114 = t235;
                    {
                        {
                            uint64_t t236 = GM_LD(uint64_t, ((uint8_t *)&gm_l114));
                            gm_l23 = t236;
                            memmove(((uint8_t *)&F.l24), (((uint8_t *)&gm_l114) + 8), 56);
                            void * t237 = F.l0;
                            void * t238 = GM_LD(void *, ((uint8_t *)t237));
                            void * t239 = (void *)((uint8_t *)&F.l5);
                            uint64_t t240 = gm_l23;
                            uint8_t t241 = gm_f_inferable_param_f80lrdi(t238, t239, t240);
                            if (t241) {
                                gm_l117 = ((uint8_t)1);
                            } else {
                                gm_b16_8 t242 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
                                gm_b16_8 t243 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                uint8_t t244 = 0;
                                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t243) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t243))) + i * 16, ((uint8_t *)&t242))) { t244 = 1; break; }
                                uint8_t t245 = (uint8_t)(t244);
                                gm_l117 = t245;
                            }
                            uint8_t t246 = gm_l117;
                            if (t246) {
                                gm_l116 = ((uint8_t)1);
                            } else {
                                gm_tf5srh4mp4b3i t247 = gm_l7;
                                uint64_t t248 = gm_l23;
                                gm_tf5srh4mp4b3i t249;
                                GM_ST(uint8_t, (uint8_t *)&t249 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t249 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t249 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t249), t248);
                                ((uint8_t *)&t249)[8] = 1;
                                uint8_t t250 = (gm_eq7mri15hk78(((uint8_t *)&t247), ((uint8_t *)&t249)));
                                gm_l116 = t250;
                            }
                            uint8_t t251 = gm_l116;
                            if (t251) {
                                gm_l115 = ((uint8_t)1);
                            } else {
                                void * t252 = (void *)((uint8_t *)&F.l24);
                                uint8_t t253 = gm_f_is_site_8sr7vam(t252);
                                gm_l115 = t253;
                            }
                            uint8_t t254 = gm_l115;
                            if (t254) {
                                goto gm_lb26;
                            }
                            void * t255 = (void *)((uint8_t *)&F.l20);
                            uint64_t t256 = gm_l23;
                            gm_tftb1odc739t5 t257 = gm_f_push_ucgmlo2(t255, t256);
                            gm_l118 = t257;
                            uint8_t t258 = (uint8_t)!((((uint8_t *)&gm_l118))[0] != 0);
                            if (!t258) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l118) + 1, gm_srcrorgjlg51brd, 105, 9);
                            goto gm_lb27;
                        }
gm_lb28:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 103, 5);
                    }
gm_lb27:;
                }
gm_lb26:;
                uint64_t t259 = gm_l22;
                uint64_t r260; bool o261 = __builtin_add_overflow(t259, ((uint64_t)1ULL), &r260);
                uint64_t t262 = r260;
                gm_l22 = t262;
            }
        }
gm_lb24:;
    }
    {
        uint64_t t263 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 8));
        uint64_t t264 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
        uint8_t t265 = (t263 > t264);
        if (t265) {
            void * t266 = F.l0;
            void * t267 = (void *)((uint8_t *)&F.l5);
            {
                {
                    uint8_t t268 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
                    if (t268) {
                        goto gm_lb30;
                    }
                    gm_l119 = ((uint8_t)0);
                    goto gm_lb29;
                }
gm_lb30:;
                {
                    gm_l119 = ((uint8_t)1);
                    goto gm_lb29;
                }
gm_lb31:;
                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 108, 40);
            }
gm_lb29:;
            uint8_t t269 = gm_l119;
            gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
            gm_b16_8 t271 = GM_LD(gm_b16_8, ((uint8_t *)&t270));
            gm_b16_8 t272 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
            gm_b16_8 t273 = GM_LD(gm_b16_8, ((uint8_t *)&t272));
            uint64_t t274 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
            gm_tl874t2atnlh3 t275 = gm_l4;
            gm_b0_1 t276 = gm_f_too_many_generic_args_bs99n7s((void *)NULL, cx, NULL, t266, t267, t269, t271, t273, t274, t275);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t277 = F.l0;
            void * t278 = GM_LD(void *, ((uint8_t *)t277));
            gm_tl874t2atnlh3 t279 = gm_l4;
            void * t280 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t278, t279);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t280;
            goto gm_fret;
        }
        gm_l25 = ((uint8_t)0);
        gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
        gm_b16_8 t282 = GM_LD(gm_b16_8, ((uint8_t *)&t281));
        gm_l26 = t282;
        gm_l27 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t284 = gm_l27;
                uint64_t t285 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                uint8_t t286 = (t284 < t285);
                if (!t286) {
                    goto gm_lb32;
                }
                {
                    uint64_t t287 = gm_l27;
                    void * t288 = GM_LD(void *, ((uint8_t *)&gm_l26));
                    uint64_t t289 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                    uint64_t t290 = gm_l27;
                    gm_t5v0c8ia6a5mo t291 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t288) + (uintptr_t)t290 * 56));
                    gm_tgsvmddj3e85e t292;
                    GM_ST(uint64_t, ((uint8_t *)&t292 + 0), t287);
                    GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t292 + 8), t291);
                    gm_l120 = t292;
                    {
                        {
                            uint64_t t293 = GM_LD(uint64_t, ((uint8_t *)&gm_l120));
                            gm_l28 = t293;
                            memmove(((uint8_t *)&F.l29), (((uint8_t *)&gm_l120) + 8), 56);
                            {
                                {
                                    void * t294 = F.l0;
                                    void * t295 = GM_LD(void *, ((uint8_t *)t294));
                                    void * t296 = (void *)((uint8_t *)&F.l5);
                                    uint64_t t297 = gm_l28;
                                    uint8_t t298 = gm_f_inferable_param_f80lrdi(t295, t296, t297);
                                    if (!t298) {
                                        goto gm_lb38;
                                    }
                                    gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&F.l29));
                                    gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                    uint8_t t301 = 0;
                                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t300) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t300))) + i * 16, ((uint8_t *)&t299))) { t301 = 1; break; }
                                    uint8_t t302 = (uint8_t)(t301);
                                    if (!t302) {
                                        goto gm_lb38;
                                    }
                                    gm_l25 = ((uint8_t)1);
                                    goto gm_lb37;
                                }
gm_lb38:;
                            }
gm_lb37:;
                            goto gm_lb35;
                        }
gm_lb36:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 114, 5);
                    }
gm_lb35:;
                }
gm_lb34:;
                uint64_t t303 = gm_l27;
                uint64_t r304; bool o305 = __builtin_add_overflow(t303, ((uint64_t)1ULL), &r304);
                uint64_t t306 = r304;
                gm_l27 = t306;
            }
        }
gm_lb32:;
    }
    {
        gm_trqfvmjtt4sij t307;
        gm_f_list_oalq03d((void *)&t307, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l30 = t307;
        gm_trqfvmjtt4sij t308;
        gm_f_list_89e90o6((void *)&t308, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l31 = t308;
        gm_l32 = ((uint8_t)0);
        gm_trqfvmjtt4sij t309;
        gm_f_list_pmp06kq((void *)&t309, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l33 = t309;
        gm_b16_8 t310 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
        gm_l34 = t310;
        gm_l35 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t312 = gm_l35;
                uint64_t t313 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                uint8_t t314 = (t312 < t313);
                if (!t314) {
                    goto gm_lb39;
                }
                {
                    void * t315 = GM_LD(void *, ((uint8_t *)&gm_l34));
                    uint64_t t316 = GM_LD(uint64_t, (((uint8_t *)&gm_l34) + 8));
                    uint64_t t317 = gm_l35;
                    {
                        {
                            void * t318 = (void *)((uint8_t *)&F.l33);
                            gm_tftb1odc739t5 t319 = gm_f_push_r2k6rjf(t318, ((void *)0));
                            gm_l121 = t319;
                            uint8_t t320 = (uint8_t)!((((uint8_t *)&gm_l121))[0] != 0);
                            if (!t320) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l121) + 1, gm_srcrorgjlg51brd, 122, 27);
                            goto gm_lb42;
                        }
gm_lb43:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 122, 5);
                    }
gm_lb42:;
                }
gm_lb41:;
                uint64_t t321 = gm_l35;
                uint64_t r322; bool o323 = __builtin_add_overflow(t321, ((uint64_t)1ULL), &r322);
                uint64_t t324 = r322;
                gm_l35 = t324;
            }
        }
gm_lb39:;
        gm_trqfvmjtt4sij t325;
        gm_f_list_lnmdod1((void *)&t325, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l36 = t325;
        gm_b16_8 t326 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
        gm_b16_8 t327 = GM_LD(gm_b16_8, ((uint8_t *)&t326));
        gm_l37 = t327;
        gm_l38 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t329 = gm_l38;
                uint64_t t330 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                uint8_t t331 = (t329 < t330);
                if (!t331) {
                    goto gm_lb44;
                }
                {
                    uint64_t t332 = gm_l38;
                    void * t333 = GM_LD(void *, ((uint8_t *)&gm_l37));
                    uint64_t t334 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                    uint64_t t335 = gm_l38;
                    gm_tc3oht4ksth9d t336 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)t333) + (uintptr_t)t335 * 88));
                    gm_tkppg5stlt438 t337;
                    GM_ST(uint64_t, ((uint8_t *)&t337 + 0), t332);
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t337 + 8), t336);
                    gm_l122 = t337;
                    {
                        {
                            uint64_t t338 = GM_LD(uint64_t, ((uint8_t *)&gm_l122));
                            gm_l39 = t338;
                            memmove(((uint8_t *)&F.l40), (((uint8_t *)&gm_l122) + 8), 88);
                            void * t339 = (void *)((uint8_t *)&F.l36);
                            void * t340 = GM_LD(void *, ((uint8_t *)&F.l20));
                            uint64_t t341 = GM_LD(uint64_t, (((uint8_t *)&F.l20) + 8));
                            uint64_t t342 = gm_l39;
                            if ((uintptr_t)t342 >= (uintptr_t)(t341)) gm_trap_index((uintptr_t)t342, t341, gm_srcrorgjlg51brd, 124, 77);
                            uint64_t t343 = GM_LD(uint64_t, (((uint8_t *)t340) + (uintptr_t)t342 * 8));
                            gm_tc3oht4ksth9d t344 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l40));
                            gm_tsdklcaprgv9r t345;
                            GM_ST(uint8_t, (uint8_t *)&t345 + 97, 0);
                            GM_ST(uint16_t, (uint8_t *)&t345 + 98, 0);
                            GM_ST(uint32_t, (uint8_t *)&t345 + 100, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t345 + 0), t343);
                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t345 + 8), t344);
                            GM_ST(uint8_t, ((uint8_t *)&t345 + 96), ((uint8_t)0ULL));
                            gm_tftb1odc739t5 t346 = gm_f_push_0ee93e4(t339, &t345);
                            gm_l123 = t346;
                            uint8_t t347 = (uint8_t)!((((uint8_t *)&gm_l123))[0] != 0);
                            if (!t347) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l123) + 1, gm_srcrorgjlg51brd, 124, 47);
                            goto gm_lb47;
                        }
gm_lb48:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 124, 5);
                    }
gm_lb47:;
                }
gm_lb46:;
                uint64_t t348 = gm_l38;
                uint64_t r349; bool o350 = __builtin_add_overflow(t348, ((uint64_t)1ULL), &r349);
                uint64_t t351 = r349;
                gm_l38 = t351;
            }
        }
gm_lb44:;
    }
    {
        gm_b16_8 t352 = gm_l3;
        gm_l41 = t352;
        gm_l42 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t354 = gm_l42;
                uint64_t t355 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                uint8_t t356 = (t354 < t355);
                if (!t356) {
                    goto gm_lb49;
                }
                {
                    void * t357 = GM_LD(void *, ((uint8_t *)&gm_l41));
                    uint64_t t358 = GM_LD(uint64_t, (((uint8_t *)&gm_l41) + 8));
                    uint64_t t359 = gm_l42;
                    {
                        {
                            memmove(((uint8_t *)&F.l43), (((uint8_t *)t357) + (uintptr_t)t359 * 144), 144);
                            {
                                {
                                    uint8_t t360 = GM_LD(uint8_t, (((uint8_t *)&F.l43) + 0));
                                    uint8_t t361 = (uint8_t)(t360 == ((uint8_t)1ULL));
                                    if (!t361) {
                                        goto gm_lb55;
                                    }
                                    memmove(((uint8_t *)&gm_l44), (((uint8_t *)&F.l43) + 8), 32);
                                    memmove(((uint8_t *)&F.l45), ((((uint8_t *)&F.l43) + 8) + 32), 88);
                                    gm_b16_8 t362 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                                    gm_b16_8 t363 = GM_LD(gm_b16_8, ((uint8_t *)&t362));
                                    gm_b16_8 t364 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l44));
                                    gm_tf5srh4mp4b3i t365 = gm_f_param_index_78k4iut(t363, t364);
                                    gm_l124 = t365;
                                    uint8_t t366 = (uint8_t)((((uint8_t *)&gm_l124))[8] != 0);
                                    if (t366) {
                                        uint64_t t367 = GM_LD(uint64_t, ((uint8_t *)&gm_l124));
                                        gm_l125 = t367;
                                    } else {
                                        void * t368 = F.l0;
                                        gm_tl874t2atnlh3 t369 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l44) + 16));
                                        gm_b16_8 t370 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l44));
                                        gm_tnpifv20irnph t371;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t371 + 0), t370);
                                        gm_f_format_itam0lj((void *)&gm_l126, (void *)NULL, cx, NULL, t371);
                                        if (gm_unw.kind) {
                                            goto gm_ll353;
                                        }
                                        uint8_t t372 = (uint8_t)!((((uint8_t *)&gm_l126))[0] != 0);
                                        if (!t372) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l126) + 8, gm_srcrorgjlg51brd, 128, 31);
                                        gm_b16_8 t373 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l126) + 8));
                                        gm_b16_8 t374 = GM_LD(gm_b16_8, ((uint8_t *)&t373));
                                        gm_b0_1 t375 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t368, t369, t374);
                                        if (gm_unw.kind) {
                                            goto gm_ll353;
                                        }
                                        goto gm_lb51;
                                    }
                                    uint64_t t376 = gm_l125;
                                    gm_l46 = t376;
                                    void * t377 = (void *)((uint8_t *)&F.l36);
                                    uint64_t t378 = gm_l46;
                                    gm_tc3oht4ksth9d t379 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l45));
                                    gm_tsdklcaprgv9r t380;
                                    GM_ST(uint8_t, (uint8_t *)&t380 + 97, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t380 + 98, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t380 + 100, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t380 + 0), t378);
                                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t380 + 8), t379);
                                    GM_ST(uint8_t, ((uint8_t *)&t380 + 96), ((uint8_t)0ULL));
                                    gm_tftb1odc739t5 t381 = gm_f_push_0ee93e4(t377, &t380);
                                    gm_l127 = t381;
                                    uint8_t t382 = (uint8_t)!((((uint8_t *)&gm_l127))[0] != 0);
                                    if (!t382) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l127) + 1, gm_srcrorgjlg51brd, 131, 9);
                                    goto gm_lb54;
                                }
gm_lb55:;
                                goto gm_lb51;
                            }
gm_lb54:;
                            goto gm_lb52;
                        }
gm_lb53:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 125, 5);
                    }
gm_lb52:;
                }
gm_lb51:;
                uint64_t t383 = gm_l42;
                uint64_t r384; bool o385 = __builtin_add_overflow(t383, ((uint64_t)1ULL), &r384);
                uint64_t t386 = r384;
                gm_l42 = t386;
            }
        }
        goto gm_ls387;
gm_ll353:;
        if (gm_unw.frame == &F.h && gm_unw.label == 6) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb49; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb51; }
        }
        goto gm_lad;
gm_ls387:;
gm_lb49:;
        gm_trqfvmjtt4sij t388;
        gm_f_list_5dhgg4i((void *)&t388, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l47 = t388;
    }
    {
        gm_b16_8 t389 = GM_LD(gm_b16_8, ((uint8_t *)&F.l36));
        gm_l48 = t389;
        gm_l49 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t391 = gm_l49;
                uint64_t t392 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                uint8_t t393 = (t391 < t392);
                if (!t393) {
                    goto gm_lb56;
                }
                {
                    void * t394 = GM_LD(void *, ((uint8_t *)&gm_l48));
                    uint64_t t395 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                    uint64_t t396 = gm_l49;
                    {
                        {
                            memmove(((uint8_t *)&F.l50), (((uint8_t *)t394) + (uintptr_t)t396 * 104), 104);
                            void * t397 = (void *)((uint8_t *)&F.l47);
                            uint64_t t398 = GM_LD(uint64_t, ((uint8_t *)&F.l50));
                            gm_tftb1odc739t5 t399 = gm_f_push_ucgmlo2(t397, t398);
                            gm_l128 = t399;
                            uint8_t t400 = (uint8_t)!((((uint8_t *)&gm_l128))[0] != 0);
                            if (!t400) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l128) + 1, gm_srcrorgjlg51brd, 134, 32);
                            goto gm_lb59;
                        }
gm_lb60:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 134, 5);
                    }
gm_lb59:;
                }
gm_lb58:;
                uint64_t t401 = gm_l49;
                uint64_t r402; bool o403 = __builtin_add_overflow(t401, ((uint64_t)1ULL), &r402);
                uint64_t t404 = r402;
                gm_l49 = t404;
            }
        }
gm_lb56:;
        gm_trqfvmjtt4sij t405;
        gm_f_list_p2dh8s2((void *)&t405, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l51 = t405;
        gm_l53 = ((uint64_t)0ULL);
        uint64_t t406 = GM_LD(uint64_t, (((uint8_t *)&F.l36) + 8));
        gm_l52 = t406;
        {
            for (;;) {
                uint64_t t408 = gm_l53;
                uint64_t t409 = gm_l52;
                uint8_t t410 = (t408 < t409);
                if (!t410) {
                    goto gm_lb61;
                }
                {
                    {
                        {
                            uint64_t t411 = gm_l53;
                            gm_l54 = t411;
                            void * t412 = GM_LD(void *, ((uint8_t *)&F.l36));
                            uint64_t t413 = GM_LD(uint64_t, (((uint8_t *)&F.l36) + 8));
                            uint64_t t414 = gm_l54;
                            if ((uintptr_t)t414 >= (uintptr_t)(t413)) gm_trap_index((uintptr_t)t414, t413, gm_srcrorgjlg51brd, 138, 17);
                            void * t415 = (void *)(((uint8_t *)t412) + (uintptr_t)t414 * 104);
                            gm_l55 = t415;
                            void * t416 = gm_l55;
                            {
                                {
                                    void * t417 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
                                    uint64_t t418 = GM_LD(uint64_t, ((((uint8_t *)&F.l5) + 40) + 8));
                                    void * t419 = gm_l55;
                                    uint64_t t420 = GM_LD(uint64_t, ((uint8_t *)t419));
                                    if ((uintptr_t)t420 >= (uintptr_t)(t418)) gm_trap_index((uintptr_t)t420, t418, gm_srcrorgjlg51brd, 139, 28);
                                    uint8_t t421 = (uint8_t)((((((uint8_t *)t417) + (uintptr_t)t420 * 56) + 40))[4] != 0);
                                    if (!t421) {
                                        goto gm_lb67;
                                    }
                                    uint32_t t422 = GM_LD(uint32_t, ((((uint8_t *)t417) + (uintptr_t)t420 * 56) + 40));
                                    gm_l56 = t422;
                                    void * t423 = F.l0;
                                    void * t424 = GM_LD(void *, ((uint8_t *)t423));
                                    uint32_t t425 = gm_l56;
                                    gm_tnnjeerolos0a t426 = gm_f_param_kind_a0aikmb(t424, t425);
                                    gm_l132 = t426;
                                    {
                                        {
                                            uint8_t t427 = GM_LD(uint8_t, (((uint8_t *)&gm_l132) + 0));
                                            uint8_t t428 = (uint8_t)(t427 == ((uint8_t)2ULL));
                                            if (!t428) {
                                                goto gm_lb69;
                                            }
                                            gm_l131 = ((uint8_t)1);
                                            goto gm_lb68;
                                        }
gm_lb69:;
                                        {
                                            gm_l131 = ((uint8_t)0);
                                            goto gm_lb68;
                                        }
gm_lb70:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 140, 16);
                                    }
gm_lb68:;
                                    uint8_t t429 = gm_l131;
                                    if (t429) {
                                        gm_l130 = ((uint8_t)3ULL);
                                    } else {
                                        gm_l130 = ((uint8_t)0ULL);
                                    }
                                    uint8_t t430 = gm_l130;
                                    gm_l129 = t430;
                                    goto gm_lb66;
                                }
gm_lb67:;
                                void * t431 = gm_l55;
                                {
                                    {
                                        uint8_t t432 = GM_LD(uint8_t, (((((uint8_t *)t431) + 8) + 8) + 0));
                                        uint8_t t433 = (uint8_t)(t432 == ((uint8_t)36ULL));
                                        if (!t433) {
                                            goto gm_lb72;
                                        }
                                        gm_l134 = ((uint8_t)1);
                                        goto gm_lb71;
                                    }
gm_lb72:;
                                    {
                                        gm_l134 = ((uint8_t)0);
                                        goto gm_lb71;
                                    }
gm_lb73:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 141, 19);
                                }
gm_lb71:;
                                uint8_t t434 = gm_l134;
                                if (t434) {
                                    gm_l133 = ((uint8_t)2ULL);
                                } else {
                                    gm_l133 = ((uint8_t)1ULL);
                                }
                                uint8_t t435 = gm_l133;
                                gm_l129 = t435;
                            }
gm_lb66:;
                            uint8_t t436 = gm_l129;
                            GM_ST(uint8_t, (((uint8_t *)t416) + 96), t436);
                            goto gm_lb64;
                        }
gm_lb65:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 137, 5);
                    }
gm_lb64:;
                }
gm_lb63:;
                uint64_t t437 = gm_l53;
                uint64_t r438; bool o439 = __builtin_add_overflow(t437, ((uint64_t)1ULL), &r438);
                uint64_t t440 = r438;
                gm_l53 = t440;
            }
        }
gm_lb61:;
    }
    {
        gm_b16_8 t441 = GM_LD(gm_b16_8, ((uint8_t *)&F.l36));
        gm_closure t442 = ((gm_closure){GM_PTR_TAG(gm_f_fn_dink6tf), NULL, cx});
        gm_f_stable_sort_by_3nevrtc(t441, t442);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                uint8_t t443 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
                if (!t443) {
                    goto gm_lb75;
                }
                uint64_t t444 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                gm_l57 = t444;
                {
                    {
                        uint8_t t445 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l2)) != 0);
                        if (!t445) {
                            goto gm_lb77;
                        }
                        void * t446 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        gm_l58 = t446;
                        void * t447 = F.l0;
                        void * t448 = GM_LD(void *, ((uint8_t *)t447));
                        void * t449 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
                        uint64_t t450 = GM_LD(uint64_t, ((((uint8_t *)&F.l5) + 40) + 8));
                        uint64_t t451 = gm_l57;
                        if ((uintptr_t)t451 >= (uintptr_t)(t450)) gm_trap_index((uintptr_t)t451, t450, gm_srcrorgjlg51brd, 151, 31);
                        gm_t88rcucg1b7uk t452 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t449) + (uintptr_t)t451 * 56) + 16));
                        void * t453 = F.l0;
                        void * t454 = (void *)(((uint8_t *)t453) + 96);
                        void * t455 = F.l0;
                        void * t456 = GM_LD(void *, ((uint8_t *)t455));
                        void * t457 = gm_l58;
                        gm_t88rcucg1b7uk t458 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t457));
                        gm_t88rcucg1b7uk t459 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t454, t456, t458);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t460 = (void *)((uint8_t *)&F.l30);
                        gm_b0_1 t461 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t448, t452, t459, t460);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t462 = GM_LD(void *, ((uint8_t *)&F.l33));
                        uint64_t t463 = GM_LD(uint64_t, (((uint8_t *)&F.l33) + 8));
                        uint64_t t464 = gm_l57;
                        if ((uintptr_t)t464 >= (uintptr_t)(t463)) gm_trap_index((uintptr_t)t464, t463, gm_srcrorgjlg51brd, 152, 13);
                        void * t465 = gm_l58;
                        GM_ST(void *, (((uint8_t *)t462) + (uintptr_t)t464 * 8), t465);
                        goto gm_lb76;
                    }
gm_lb77:;
                }
gm_lb76:;
                goto gm_lb74;
            }
gm_lb75:;
        }
gm_lb74:;
        gm_trqfvmjtt4sij t466;
        gm_f_list_go2qg0b((void *)&t466, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l59 = t466;
        gm_b16_8 t467 = GM_LD(gm_b16_8, ((uint8_t *)&F.l30));
        gm_l60 = t467;
        gm_l61 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t469 = gm_l61;
                uint64_t t470 = GM_LD(uint64_t, (((uint8_t *)&gm_l60) + 8));
                uint8_t t471 = (t469 < t470);
                if (!t471) {
                    goto gm_lb78;
                }
                {
                    void * t472 = GM_LD(void *, ((uint8_t *)&gm_l60));
                    uint64_t t473 = GM_LD(uint64_t, (((uint8_t *)&gm_l60) + 8));
                    uint64_t t474 = gm_l61;
                    {
                        {
                            gm_tplpbqf3duvn3 t475 = GM_LD(gm_tplpbqf3duvn3, (((uint8_t *)t472) + (uintptr_t)t474 * 8));
                            gm_l62 = t475;
                            void * t476 = (void *)((uint8_t *)&F.l59);
                            uint32_t t477 = GM_LD(uint32_t, ((uint8_t *)&gm_l62));
                            gm_tftb1odc739t5 t478 = gm_f_push_im9d4ro(t476, t477);
                            gm_l135 = t478;
                            uint8_t t479 = (uint8_t)!((((uint8_t *)&gm_l135))[0] != 0);
                            if (!t479) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l135) + 1, gm_srcrorgjlg51brd, 157, 28);
                            goto gm_lb81;
                        }
gm_lb82:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 157, 5);
                    }
gm_lb81:;
                }
gm_lb80:;
                uint64_t t480 = gm_l61;
                uint64_t r481; bool o482 = __builtin_add_overflow(t480, ((uint64_t)1ULL), &r481);
                uint64_t t483 = r481;
                gm_l61 = t483;
            }
        }
gm_lb78:;
    }
    {
        gm_l64 = ((uint64_t)0ULL);
        uint64_t t484 = GM_LD(uint64_t, (((uint8_t *)&F.l36) + 8));
        gm_l63 = t484;
        {
            for (;;) {
                {
                    uint64_t t486 = gm_l64;
                    uint64_t t487 = gm_l63;
                    uint8_t t488 = (t486 < t487);
                    if (!t488) {
                        goto gm_lb83;
                    }
                    {
                        {
                            {
                                {
                                    {
                                        uint64_t t489 = gm_l64;
                                        gm_l65 = t489;
                                        void * t490 = GM_LD(void *, ((uint8_t *)&F.l36));
                                        uint64_t t491 = GM_LD(uint64_t, (((uint8_t *)&F.l36) + 8));
                                        uint64_t t492 = gm_l65;
                                        if ((uintptr_t)t492 >= (uintptr_t)(t491)) gm_trap_index((uintptr_t)t492, t491, gm_srcrorgjlg51brd, 159, 14);
                                        uint64_t t493 = GM_LD(uint64_t, (((uint8_t *)t490) + (uintptr_t)t492 * 104));
                                        gm_l66 = t493;
                                        void * t494 = GM_LD(void *, ((uint8_t *)&F.l36));
                                        uint64_t t495 = GM_LD(uint64_t, (((uint8_t *)&F.l36) + 8));
                                        uint64_t t496 = gm_l65;
                                        if ((uintptr_t)t496 >= (uintptr_t)(t495)) gm_trap_index((uintptr_t)t496, t495, gm_srcrorgjlg51brd, 160, 15);
                                        void * t497 = (void *)((((uint8_t *)t494) + (uintptr_t)t496 * 104) + 8);
                                        gm_l67 = t497;
                                        void * t498 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
                                        uint64_t t499 = GM_LD(uint64_t, ((((uint8_t *)&F.l5) + 40) + 8));
                                        uint64_t t500 = gm_l66;
                                        if ((uintptr_t)t500 >= (uintptr_t)(t499)) gm_trap_index((uintptr_t)t500, t499, gm_srcrorgjlg51brd, 161, 14);
                                        memmove(((uint8_t *)&F.l68), (((uint8_t *)t498) + (uintptr_t)t500 * 56), 56);
                                        {
                                            {
                                                uint8_t t501 = (uint8_t)(((((uint8_t *)&F.l68) + 40))[4] != 0);
                                                if (!t501) {
                                                    goto gm_lb89;
                                                }
                                                uint32_t t502 = GM_LD(uint32_t, (((uint8_t *)&F.l68) + 40));
                                                gm_l69 = t502;
                                                void * t503 = F.l0;
                                                void * t504 = GM_LD(void *, ((uint8_t *)t503));
                                                uint32_t t505 = gm_l69;
                                                gm_tnnjeerolos0a t506 = gm_f_param_kind_a0aikmb(t504, t505);
                                                gm_l137 = t506;
                                                {
                                                    {
                                                        uint8_t t507 = GM_LD(uint8_t, (((uint8_t *)&gm_l137) + 0));
                                                        uint8_t t508 = (uint8_t)(t507 == ((uint8_t)2ULL));
                                                        if (!t508) {
                                                            goto gm_lb91;
                                                        }
                                                        gm_l136 = ((uint8_t)1);
                                                        goto gm_lb90;
                                                    }
gm_lb91:;
                                                    {
                                                        gm_l136 = ((uint8_t)0);
                                                        goto gm_lb90;
                                                    }
gm_lb92:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 163, 16);
                                                }
gm_lb90:;
                                                uint8_t t509 = gm_l136;
                                                if (t509) {
                                                    void * t510 = F.l0;
                                                    void * t511 = F.l0;
                                                    void * t512 = GM_LD(void *, ((uint8_t *)t511));
                                                    gm_t88rcucg1b7uk t513 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                                                    gm_b16_8 t514 = GM_LD(gm_b16_8, ((uint8_t *)&F.l30));
                                                    gm_b16_8 t515 = GM_LD(gm_b16_8, ((uint8_t *)&t514));
                                                    gm_t88rcucg1b7uk t516 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t512, t513, t515);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll485;
                                                    }
                                                    void * t517 = gm_l67;
                                                    gm_tkn42u7na3t81 t518 = gm_f_comptime_arg_vtq6sop((void *)NULL, cx, NULL, t510, t516, t517);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll485;
                                                    }
                                                    gm_l138 = t518;
                                                    uint8_t t519 = (uint8_t)((((uint8_t *)&gm_l138))[4] != 0);
                                                    if (t519) {
                                                        gm_t88rcucg1b7uk t520 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l138));
                                                        gm_l139 = t520;
                                                    } else {
                                                        void * t521 = F.l0;
                                                        void * t522 = GM_LD(void *, ((uint8_t *)t521));
                                                        gm_tl874t2atnlh3 t523 = gm_l4;
                                                        void * t524 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t522, t523);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll485;
                                                        }
                                                        F.ret = t524;
                                                        goto gm_fret;
                                                    }
                                                    gm_t88rcucg1b7uk t525 = gm_l139;
                                                    gm_l70 = t525;
                                                    void * t526 = (void *)((uint8_t *)&F.l30);
                                                    uint32_t t527 = gm_l69;
                                                    gm_t88rcucg1b7uk t528 = gm_l70;
                                                    gm_b0_1 t529 = gm_f_set_bind_nsjnlvq((void *)NULL, cx, NULL, t526, t527, t528);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll485;
                                                    }
                                                } else {
                                                    void * t530 = F.l0;
                                                    void * t531 = GM_LD(void *, ((uint8_t *)t530));
                                                    uint32_t t532 = gm_l69;
                                                    gm_tnnjeerolos0a t533 = gm_f_param_kind_a0aikmb(t531, t532);
                                                    uint8_t t534 = GM_LD(uint8_t, (((uint8_t *)&t533) + 0));
                                                    uint8_t t535 = (uint8_t)((t534 == ((uint8_t)1ULL)));
                                                    gm_tnnjeerolos0a t536;
                                                    GM_ST(uint8_t, (uint8_t *)&t536 + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t536 + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t536 + 4, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&t536 + 0), ((uint8_t)1ULL));
                                                    if (t535) {
                                                        void * t537 = (void *)((uint8_t *)&F.l30);
                                                        uint32_t t538 = gm_l69;
                                                        void * t539 = F.l0;
                                                        void * t540 = GM_LD(void *, ((uint8_t *)t539));
                                                        void * t541 = F.l0;
                                                        uint64_t t542 = GM_LD(uint64_t, (((uint8_t *)t541) + 8));
                                                        void * t543 = gm_l67;
                                                        gm_t88rcucg1b7uk t544 = gm_f_resolve_effect_arg_4h6q4qv((void *)NULL, cx, NULL, t540, t542, t543);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll485;
                                                        }
                                                        gm_b0_1 t545 = gm_f_set_bind_nsjnlvq((void *)NULL, cx, NULL, t537, t538, t544);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll485;
                                                        }
                                                    } else {
                                                        void * t546 = (void *)((uint8_t *)&F.l30);
                                                        uint32_t t547 = gm_l69;
                                                        void * t548 = F.l0;
                                                        void * t549 = gm_l67;
                                                        gm_t88rcucg1b7uk t550 = gm_f_resolve_ty_uk6c1lp((void *)NULL, cx, NULL, t548, t549);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll485;
                                                        }
                                                        gm_b0_1 t551 = gm_f_set_bind_nsjnlvq((void *)NULL, cx, NULL, t546, t547, t550);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll485;
                                                        }
                                                    }
                                                }
                                                goto gm_lb85;
                                            }
gm_lb89:;
                                        }
gm_lb88:;
                                    }
                                    {
                                        void * t552 = F.l0;
                                        void * t553 = GM_LD(void *, ((uint8_t *)t552));
                                        gm_t88rcucg1b7uk t554 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                                        gm_b16_8 t555 = GM_LD(gm_b16_8, ((uint8_t *)&F.l30));
                                        gm_b16_8 t556 = GM_LD(gm_b16_8, ((uint8_t *)&t555));
                                        gm_t88rcucg1b7uk t557 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t553, t554, t556);
                                        if (gm_unw.kind) {
                                            goto gm_ll485;
                                        }
                                        gm_l71 = t557;
                                        void * t558 = gm_l6;
                                        gm_t88rcucg1b7uk t559 = gm_l71;
                                        gm_f_kind_jcnt4hm((void *)&gm_l141, t558, GM_CONV(gm_csguankko4lmhd, t559));
                                        {
                                            {
                                                uint8_t t560 = GM_LD(uint8_t, (((uint8_t *)&gm_l141) + 0));
                                                uint8_t t561 = (uint8_t)(t560 == ((uint8_t)15ULL));
                                                if (!t561) {
                                                    goto gm_lb94;
                                                }
                                                gm_l140 = ((uint8_t)1);
                                                goto gm_lb93;
                                            }
gm_lb94:;
                                            {
                                                uint8_t t562 = GM_LD(uint8_t, (((uint8_t *)&gm_l141) + 0));
                                                uint8_t t563 = (uint8_t)(t562 == ((uint8_t)16ULL));
                                                if (!t563) {
                                                    goto gm_lb95;
                                                }
                                                gm_l140 = ((uint8_t)1);
                                                goto gm_lb93;
                                            }
gm_lb95:;
                                            {
                                                gm_l140 = ((uint8_t)0);
                                                goto gm_lb93;
                                            }
gm_lb96:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 176, 20);
                                        }
gm_lb93:;
                                        uint8_t t564 = gm_l140;
                                        gm_l72 = t564;
                                        F.l73 = ((uint8_t)0);
                                        void * t565 = F.l0;
                                        void * t566 = GM_LD(void *, ((uint8_t *)t565));
                                        gm_t88rcucg1b7uk t567 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                                        gm_closure t568 = ((gm_closure){(void *)gm_f_fn_pjcgmfa, (void *)&F.h, cx});
                                        gm_f_walk_v858co7(t566, GM_CONV(gm_csguankko4lmhd, t567), t568);
                                        if (gm_unw.kind) {
                                            goto gm_ll485;
                                        }
                                        {
                                            {
                                                void * t569 = gm_l6;
                                                gm_t88rcucg1b7uk t570 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                                                gm_t53at80pfdus3 t571;
                                                gm_f_kind_jcnt4hm((void *)&t571, t569, GM_CONV(gm_csguankko4lmhd, t570));
                                                uint8_t t572 = GM_LD(uint8_t, (((uint8_t *)&t571) + 0));
                                                uint8_t t573 = (uint8_t)(!(t572 == ((uint8_t)6ULL)));
                                                if (!t573) {
                                                    goto gm_lb98;
                                                }
                                                void * t574 = F.l0;
                                                void * t575 = gm_l67;
                                                gm_t53at80pfdus3 t576;
                                                memset(&t576, 0, sizeof t576);
                                                GM_ST(uint8_t, ((uint8_t *)&t576 + 0), ((uint8_t)6ULL));
                                                uint8_t t577 = gm_f_clearly_value_f7icrt5(t574, t575);
                                                uint8_t t578 = (uint8_t)!t577;
                                                if (!t578) {
                                                    goto gm_lb98;
                                                }
                                                void * t579 = gm_l67;
                                                {
                                                    {
                                                        uint8_t t580 = GM_LD(uint8_t, ((((uint8_t *)t579) + 8) + 0));
                                                        uint8_t t581 = (uint8_t)(t580 == ((uint8_t)10ULL));
                                                        if (!t581) {
                                                            goto gm_lb100;
                                                        }
                                                        gm_l142 = ((uint8_t)0);
                                                        goto gm_lb99;
                                                    }
gm_lb100:;
                                                    {
                                                        gm_l142 = ((uint8_t)1);
                                                        goto gm_lb99;
                                                    }
gm_lb101:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 186, 72);
                                                }
gm_lb99:;
                                                uint8_t t582 = gm_l142;
                                                if (!t582) {
                                                    goto gm_lb98;
                                                }
                                                void * t583 = gm_l67;
                                                {
                                                    {
                                                        uint8_t t584 = GM_LD(uint8_t, ((((uint8_t *)t583) + 8) + 0));
                                                        uint8_t t585 = (uint8_t)(t584 == ((uint8_t)9ULL));
                                                        if (!t585) {
                                                            goto gm_lb103;
                                                        }
                                                        gm_l143 = ((uint8_t)0);
                                                        goto gm_lb102;
                                                    }
gm_lb103:;
                                                    {
                                                        gm_l143 = ((uint8_t)1);
                                                        goto gm_lb102;
                                                    }
gm_lb104:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 186, 100);
                                                }
gm_lb102:;
                                                uint8_t t586 = gm_l143;
                                                if (!t586) {
                                                    goto gm_lb98;
                                                }
                                                void * t587 = F.l0;
                                                void * t588 = gm_l67;
                                                uint8_t t589 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t587, t588);
                                                if (gm_unw.kind) {
                                                    goto gm_ll485;
                                                }
                                                if (!t589) {
                                                    goto gm_lb98;
                                                }
                                                gm_b16_8 t590 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                                                gm_b16_8 t591 = GM_LD(gm_b16_8, ((uint8_t *)&t590));
                                                gm_l74 = t591;
                                                gm_l75 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t593 = gm_l75;
                                                        uint64_t t594 = GM_LD(uint64_t, (((uint8_t *)&gm_l74) + 8));
                                                        uint8_t t595 = (t593 < t594);
                                                        if (!t595) {
                                                            goto gm_lb105;
                                                        }
                                                        {
                                                            uint64_t t596 = gm_l75;
                                                            void * t597 = GM_LD(void *, ((uint8_t *)&gm_l74));
                                                            uint64_t t598 = GM_LD(uint64_t, (((uint8_t *)&gm_l74) + 8));
                                                            uint64_t t599 = gm_l75;
                                                            gm_t5v0c8ia6a5mo t600 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t597) + (uintptr_t)t599 * 56));
                                                            gm_tgsvmddj3e85e t601;
                                                            GM_ST(uint64_t, ((uint8_t *)&t601 + 0), t596);
                                                            GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t601 + 8), t600);
                                                            gm_l144 = t601;
                                                            {
                                                                {
                                                                    uint64_t t602 = GM_LD(uint64_t, ((uint8_t *)&gm_l144));
                                                                    gm_l76 = t602;
                                                                    memmove(((uint8_t *)&F.l77), (((uint8_t *)&gm_l144) + 8), 56);
                                                                    {
                                                                        {
                                                                            {
                                                                                {
                                                                                    uint8_t t603 = (uint8_t)(((((uint8_t *)&F.l77) + 40))[4] != 0);
                                                                                    if (t603) {
                                                                                        goto gm_lb113;
                                                                                    }
                                                                                    gm_l145 = ((uint8_t)0);
                                                                                    goto gm_lb112;
                                                                                }
gm_lb113:;
                                                                                {
                                                                                    gm_l145 = ((uint8_t)1);
                                                                                    goto gm_lb112;
                                                                                }
gm_lb114:;
                                                                                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 188, 20);
                                                                            }
gm_lb112:;
                                                                            uint8_t t604 = gm_l145;
                                                                            if (!t604) {
                                                                                goto gm_lb111;
                                                                            }
                                                                            void * t605 = F.l0;
                                                                            void * t606 = GM_LD(void *, ((uint8_t *)t605));
                                                                            void * t607 = (void *)((uint8_t *)&F.l5);
                                                                            uint64_t t608 = gm_l76;
                                                                            uint8_t t609 = gm_f_inferable_param_f80lrdi(t606, t607, t608);
                                                                            if (!t609) {
                                                                                goto gm_lb111;
                                                                            }
                                                                            gm_b16_8 t610 = GM_LD(gm_b16_8, ((uint8_t *)&F.l77));
                                                                            gm_b16_8 t611 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                                                            uint8_t t612 = 0;
                                                                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t611) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t611))) + i * 16, ((uint8_t *)&t610))) { t612 = 1; break; }
                                                                            uint8_t t613 = (uint8_t)(!t612);
                                                                            if (!t613) {
                                                                                goto gm_lb111;
                                                                            }
                                                                            void * t614 = F.l0;
                                                                            void * t615 = gm_l67;
                                                                            gm_tl874t2atnlh3 t616 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t615) + 72));
                                                                            gm_b16_8 t617 = GM_LD(gm_b16_8, ((uint8_t *)&F.l68));
                                                                            gm_b16_8 t618 = GM_LD(gm_b16_8, ((uint8_t *)&F.l77));
                                                                            void * t619 = F.l0;
                                                                            void * t620 = gm_l67;
                                                                            gm_tl874t2atnlh3 t621 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t620) + 72));
                                                                            gm_b16_8 t622 = gm_f_span_text_sv6qv18(t619, GM_CONV(gm_cseahvg8v7p0vs, t621));
                                                                            gm_tfeab279c16rr t623;
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t623 + 0), t617);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t623 + 16), t618);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t623 + 32), t622);
                                                                            gm_f_format_s1gurul((void *)&gm_l146, (void *)NULL, cx, NULL, &t623);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_ll592;
                                                                            }
                                                                            uint8_t t624 = (uint8_t)!((((uint8_t *)&gm_l146))[0] != 0);
                                                                            if (!t624) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l146) + 8, gm_srcrorgjlg51brd, 189, 38);
                                                                            gm_b16_8 t625 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l146) + 8));
                                                                            gm_b16_8 t626 = GM_LD(gm_b16_8, ((uint8_t *)&t625));
                                                                            gm_b0_1 t627 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t614, t616, t626);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_ll592;
                                                                            }
                                                                            void * t628 = F.l0;
                                                                            void * t629 = GM_LD(void *, ((uint8_t *)t628));
                                                                            gm_tl874t2atnlh3 t630 = gm_l4;
                                                                            void * t631 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t629, t630);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_ll592;
                                                                            }
                                                                            F.ret = t631;
                                                                            goto gm_fret;
                                                                        }
gm_lb111:;
                                                                    }
gm_lb110:;
                                                                    goto gm_lb108;
                                                                }
gm_lb109:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 187, 13);
                                                            }
gm_lb108:;
                                                        }
gm_lb107:;
                                                        uint64_t t632 = gm_l75;
                                                        uint64_t r633; bool o634 = __builtin_add_overflow(t632, ((uint64_t)1ULL), &r633);
                                                        uint64_t t635 = r633;
                                                        gm_l75 = t635;
                                                    }
                                                }
                                                goto gm_ls636;
gm_ll592:;
                                                if (gm_unw.frame == &F.h && gm_unw.label == 11) {
                                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb105; }
                                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb107; }
                                                }
                                                goto gm_ll485;
gm_ls636:;
gm_lb105:;
                                                goto gm_lb97;
                                            }
gm_lb98:;
                                        }
gm_lb97:;
                                    }
                                    {
                                        {
                                            {
                                                void * t637 = gm_l67;
                                                uint8_t t638 = GM_LD(uint8_t, ((((uint8_t *)t637) + 8) + 0));
                                                uint8_t t639 = (uint8_t)((t638 == ((uint8_t)5ULL)));
                                                if (!t639) {
                                                    goto gm_lb116;
                                                }
                                                uint8_t t640 = F.l73;
                                                uint8_t t641 = (uint8_t)!t640;
                                                if (!t641) {
                                                    goto gm_lb116;
                                                }
                                                void * t642 = GM_LD(void *, ((uint8_t *)&F.l33));
                                                uint64_t t643 = GM_LD(uint64_t, (((uint8_t *)&F.l33) + 8));
                                                uint64_t t644 = gm_l66;
                                                if ((uintptr_t)t644 >= (uintptr_t)(t643)) gm_trap_index((uintptr_t)t644, t643, gm_srcrorgjlg51brd, 196, 13);
                                                void * t645 = F.l0;
                                                void * t646 = gm_l67;
                                                gm_t88rcucg1b7uk t647 = gm_l71;
                                                gm_t2gtbh39ptekd t648 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t637) + 8));
                                                gm_t2gtbh39ptekd t649;
                                                memset(&t649, 0, sizeof t649);
                                                GM_ST(uint8_t, ((uint8_t *)&t649 + 0), ((uint8_t)5ULL));
                                                void * t650 = gm_f_arg_value_gavegdu((void *)NULL, cx, NULL, t645, t646, t647);
                                                if (gm_unw.kind) {
                                                    goto gm_ll485;
                                                }
                                                GM_ST(void *, (((uint8_t *)t642) + (uintptr_t)t644 * 8), t650);
                                                goto gm_lb85;
                                            }
gm_lb116:;
                                        }
gm_lb115:;
                                        void * t651 = F.l0;
                                        void * t652 = gm_l67;
                                        gm_t88rcucg1b7uk t653 = gm_l71;
                                        gm_f_iterator_arg_r0if28f((void *)&F.l78, (void *)NULL, cx, NULL, t651, t652, t653);
                                        if (gm_unw.kind) {
                                            goto gm_ll485;
                                        }
                                        void * t654 = F.l0;
                                        uint64_t t655 = GM_LD(uint64_t, ((((uint8_t *)t654) + 1160) + 8));
                                        uint8_t t656 = (t655 > ((uint64_t)0ULL));
                                        if (t656) {
                                            void * t657 = (void *)((uint8_t *)&F.l51);
                                            uint64_t t658 = gm_l66;
                                            void * t659 = F.l0;
                                            gm_b16_8 t660 = GM_LD(gm_b16_8, (((uint8_t *)t659) + 1160));
                                            gm_tf1bjg81u0glv t661;
                                            GM_ST(uint64_t, ((uint8_t *)&t661 + 0), t658);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t661 + 8), t660);
                                            gm_tftb1odc739t5 t662 = gm_f_push_14olj4s(t657, &t661);
                                            gm_l147 = t662;
                                            uint8_t t663 = (uint8_t)!((((uint8_t *)&gm_l147))[0] != 0);
                                            if (!t663) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l147) + 1, gm_srcrorgjlg51brd, 201, 13);
                                            void * t664 = F.l0;
                                            gm_trqfvmjtt4sij t665;
                                            gm_f_list_ir0h3tc_2((void *)&t665, (void *)NULL, cx, NULL);
                                            if (gm_unw.kind) {
                                                goto gm_ll485;
                                            }
                                            GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t664) + 1160), t665);
                                        }
                                        {
                                            {
                                                uint8_t t666 = (uint8_t)((((uint8_t *)&F.l78))[88] != 0);
                                                if (!t666) {
                                                    goto gm_lb118;
                                                }
                                                memmove(((uint8_t *)&F.l79), ((uint8_t *)&F.l78), 88);
                                                memmove(((uint8_t *)&gm_l148), ((uint8_t *)&F.l79), 88);
                                                goto gm_lb117;
                                            }
gm_lb118:;
                                            void * t667 = gm_l67;
                                            memmove(((uint8_t *)&gm_l148), ((uint8_t *)t667), 88);
                                        }
gm_lb117:;
                                        memmove(((uint8_t *)&F.l80), ((uint8_t *)&gm_l148), 88);
                                        void * t668 = F.l0;
                                        void * t669 = F.l0;
                                        void * t670 = GM_LD(void *, ((uint8_t *)t669));
                                        void * t671 = (void *)((uint8_t *)&F.l80);
                                        gm_t88rcucg1b7uk t672 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                                        uint8_t t673 = gm_f_is_const_ref_arg_s0f37he((void *)NULL, cx, NULL, t670, t671, t672);
                                        if (gm_unw.kind) {
                                            goto gm_ll485;
                                        }
                                        GM_ST(uint8_t, (((uint8_t *)t668) + 472), t673);
                                    }
                                    {
                                        void * t674 = F.l0;
                                        void * t675 = (void *)((uint8_t *)&F.l80);
                                        uint8_t t676 = F.l73;
                                        uint8_t t677 = (uint8_t)!t676;
                                        if (t677) {
                                            gm_l150 = ((uint8_t)1);
                                        } else {
                                            uint8_t t678 = gm_l72;
                                            gm_l150 = t678;
                                        }
                                        uint8_t t679 = gm_l150;
                                        if (t679) {
                                            gm_t88rcucg1b7uk t680 = gm_l71;
                                            gm_tkn42u7na3t81 t681;
                                            GM_ST(uint8_t, (uint8_t *)&t681 + 5, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t681 + 6, 0);
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t681), t680);
                                            ((uint8_t *)&t681)[4] = 1;
                                            gm_l149 = t681;
                                        } else {
                                            gm_tkn42u7na3t81 t682;
                                            memset(&t682, 0, sizeof t682);
                                            gm_l149 = t682;
                                        }
                                        gm_tkn42u7na3t81 t683 = gm_l149;
                                        void * t684 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t674, t675, t683);
                                        if (gm_unw.kind) {
                                            goto gm_ll485;
                                        }
                                        gm_l81 = t684;
                                        void * t685 = F.l0;
                                        GM_ST(uint8_t, (((uint8_t *)t685) + 472), ((uint8_t)0));
                                        {
                                            {
                                                uint8_t t686 = F.l73;
                                                if (!t686) {
                                                    goto gm_lb120;
                                                }
                                                uint8_t t687 = gm_l72;
                                                uint8_t t688 = (uint8_t)!t687;
                                                if (!t688) {
                                                    goto gm_lb120;
                                                }
                                                void * t689 = F.l0;
                                                void * t690 = (void *)((uint8_t *)&F.l80);
                                                void * t691 = gm_l81;
                                                gm_b0_1 t692 = gm_f_no_inference_from_fn_klvrs5n((void *)NULL, cx, NULL, t689, t690, t691);
                                                if (gm_unw.kind) {
                                                    goto gm_ll485;
                                                }
                                                goto gm_lb119;
                                            }
gm_lb120:;
                                        }
gm_lb119:;
                                        {
                                            {
                                                {
                                                    {
                                                        uint8_t t693 = (uint8_t)((((uint8_t *)&F.l78))[88] != 0);
                                                        if (t693) {
                                                            goto gm_lb124;
                                                        }
                                                        gm_l151 = ((uint8_t)1);
                                                        goto gm_lb123;
                                                    }
gm_lb124:;
                                                    {
                                                        gm_l151 = ((uint8_t)0);
                                                        goto gm_lb123;
                                                    }
gm_lb125:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 209, 12);
                                                }
gm_lb123:;
                                                uint8_t t694 = gm_l151;
                                                if (!t694) {
                                                    goto gm_lb122;
                                                }
                                                void * t695 = F.l0;
                                                void * t696 = (void *)((uint8_t *)&F.l80);
                                                void * t697 = gm_l81;
                                                gm_t88rcucg1b7uk t698 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                                                uint8_t t699 = gm_f_iterator_call_as_value_00i5mch((void *)NULL, cx, NULL, t695, t696, t697, t698);
                                                if (gm_unw.kind) {
                                                    goto gm_ll485;
                                                }
                                                if (!t699) {
                                                    goto gm_lb122;
                                                }
                                                void * t700 = F.l0;
                                                void * t701 = GM_LD(void *, ((uint8_t *)t700));
                                                gm_tl874t2atnlh3 t702 = gm_l4;
                                                void * t703 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t701, t702);
                                                if (gm_unw.kind) {
                                                    goto gm_ll485;
                                                }
                                                F.ret = t703;
                                                goto gm_fret;
                                            }
gm_lb122:;
                                        }
gm_lb121:;
                                        void * t704 = F.l0;
                                        void * t705 = gm_l81;
                                        gm_t88rcucg1b7uk t706 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                                        void * t707 = gm_f_array_as_slice_gvflfg2((void *)NULL, cx, NULL, t704, t705, t706);
                                        if (gm_unw.kind) {
                                            goto gm_ll485;
                                        }
                                        gm_l81 = t707;
                                    }
                                    {
                                        F.l82 = ((uint8_t)0);
                                        void * t708 = F.l0;
                                        void * t709 = GM_LD(void *, ((uint8_t *)t708));
                                        gm_t88rcucg1b7uk t710 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                                        gm_closure t711 = ((gm_closure){(void *)gm_f_fn_hitalag, (void *)&F.h, cx});
                                        gm_f_walk_v858co7(t709, GM_CONV(gm_csguankko4lmhd, t710), t711);
                                        if (gm_unw.kind) {
                                            goto gm_ll485;
                                        }
                                        {
                                            {
                                                uint64_t t712 = GM_LD(uint64_t, (((uint8_t *)&F.l59) + 8));
                                                uint8_t t713 = (t712 > ((uint64_t)0ULL));
                                                if (!t713) {
                                                    goto gm_lb127;
                                                }
                                                uint8_t t714 = F.l82;
                                                uint8_t t715 = (uint8_t)!t714;
                                                if (!t715) {
                                                    goto gm_lb127;
                                                }
                                                void * t716 = gm_l6;
                                                gm_t88rcucg1b7uk t717 = gm_l71;
                                                uint8_t t718 = gm_f_has_param_u9t0efs(t716, GM_CONV(gm_csguankko4lmhd, t717));
                                                uint8_t t719 = (uint8_t)!t718;
                                                if (!t719) {
                                                    goto gm_lb127;
                                                }
                                                void * t720 = GM_LD(void *, ((uint8_t *)&F.l33));
                                                uint64_t t721 = GM_LD(uint64_t, (((uint8_t *)&F.l33) + 8));
                                                uint64_t t722 = gm_l66;
                                                if ((uintptr_t)t722 >= (uintptr_t)(t721)) gm_trap_index((uintptr_t)t722, t721, gm_srcrorgjlg51brd, 217, 13);
                                                void * t723 = gm_l81;
                                                GM_ST(void *, (((uint8_t *)t720) + (uintptr_t)t722 * 8), t723);
                                                goto gm_lb85;
                                            }
gm_lb127:;
                                        }
gm_lb126:;
                                        void * t724 = F.l0;
                                        void * t725 = (void *)(((uint8_t *)t724) + 96);
                                        void * t726 = F.l0;
                                        void * t727 = GM_LD(void *, ((uint8_t *)t726));
                                        void * t728 = gm_l81;
                                        gm_t88rcucg1b7uk t729 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t728));
                                        gm_t88rcucg1b7uk t730 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t725, t727, t729);
                                        if (gm_unw.kind) {
                                            goto gm_ll485;
                                        }
                                        gm_l83 = t730;
                                        void * t731 = gm_l6;
                                        gm_t88rcucg1b7uk t732 = gm_l83;
                                        uint8_t t733 = gm_f_has_infer_3o82a0b(t731, GM_CONV(gm_csguankko4lmhd, t732));
                                        if (t733) {
                                            void * t734 = (void *)((uint8_t *)&F.l31);
                                            gm_t88rcucg1b7uk t735 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                                            void * t736 = gm_l81;
                                            gm_t88rcucg1b7uk t737 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t736));
                                            void * t738 = gm_l81;
                                            gm_tl874t2atnlh3 t739 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t738) + 72));
                                            gm_thsh8t5u55gps t740;
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t740 + 0), t735);
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t740 + 4), t737);
                                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t740 + 8), t739);
                                            gm_tftb1odc739t5 t741 = gm_f_push_cl1fihl(t734, &t740);
                                            gm_l152 = t741;
                                            uint8_t t742 = (uint8_t)!((((uint8_t *)&gm_l152))[0] != 0);
                                            if (!t742) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l152) + 1, gm_srcrorgjlg51brd, 221, 31);
                                        }
                                        gm_trqfvmjtt4sij t743;
                                        gm_f_list_oalq03d((void *)&t743, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_ll485;
                                        }
                                        F.l84 = t743;
                                        void * t744 = F.l0;
                                        void * t745 = GM_LD(void *, ((uint8_t *)t744));
                                        gm_t88rcucg1b7uk t746 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l68) + 16));
                                        gm_t88rcucg1b7uk t747 = gm_l83;
                                        void * t748 = (void *)((uint8_t *)&F.l84);
                                        gm_b0_1 t749 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t745, t746, t747, t748);
                                        if (gm_unw.kind) {
                                            goto gm_ll485;
                                        }
                                        gm_b16_8 t750 = GM_LD(gm_b16_8, ((uint8_t *)&F.l84));
                                        gm_l85 = t750;
                                        gm_l86 = ((uint64_t)0ULL);
                                    }
                                    {
                                        {
                                            for (;;) {
                                                uint64_t t752 = gm_l86;
                                                uint64_t t753 = GM_LD(uint64_t, (((uint8_t *)&gm_l85) + 8));
                                                uint8_t t754 = (t752 < t753);
                                                if (!t754) {
                                                    goto gm_lb128;
                                                }
                                                {
                                                    void * t755 = GM_LD(void *, ((uint8_t *)&gm_l85));
                                                    uint64_t t756 = GM_LD(uint64_t, (((uint8_t *)&gm_l85) + 8));
                                                    uint64_t t757 = gm_l86;
                                                    {
                                                        {
                                                            gm_tplpbqf3duvn3 t758 = GM_LD(gm_tplpbqf3duvn3, (((uint8_t *)t755) + (uintptr_t)t757 * 8));
                                                            gm_l87 = t758;
                                                            gm_b16_8 t759 = GM_LD(gm_b16_8, ((uint8_t *)&F.l30));
                                                            gm_b16_8 t760 = GM_LD(gm_b16_8, ((uint8_t *)&t759));
                                                            uint32_t t761 = GM_LD(uint32_t, ((uint8_t *)&gm_l87));
                                                            gm_tkn42u7na3t81 t762 = gm_f_bound_to_5c11t0h(t760, t761);
                                                            gm_l153 = t762;
                                                            uint8_t t763 = (uint8_t)((((uint8_t *)&gm_l153))[4] != 0);
                                                            if (t763) {
                                                                gm_t88rcucg1b7uk t764 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l153));
                                                                gm_l154 = t764;
                                                            } else {
                                                                void * t765 = (void *)((uint8_t *)&F.l30);
                                                                gm_tplpbqf3duvn3 t766 = gm_l87;
                                                                gm_tftb1odc739t5 t767 = gm_f_push_h69n5fi(t765, GM_CONV(gm_cs7e9ppsd384bj, t766));
                                                                gm_l155 = t767;
                                                                uint8_t t768 = (uint8_t)!((((uint8_t *)&gm_l155))[0] != 0);
                                                                if (!t768) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l155) + 1, gm_srcrorgjlg51brd, 226, 17);
                                                                goto gm_lb130;
                                                            }
                                                            gm_t88rcucg1b7uk t769 = gm_l154;
                                                            gm_l88 = t769;
                                                            gm_t88rcucg1b7uk t770 = gm_l88;
                                                            gm_t88rcucg1b7uk t771 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l87) + 4));
                                                            uint8_t t772 = (gm_eq9m6mvtmgde(((uint8_t *)&t770), ((uint8_t *)&t771)));
                                                            if (t772) {
                                                                gm_l157 = ((uint8_t)1);
                                                            } else {
                                                                uint8_t t773 = gm_l25;
                                                                gm_l157 = t773;
                                                            }
                                                            uint8_t t774 = gm_l157;
                                                            if (t774) {
                                                                gm_l156 = ((uint8_t)1);
                                                            } else {
                                                                void * t775 = F.l0;
                                                                void * t776 = GM_LD(void *, ((uint8_t *)t775));
                                                                gm_t88rcucg1b7uk t777 = gm_l88;
                                                                gm_t88rcucg1b7uk t778 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l87) + 4));
                                                                uint8_t t779 = gm_f_same_int_value_ngrqsok((void *)NULL, cx, NULL, t776, t777, t778);
                                                                if (gm_unw.kind) {
                                                                    goto gm_ll751;
                                                                }
                                                                gm_l156 = t779;
                                                            }
                                                            uint8_t t780 = gm_l156;
                                                            if (t780) {
                                                                goto gm_lb130;
                                                            }
                                                            gm_l32 = ((uint8_t)1);
                                                            void * t781 = F.l0;
                                                            void * t782 = gm_l67;
                                                            gm_tl874t2atnlh3 t783 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t782) + 72));
                                                            void * t784 = gm_l6;
                                                            uint32_t t785 = GM_LD(uint32_t, ((uint8_t *)&gm_l87));
                                                            gm_b16_8 t786 = gm_f_param_name_rbnop5l(t784, t785);
                                                            void * t787 = F.l0;
                                                            gm_t88rcucg1b7uk t788 = gm_l88;
                                                            gm_b16_8 t789 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t787, t788);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll751;
                                                            }
                                                            void * t790 = F.l0;
                                                            gm_t88rcucg1b7uk t791 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l87) + 4));
                                                            gm_b16_8 t792 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t790, t791);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll751;
                                                            }
                                                            gm_tfeab279c16rr t793;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t793 + 0), t786);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t793 + 16), t789);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t793 + 32), t792);
                                                            gm_f_format_7u7i1da((void *)&gm_l158, (void *)NULL, cx, NULL, &t793);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll751;
                                                            }
                                                            uint8_t t794 = (uint8_t)!((((uint8_t *)&gm_l158))[0] != 0);
                                                            if (!t794) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l158) + 8, gm_srcrorgjlg51brd, 231, 30);
                                                            gm_b16_8 t795 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l158) + 8));
                                                            gm_b16_8 t796 = GM_LD(gm_b16_8, ((uint8_t *)&t795));
                                                            gm_b0_1 t797 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t781, t783, t796);
                                                            if (gm_unw.kind) {
                                                                goto gm_ll751;
                                                            }
                                                            goto gm_lb131;
                                                        }
gm_lb132:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 224, 9);
                                                    }
gm_lb131:;
                                                }
gm_lb130:;
                                                uint64_t t798 = gm_l86;
                                                uint64_t r799; bool o800 = __builtin_add_overflow(t798, ((uint64_t)1ULL), &r799);
                                                uint64_t t801 = r799;
                                                gm_l86 = t801;
                                            }
                                        }
                                        goto gm_ls802;
gm_ll751:;
                                        if (gm_unw.frame == &F.h && gm_unw.label == 12) {
                                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb128; }
                                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb130; }
                                        }
                                        goto gm_ll485;
gm_ls802:;
gm_lb128:;
                                    }
                                    {
                                        void * t803 = GM_LD(void *, ((uint8_t *)&F.l33));
                                        uint64_t t804 = GM_LD(uint64_t, (((uint8_t *)&F.l33) + 8));
                                        uint64_t t805 = gm_l66;
                                        if ((uintptr_t)t805 >= (uintptr_t)(t804)) gm_trap_index((uintptr_t)t805, t804, gm_srcrorgjlg51brd, 233, 9);
                                        void * t806 = gm_l81;
                                        GM_ST(void *, (((uint8_t *)t803) + (uintptr_t)t805 * 8), t806);
                                        goto gm_lb86;
                                    }
                                }
gm_lb87:;
                            }
                            {
                                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 158, 5);
                            }
                        }
gm_lb86:;
                    }
gm_lb85:;
                }
                {
                    uint64_t t807 = gm_l64;
                    uint64_t r808; bool o809 = __builtin_add_overflow(t807, ((uint64_t)1ULL), &r808);
                    uint64_t t810 = r808;
                    gm_l64 = t810;
                }
            }
        }
        goto gm_ls811;
gm_ll485:;
        if (gm_unw.frame == &F.h && gm_unw.label == 10) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb83; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb85; }
        }
        goto gm_lad;
gm_ls811:;
gm_lb83:;
    }
    {
        uint8_t t812 = gm_l32;
        if (t812) {
            void * t813 = F.l0;
            void * t814 = GM_LD(void *, ((uint8_t *)t813));
            gm_tl874t2atnlh3 t815 = gm_l4;
            void * t816 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t814, t815);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t816;
            goto gm_fret;
        }
        void * t817 = F.l0;
        void * t818 = (void *)((uint8_t *)&F.l5);
        void * t819 = (void *)((uint8_t *)&F.l30);
        gm_b16_8 t820 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
        gm_b16_8 t821 = GM_LD(gm_b16_8, ((uint8_t *)&t820));
        gm_b0_1 t822 = gm_f_bind_literal_fallbacks_r2021qp((void *)NULL, cx, NULL, t817, t818, t819, t821);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t823 = F.l0;
        void * t824 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t825 = GM_LD(gm_b16_8, ((uint8_t *)&F.l33));
        gm_b16_8 t826 = GM_LD(gm_b16_8, ((uint8_t *)&t825));
        void * t827 = (void *)((uint8_t *)&F.l30);
        gm_b0_1 t828 = gm_f_bind_unit_defaults_htn8poj((void *)NULL, cx, NULL, t823, t824, t826, t827);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t829 = F.l0;
        void * t830 = (void *)((uint8_t *)&F.l5);
        void * t831 = (void *)((uint8_t *)&F.l30);
        gm_b0_1 t832 = gm_f_bind_available_effects_uav06po((void *)NULL, cx, NULL, t829, t830, t831);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t833 = F.l0;
        void * t834 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t835 = GM_LD(gm_b16_8, ((uint8_t *)&F.l30));
        gm_b16_8 t836 = GM_LD(gm_b16_8, ((uint8_t *)&t835));
        gm_b16_8 t837 = GM_LD(gm_b16_8, ((uint8_t *)&F.l33));
        gm_b16_8 t838 = GM_LD(gm_b16_8, ((uint8_t *)&t837));
        gm_tl874t2atnlh3 t839 = gm_l4;
        gm_f_generic_type_args_mj9hh1l((void *)&gm_l159, (void *)NULL, cx, NULL, t833, t834, t836, t838, t839);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t840 = (uint8_t)((((uint8_t *)&gm_l159))[16] != 0);
        if (t840) {
            gm_b16_8 t841 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l159));
            gm_l160 = t841;
        } else {
            void * t842 = F.l0;
            void * t843 = GM_LD(void *, ((uint8_t *)t842));
            gm_tl874t2atnlh3 t844 = gm_l4;
            void * t845 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t843, t844);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t845;
            goto gm_fret;
        }
        gm_b16_8 t846 = gm_l160;
        gm_l89 = t846;
        void * t847 = F.l0;
        void * t848 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t849 = gm_l89;
        gm_b16_8 t850 = GM_LD(gm_b16_8, ((uint8_t *)&t849));
        gm_tl874t2atnlh3 t851 = gm_l4;
        uint8_t t852 = gm_f_check_generic_constraint_6138rrh((void *)NULL, cx, NULL, t847, t848, t850, t851);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t853 = (uint8_t)!t852;
        if (t853) {
            void * t854 = F.l0;
            void * t855 = GM_LD(void *, ((uint8_t *)t854));
            gm_tl874t2atnlh3 t856 = gm_l4;
            void * t857 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t855, t856);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t857;
            goto gm_fret;
        }
        uint8_t t858 = gm_l19;
        if (t858) {
            void * t859 = F.l0;
            void * t860 = F.l0;
            void * t861 = GM_LD(void *, ((uint8_t *)t860));
            uint32_t t862 = gm_l1;
            gm_b16_8 t863 = gm_l89;
            gm_b16_8 t864 = GM_LD(gm_b16_8, ((uint8_t *)&t863));
            gm_tl874t2atnlh3 t865 = gm_l4;
            uint32_t t866 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t861, t862, t864, t865);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t867 = gm_l4;
            void * t868 = gm_f_fn_ref_aqs2fl4((void *)NULL, cx, NULL, t859, t866, t867);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t868;
            goto gm_fret;
        }
    }
    {
        gm_trqfvmjtt4sij t869;
        gm_f_list_oalq03d((void *)&t869, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l90 = t869;
        gm_b16_8 t870 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 80));
        gm_b16_8 t871 = GM_LD(gm_b16_8, ((uint8_t *)&t870));
        gm_l91 = t871;
        gm_l92 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t873 = gm_l92;
                uint64_t t874 = GM_LD(uint64_t, (((uint8_t *)&gm_l91) + 8));
                uint8_t t875 = (t873 < t874);
                if (!t875) {
                    goto gm_lb133;
                }
                {
                    uint64_t t876 = gm_l92;
                    void * t877 = GM_LD(void *, ((uint8_t *)&gm_l91));
                    uint64_t t878 = GM_LD(uint64_t, (((uint8_t *)&gm_l91) + 8));
                    uint64_t t879 = gm_l92;
                    uint32_t t880 = GM_LD(uint32_t, (((uint8_t *)t877) + (uintptr_t)t879 * 4));
                    gm_tn68el8d14vku t881;
                    GM_ST(uint32_t, (uint8_t *)&t881 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t881 + 0), t876);
                    GM_ST(uint32_t, ((uint8_t *)&t881 + 8), t880);
                    gm_l161 = t881;
                    {
                        {
                            uint64_t t882 = GM_LD(uint64_t, ((uint8_t *)&gm_l161));
                            gm_l93 = t882;
                            uint32_t t883 = GM_LD(uint32_t, (((uint8_t *)&gm_l161) + 8));
                            gm_l94 = t883;
                            void * t884 = (void *)((uint8_t *)&F.l90);
                            uint32_t t885 = gm_l94;
                            void * t886 = GM_LD(void *, ((uint8_t *)&gm_l89));
                            uint64_t t887 = GM_LD(uint64_t, (((uint8_t *)&gm_l89) + 8));
                            uint64_t t888 = gm_l93;
                            if ((uintptr_t)t888 >= (uintptr_t)(t887)) gm_trap_index((uintptr_t)t888, t887, gm_srcrorgjlg51brd, 243, 72);
                            gm_t88rcucg1b7uk t889 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t886) + (uintptr_t)t888 * 4));
                            gm_tplpbqf3duvn3 t890;
                            GM_ST(uint32_t, ((uint8_t *)&t890 + 0), t885);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t890 + 4), t889);
                            gm_tftb1odc739t5 t891 = gm_f_push_h69n5fi(t884, GM_CONV(gm_cs7e9ppsd384bj, t890));
                            gm_l162 = t891;
                            uint8_t t892 = (uint8_t)!((((uint8_t *)&gm_l162))[0] != 0);
                            if (!t892) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l162) + 1, gm_srcrorgjlg51brd, 243, 43);
                            goto gm_lb136;
                        }
gm_lb137:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 243, 5);
                    }
gm_lb136:;
                }
gm_lb135:;
                uint64_t t893 = gm_l92;
                uint64_t r894; bool o895 = __builtin_add_overflow(t893, ((uint64_t)1ULL), &r894);
                uint64_t t896 = r894;
                gm_l92 = t896;
            }
        }
gm_lb133:;
        gm_trqfvmjtt4sij t897;
        gm_f_list_5dhgg4i((void *)&t897, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l95 = t897;
        {
            {
                uint8_t t898 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
                if (!t898) {
                    goto gm_lb139;
                }
                uint64_t t899 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                gm_l96 = t899;
                void * t900 = (void *)((uint8_t *)&F.l95);
                uint64_t t901 = gm_l96;
                gm_tftb1odc739t5 t902 = gm_f_push_ucgmlo2(t900, t901);
                gm_l163 = t902;
                uint8_t t903 = (uint8_t)!((((uint8_t *)&gm_l163))[0] != 0);
                if (!t903) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l163) + 1, gm_srcrorgjlg51brd, 246, 26);
                goto gm_lb138;
            }
gm_lb139:;
        }
gm_lb138:;
        gm_b16_8 t904 = GM_LD(gm_b16_8, ((uint8_t *)&F.l36));
        gm_l97 = t904;
        gm_l98 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t906 = gm_l98;
                uint64_t t907 = GM_LD(uint64_t, (((uint8_t *)&gm_l97) + 8));
                uint8_t t908 = (t906 < t907);
                if (!t908) {
                    goto gm_lb140;
                }
                {
                    void * t909 = GM_LD(void *, ((uint8_t *)&gm_l97));
                    uint64_t t910 = GM_LD(uint64_t, (((uint8_t *)&gm_l97) + 8));
                    uint64_t t911 = gm_l98;
                    {
                        {
                            memmove(((uint8_t *)&F.l99), (((uint8_t *)t909) + (uintptr_t)t911 * 104), 104);
                            {
                                {
                                    void * t912 = GM_LD(void *, (((uint8_t *)&F.l5) + 40));
                                    uint64_t t913 = GM_LD(uint64_t, ((((uint8_t *)&F.l5) + 40) + 8));
                                    uint64_t t914 = GM_LD(uint64_t, ((uint8_t *)&F.l99));
                                    if ((uintptr_t)t914 >= (uintptr_t)(t913)) gm_trap_index((uintptr_t)t914, t913, gm_srcrorgjlg51brd, 248, 12);
                                    {
                                        {
                                            uint8_t t915 = (uint8_t)((((((uint8_t *)t912) + (uintptr_t)t914 * 56) + 40))[4] != 0);
                                            if (t915) {
                                                goto gm_lb148;
                                            }
                                            gm_l164 = ((uint8_t)1);
                                            goto gm_lb147;
                                        }
gm_lb148:;
                                        {
                                            gm_l164 = ((uint8_t)0);
                                            goto gm_lb147;
                                        }
gm_lb149:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 248, 12);
                                    }
gm_lb147:;
                                    uint8_t t916 = gm_l164;
                                    if (!t916) {
                                        goto gm_lb146;
                                    }
                                    void * t917 = GM_LD(void *, ((uint8_t *)&F.l33));
                                    uint64_t t918 = GM_LD(uint64_t, (((uint8_t *)&F.l33) + 8));
                                    uint64_t t919 = GM_LD(uint64_t, ((uint8_t *)&F.l99));
                                    if ((uintptr_t)t919 >= (uintptr_t)(t918)) gm_trap_index((uintptr_t)t919, t918, gm_srcrorgjlg51brd, 248, 54);
                                    {
                                        {
                                            uint8_t t920 = (uint8_t)(GM_LD(void *, (((uint8_t *)t917) + (uintptr_t)t919 * 8)) != 0);
                                            if (t920) {
                                                goto gm_lb151;
                                            }
                                            gm_l165 = ((uint8_t)0);
                                            goto gm_lb150;
                                        }
gm_lb151:;
                                        {
                                            gm_l165 = ((uint8_t)1);
                                            goto gm_lb150;
                                        }
gm_lb152:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 248, 54);
                                    }
gm_lb150:;
                                    uint8_t t921 = gm_l165;
                                    if (!t921) {
                                        goto gm_lb146;
                                    }
                                    void * t922 = (void *)((uint8_t *)&F.l95);
                                    uint64_t t923 = GM_LD(uint64_t, ((uint8_t *)&F.l99));
                                    gm_tftb1odc739t5 t924 = gm_f_push_ucgmlo2(t922, t923);
                                    gm_l166 = t924;
                                    uint8_t t925 = (uint8_t)!((((uint8_t *)&gm_l166))[0] != 0);
                                    if (!t925) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l166) + 1, gm_srcrorgjlg51brd, 248, 92);
                                    goto gm_lb145;
                                }
gm_lb146:;
                            }
gm_lb145:;
                            goto gm_lb143;
                        }
gm_lb144:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 247, 5);
                    }
gm_lb143:;
                }
gm_lb142:;
                uint64_t t926 = gm_l98;
                uint64_t r927; bool o928 = __builtin_add_overflow(t926, ((uint64_t)1ULL), &r927);
                uint64_t t929 = r927;
                gm_l98 = t929;
            }
        }
gm_lb140:;
    }
    {
        void * t930 = F.l0;
        void * t931 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t932 = GM_LD(gm_b16_8, ((uint8_t *)&F.l33));
        gm_b16_8 t933 = GM_LD(gm_b16_8, ((uint8_t *)&t932));
        gm_b16_8 t934 = GM_LD(gm_b16_8, ((uint8_t *)&F.l90));
        gm_b16_8 t935 = GM_LD(gm_b16_8, ((uint8_t *)&t934));
        gm_tl874t2atnlh3 t936 = gm_l4;
        gm_td7pkamr64l0c t937;
        gm_f_coerce_generic_args_9ald24q((void *)&t937, (void *)NULL, cx, NULL, t930, t931, t933, t935, t936);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l100 = t937;
        void * t938 = (void *)((uint8_t *)&F.l100);
        gm_l101 = t938;
        memmove(((uint8_t *)&F.l102), (((uint8_t *)&F.l100) + 40), 40);
        void * t939 = F.l0;
        void * t940 = GM_LD(void *, ((uint8_t *)t939));
        void * t941 = (void *)(((uint8_t *)&F.l5) + 64);
        gm_b16_8 t942 = GM_LD(gm_b16_8, ((uint8_t *)&F.l90));
        gm_b16_8 t943 = GM_LD(gm_b16_8, ((uint8_t *)&t942));
        gm_tnpifv20irnph t944 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t940, t941, t943);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l103 = t944;
        void * t945 = F.l0;
        void * t946 = (void *)((uint8_t *)&gm_l103);
        gm_tl874t2atnlh3 t947 = gm_l4;
        gm_b0_1 t948 = gm_f_record_effects_0eel5vu((void *)NULL, cx, NULL, t945, t946, t947);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t949 = F.l0;
        void * t950 = gm_l101;
        gm_b16_8 t951 = GM_LD(gm_b16_8, ((uint8_t *)&F.l102));
        gm_b16_8 t952 = GM_LD(gm_b16_8, ((uint8_t *)&t951));
        gm_b16_8 t953 = GM_LD(gm_b16_8, ((uint8_t *)&F.l95));
        void * t954 = (void *)((uint8_t *)&F.l51);
        gm_b16_8 t955 = GM_LD(gm_b16_8, ((uint8_t *)&F.l47));
        gm_b16_8 t956 = GM_LD(gm_b16_8, ((uint8_t *)&t955));
        gm_b16_8 t957 = gm_f_order_generic_args_m8gcs0c((void *)NULL, cx, NULL, t949, t950, t952, t953, t954, t956);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l104 = t957;
        void * t958 = F.l0;
        void * t959 = GM_LD(void *, ((uint8_t *)t958));
        uint32_t t960 = gm_l1;
        gm_b16_8 t961 = gm_l89;
        gm_b16_8 t962 = GM_LD(gm_b16_8, ((uint8_t *)&t961));
        gm_tl874t2atnlh3 t963 = gm_l4;
        uint32_t t964 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t959, t960, t962, t963);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l105 = t964;
        void * t965 = F.l0;
        void * t966 = GM_LD(void *, ((uint8_t *)t965));
        gm_t88rcucg1b7uk t967 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 56));
        gm_b16_8 t968 = GM_LD(gm_b16_8, ((uint8_t *)&F.l90));
        gm_b16_8 t969 = GM_LD(gm_b16_8, ((uint8_t *)&t968));
        gm_t88rcucg1b7uk t970 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t966, t967, t969);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t971 = gm_l105;
        void * t972 = gm_l101;
        gm_b16_8 t973 = GM_LD(gm_b16_8, ((uint8_t *)t972));
        gm_tsth9qu1dv0g3 t974;
        memset(&t974, 0, sizeof t974);
        GM_ST(uint8_t, ((uint8_t *)&t974 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t974 + 8), t971);
        gm_to76jli3bqddq t975;
        GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t975 + 0), t974);
        GM_ST(gm_b16_8, ((uint8_t *)&t975 + 40), t973);
        gm_tl874t2atnlh3 t976 = gm_l4;
        gm_t2b6tm8c0vgo1 t977;
        GM_ST(uint8_t, (uint8_t *)&t977 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t977 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t977 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t977 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t977 + 8), t975);
        void * t978 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t970, &t977, t976);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l106 = t978;
        void * t979 = F.l0;
        gm_b16_8 t980 = gm_l104;
        void * t981 = gm_l106;
        void * t982 = gm_f_finish_call_kjknkfg((void *)NULL, cx, NULL, t979, t980, t981, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t982;
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
__attribute__((aligned(4))) void * gm_f_generic_fn_from_expected_ok2k7go(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tkn42u7na3t81 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_generic_fn_from_expected_ok2k7go F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* f */
    gm_tkn42u7na3t81 gm_l2; /* expect */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_t88rcucg1b7uk gm_l4; /* e */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    gm_t88rcucg1b7uk gm_l15; /* t */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint32_t gm_l19; /* pid */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* k */
    uint32_t gm_l23; /* pid */
    gm_t88rcucg1b7uk gm_l24; /* t */
    gm_t88rcucg1b7uk gm_l25; /* part */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l28; /* c */
    GM_STACK_CHECK(&F, 1747u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t88rcucg1b7uk gm_l29;
    gm_t53at80pfdus3 gm_l30;
    uint8_t gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tlcm4lvu3g1a8 gm_l33;
    uint8_t gm_l34;
    gm_tnnjeerolos0a gm_l35;
    gm_tkn42u7na3t81 gm_l36;
    gm_t88rcucg1b7uk gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tn68el8d14vku gm_l39;
    gm_tkn42u7na3t81 gm_l40;
    gm_tabb710d9mq2e gm_l41;
    {
        uint8_t t37 = (uint8_t)((((uint8_t *)&gm_l2))[4] != 0);
        if (t37) {
            gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l2));
            gm_l29 = t38;
        } else {
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t39 = gm_l29;
        gm_l4 = t39;
        {
            {
                {
                    {
                        void * t40 = gm_l0;
                        void * t41 = GM_LD(void *, ((uint8_t *)t40));
                        void * t42 = (void *)(((uint8_t *)t41) + 8);
                        void * t43 = gm_l0;
                        void * t44 = (void *)(((uint8_t *)t43) + 96);
                        void * t45 = gm_l0;
                        void * t46 = GM_LD(void *, ((uint8_t *)t45));
                        gm_t88rcucg1b7uk t47 = gm_l4;
                        gm_t88rcucg1b7uk t48 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t44, t46, t47);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_f_kind_jcnt4hm((void *)&gm_l30, t42, GM_CONV(gm_csguankko4lmhd, t48));
                        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l30) + 0));
                        uint8_t t50 = (uint8_t)(t49 == ((uint8_t)16ULL));
                        if (!t50) {
                            goto gm_lb2;
                        }
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l30) + 8), 48);
                        void * t51 = gm_l0;
                        void * t52 = GM_LD(void *, ((uint8_t *)t51));
                        void * t53 = GM_LD(void *, (((uint8_t *)t52) + 344));
                        uint64_t t54 = GM_LD(uint64_t, ((((uint8_t *)t52) + 344) + 8));
                        uint32_t t55 = gm_l1;
                        uint64_t t56 = (uint64_t)t55;
                        if ((uintptr_t)t56 >= (uintptr_t)(t54)) gm_trap_index((uintptr_t)t56, t54, gm_srcrorgjlg51brd, 675, 12);
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t53) + (uintptr_t)t56 * 136), 136);
                        gm_trqfvmjtt4sij t57;
                        gm_f_list_nof7vfj((void *)&t57, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l7 = t57;
                        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 40));
                        gm_l8 = t58;
                        gm_l9 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t59 = gm_l9;
                                uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                uint8_t t61 = (t59 < t60);
                                if (!t61) {
                                    goto gm_lb3;
                                }
                                {
                                    void * t62 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                    uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    uint64_t t64 = gm_l9;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l10), (((uint8_t *)t62) + (uintptr_t)t64 * 56), 56);
                                            {
                                                {
                                                    uint8_t t65 = (uint8_t)(((((uint8_t *)&F.l10) + 40))[4] != 0);
                                                    if (t65) {
                                                        goto gm_lb9;
                                                    }
                                                    gm_l31 = ((uint8_t)1);
                                                    goto gm_lb8;
                                                }
gm_lb9:;
                                                {
                                                    gm_l31 = ((uint8_t)0);
                                                    goto gm_lb8;
                                                }
gm_lb10:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 678, 12);
                                            }
gm_lb8:;
                                            uint8_t t66 = gm_l31;
                                            if (t66) {
                                                void * t67 = (void *)((uint8_t *)&F.l7);
                                                gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                                                gm_tftb1odc739t5 t69 = gm_f_push_k3l9nic(t67, GM_CONV(gm_csguankko4lmhd, t68));
                                                gm_l32 = t69;
                                                uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                                if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcrorgjlg51brd, 678, 32);
                                            }
                                            goto gm_lb6;
                                        }
gm_lb7:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 677, 5);
                                    }
gm_lb6:;
                                }
gm_lb5:;
                                uint64_t t71 = gm_l9;
                                uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                                uint64_t t74 = r72;
                                gm_l9 = t74;
                            }
                        }
gm_lb3:;
                    }
                    {
                        uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                        uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                        uint8_t t77 = (!(t75 == t76));
                        if (t77) {
                            F.ret = ((void *)0);
                            goto gm_fret;
                        }
                        gm_trqfvmjtt4sij t78;
                        gm_f_list_oalq03d((void *)&t78, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l11 = t78;
                        gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                        gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                        gm_l12 = t80;
                        gm_l13 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t81 = gm_l13;
                                uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint8_t t83 = (t81 < t82);
                                if (!t83) {
                                    goto gm_lb11;
                                }
                                {
                                    uint64_t t84 = gm_l13;
                                    void * t85 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                    uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                    uint64_t t87 = gm_l13;
                                    gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t85) + (uintptr_t)t87 * 4));
                                    gm_tlcm4lvu3g1a8 t89;
                                    GM_ST(uint32_t, (uint8_t *)&t89 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t89 + 0), t84);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t89 + 8), t88);
                                    gm_l33 = t89;
                                    {
                                        {
                                            uint64_t t90 = GM_LD(uint64_t, ((uint8_t *)&gm_l33));
                                            gm_l14 = t90;
                                            gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l33) + 8));
                                            gm_l15 = t91;
                                            void * t92 = gm_l0;
                                            void * t93 = GM_LD(void *, ((uint8_t *)t92));
                                            gm_t88rcucg1b7uk t94 = gm_l15;
                                            void * t95 = GM_LD(void *, ((uint8_t *)&F.l5));
                                            uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                            uint64_t t97 = gm_l14;
                                            if ((uintptr_t)t97 >= (uintptr_t)(t96)) gm_trap_index((uintptr_t)t97, t96, gm_srcrorgjlg51brd, 682, 65);
                                            gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t95) + (uintptr_t)t97 * 4));
                                            void * t99 = (void *)((uint8_t *)&F.l11);
                                            gm_b0_1 t100 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t93, t94, t98, t99);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_lb14;
                                        }
gm_lb15:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 682, 5);
                                    }
gm_lb14:;
                                }
gm_lb13:;
                                uint64_t t101 = gm_l13;
                                uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                                uint64_t t104 = r102;
                                gm_l13 = t104;
                            }
                        }
gm_lb11:;
                        void * t105 = gm_l0;
                        void * t106 = GM_LD(void *, ((uint8_t *)t105));
                        gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 56));
                        gm_t88rcucg1b7uk t108 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 16));
                        void * t109 = (void *)((uint8_t *)&F.l11);
                        gm_b0_1 t110 = gm_f_match_params_6vndkud((void *)NULL, cx, NULL, t106, t107, t108, t109);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_trqfvmjtt4sij t111;
                        gm_f_list_nof7vfj((void *)&t111, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l16 = t111;
                        gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 80));
                        gm_l17 = t112;
                        gm_l18 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t113 = gm_l18;
                                uint64_t t114 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                uint8_t t115 = (t113 < t114);
                                if (!t115) {
                                    goto gm_lb16;
                                }
                                {
                                    void * t116 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                    uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                    uint64_t t118 = gm_l18;
                                    {
                                        {
                                            uint32_t t119 = GM_LD(uint32_t, (((uint8_t *)t116) + (uintptr_t)t118 * 4));
                                            gm_l19 = t119;
                                            void * t120 = gm_l0;
                                            void * t121 = GM_LD(void *, ((uint8_t *)t120));
                                            uint32_t t122 = gm_l19;
                                            gm_tnnjeerolos0a t123 = gm_f_param_kind_a0aikmb(t121, t122);
                                            gm_l35 = t123;
                                            {
                                                {
                                                    uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                                                    uint8_t t125 = (uint8_t)(t124 == ((uint8_t)2ULL));
                                                    if (!t125) {
                                                        goto gm_lb22;
                                                    }
                                                    gm_l34 = ((uint8_t)1);
                                                    goto gm_lb21;
                                                }
gm_lb22:;
                                                {
                                                    gm_l34 = ((uint8_t)0);
                                                    goto gm_lb21;
                                                }
gm_lb23:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 686, 12);
                                            }
gm_lb21:;
                                            uint8_t t126 = gm_l34;
                                            if (t126) {
                                                F.ret = ((void *)0);
                                                goto gm_fret;
                                            }
                                            void * t127 = (void *)((uint8_t *)&F.l16);
                                            gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                                            gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                                            uint32_t t130 = gm_l19;
                                            gm_tkn42u7na3t81 t131 = gm_f_bound_to_5c11t0h(t129, t130);
                                            gm_l36 = t131;
                                            uint8_t t132 = (uint8_t)((((uint8_t *)&gm_l36))[4] != 0);
                                            if (t132) {
                                                gm_t88rcucg1b7uk t133 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l36));
                                                gm_l37 = t133;
                                            } else {
                                                F.ret = ((void *)0);
                                                goto gm_fret;
                                            }
                                            gm_t88rcucg1b7uk t134 = gm_l37;
                                            gm_tftb1odc739t5 t135 = gm_f_push_k3l9nic(t127, GM_CONV(gm_csguankko4lmhd, t134));
                                            gm_l38 = t135;
                                            uint8_t t136 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                            if (!t136) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srcrorgjlg51brd, 687, 9);
                                            goto gm_lb19;
                                        }
gm_lb20:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 685, 5);
                                    }
gm_lb19:;
                                }
gm_lb18:;
                                uint64_t t137 = gm_l18;
                                uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                                uint64_t t140 = r138;
                                gm_l18 = t140;
                            }
                        }
gm_lb16:;
                    }
                    {
                        gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 80));
                        gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                        gm_l20 = t142;
                        gm_l21 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t143 = gm_l21;
                                uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                uint8_t t145 = (t143 < t144);
                                if (!t145) {
                                    goto gm_lb24;
                                }
                                {
                                    uint64_t t146 = gm_l21;
                                    void * t147 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                    uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                    uint64_t t149 = gm_l21;
                                    uint32_t t150 = GM_LD(uint32_t, (((uint8_t *)t147) + (uintptr_t)t149 * 4));
                                    gm_tn68el8d14vku t151;
                                    GM_ST(uint32_t, (uint8_t *)&t151 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t151 + 0), t146);
                                    GM_ST(uint32_t, ((uint8_t *)&t151 + 8), t150);
                                    gm_l39 = t151;
                                    {
                                        {
                                            uint64_t t152 = GM_LD(uint64_t, ((uint8_t *)&gm_l39));
                                            gm_l22 = t152;
                                            uint32_t t153 = GM_LD(uint32_t, (((uint8_t *)&gm_l39) + 8));
                                            gm_l23 = t153;
                                            void * t154 = GM_LD(void *, ((uint8_t *)&F.l16));
                                            uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                                            uint64_t t156 = gm_l22;
                                            if ((uintptr_t)t156 >= (uintptr_t)(t155)) gm_trap_index((uintptr_t)t156, t155, gm_srcrorgjlg51brd, 690, 14);
                                            gm_t88rcucg1b7uk t157 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t154) + (uintptr_t)t156 * 4));
                                            gm_l24 = t157;
                                            {
                                                {
                                                    void * t158 = gm_l0;
                                                    void * t159 = GM_LD(void *, ((uint8_t *)t158));
                                                    gm_t88rcucg1b7uk t160 = gm_l24;
                                                    gm_tkn42u7na3t81 t161 = gm_f_second_class_part_k6i54mk(t159, GM_CONV(gm_csguankko4lmhd, t160));
                                                    gm_l40 = t161;
                                                    uint8_t t162 = (uint8_t)((((uint8_t *)&gm_l40))[4] != 0);
                                                    if (!t162) {
                                                        goto gm_lb30;
                                                    }
                                                    gm_t88rcucg1b7uk t163 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l40));
                                                    gm_l25 = t163;
                                                    void * t164 = gm_l0;
                                                    gm_tl874t2atnlh3 t165 = gm_l3;
                                                    void * t166 = gm_l0;
                                                    void * t167 = GM_LD(void *, ((uint8_t *)t166));
                                                    gm_t88rcucg1b7uk t168 = gm_l24;
                                                    gm_t88rcucg1b7uk t169 = gm_l25;
                                                    gm_b16_8 t170 = gm_f_not_first_class_dve75oi((void *)NULL, cx, NULL, t167, t168, t169);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_b0_1 t171 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t164, t165, t170);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    goto gm_lb29;
                                                }
gm_lb30:;
                                            }
gm_lb29:;
                                            void * t172 = gm_l0;
                                            void * t173 = GM_LD(void *, ((uint8_t *)t172));
                                            uint32_t t174 = gm_l23;
                                            gm_b16_8 t175 = gm_f_param_constraints_2k5fnnl(t173, t174);
                                            gm_l26 = t175;
                                            gm_l27 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t176 = gm_l27;
                                                    uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                                    uint8_t t178 = (t176 < t177);
                                                    if (!t178) {
                                                        goto gm_lb31;
                                                    }
                                                    {
                                                        void * t179 = GM_LD(void *, ((uint8_t *)&gm_l26));
                                                        uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                                        uint64_t t181 = gm_l27;
                                                        {
                                                            {
                                                                gm_t9qkh0rsooc8d t182 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t179) + (uintptr_t)t181 * 1));
                                                                gm_l28 = t182;
                                                                void * t183 = gm_l0;
                                                                void * t184 = GM_LD(void *, ((uint8_t *)t183));
                                                                gm_t88rcucg1b7uk t185 = gm_l24;
                                                                gm_t9qkh0rsooc8d t186 = gm_l28;
                                                                uint8_t t187 = gm_f_satisfies_p0n2er8((void *)NULL, cx, NULL, t184, t185, t186);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                uint8_t t188 = (uint8_t)!t187;
                                                                if (t188) {
                                                                    void * t189 = gm_l0;
                                                                    gm_tl874t2atnlh3 t190 = gm_l3;
                                                                    void * t191 = gm_l0;
                                                                    gm_t88rcucg1b7uk t192 = gm_l24;
                                                                    gm_b16_8 t193 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t191, t192);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_t9qkh0rsooc8d t194 = gm_l28;
                                                                    gm_b16_8 t195 = gm_f_constraint_name_ombut3o(t194);
                                                                    void * t196 = gm_l0;
                                                                    void * t197 = GM_LD(void *, ((uint8_t *)t196));
                                                                    void * t198 = (void *)(((uint8_t *)t197) + 8);
                                                                    uint32_t t199 = gm_l23;
                                                                    gm_b16_8 t200 = gm_f_param_name_rbnop5l(t198, t199);
                                                                    gm_tfeab279c16rr t201;
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t201 + 0), t193);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t201 + 16), t195);
                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t201 + 32), t200);
                                                                    gm_f_format_d2d7bae((void *)&gm_l41, (void *)NULL, cx, NULL, &t201);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    uint8_t t202 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                                                    if (!t202) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 8, gm_srcrorgjlg51brd, 694, 32);
                                                                    gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 8));
                                                                    gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&t203));
                                                                    gm_b0_1 t205 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t189, t190, t204);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                }
                                                                goto gm_lb34;
                                                            }
gm_lb35:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 692, 9);
                                                        }
gm_lb34:;
                                                    }
gm_lb33:;
                                                    uint64_t t206 = gm_l27;
                                                    uint64_t r207; bool o208 = __builtin_add_overflow(t206, ((uint64_t)1ULL), &r207);
                                                    uint64_t t209 = r207;
                                                    gm_l27 = t209;
                                                }
                                            }
gm_lb31:;
                                            goto gm_lb27;
                                        }
gm_lb28:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 689, 5);
                                    }
gm_lb27:;
                                }
gm_lb26:;
                                uint64_t t210 = gm_l21;
                                uint64_t r211; bool o212 = __builtin_add_overflow(t210, ((uint64_t)1ULL), &r211);
                                uint64_t t213 = r211;
                                gm_l21 = t213;
                            }
                        }
gm_lb24:;
                    }
                    {
                        void * t214 = gm_l0;
                        void * t215 = gm_l0;
                        void * t216 = GM_LD(void *, ((uint8_t *)t215));
                        uint32_t t217 = gm_l1;
                        gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
                        gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&t218));
                        gm_tl874t2atnlh3 t220 = gm_l3;
                        uint32_t t221 = gm_f_instantiate_35r5pb2((void *)NULL, cx, NULL, t216, t217, t219, t220);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tl874t2atnlh3 t222 = gm_l3;
                        void * t223 = gm_f_fn_ref_aqs2fl4((void *)NULL, cx, NULL, t214, t221, t222);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t223;
                        goto gm_fret;
                    }
                }
gm_lb2:;
            }
            {
                F.ret = ((void *)0);
                goto gm_fret;
            }
        }
gm_lb1:;
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
__attribute__((aligned(4))) void gm_f_generic_type_args_mj9hh1l(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_generic_type_args_mj9hh1l F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l2; /* binds */
    gm_b16_8 gm_l3; /* values */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    uint8_t gm_l6; /* failed_arg */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    void * gm_l9; /* v */
    void * gm_l10; /* x */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint32_t gm_l14; /* pid */
    gm_t88rcucg1b7uk gm_l15; /* vty */
    gm_t88rcucg1b7uk gm_l16; /* t */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    void * gm_l20; /* d */
    gm_t88rcucg1b7uk gm_l21; /* t */
    GM_STACK_CHECK(&F, 1413u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * gm_l22;
    uint8_t gm_l23;
    gm_tkn42u7na3t81 gm_l24;
    gm_tnnjeerolos0a gm_l25;
    gm_tkn42u7na3t81 gm_l26;
    gm_tkn42u7na3t81 gm_l27;
    gm_t88rcucg1b7uk gm_l28;
    gm_tftb1odc739t5 gm_l29;
    void * gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tkn42u7na3t81 gm_l32;
    gm_t88rcucg1b7uk gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tkn42u7na3t81 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    void * t28 = gm_l0;
    void * t29 = GM_LD(void *, ((uint8_t *)t28));
    void * t30 = (void *)(((uint8_t *)t29) + 8);
    gm_l5 = t30;
    gm_l6 = ((uint8_t)0);
    gm_b16_8 t31 = gm_l3;
    gm_l7 = t31;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t32 = gm_l8;
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t34 = (t32 < t33);
            if (!t34) {
                goto gm_lb1;
            }
            {
                void * t35 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t37 = gm_l8;
                {
                    {
                        void * t38 = GM_LD(void *, (((uint8_t *)t35) + (uintptr_t)t37 * 8));
                        gm_l9 = t38;
                        uint8_t t39 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l9)) != 0);
                        if (t39) {
                            void * t40 = GM_LD(void *, ((uint8_t *)&gm_l9));
                            gm_l22 = t40;
                        } else {
                            goto gm_lb3;
                        }
                        void * t41 = gm_l22;
                        gm_l10 = t41;
                        void * t42 = gm_l5;
                        void * t43 = gm_l0;
                        void * t44 = (void *)(((uint8_t *)t43) + 96);
                        void * t45 = gm_l0;
                        void * t46 = GM_LD(void *, ((uint8_t *)t45));
                        void * t47 = gm_l10;
                        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t47));
                        gm_t88rcucg1b7uk t49 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t44, t46, t48);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t50 = gm_f_mentions_error_37knu7r(t42, GM_CONV(gm_csguankko4lmhd, t49));
                        if (t50) {
                            gm_l6 = ((uint8_t)1);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 362, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t51 = gm_l8;
            uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
            uint64_t t54 = r52;
            gm_l8 = t54;
        }
    }
gm_lb1:;
    gm_trqfvmjtt4sij t55;
    gm_f_list_nof7vfj((void *)&t55, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l11 = t55;
    void * t56 = gm_l1;
    gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)t56) + 80));
    gm_l12 = t57;
    gm_l13 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t58 = gm_l13;
            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
            uint8_t t60 = (t58 < t59);
            if (!t60) {
                goto gm_lb6;
            }
            {
                void * t61 = GM_LD(void *, ((uint8_t *)&gm_l12));
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint64_t t63 = gm_l13;
                {
                    {
                        uint32_t t64 = GM_LD(uint32_t, (((uint8_t *)t61) + (uintptr_t)t63 * 4));
                        gm_l14 = t64;
                        {
                            {
                                uint8_t t65 = gm_l6;
                                if (!t65) {
                                    goto gm_lb12;
                                }
                                gm_b16_8 t66 = gm_l2;
                                uint32_t t67 = gm_l14;
                                gm_tkn42u7na3t81 t68 = gm_f_bound_to_5c11t0h(t66, t67);
                                gm_l24 = t68;
                                {
                                    {
                                        uint8_t t69 = (uint8_t)((((uint8_t *)&gm_l24))[4] != 0);
                                        if (t69) {
                                            goto gm_lb14;
                                        }
                                        gm_l23 = ((uint8_t)1);
                                        goto gm_lb13;
                                    }
gm_lb14:;
                                    {
                                        gm_l23 = ((uint8_t)0);
                                        goto gm_lb13;
                                    }
gm_lb15:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 368, 27);
                                }
gm_lb13:;
                                uint8_t t70 = gm_l23;
                                if (!t70) {
                                    goto gm_lb12;
                                }
                                gm_th2lqbo77pvsc t71;
                                memset(&t71, 0, sizeof t71);
                                memmove(gm_out, ((uint8_t *)&t71), 24);
                                goto gm_fdone;
                            }
gm_lb12:;
                        }
gm_lb11:;
                        {
                            {
                                void * t72 = gm_l0;
                                void * t73 = GM_LD(void *, ((uint8_t *)t72));
                                uint32_t t74 = gm_l14;
                                gm_tnnjeerolos0a t75 = gm_f_param_kind_a0aikmb(t73, t74);
                                gm_l25 = t75;
                                uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                                uint8_t t77 = (uint8_t)(t76 == ((uint8_t)2ULL));
                                if (!t77) {
                                    goto gm_lb17;
                                }
                                gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l25) + 4));
                                gm_l15 = t78;
                                {
                                    {
                                        gm_b16_8 t79 = gm_l2;
                                        uint32_t t80 = gm_l14;
                                        gm_tkn42u7na3t81 t81 = gm_f_bound_to_5c11t0h(t79, t80);
                                        gm_l26 = t81;
                                        uint8_t t82 = (uint8_t)((((uint8_t *)&gm_l26))[4] != 0);
                                        if (!t82) {
                                            goto gm_lb19;
                                        }
                                        gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l26));
                                        gm_l16 = t83;
                                        void * t84 = (void *)((uint8_t *)&F.l11);
                                        void * t85 = gm_l0;
                                        gm_t88rcucg1b7uk t86 = gm_l16;
                                        gm_t88rcucg1b7uk t87 = gm_l15;
                                        void * t88 = gm_l5;
                                        uint32_t t89 = gm_l14;
                                        gm_b16_8 t90 = gm_f_param_name_rbnop5l(t88, t89);
                                        gm_tl874t2atnlh3 t91 = gm_l4;
                                        gm_tkn42u7na3t81 t92 = gm_f_normalize_value_fgo7ofj((void *)NULL, cx, NULL, t85, t86, t87, t90, t91);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l27 = t92;
                                        uint8_t t93 = (uint8_t)((((uint8_t *)&gm_l27))[4] != 0);
                                        if (t93) {
                                            gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l27));
                                            gm_l28 = t94;
                                        } else {
                                            gm_th2lqbo77pvsc t95;
                                            memset(&t95, 0, sizeof t95);
                                            memmove(gm_out, ((uint8_t *)&t95), 24);
                                            goto gm_fdone;
                                        }
                                        gm_t88rcucg1b7uk t96 = gm_l28;
                                        gm_tftb1odc739t5 t97 = gm_f_push_k3l9nic(t84, GM_CONV(gm_csguankko4lmhd, t96));
                                        gm_l29 = t97;
                                        uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                        if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcrorgjlg51brd, 371, 17);
                                        goto gm_lb8;
                                    }
gm_lb19:;
                                }
gm_lb18:;
                                void * t99 = gm_l1;
                                gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)t99) + 40));
                                gm_l17 = t100;
                                gm_l18 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t101 = gm_l18;
                                        uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                        uint8_t t103 = (t101 < t102);
                                        if (!t103) {
                                            goto gm_lb20;
                                        }
                                        {
                                            void * t104 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                            uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                            uint64_t t106 = gm_l18;
                                            {
                                                {
                                                    memmove(((uint8_t *)&F.l19), (((uint8_t *)t104) + (uintptr_t)t106 * 56), 56);
                                                    gm_t9805qtonbsag t107 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l19) + 40));
                                                    uint32_t t108 = gm_l14;
                                                    gm_t9805qtonbsag t109;
                                                    GM_ST(uint8_t, (uint8_t *)&t109 + 5, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t109 + 6, 0);
                                                    GM_ST(uint32_t, ((uint8_t *)&t109), t108);
                                                    ((uint8_t *)&t109)[4] = 1;
                                                    uint8_t t110 = (!gm_eqkricceou4u(((uint8_t *)&t107), ((uint8_t *)&t109)));
                                                    if (t110) {
                                                        goto gm_lb22;
                                                    }
                                                    uint8_t t111 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l19) + 32)) != 0);
                                                    if (t111) {
                                                        void * t112 = GM_LD(void *, (((uint8_t *)&F.l19) + 32));
                                                        gm_l30 = t112;
                                                    } else {
                                                        void * t113 = gm_l0;
                                                        gm_tl874t2atnlh3 t114 = gm_l4;
                                                        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                        gm_tnpifv20irnph t116;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t116 + 0), t115);
                                                        gm_f_format_qv493su((void *)&gm_l31, (void *)NULL, cx, NULL, t116);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                                                        if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srcrorgjlg51brd, 377, 36);
                                                        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                                                        gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
                                                        gm_b0_1 t120 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t113, t114, t119);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_th2lqbo77pvsc t121;
                                                        memset(&t121, 0, sizeof t121);
                                                        memmove(gm_out, ((uint8_t *)&t121), 24);
                                                        goto gm_fdone;
                                                    }
                                                    void * t122 = gm_l30;
                                                    gm_l20 = t122;
                                                    void * t123 = (void *)((uint8_t *)&F.l11);
                                                    void * t124 = gm_l0;
                                                    gm_t88rcucg1b7uk t125 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 16));
                                                    void * t126 = gm_l20;
                                                    gm_tkn42u7na3t81 t127 = gm_f_comptime_arg_vtq6sop((void *)NULL, cx, NULL, t124, t125, t126);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l32 = t127;
                                                    uint8_t t128 = (uint8_t)((((uint8_t *)&gm_l32))[4] != 0);
                                                    if (t128) {
                                                        gm_t88rcucg1b7uk t129 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l32));
                                                        gm_l33 = t129;
                                                    } else {
                                                        gm_th2lqbo77pvsc t130;
                                                        memset(&t130, 0, sizeof t130);
                                                        memmove(gm_out, ((uint8_t *)&t130), 24);
                                                        goto gm_fdone;
                                                    }
                                                    gm_t88rcucg1b7uk t131 = gm_l33;
                                                    gm_tftb1odc739t5 t132 = gm_f_push_k3l9nic(t123, GM_CONV(gm_csguankko4lmhd, t131));
                                                    gm_l34 = t132;
                                                    uint8_t t133 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                                    if (!t133) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_srcrorgjlg51brd, 380, 17);
                                                    goto gm_lb20;
                                                }
gm_lb24:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 374, 13);
                                            }
gm_lb23:;
                                        }
gm_lb22:;
                                        uint64_t t134 = gm_l18;
                                        uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                                        uint64_t t137 = r135;
                                        gm_l18 = t137;
                                    }
                                }
gm_lb20:;
                                goto gm_lb8;
                            }
gm_lb17:;
                        }
gm_lb16:;
                        {
                            {
                                gm_b16_8 t138 = gm_l2;
                                uint32_t t139 = gm_l14;
                                gm_tkn42u7na3t81 t140 = gm_f_bound_to_5c11t0h(t138, t139);
                                gm_l35 = t140;
                                uint8_t t141 = (uint8_t)((((uint8_t *)&gm_l35))[4] != 0);
                                if (!t141) {
                                    goto gm_lb26;
                                }
                                gm_t88rcucg1b7uk t142 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l35));
                                gm_l21 = t142;
                                void * t143 = (void *)((uint8_t *)&F.l11);
                                gm_t88rcucg1b7uk t144 = gm_l21;
                                gm_tftb1odc739t5 t145 = gm_f_push_k3l9nic(t143, GM_CONV(gm_csguankko4lmhd, t144));
                                gm_l36 = t145;
                                uint8_t t146 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                if (!t146) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srcrorgjlg51brd, 386, 13);
                                goto gm_lb8;
                            }
gm_lb26:;
                        }
gm_lb25:;
                        void * t147 = gm_l0;
                        void * t148 = GM_LD(void *, ((uint8_t *)t147));
                        uint32_t t149 = gm_l14;
                        gm_tnnjeerolos0a t150 = gm_f_param_kind_a0aikmb(t148, t149);
                        uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&t150) + 0));
                        uint8_t t152 = (uint8_t)((t151 == ((uint8_t)1ULL)));
                        gm_tnnjeerolos0a t153;
                        GM_ST(uint8_t, (uint8_t *)&t153 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t153 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t153 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t153 + 0), ((uint8_t)1ULL));
                        if (t152) {
                            void * t154 = (void *)((uint8_t *)&F.l11);
                            void * t155 = gm_l5;
                            gm_tnpifv20irnph t156 = gm_f_empty_effects_r0iv3pv();
                            gm_t53at80pfdus3 t157;
                            memset(&t157, 0, sizeof t157);
                            GM_ST(uint8_t, ((uint8_t *)&t157 + 0), ((uint8_t)24ULL));
                            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t157 + 8), t156);
                            gm_t88rcucg1b7uk t158 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t155, &t157);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t159 = gm_f_push_k3l9nic(t154, GM_CONV(gm_csguankko4lmhd, t158));
                            gm_l37 = t159;
                            uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                            if (!t160) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srcrorgjlg51brd, 391, 13);
                            goto gm_lb8;
                        }
                        void * t161 = gm_l0;
                        void * t162 = gm_l1;
                        uint32_t t163 = gm_l14;
                        gm_b16_8 t164 = gm_l3;
                        gm_tl874t2atnlh3 t165 = gm_l4;
                        gm_b0_1 t166 = gm_f_report_cannot_infer_tor1q4u((void *)NULL, cx, NULL, t161, t162, t163, t164, t165);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_th2lqbo77pvsc t167;
                        memset(&t167, 0, sizeof t167);
                        memmove(gm_out, ((uint8_t *)&t167), 24);
                        goto gm_fdone;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 367, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t168 = gm_l13;
            uint64_t r169; bool o170 = __builtin_add_overflow(t168, ((uint64_t)1ULL), &r169);
            uint64_t t171 = r169;
            gm_l13 = t171;
        }
    }
gm_lb6:;
    gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
    gm_th2lqbo77pvsc t173;
    GM_ST(uint8_t, (uint8_t *)&t173 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t173 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t173 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t173), t172);
    ((uint8_t *)&t173)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t173), 24);
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
inline __attribute__((aligned(4))) uint8_t gm_f_given_by_name_pd5ma4t(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_given_by_name_pd5ma4t F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_t08olq97rscn3 gm_l4; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_is_instantiation_vgsqc8f *)((&F.h)->up))->l2));
    gm_l1 = t9;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l2;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t15 = gm_l2;
                {
                    {
                        memmove(((uint8_t *)&F.l3), (((uint8_t *)t13) + (uintptr_t)t15 * 144), 144);
                        {
                            {
                                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                                uint8_t t17 = (uint8_t)(t16 == ((uint8_t)1ULL));
                                if (!t17) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&gm_l4), (((uint8_t *)&F.l3) + 8), 32);
                                gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                                void * t19 = gm_l0;
                                gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)t19));
                                uint8_t t21 = (gm_eq0iqiubfm7e(((uint8_t *)&t18), ((uint8_t *)&t20)));
                                if (!t21) {
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
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 637, 9);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t22 = gm_l2;
            uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
            uint64_t t25 = r23;
            gm_l2 = t25;
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
__attribute__((aligned(4))) uint8_t gm_f_inferable_param_f80lrdi(void * a0, void * a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_inferable_param_f80lrdi F;
    void * gm_l1; /* sig */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tiilmfd16v3fc gm_l6; /* e */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* j */
    GM_STACK_CHECK(&F, 634u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t gm_l13;
    gm_tgsvmddj3e85e gm_l14;
    void * t12 = gm_l1;
    void * t13 = GM_LD(void *, (((uint8_t *)t12) + 40));
    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 40) + 8));
    uint64_t t15 = gm_l2;
    if ((uintptr_t)t15 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t15, t14, gm_srcrorgjlg51brd, 592, 12);
    uint8_t t16 = (uint8_t)((((((uint8_t *)t13) + (uintptr_t)t15 * 56) + 40))[4] != 0);
    if (t16) {
        uint32_t t17 = GM_LD(uint32_t, ((((uint8_t *)t13) + (uintptr_t)t15 * 56) + 40));
        gm_l13 = t17;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint32_t t18 = gm_l13;
    F.l3 = t18;
    void * t19 = F.l0;
    uint32_t t20 = F.l3;
    gm_tnnjeerolos0a t21 = gm_f_param_kind_a0aikmb(t19, t20);
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
    uint8_t t23 = (uint8_t)((t22 == ((uint8_t)1ULL)));
    gm_tnnjeerolos0a t24;
    GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)1ULL));
    if (t23) {
        void * t25 = gm_l1;
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 64));
        gm_l4 = t26;
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
                            memmove(((uint8_t *)&gm_l6), (((uint8_t *)t31) + (uintptr_t)t33 * 24), 24);
                            void * t34 = F.l0;
                            void * t35 = (void *)((uint8_t *)&gm_l6);
                            uint32_t t36 = F.l3;
                            uint8_t t37 = gm_f_effect_args_name_jgklot1(t34, t35, t36);
                            if (t37) {
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 594, 9);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t38 = gm_l5;
                uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
                uint64_t t41 = r39;
                gm_l5 = t41;
            }
        }
gm_lb1:;
    }
    void * t42 = gm_l1;
    gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)t42) + 40));
    gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
    gm_l7 = t44;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t46 = gm_l8;
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t48 = (t46 < t47);
            if (!t48) {
                goto gm_lb6;
            }
            {
                uint64_t t49 = gm_l8;
                void * t50 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t52 = gm_l8;
                gm_t5v0c8ia6a5mo t53 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t50) + (uintptr_t)t52 * 56));
                gm_tgsvmddj3e85e t54;
                GM_ST(uint64_t, ((uint8_t *)&t54 + 0), t49);
                GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t54 + 8), t53);
                gm_l14 = t54;
                {
                    {
                        uint64_t t55 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                        gm_l9 = t55;
                        memmove(((uint8_t *)&F.l10), (((uint8_t *)&gm_l14) + 8), 56);
                        uint64_t t56 = gm_l9;
                        uint64_t t57 = gm_l2;
                        uint8_t t58 = ((t56 == t57));
                        if (t58) {
                            goto gm_lb8;
                        }
                        F.l11 = ((uint32_t)0ULL);
                        F.l12 = ((uint32_t)0ULL);
                        void * t59 = F.l0;
                        gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                        gm_closure t61 = ((gm_closure){(void *)gm_f_fn_qtpj77g, (void *)&F.h, cx});
                        gm_f_walk_v858co7(t59, GM_CONV(gm_csguankko4lmhd, t60), t61);
                        if (gm_unw.kind) {
                            goto gm_ll45;
                        }
                        uint32_t t62 = F.l11;
                        uint32_t t63 = F.l12;
                        uint8_t t64 = (t62 > t63);
                        if (t64) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 598, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t65 = gm_l8;
            uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
            uint64_t t68 = r66;
            gm_l8 = t68;
        }
    }
    goto gm_ls69;
gm_ll45:;
    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb6; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb8; }
    }
    goto gm_lad;
gm_ls69:;
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
__attribute__((aligned(4))) uint8_t gm_f_is_instantiation_vgsqc8f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_is_instantiation_vgsqc8f F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l3; /* positional */
    gm_closure gm_l4; /* given_by_name */
    uint8_t gm_l5; /* needs_runtime */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* k */
    uint32_t gm_l26; /* g */
    uint8_t gm_l27; /* is_value */
    GM_STACK_CHECK(&F, 2005u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l28;
    uint8_t gm_l29;
    uint8_t gm_l30;
    uint8_t gm_l31;
    gm_tgsvmddj3e85e gm_l32;
    gm_tftb1odc739t5 gm_l33;
    uint8_t gm_l34;
    gm_tgsvmddj3e85e gm_l35;
    uint8_t gm_l36;
    gm_tkppg5stlt438 gm_l37;
    uint32_t gm_l38;
    uint8_t gm_l39;
    gm_tnnjeerolos0a gm_l40;
    {
        gm_closure t55 = ((gm_closure){(void *)gm_f_given_by_name_pd5ma4t, (void *)&F.h, cx});
        gm_l4 = t55;
        gm_l5 = ((uint8_t)0);
        void * t56 = gm_l1;
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)t56) + 40));
        gm_l6 = t57;
        gm_l7 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t59 = gm_l7;
                uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint8_t t61 = (t59 < t60);
                if (!t61) {
                    goto gm_lb1;
                }
                {
                    void * t62 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint64_t t64 = gm_l7;
                    {
                        {
                            memmove(((uint8_t *)&F.l8), (((uint8_t *)t62) + (uintptr_t)t64 * 56), 56);
                            {
                                {
                                    {
                                        {
                                            uint8_t t65 = (uint8_t)(((((uint8_t *)&F.l8) + 40))[4] != 0);
                                            if (t65) {
                                                goto gm_lb9;
                                            }
                                            gm_l28 = ((uint8_t)1);
                                            goto gm_lb8;
                                        }
gm_lb9:;
                                        {
                                            gm_l28 = ((uint8_t)0);
                                            goto gm_lb8;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 644, 12);
                                    }
gm_lb8:;
                                    uint8_t t66 = gm_l28;
                                    if (!t66) {
                                        goto gm_lb7;
                                    }
                                    gm_closure t67 = gm_l4;
                                    void * t68 = (void *)((uint8_t *)&F.l8);
                                    gm_closure t69 = t67;
                                    gm_node *ccx70 = t69.cx;
                                    void * t71 = t69.code;
                                    uint8_t t72;
                                    if (GM_PTR_TAGGED(t71)) {
                                        t72 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t71))(t68);
                                    } else {
                                        t72 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t71)(t69.env, ccx70, NULL, t68);
                                    }
                                    if (gm_unw.kind) {
                                        goto gm_ll58;
                                    }
                                    if (!t72) {
                                        goto gm_lb7;
                                    }
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
gm_lb7:;
                            }
gm_lb6:;
                            {
                                {
                                    {
                                        {
                                            uint8_t t73 = (uint8_t)(((((uint8_t *)&F.l8) + 40))[4] != 0);
                                            if (t73) {
                                                goto gm_lb14;
                                            }
                                            gm_l29 = ((uint8_t)1);
                                            goto gm_lb13;
                                        }
gm_lb14:;
                                        {
                                            gm_l29 = ((uint8_t)0);
                                            goto gm_lb13;
                                        }
gm_lb15:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 645, 12);
                                    }
gm_lb13:;
                                    uint8_t t74 = gm_l29;
                                    if (!t74) {
                                        goto gm_lb12;
                                    }
                                    {
                                        {
                                            uint8_t t75 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l8) + 32)) != 0);
                                            if (t75) {
                                                goto gm_lb17;
                                            }
                                            gm_l30 = ((uint8_t)1);
                                            goto gm_lb16;
                                        }
gm_lb17:;
                                        {
                                            gm_l30 = ((uint8_t)0);
                                            goto gm_lb16;
                                        }
gm_lb18:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 645, 34);
                                    }
gm_lb16:;
                                    uint8_t t76 = gm_l30;
                                    if (!t76) {
                                        goto gm_lb12;
                                    }
                                    void * t77 = (void *)((uint8_t *)&F.l8);
                                    uint8_t t78 = gm_f_is_site_8sr7vam(t77);
                                    uint8_t t79 = (uint8_t)!t78;
                                    if (!t79) {
                                        goto gm_lb12;
                                    }
                                    gm_l5 = ((uint8_t)1);
                                    goto gm_lb11;
                                }
gm_lb12:;
                            }
gm_lb11:;
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 643, 5);
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
        goto gm_ls84;
gm_ll58:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb1; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb3; }
        }
        goto gm_lad;
gm_ls84:;
gm_lb1:;
    }
    {
        uint8_t t85 = gm_l5;
        uint8_t t86 = (uint8_t)!t85;
        if (t86) {
            gm_l31 = ((uint8_t)1);
        } else {
            uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
            uint8_t t88 = ((t87 == ((uint64_t)0ULL)));
            gm_l31 = t88;
        }
        uint8_t t89 = gm_l31;
        if (t89) {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_trqfvmjtt4sij t90;
        gm_f_list_5dhgg4i((void *)&t90, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l9 = t90;
        void * t91 = gm_l1;
        gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)t91) + 40));
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
        gm_l10 = t93;
        gm_l11 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t95 = gm_l11;
                uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint8_t t97 = (t95 < t96);
                if (!t97) {
                    goto gm_lb19;
                }
                {
                    uint64_t t98 = gm_l11;
                    void * t99 = GM_LD(void *, ((uint8_t *)&gm_l10));
                    uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint64_t t101 = gm_l11;
                    gm_t5v0c8ia6a5mo t102 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t99) + (uintptr_t)t101 * 56));
                    gm_tgsvmddj3e85e t103;
                    GM_ST(uint64_t, ((uint8_t *)&t103 + 0), t98);
                    GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t103 + 8), t102);
                    gm_l32 = t103;
                    {
                        {
                            uint64_t t104 = GM_LD(uint64_t, ((uint8_t *)&gm_l32));
                            gm_l12 = t104;
                            memmove(((uint8_t *)&F.l13), (((uint8_t *)&gm_l32) + 8), 56);
                            {
                                {
                                    void * t105 = gm_l0;
                                    void * t106 = GM_LD(void *, ((uint8_t *)t105));
                                    void * t107 = gm_l1;
                                    uint64_t t108 = gm_l12;
                                    uint8_t t109 = gm_f_inferable_param_f80lrdi(t106, t107, t108);
                                    uint8_t t110 = (uint8_t)!t109;
                                    if (!t110) {
                                        goto gm_lb25;
                                    }
                                    gm_closure t111 = gm_l4;
                                    void * t112 = (void *)((uint8_t *)&F.l13);
                                    gm_closure t113 = t111;
                                    gm_node *ccx114 = t113.cx;
                                    void * t115 = t113.code;
                                    uint8_t t116;
                                    if (GM_PTR_TAGGED(t115)) {
                                        t116 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t115))(t112);
                                    } else {
                                        t116 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t115)(t113.env, ccx114, NULL, t112);
                                    }
                                    if (gm_unw.kind) {
                                        goto gm_ll94;
                                    }
                                    uint8_t t117 = (uint8_t)!t116;
                                    if (!t117) {
                                        goto gm_lb25;
                                    }
                                    void * t118 = (void *)((uint8_t *)&F.l13);
                                    uint8_t t119 = gm_f_is_site_8sr7vam(t118);
                                    uint8_t t120 = (uint8_t)!t119;
                                    if (!t120) {
                                        goto gm_lb25;
                                    }
                                    void * t121 = (void *)((uint8_t *)&F.l9);
                                    uint64_t t122 = gm_l12;
                                    gm_tftb1odc739t5 t123 = gm_f_push_ucgmlo2(t121, t122);
                                    gm_l33 = t123;
                                    uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                    if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcrorgjlg51brd, 652, 99);
                                    goto gm_lb24;
                                }
gm_lb25:;
                            }
gm_lb24:;
                            goto gm_lb22;
                        }
gm_lb23:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 651, 5);
                    }
gm_lb22:;
                }
gm_lb21:;
                uint64_t t125 = gm_l11;
                uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                uint64_t t128 = r126;
                gm_l11 = t128;
            }
        }
        goto gm_ls129;
gm_ll94:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb19; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb21; }
        }
        goto gm_lad;
gm_ls129:;
gm_lb19:;
    }
    {
        uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
        uint8_t t132 = (t130 > t131);
        if (t132) {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        void * t133 = GM_LD(void *, ((uint8_t *)&F.l9));
        uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
        uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint64_t t136 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t137 = (uintptr_t)t135;
        if (t136 > t137 || t137 > (uintptr_t)t134) gm_trap_slice(t136, t137, t134, gm_srcrorgjlg51brd, 655, 14);
        uint64_t t138 = ((uintptr_t)t135) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t139;
        GM_ST(void *, (uint8_t *)&t139, (void *)(((uint8_t *)t133 + (uintptr_t)((uint64_t)0ULL) * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(t138));
        gm_l14 = t139;
        gm_l15 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t141 = gm_l15;
                uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t143 = (t141 < t142);
                if (!t143) {
                    goto gm_lb26;
                }
                {
                    void * t144 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint64_t t146 = gm_l15;
                    {
                        {
                            uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)t144) + (uintptr_t)t146 * 8));
                            gm_l16 = t147;
                            void * t148 = gm_l1;
                            void * t149 = GM_LD(void *, (((uint8_t *)t148) + 40));
                            uint64_t t150 = GM_LD(uint64_t, ((((uint8_t *)t148) + 40) + 8));
                            uint64_t t151 = gm_l16;
                            if ((uintptr_t)t151 >= (uintptr_t)(t150)) gm_trap_index((uintptr_t)t151, t150, gm_srcrorgjlg51brd, 656, 12);
                            {
                                {
                                    uint8_t t152 = (uint8_t)((((((uint8_t *)t149) + (uintptr_t)t151 * 56) + 40))[4] != 0);
                                    if (t152) {
                                        goto gm_lb32;
                                    }
                                    gm_l34 = ((uint8_t)1);
                                    goto gm_lb31;
                                }
gm_lb32:;
                                {
                                    gm_l34 = ((uint8_t)0);
                                    goto gm_lb31;
                                }
gm_lb33:;
                                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 656, 12);
                            }
gm_lb31:;
                            uint8_t t153 = gm_l34;
                            if (t153) {
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto gm_lb29;
                        }
gm_lb30:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 655, 5);
                    }
gm_lb29:;
                }
gm_lb28:;
                uint64_t t154 = gm_l15;
                uint64_t r155; bool o156 = __builtin_add_overflow(t154, ((uint64_t)1ULL), &r155);
                uint64_t t157 = r155;
                gm_l15 = t157;
            }
        }
gm_lb26:;
        void * t158 = gm_l1;
        gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)t158) + 40));
        gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
        gm_l17 = t160;
        gm_l18 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t162 = gm_l18;
                uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint8_t t164 = (t162 < t163);
                if (!t164) {
                    goto gm_lb34;
                }
                {
                    uint64_t t165 = gm_l18;
                    void * t166 = GM_LD(void *, ((uint8_t *)&gm_l17));
                    uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint64_t t168 = gm_l18;
                    gm_t5v0c8ia6a5mo t169 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t166) + (uintptr_t)t168 * 56));
                    gm_tgsvmddj3e85e t170;
                    GM_ST(uint64_t, ((uint8_t *)&t170 + 0), t165);
                    GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t170 + 8), t169);
                    gm_l35 = t170;
                    {
                        {
                            uint64_t t171 = GM_LD(uint64_t, ((uint8_t *)&gm_l35));
                            gm_l19 = t171;
                            memmove(((uint8_t *)&F.l20), (((uint8_t *)&gm_l35) + 8), 56);
                            {
                                {
                                    {
                                        {
                                            uint8_t t172 = (uint8_t)(((((uint8_t *)&F.l20) + 40))[4] != 0);
                                            if (t172) {
                                                goto gm_lb42;
                                            }
                                            gm_l36 = ((uint8_t)0);
                                            goto gm_lb41;
                                        }
gm_lb42:;
                                        {
                                            gm_l36 = ((uint8_t)1);
                                            goto gm_lb41;
                                        }
gm_lb43:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 659, 12);
                                    }
gm_lb41:;
                                    uint8_t t173 = gm_l36;
                                    if (!t173) {
                                        goto gm_lb40;
                                    }
                                    gm_closure t174 = gm_l4;
                                    void * t175 = (void *)((uint8_t *)&F.l20);
                                    gm_closure t176 = t174;
                                    gm_node *ccx177 = t176.cx;
                                    void * t178 = t176.code;
                                    uint8_t t179;
                                    if (GM_PTR_TAGGED(t178)) {
                                        t179 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t178))(t175);
                                    } else {
                                        t179 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t178)(t176.env, ccx177, NULL, t175);
                                    }
                                    if (gm_unw.kind) {
                                        goto gm_ll161;
                                    }
                                    uint8_t t180 = (uint8_t)!t179;
                                    if (!t180) {
                                        goto gm_lb40;
                                    }
                                    uint64_t t181 = gm_l19;
                                    void * t182 = GM_LD(void *, ((uint8_t *)&F.l9));
                                    uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                    uint64_t t184 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                    uint64_t t185 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t186 = (uintptr_t)t184;
                                    if (t185 > t186 || t186 > (uintptr_t)t183) gm_trap_slice(t185, t186, t183, gm_srcrorgjlg51brd, 659, 73);
                                    uint64_t t187 = ((uintptr_t)t184) - (uintptr_t)((uint64_t)0ULL);
                                    gm_b16_8 t188;
                                    GM_ST(void *, (uint8_t *)&t188, (void *)(((uint8_t *)t182 + (uintptr_t)((uint64_t)0ULL) * 8)));
                                    GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(t187));
                                    uint8_t t189 = 0;
                                    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t188) + 8)); i++) if (gm_eq6groq37ram(((uint8_t *)GM_LD(void *, ((uint8_t *)&t188))) + i * 8, ((uint8_t *)&t181))) { t189 = 1; break; }
                                    uint8_t t190 = (uint8_t)(!t189);
                                    if (!t190) {
                                        goto gm_lb40;
                                    }
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
gm_lb40:;
                            }
gm_lb39:;
                            goto gm_lb37;
                        }
gm_lb38:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 658, 5);
                    }
gm_lb37:;
                }
gm_lb36:;
                uint64_t t191 = gm_l18;
                uint64_t r192; bool o193 = __builtin_add_overflow(t191, ((uint64_t)1ULL), &r192);
                uint64_t t194 = r192;
                gm_l18 = t194;
            }
        }
        goto gm_ls195;
gm_ll161:;
        if (gm_unw.frame == &F.h && gm_unw.label == 3) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb34; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb36; }
        }
        goto gm_lad;
gm_ls195:;
gm_lb34:;
    }
    {
        gm_b16_8 t196 = gm_l3;
        gm_l21 = t196;
        gm_l22 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t198 = gm_l22;
                uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                uint8_t t200 = (t198 < t199);
                if (!t200) {
                    goto gm_lb44;
                }
                {
                    uint64_t t201 = gm_l22;
                    void * t202 = GM_LD(void *, ((uint8_t *)&gm_l21));
                    uint64_t t203 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                    uint64_t t204 = gm_l22;
                    gm_tc3oht4ksth9d t205 = GM_LD(gm_tc3oht4ksth9d, (((uint8_t *)t202) + (uintptr_t)t204 * 88));
                    gm_tkppg5stlt438 t206;
                    GM_ST(uint64_t, ((uint8_t *)&t206 + 0), t201);
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t206 + 8), t205);
                    gm_l37 = t206;
                    {
                        {
                            uint64_t t207 = GM_LD(uint64_t, ((uint8_t *)&gm_l37));
                            gm_l23 = t207;
                            memmove(((uint8_t *)&F.l24), (((uint8_t *)&gm_l37) + 8), 88);
                            void * t208 = gm_l1;
                            void * t209 = GM_LD(void *, (((uint8_t *)t208) + 40));
                            uint64_t t210 = GM_LD(uint64_t, ((((uint8_t *)t208) + 40) + 8));
                            void * t211 = GM_LD(void *, ((uint8_t *)&F.l9));
                            uint64_t t212 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                            uint64_t t213 = gm_l23;
                            if ((uintptr_t)t213 >= (uintptr_t)(t212)) gm_trap_index((uintptr_t)t213, t212, gm_srcrorgjlg51brd, 662, 25);
                            uint64_t t214 = GM_LD(uint64_t, (((uint8_t *)t211) + (uintptr_t)t213 * 8));
                            if ((uintptr_t)t214 >= (uintptr_t)(t210)) gm_trap_index((uintptr_t)t214, t210, gm_srcrorgjlg51brd, 662, 14);
                            memmove(((uint8_t *)&F.l25), (((uint8_t *)t209) + (uintptr_t)t214 * 56), 56);
                            uint8_t t215 = (uint8_t)(((((uint8_t *)&F.l25) + 40))[4] != 0);
                            if (t215) {
                                uint32_t t216 = GM_LD(uint32_t, (((uint8_t *)&F.l25) + 40));
                                gm_l38 = t216;
                            } else {
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            uint32_t t217 = gm_l38;
                            gm_l26 = t217;
                            void * t218 = gm_l0;
                            void * t219 = GM_LD(void *, ((uint8_t *)t218));
                            uint32_t t220 = gm_l26;
                            gm_tnnjeerolos0a t221 = gm_f_param_kind_a0aikmb(t219, t220);
                            gm_l40 = t221;
                            {
                                {
                                    uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                                    uint8_t t223 = (uint8_t)(t222 == ((uint8_t)2ULL));
                                    if (!t223) {
                                        goto gm_lb50;
                                    }
                                    gm_l39 = ((uint8_t)1);
                                    goto gm_lb49;
                                }
gm_lb50:;
                                {
                                    gm_l39 = ((uint8_t)0);
                                    goto gm_lb49;
                                }
gm_lb51:;
                                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 664, 21);
                            }
gm_lb49:;
                            uint8_t t224 = gm_l39;
                            gm_l27 = t224;
                            {
                                {
                                    uint8_t t225 = gm_l27;
                                    uint8_t t226 = (uint8_t)!t225;
                                    if (!t226) {
                                        goto gm_lb53;
                                    }
                                    void * t227 = gm_l0;
                                    void * t228 = (void *)((uint8_t *)&F.l24);
                                    uint8_t t229 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t227, t228);
                                    if (gm_unw.kind) {
                                        goto gm_ll197;
                                    }
                                    uint8_t t230 = (uint8_t)!t229;
                                    if (!t230) {
                                        goto gm_lb53;
                                    }
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
gm_lb53:;
                            }
gm_lb52:;
                            goto gm_lb47;
                        }
gm_lb48:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 661, 5);
                    }
gm_lb47:;
                }
gm_lb46:;
                uint64_t t231 = gm_l22;
                uint64_t r232; bool o233 = __builtin_add_overflow(t231, ((uint64_t)1ULL), &r232);
                uint64_t t234 = r232;
                gm_l22 = t234;
            }
        }
        goto gm_ls235;
gm_ll197:;
        if (gm_unw.frame == &F.h && gm_unw.label == 4) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb44; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb46; }
        }
        goto gm_lad;
gm_ls235:;
gm_lb44:;
    }
    {
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_location_function_thau9ff(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_location_function_thau9ff F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* last */
    uint32_t gm_l2; /* f */
    uint64_t gm_l3; /* k */
    uint32_t gm_l4; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 423u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t gm_l5;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(0));
    gm_l1 = t7;
    {
        {
            void * t8 = gm_l0;
            uint8_t t9 = (uint8_t)(((((uint8_t *)t8) + 400))[4] != 0);
            if (!t9) {
                goto gm_lb2;
            }
            uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t8) + 400));
            gm_l2 = t10;
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, ((uint8_t *)t11));
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 344));
            uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 344) + 8));
            uint32_t t15 = gm_l2;
            uint64_t t16 = (uint64_t)t15;
            if ((uintptr_t)t16 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t16, t14, gm_srcrorgjlg51brd, 731, 16);
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t13) + (uintptr_t)t16 * 136));
            gm_l1 = t17;
            gm_b16_8 t18 = gm_l1;
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1f440st4mnkp));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            uint8_t t20 = gm_f_starts_with_6vvt01e(t18, t19);
            uint8_t t21 = (uint8_t)!t20;
            if (t21) {
                gm_b16_8 t22 = gm_l1;
                F.ret = t22;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t23 = gm_l0;
    uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)t23) + 576) + 8));
    gm_l3 = t24;
    {
        for (;;) {
            uint64_t t25 = gm_l3;
            uint8_t t26 = (t25 > ((uint64_t)0ULL));
            if (!t26) {
                goto gm_lb3;
            }
            {
                uint64_t t27 = gm_l3;
                uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                if (o29) gm_trap("integer overflow in `-`", gm_srcrorgjlg51brd, 736, 9);
                uint64_t t30 = r28;
                gm_l3 = t30;
                void * t31 = gm_l0;
                void * t32 = GM_LD(void *, (((uint8_t *)t31) + 576));
                uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t31) + 576) + 8));
                uint64_t t34 = gm_l3;
                if ((uintptr_t)t34 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t34, t33, gm_srcrorgjlg51brd, 737, 14);
                uint8_t t35 = (uint8_t)((((((uint8_t *)t32) + (uintptr_t)t34 * 384) + 160))[4] != 0);
                if (t35) {
                    uint32_t t36 = GM_LD(uint32_t, ((((uint8_t *)t32) + (uintptr_t)t34 * 384) + 160));
                    gm_l5 = t36;
                } else {
                    goto gm_lb5;
                }
                uint32_t t37 = gm_l5;
                gm_l4 = t37;
                void * t38 = gm_l0;
                void * t39 = GM_LD(void *, ((uint8_t *)t38));
                void * t40 = GM_LD(void *, (((uint8_t *)t39) + 344));
                uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 344) + 8));
                uint32_t t42 = gm_l4;
                uint64_t t43 = (uint64_t)t42;
                if ((uintptr_t)t43 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t43, t41, gm_srcrorgjlg51brd, 738, 16);
                gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)t40) + (uintptr_t)t43 * 136));
                gm_l1 = t44;
                gm_b16_8 t45 = gm_l1;
                gm_b16_8 t46;
                GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s1f440st4mnkp));
                GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(1));
                uint8_t t47 = gm_f_starts_with_6vvt01e(t45, t46);
                uint8_t t48 = (uint8_t)!t47;
                if (t48) {
                    gm_b16_8 t49 = gm_l1;
                    F.ret = t49;
                    goto gm_fret;
                }
            }
gm_lb5:;
        }
    }
gm_lb3:;
    gm_b16_8 t50 = gm_l1;
    F.ret = t50;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_no_inference_from_fn_klvrs5n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_no_inference_from_fn_klvrs5n F;
    void * gm_l0; /* cx */
    void * gm_l1; /* arg */
    void * gm_l2; /* v */
    gm_b16_8 gm_l4; /* name */
    gm_b16_8 gm_l6; /* elems */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 1016u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l10;
    gm_b16_8 gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tabb710d9mq2e gm_l13;
    uint8_t gm_l14;
    void * t14 = gm_l2;
    {
        {
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)26ULL));
            if (!t16) {
                goto gm_lb2;
            }
            void * t17 = gm_l0;
            void * t18 = gm_l2;
            gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t18) + 72));
            gm_f_fn_name_at_5o2j1h9((void *)&gm_l10, (void *)NULL, cx, NULL, t17, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
            if (t20) {
                gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
                gm_l11 = t21;
            } else {
                void * t22 = gm_l2;
                gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t22) + 72));
                gm_b16_8 t24;
                GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s6kdis17b15fm));
                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(113));
                gm_t4gut2c9fj1kt t25;
                gm_f_error_s6hn871((void *)&t25, (void *)NULL, cx, NULL, t23, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l3 = t25;
                void * t26 = (void *)((uint8_t *)&F.l3);
                gm_b16_8 t27;
                GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sseb1k7dlt7ge));
                GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(66));
                gm_f_add_note_46v502s(t26, t27);
                void * t28 = gm_l0;
                void * t29 = GM_LD(void *, ((uint8_t *)t28));
                gm_b0_1 t30 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t29, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l3)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            gm_b16_8 t31 = gm_l11;
            gm_l4 = t31;
            void * t32 = gm_l2;
            gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 72));
            gm_b16_8 t34 = gm_l4;
            gm_tnpifv20irnph t35;
            GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t34);
            gm_f_format_ldnd7h1((void *)&gm_l12, (void *)NULL, cx, NULL, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcrorgjlg51brd, 273, 37);
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
            gm_t4gut2c9fj1kt t39;
            gm_f_error_s6hn871((void *)&t39, (void *)NULL, cx, NULL, t33, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l5 = t39;
            void * t40 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t41 = gm_l4;
            gm_b16_8 t42 = gm_l4;
            gm_t6j6smj7qoctu t43;
            GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t41);
            GM_ST(gm_b16_8, ((uint8_t *)&t43 + 16), t42);
            gm_f_format_m4otrtu((void *)&gm_l13, (void *)NULL, cx, NULL, &t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcrorgjlg51brd, 274, 24);
            gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
            gm_f_add_note_46v502s(t40, t46);
            void * t47 = gm_l0;
            void * t48 = GM_LD(void *, ((uint8_t *)t47));
            gm_b0_1 t49 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t48, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)35ULL));
            if (!t51) {
                goto gm_lb3;
            }
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((((uint8_t *)t14) + 8) + 8));
            gm_l6 = t52;
            void * t53 = gm_l1;
            {
                {
                    uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t53) + 8) + 0));
                    uint8_t t55 = (uint8_t)(t54 == ((uint8_t)10ULL));
                    if (!t55) {
                        goto gm_lb5;
                    }
                    gm_l14 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    gm_l14 = ((uint8_t)0);
                    goto gm_lb4;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 277, 33);
            }
gm_lb4:;
            uint8_t t56 = gm_l14;
            if (t56) {
                gm_b16_8 t57 = gm_l6;
                gm_l7 = t57;
                gm_l8 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t58 = gm_l8;
                        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        uint8_t t60 = (t58 < t59);
                        if (!t60) {
                            goto gm_lb7;
                        }
                        {
                            void * t61 = GM_LD(void *, ((uint8_t *)&gm_l7));
                            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            uint64_t t63 = gm_l8;
                            {
                                {
                                    memmove(((uint8_t *)&F.l9), (((uint8_t *)t61) + (uintptr_t)t63 * 88), 88);
                                    void * t64 = gm_l0;
                                    void * t65 = gm_l1;
                                    void * t66 = (void *)((uint8_t *)&F.l9);
                                    gm_b0_1 t67 = gm_f_no_inference_from_fn_klvrs5n((void *)NULL, cx, NULL, t64, t65, t66);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb10;
                                }
gm_lb11:;
                                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 278, 13);
                            }
gm_lb10:;
                        }
gm_lb9:;
                        uint64_t t68 = gm_l8;
                        uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                        uint64_t t71 = r69;
                        gm_l8 = t71;
                    }
                }
gm_lb7:;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            goto gm_lb1;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 265, 5);
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
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_normalize_value_fgo7ofj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_normalize_value_fgo7ofj F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_b16_8 gm_l3; /* name */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t428ue2u3hf5g gm_l6; /* k */
    gm_t428ue2u3hf5g gm_l7; /* x */
    uint32_t gm_l8; /* p */
    gm_t88rcucg1b7uk gm_l9; /* t */
    GM_STACK_CHECK(&F, 1356u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t53at80pfdus3 gm_l11;
    uint8_t gm_l12;
    gm_tnnjeerolos0a gm_l13;
    gm_tabb710d9mq2e gm_l14;
    uint8_t gm_l15;
    gm_t53at80pfdus3 gm_l16;
    gm_tabb710d9mq2e gm_l17;
    uint8_t gm_l18;
    uint8_t gm_l19;
    gm_thrdpodrs8pgt gm_l20;
    gm_tbci5ospk5nnn gm_l21;
    gm_tabb710d9mq2e gm_l22;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    void * t17 = (void *)(((uint8_t *)t16) + 8);
    gm_l5 = t17;
    gm_t428ue2u3hf5g t18;
    GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t18 + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&t18 + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)2ULL));
    gm_l6 = t18;
    void * t19 = gm_l5;
    gm_t88rcucg1b7uk t20 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l11, t19, GM_CONV(gm_csguankko4lmhd, t20));
    {
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)21ULL));
            if (!t22) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l7), (((uint8_t *)&gm_l11) + 8), 32);
            memmove(((uint8_t *)&gm_l6), ((uint8_t *)&gm_l7), 32);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)19ULL));
            if (!t24) {
                goto gm_lb3;
            }
            uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 8));
            gm_l8 = t25;
            void * t26 = gm_l0;
            void * t27 = GM_LD(void *, ((uint8_t *)t26));
            uint32_t t28 = gm_l8;
            gm_tnnjeerolos0a t29 = gm_f_param_kind_a0aikmb(t27, t28);
            gm_l13 = t29;
            {
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)2ULL));
                    if (!t31) {
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
                gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 544, 25);
            }
gm_lb4:;
            uint8_t t32 = gm_l12;
            if (t32) {
                gm_t88rcucg1b7uk t33 = gm_l1;
                gm_tkn42u7na3t81 t34;
                GM_ST(uint8_t, (uint8_t *)&t34 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t34 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t34), t33);
                ((uint8_t *)&t34)[4] = 1;
                F.ret = t34;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)22ULL));
            if (!t36) {
                goto gm_lb7;
            }
            gm_t88rcucg1b7uk t37 = gm_l2;
            void * t38 = gm_l5;
            gm_t88rcucg1b7uk t39 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t40 = (gm_eq9m6mvtmgde(((uint8_t *)&t37), ((uint8_t *)&t39)));
            if (t40) {
                gm_t88rcucg1b7uk t41 = gm_l1;
                gm_tkn42u7na3t81 t42;
                GM_ST(uint8_t, (uint8_t *)&t42 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t42 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t42), t41);
                ((uint8_t *)&t42)[4] = 1;
                F.ret = t42;
                goto gm_fret;
            }
            void * t43 = gm_l0;
            gm_tl874t2atnlh3 t44 = gm_l4;
            gm_b16_8 t45 = gm_l3;
            void * t46 = gm_l0;
            gm_t88rcucg1b7uk t47 = gm_l1;
            gm_b16_8 t48 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t49 = gm_l3;
            void * t50 = gm_l0;
            gm_t88rcucg1b7uk t51 = gm_l2;
            gm_b16_8 t52 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t0uuhaljsf410 t53;
            GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t45);
            GM_ST(gm_b16_8, ((uint8_t *)&t53 + 16), t48);
            GM_ST(gm_b16_8, ((uint8_t *)&t53 + 32), t49);
            GM_ST(gm_b16_8, ((uint8_t *)&t53 + 48), t52);
            gm_f_format_kj30rlk((void *)&gm_l14, (void *)NULL, cx, NULL, &t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcrorgjlg51brd, 548, 28);
            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
            gm_b0_1 t57 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t43, t44, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t58;
            memset(&t58, 0, sizeof t58);
            F.ret = t58;
            goto gm_fret;
        }
gm_lb7:;
        {
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 542, 5);
    }
gm_lb1:;
    void * t59 = gm_l5;
    gm_t88rcucg1b7uk t60 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l16, t59, GM_CONV(gm_csguankko4lmhd, t60));
    {
        {
            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
            uint8_t t62 = (uint8_t)(t61 == ((uint8_t)21ULL));
            if (!t62) {
                goto gm_lb10;
            }
            gm_l15 = ((uint8_t)0);
            goto gm_lb9;
        }
gm_lb10:;
        {
            gm_l15 = ((uint8_t)1);
            goto gm_lb9;
        }
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 553, 8);
    }
gm_lb9:;
    uint8_t t63 = gm_l15;
    if (t63) {
        void * t64 = gm_l0;
        gm_tl874t2atnlh3 t65 = gm_l4;
        gm_b16_8 t66 = gm_l3;
        gm_tnpifv20irnph t67;
        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t66);
        gm_f_format_3ifsvek((void *)&gm_l17, (void *)NULL, cx, NULL, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
        if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcrorgjlg51brd, 554, 24);
        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
        gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
        gm_b0_1 t71 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t65, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkn42u7na3t81 t72;
        memset(&t72, 0, sizeof t72);
        F.ret = t72;
        goto gm_fret;
    }
    {
        {
            uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t74 = (uint8_t)(t73 == ((uint8_t)0ULL));
            if (!t74) {
                goto gm_lb13;
            }
            gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
            gm_l9 = t75;
            gm_t88rcucg1b7uk t76 = gm_l9;
            gm_t88rcucg1b7uk t77 = gm_l2;
            uint8_t t78 = (gm_eq9m6mvtmgde(((uint8_t *)&t76), ((uint8_t *)&t77)));
            if (t78) {
                gm_l19 = ((uint8_t)1);
            } else {
                void * t79 = gm_l5;
                gm_t88rcucg1b7uk t80 = gm_l9;
                uint8_t t81 = gm_f_is_integer_8d4f9ek(t79, GM_CONV(gm_csguankko4lmhd, t80));
                uint8_t t82 = (uint8_t)!t81;
                gm_l19 = t82;
            }
            uint8_t t83 = gm_l19;
            if (t83) {
                gm_l18 = ((uint8_t)1);
            } else {
                void * t84 = gm_l5;
                gm_t88rcucg1b7uk t85 = gm_l2;
                uint8_t t86 = gm_f_is_integer_8d4f9ek(t84, GM_CONV(gm_csguankko4lmhd, t85));
                uint8_t t87 = (uint8_t)!t86;
                gm_l18 = t87;
            }
            uint8_t t88 = gm_l18;
            if (t88) {
                gm_t88rcucg1b7uk t89 = gm_l1;
                gm_tkn42u7na3t81 t90;
                GM_ST(uint8_t, (uint8_t *)&t90 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t90 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t90), t89);
                ((uint8_t *)&t90)[4] = 1;
                F.ret = t90;
                goto gm_fret;
            }
            void * t91 = gm_l0;
            void * t92 = GM_LD(void *, ((uint8_t *)t91));
            gm_t428ue2u3hf5g t93 = GM_LD(gm_t428ue2u3hf5g, ((uint8_t *)&gm_l6));
            gm_f_key_int_pqscuvn((void *)&gm_l20, (void *)NULL, cx, NULL, t92, &t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t94 = (uint8_t)((((uint8_t *)&gm_l20))[48] != 0);
            if (t94) {
                memmove(((uint8_t *)&gm_l21), ((uint8_t *)&gm_l20), 48);
            } else {
                gm_tkn42u7na3t81 t95;
                memset(&t95, 0, sizeof t95);
                F.ret = t95;
                goto gm_fret;
            }
            memmove(((uint8_t *)&F.l10), ((uint8_t *)&gm_l21), 48);
            void * t96 = gm_l0;
            void * t97 = GM_LD(void *, ((uint8_t *)t96));
            gm_t88rcucg1b7uk t98 = gm_l2;
            uint8_t t99 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t97, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)), t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t100 = (uint8_t)!t99;
            if (t100) {
                void * t101 = gm_l0;
                gm_tl874t2atnlh3 t102 = gm_l4;
                gm_b16_8 t103 = gm_l3;
                gm_b16_8 t104 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t105 = gm_l0;
                gm_t88rcucg1b7uk t106 = gm_l2;
                gm_b16_8 t107 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t105, t106);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tfeab279c16rr t108;
                GM_ST(gm_b16_8, ((uint8_t *)&t108 + 0), t103);
                GM_ST(gm_b16_8, ((uint8_t *)&t108 + 16), t104);
                GM_ST(gm_b16_8, ((uint8_t *)&t108 + 32), t107);
                gm_f_format_r57fu0b((void *)&gm_l22, (void *)NULL, cx, NULL, &t108);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srcrorgjlg51brd, 561, 24);
                gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                gm_b0_1 t112 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t101, t102, t111);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t113;
                memset(&t113, 0, sizeof t113);
                F.ret = t113;
                goto gm_fret;
            }
            void * t114 = gm_l5;
            void * t115 = gm_l0;
            void * t116 = GM_LD(void *, ((uint8_t *)t115));
            gm_t88rcucg1b7uk t117 = gm_l2;
            gm_t428ue2u3hf5g t118;
            gm_f_int_key_dmcraup((void *)&t118, (void *)NULL, cx, NULL, t116, t117, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l10)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t53at80pfdus3 t119;
            GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t119 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t119 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t119 + 0), ((uint8_t)21ULL));
            GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t119 + 8), t118);
            gm_t88rcucg1b7uk t120 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t114, &t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t121;
            GM_ST(uint8_t, (uint8_t *)&t121 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t121 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t121), t120);
            ((uint8_t *)&t121)[4] = 1;
            F.ret = t121;
            goto gm_fret;
        }
gm_lb13:;
        gm_t88rcucg1b7uk t122 = gm_l1;
        gm_tkn42u7na3t81 t123;
        GM_ST(uint8_t, (uint8_t *)&t123 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t123 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t123), t122);
        ((uint8_t *)&t123)[4] = 1;
        F.ret = t123;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_order_generic_args_m8gcs0c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_order_generic_args_m8gcs0c F;
    void * gm_l0; /* cx */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* slot */
    gm_b16_8 gm_l3; /* written */
    void * gm_l4; /* iter_pre */
    uint8_t gm_l7; /* reordered */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* k */
    gm_b16_8 gm_l11; /* order */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    gm_tf5srh4mp4b3i gm_l15; /* found */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* n */
    gm_tf1bjg81u0glv gm_l19; /* ip */
    uint64_t gm_l20; /* n */
    void * gm_l21; /* v */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    void * gm_l25; /* v */
    GM_STACK_CHECK(&F, 1465u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    F.l5 = a5;
    gm_td38of7a349nj gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tq7s98ihonkju gm_l28;
    gm_tq7s98ihonkju gm_l29;
    gm_trqfvmjtt4sij t24;
    gm_f_list_ir0h3tc_2((void *)&t24, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t24;
    gm_l7 = ((uint8_t)0);
    gm_l9 = ((uint64_t)1ULL);
    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t26 = (t25 > ((uint64_t)1ULL) ? t25 : ((uint64_t)1ULL));
    gm_l8 = t26;
    {
        for (;;) {
            uint64_t t28 = gm_l9;
            uint64_t t29 = gm_l8;
            uint8_t t30 = (t28 < t29);
            if (!t30) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t31 = gm_l9;
                        gm_l10 = t31;
                        void * t32 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t34 = gm_l10;
                        uint64_t r35; bool o36 = __builtin_sub_overflow(t34, ((uint64_t)1ULL), &r35);
                        if (o36) gm_trap("integer overflow in `-`", gm_srcrorgjlg51brd, 483, 20);
                        uint64_t t37 = r35;
                        if ((uintptr_t)t37 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t37, t33, gm_srcrorgjlg51brd, 483, 12);
                        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t32) + (uintptr_t)t37 * 8));
                        void * t39 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t41 = gm_l10;
                        if ((uintptr_t)t41 >= (uintptr_t)(t40)) gm_trap_index((uintptr_t)t41, t40, gm_srcrorgjlg51brd, 483, 29);
                        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t39) + (uintptr_t)t41 * 8));
                        uint8_t t43 = (t38 > t42);
                        if (t43) {
                            gm_l7 = ((uint8_t)1);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 482, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t44 = gm_l9;
            uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
            uint64_t t47 = r45;
            gm_l9 = t47;
        }
    }
gm_lb1:;
    void * t48 = gm_l4;
    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)t48) + 8));
    uint8_t t50 = (t49 > ((uint64_t)0ULL));
    if (t50) {
        gm_b16_8 t51 = gm_l3;
        gm_l11 = t51;
        gm_b16_8 t52 = gm_l11;
        gm_closure t53 = ((gm_closure){(void *)gm_f_fn_dgpelc4, (void *)&F.h, cx});
        gm_f_stable_sort_by_spa1mja(t52, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t54 = gm_l11;
        gm_l12 = t54;
        gm_l13 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t56 = gm_l13;
                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint8_t t58 = (t56 < t57);
                if (!t58) {
                    goto gm_lb6;
                }
                {
                    void * t59 = GM_LD(void *, ((uint8_t *)&gm_l12));
                    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                    uint64_t t61 = gm_l13;
                    {
                        {
                            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)t59) + (uintptr_t)t61 * 8));
                            gm_l14 = t62;
                            gm_tf5srh4mp4b3i t63;
                            memset(&t63, 0, sizeof t63);
                            gm_l15 = t63;
                            void * t64 = gm_l4;
                            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)t64));
                            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                            gm_l16 = t66;
                            gm_l17 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t68 = gm_l17;
                                    uint64_t t69 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                    uint8_t t70 = (t68 < t69);
                                    if (!t70) {
                                        goto gm_lb11;
                                    }
                                    {
                                        uint64_t t71 = gm_l17;
                                        void * t72 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                        uint64_t t74 = gm_l17;
                                        gm_tf1bjg81u0glv t75 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)t72) + (uintptr_t)t74 * 24));
                                        gm_td38of7a349nj t76;
                                        GM_ST(uint64_t, ((uint8_t *)&t76 + 0), t71);
                                        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t76 + 8), t75);
                                        gm_l26 = t76;
                                        {
                                            {
                                                uint64_t t77 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
                                                gm_l18 = t77;
                                                memmove(((uint8_t *)&gm_l19), (((uint8_t *)&gm_l26) + 8), 24);
                                                uint64_t t78 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
                                                uint64_t t79 = gm_l14;
                                                uint8_t t80 = ((t78 == t79));
                                                if (t80) {
                                                    uint64_t t81 = gm_l18;
                                                    gm_tf5srh4mp4b3i t82;
                                                    GM_ST(uint8_t, (uint8_t *)&t82 + 9, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t82 + 10, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t82 + 12, 0);
                                                    GM_ST(uint64_t, ((uint8_t *)&t82), t81);
                                                    ((uint8_t *)&t82)[8] = 1;
                                                    gm_l15 = t82;
                                                }
                                                goto gm_lb14;
                                            }
gm_lb15:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 491, 13);
                                        }
gm_lb14:;
                                    }
gm_lb13:;
                                    uint64_t t83 = gm_l17;
                                    uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                                    uint64_t t86 = r84;
                                    gm_l17 = t86;
                                }
                            }
gm_lb11:;
                            {
                                {
                                    uint8_t t87 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                                    if (!t87) {
                                        goto gm_lb17;
                                    }
                                    uint64_t t88 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                    gm_l20 = t88;
                                    void * t89 = (void *)((uint8_t *)&F.l6);
                                    void * t90 = gm_l4;
                                    void * t91 = GM_LD(void *, ((uint8_t *)t90));
                                    uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)t90) + 8));
                                    uint64_t t93 = gm_l20;
                                    if ((uintptr_t)t93 >= (uintptr_t)(t92)) gm_trap_index((uintptr_t)t93, t92, gm_srcrorgjlg51brd, 495, 28);
                                    gm_b16_8 t94 = GM_LD(gm_b16_8, ((((uint8_t *)t91) + (uintptr_t)t93 * 24) + 8));
                                    gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
                                    gm_tftb1odc739t5 t96 = gm_f_extend_nkql463(t89, t95);
                                    gm_l27 = t96;
                                    uint8_t t97 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                    if (!t97) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcrorgjlg51brd, 495, 17);
                                    void * t98 = gm_l4;
                                    uint64_t t99 = gm_l20;
                                    gm_tf1bjg81u0glv t100;
                                    gm_f_remove_iqb0btt((void *)&t100, t98, t99);
                                    goto gm_lb16;
                                }
gm_lb17:;
                                void * t101 = gm_l1;
                                void * t102 = GM_LD(void *, ((uint8_t *)t101));
                                uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)t101) + 8));
                                void * t104 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint64_t t106 = gm_l14;
                                if ((uintptr_t)t106 >= (uintptr_t)(t105)) gm_trap_index((uintptr_t)t106, t105, gm_srcrorgjlg51brd, 498, 36);
                                uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)t104) + (uintptr_t)t106 * 8));
                                if ((uintptr_t)t107 >= (uintptr_t)(t103)) gm_trap_index((uintptr_t)t107, t103, gm_srcrorgjlg51brd, 498, 26);
                                gm_tq7s98ihonkju t108 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_tcgrjq9toscke *)(((uint8_t *)t102) + (uintptr_t)t107 * 88)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kieqgrgghc5)));
                                if (gm_unw.kind) {
                                    goto gm_ll55;
                                }
                                gm_l28 = t108;
                                uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srcrorgjlg51brd, 498, 22);
                                void * t110 = GM_LD(void *, (((uint8_t *)&gm_l28) + 8));
                                gm_l21 = t110;
                                void * t111 = gm_l1;
                                void * t112 = GM_LD(void *, ((uint8_t *)t111));
                                uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)t111) + 8));
                                void * t114 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint64_t t116 = gm_l14;
                                if ((uintptr_t)t116 >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)t116, t115, gm_srcrorgjlg51brd, 499, 27);
                                uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)t114) + (uintptr_t)t116 * 8));
                                if ((uintptr_t)t117 >= (uintptr_t)(t113)) gm_trap_index((uintptr_t)t117, t113, gm_srcrorgjlg51brd, 499, 17);
                                void * t118 = gm_l0;
                                void * t119 = gm_l21;
                                void * t120 = (void *)((uint8_t *)&F.l6);
                                void * t121 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t118, t119, t120);
                                if (gm_unw.kind) {
                                    goto gm_ll55;
                                }
                                memmove((((uint8_t *)t112) + (uintptr_t)t117 * 88), ((uint8_t *)t121), 88);
                            }
gm_lb16:;
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 489, 9);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t122 = gm_l13;
                uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                uint64_t t125 = r123;
                gm_l13 = t125;
            }
        }
        goto gm_ls126;
gm_ll55:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb6; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb8; }
        }
        goto gm_lad;
gm_ls126:;
gm_lb6:;
    } else {
        uint8_t t127 = gm_l7;
        if (t127) {
            gm_b16_8 t128 = gm_l3;
            gm_l22 = t128;
            gm_l23 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t130 = gm_l23;
                    uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                    uint8_t t132 = (t130 < t131);
                    if (!t132) {
                        goto gm_lb18;
                    }
                    {
                        void * t133 = GM_LD(void *, ((uint8_t *)&gm_l22));
                        uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                        uint64_t t135 = gm_l23;
                        {
                            {
                                uint64_t t136 = GM_LD(uint64_t, (((uint8_t *)t133) + (uintptr_t)t135 * 8));
                                gm_l24 = t136;
                                void * t137 = gm_l1;
                                void * t138 = GM_LD(void *, ((uint8_t *)t137));
                                uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)t137) + 8));
                                void * t140 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint64_t t142 = gm_l24;
                                if ((uintptr_t)t142 >= (uintptr_t)(t141)) gm_trap_index((uintptr_t)t142, t141, gm_srcrorgjlg51brd, 504, 32);
                                uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)t140) + (uintptr_t)t142 * 8));
                                if ((uintptr_t)t143 >= (uintptr_t)(t139)) gm_trap_index((uintptr_t)t143, t139, gm_srcrorgjlg51brd, 504, 22);
                                gm_tq7s98ihonkju t144 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_tcgrjq9toscke *)(((uint8_t *)t138) + (uintptr_t)t143 * 88)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ko7k0ku4679)));
                                if (gm_unw.kind) {
                                    goto gm_ll129;
                                }
                                gm_l29 = t144;
                                uint8_t t145 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                if (!t145) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srcrorgjlg51brd, 504, 18);
                                void * t146 = GM_LD(void *, (((uint8_t *)&gm_l29) + 8));
                                gm_l25 = t146;
                                void * t147 = gm_l1;
                                void * t148 = GM_LD(void *, ((uint8_t *)t147));
                                uint64_t t149 = GM_LD(uint64_t, (((uint8_t *)t147) + 8));
                                void * t150 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint64_t t152 = gm_l24;
                                if ((uintptr_t)t152 >= (uintptr_t)(t151)) gm_trap_index((uintptr_t)t152, t151, gm_srcrorgjlg51brd, 505, 23);
                                uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)t150) + (uintptr_t)t152 * 8));
                                if ((uintptr_t)t153 >= (uintptr_t)(t149)) gm_trap_index((uintptr_t)t153, t149, gm_srcrorgjlg51brd, 505, 13);
                                void * t154 = gm_l0;
                                void * t155 = gm_l25;
                                void * t156 = (void *)((uint8_t *)&F.l6);
                                void * t157 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t154, t155, t156);
                                if (gm_unw.kind) {
                                    goto gm_ll129;
                                }
                                memmove((((uint8_t *)t148) + (uintptr_t)t153 * 88), ((uint8_t *)t157), 88);
                                goto gm_lb21;
                            }
gm_lb22:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 503, 9);
                        }
gm_lb21:;
                    }
gm_lb20:;
                    uint64_t t158 = gm_l23;
                    uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
                    uint64_t t161 = r159;
                    gm_l23 = t161;
                }
            }
            goto gm_ls162;
gm_ll129:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb18; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb20; }
            }
            goto gm_lad;
gm_ls162:;
gm_lb18:;
        }
    }
    gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    F.ret = t163;
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
extern __attribute__((aligned(4))) void * gm_f_param_default_h93ci1m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, void * a3, gm_t88rcucg1b7uk a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_cannot_infer_tor1q4u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_report_cannot_infer_tor1q4u F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    uint32_t gm_l2; /* pid */
    gm_b16_8 gm_l3; /* values */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    uint8_t gm_l6; /* array_for_slice */
    uint8_t gm_l7; /* pointer_for_slice */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    void * gm_l12; /* v */
    gm_b16_8 gm_l13; /* hint */
    gm_b16_8 gm_l14; /* pname */
    gm_b16_8 gm_l15; /* how */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* k */
    GM_STACK_CHECK(&F, 1830u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tgsvmddj3e85e gm_l20;
    void * gm_l21;
    uint8_t gm_l22;
    gm_t53at80pfdus3 gm_l23;
    uint8_t gm_l24;
    gm_t53at80pfdus3 gm_l25;
    uint8_t gm_l26;
    gm_t53at80pfdus3 gm_l27;
    gm_b16_8 gm_l28;
    gm_b16_8 gm_l29;
    gm_b16_8 gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tgsvmddj3e85e gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_tabb710d9mq2e gm_l34;
    void * t23 = gm_l0;
    void * t24 = GM_LD(void *, ((uint8_t *)t23));
    void * t25 = (void *)(((uint8_t *)t24) + 8);
    gm_l5 = t25;
    gm_l6 = ((uint8_t)0);
    gm_l7 = ((uint8_t)0);
    void * t26 = gm_l1;
    gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)t26) + 40));
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
    gm_l8 = t28;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t29 = gm_l9;
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t31 = (t29 < t30);
            if (!t31) {
                goto gm_lb1;
            }
            {
                uint64_t t32 = gm_l9;
                void * t33 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t35 = gm_l9;
                gm_t5v0c8ia6a5mo t36 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t33) + (uintptr_t)t35 * 56));
                gm_tgsvmddj3e85e t37;
                GM_ST(uint64_t, ((uint8_t *)&t37 + 0), t32);
                GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t37 + 8), t36);
                gm_l20 = t37;
                {
                    {
                        uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&gm_l20));
                        gm_l10 = t38;
                        memmove(((uint8_t *)&F.l11), (((uint8_t *)&gm_l20) + 8), 56);
                        void * t39 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t41 = gm_l10;
                        if ((uintptr_t)t41 >= (uintptr_t)(t40)) gm_trap_index((uintptr_t)t41, t40, gm_srcrorgjlg51brd, 409, 14);
                        uint8_t t42 = (uint8_t)(GM_LD(void *, (((uint8_t *)t39) + (uintptr_t)t41 * 8)) != 0);
                        if (t42) {
                            void * t43 = GM_LD(void *, (((uint8_t *)t39) + (uintptr_t)t41 * 8));
                            gm_l21 = t43;
                        } else {
                            goto gm_lb3;
                        }
                        void * t44 = gm_l21;
                        gm_l12 = t44;
                        void * t45 = gm_l5;
                        gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                        gm_f_kind_jcnt4hm((void *)&gm_l23, t45, GM_CONV(gm_csguankko4lmhd, t46));
                        {
                            {
                                uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                                uint8_t t48 = (uint8_t)(t47 == ((uint8_t)10ULL));
                                if (!t48) {
                                    goto gm_lb7;
                                }
                                gm_l22 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l22 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 410, 12);
                        }
gm_lb6:;
                        uint8_t t49 = gm_l22;
                        if (t49) {
                            goto gm_lb3;
                        }
                        void * t50 = gm_l5;
                        void * t51 = gm_l12;
                        gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t51));
                        gm_f_kind_jcnt4hm((void *)&gm_l25, t50, GM_CONV(gm_csguankko4lmhd, t52));
                        {
                            {
                                uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                                uint8_t t54 = (uint8_t)(t53 == ((uint8_t)11ULL));
                                if (!t54) {
                                    goto gm_lb10;
                                }
                                gm_l24 = ((uint8_t)1);
                                goto gm_lb9;
                            }
gm_lb10:;
                            {
                                gm_l24 = ((uint8_t)0);
                                goto gm_lb9;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 411, 12);
                        }
gm_lb9:;
                        uint8_t t55 = gm_l24;
                        if (t55) {
                            gm_l6 = ((uint8_t)1);
                        }
                        void * t56 = gm_l5;
                        void * t57 = gm_l0;
                        void * t58 = (void *)(((uint8_t *)t57) + 96);
                        void * t59 = gm_l0;
                        void * t60 = GM_LD(void *, ((uint8_t *)t59));
                        void * t61 = gm_l12;
                        gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t61));
                        gm_t88rcucg1b7uk t63 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t58, t60, t62);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_f_kind_jcnt4hm((void *)&gm_l27, t56, GM_CONV(gm_csguankko4lmhd, t63));
                        {
                            {
                                uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                                uint8_t t65 = (uint8_t)(t64 == ((uint8_t)8ULL));
                                if (!t65) {
                                    goto gm_lb13;
                                }
                                gm_l26 = ((uint8_t)1);
                                goto gm_lb12;
                            }
gm_lb13:;
                            {
                                gm_l26 = ((uint8_t)0);
                                goto gm_lb12;
                            }
gm_lb14:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 412, 12);
                        }
gm_lb12:;
                        uint8_t t66 = gm_l26;
                        if (t66) {
                            gm_l7 = ((uint8_t)1);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 408, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t67 = gm_l9;
            uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
            uint64_t t70 = r68;
            gm_l9 = t70;
        }
    }
gm_lb1:;
    uint8_t t71 = gm_l6;
    if (t71) {
        gm_b16_8 t72;
        GM_ST(void *, (uint8_t *)&t72, (void *)(gm_stl5olh2jfr0d));
        GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(39));
        gm_l28 = t72;
    } else {
        {
            {
                uint8_t t73 = gm_l7;
                if (!t73) {
                    goto gm_lb16;
                }
                void * t74 = gm_l1;
                gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)t74));
                gm_b16_8 t76;
                GM_ST(void *, (uint8_t *)&t76, (void *)(gm_srv4cua6q51nq));
                GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(4));
                uint8_t t77 = (gm_eq0iqiubfm7e(((uint8_t *)&t75), ((uint8_t *)&t76)));
                if (!t77) {
                    goto gm_lb16;
                }
                gm_b16_8 t78;
                GM_ST(void *, (uint8_t *)&t78, (void *)(gm_sg7ronlh5k7js));
                GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(83));
                gm_l29 = t78;
                goto gm_lb15;
            }
gm_lb16:;
            uint8_t t79 = gm_l7;
            if (t79) {
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sgj7bt7nru3s1));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(26));
                gm_l30 = t80;
            } else {
                gm_b16_8 t81;
                GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(0));
                gm_l30 = t81;
            }
            gm_b16_8 t82 = gm_l30;
            gm_l29 = t82;
        }
gm_lb15:;
        gm_b16_8 t83 = gm_l29;
        gm_l28 = t83;
    }
    gm_b16_8 t84 = gm_l28;
    gm_l13 = t84;
    void * t85 = gm_l5;
    uint32_t t86 = gm_l2;
    gm_b16_8 t87 = gm_f_param_name_rbnop5l(t85, t86);
    gm_l14 = t87;
    void * t88 = gm_l1;
    gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)t88));
    gm_b16_8 t90 = gm_l14;
    gm_t6j6smj7qoctu t91;
    GM_ST(gm_b16_8, ((uint8_t *)&t91 + 0), t89);
    GM_ST(gm_b16_8, ((uint8_t *)&t91 + 16), t90);
    gm_f_format_vns89mr((void *)&gm_l31, (void *)NULL, cx, NULL, &t91);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
    if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srcrorgjlg51brd, 424, 19);
    gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
    gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
    gm_l15 = t94;
    void * t95 = gm_l1;
    gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)t95) + 40));
    gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
    gm_l16 = t97;
    gm_l17 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t98 = gm_l17;
            uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
            uint8_t t100 = (t98 < t99);
            if (!t100) {
                goto gm_lb17;
            }
            {
                uint64_t t101 = gm_l17;
                void * t102 = GM_LD(void *, ((uint8_t *)&gm_l16));
                uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint64_t t104 = gm_l17;
                gm_t5v0c8ia6a5mo t105 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t102) + (uintptr_t)t104 * 56));
                gm_tgsvmddj3e85e t106;
                GM_ST(uint64_t, ((uint8_t *)&t106 + 0), t101);
                GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t106 + 8), t105);
                gm_l32 = t106;
                {
                    {
                        uint64_t t107 = GM_LD(uint64_t, ((uint8_t *)&gm_l32));
                        gm_l18 = t107;
                        memmove(((uint8_t *)&F.l19), (((uint8_t *)&gm_l32) + 8), 56);
                        gm_t9805qtonbsag t108 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l19) + 40));
                        uint32_t t109 = gm_l2;
                        gm_t9805qtonbsag t110;
                        GM_ST(uint8_t, (uint8_t *)&t110 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t110 + 6, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t110), t109);
                        ((uint8_t *)&t110)[4] = 1;
                        uint8_t t111 = (!gm_eqkricceou4u(((uint8_t *)&t108), ((uint8_t *)&t110)));
                        if (t111) {
                            goto gm_lb19;
                        }
                        void * t112 = gm_l0;
                        void * t113 = GM_LD(void *, ((uint8_t *)t112));
                        void * t114 = gm_l1;
                        uint64_t t115 = gm_l18;
                        uint8_t t116 = gm_f_inferable_param_f80lrdi(t113, t114, t115);
                        if (t116) {
                            void * t117 = gm_l1;
                            gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)t117));
                            gm_b16_8 t119 = gm_l14;
                            gm_t6j6smj7qoctu t120;
                            GM_ST(gm_b16_8, ((uint8_t *)&t120 + 0), t118);
                            GM_ST(gm_b16_8, ((uint8_t *)&t120 + 16), t119);
                            gm_f_format_9n2qn66((void *)&gm_l33, (void *)NULL, cx, NULL, &t120);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t121 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                            if (!t121) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srcrorgjlg51brd, 427, 50);
                            gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                            gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
                            gm_l15 = t123;
                        }
                        goto gm_lb17;
                    }
gm_lb21:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 425, 5);
                }
gm_lb20:;
            }
gm_lb19:;
            uint64_t t124 = gm_l17;
            uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
            uint64_t t127 = r125;
            gm_l17 = t127;
        }
    }
gm_lb17:;
    void * t128 = gm_l0;
    gm_tl874t2atnlh3 t129 = gm_l4;
    gm_b16_8 t130 = gm_l14;
    gm_b16_8 t131 = gm_l15;
    gm_b16_8 t132 = gm_l13;
    gm_tfeab279c16rr t133;
    GM_ST(gm_b16_8, ((uint8_t *)&t133 + 0), t130);
    GM_ST(gm_b16_8, ((uint8_t *)&t133 + 16), t131);
    GM_ST(gm_b16_8, ((uint8_t *)&t133 + 32), t132);
    gm_f_format_obibpjh((void *)&gm_l34, (void *)NULL, cx, NULL, &t133);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
    if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_srcrorgjlg51brd, 430, 20);
    gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
    gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&t135));
    gm_b0_1 t137 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t128, t129, t136);
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_int_value_ngrqsok(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_same_int_value_ngrqsok F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* a */
    gm_t88rcucg1b7uk gm_l2; /* b */
    gm_t428ue2u3hf5g gm_l3; /* x */
    gm_t428ue2u3hf5g gm_l4; /* y */
    GM_STACK_CHECK(&F, 529u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l7;
    gm_t53at80pfdus3 gm_l8;
    gm_thrdpodrs8pgt gm_l9;
    gm_tbci5ospk5nnn gm_l10;
    gm_thrdpodrs8pgt gm_l11;
    gm_tbci5ospk5nnn gm_l12;
    {
        {
            void * t6 = gm_l0;
            void * t7 = (void *)(((uint8_t *)t6) + 8);
            gm_t88rcucg1b7uk t8 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l7, t7, GM_CONV(gm_csguankko4lmhd, t8));
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)21ULL));
            if (!t10) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l7) + 8), 32);
            {
                {
                    void * t11 = gm_l0;
                    void * t12 = (void *)(((uint8_t *)t11) + 8);
                    gm_t88rcucg1b7uk t13 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l8, t12, GM_CONV(gm_csguankko4lmhd, t13));
                    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
                    uint8_t t15 = (uint8_t)(t14 == ((uint8_t)21ULL));
                    if (!t15) {
                        goto gm_lb4;
                    }
                    memmove(((uint8_t *)&gm_l4), (((uint8_t *)&gm_l8) + 8), 32);
                    void * t16 = gm_l0;
                    gm_t428ue2u3hf5g t17 = GM_LD(gm_t428ue2u3hf5g, ((uint8_t *)&gm_l3));
                    gm_f_key_int_pqscuvn((void *)&gm_l9, (void *)NULL, cx, NULL, t16, &t17);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l9))[48] != 0);
                    if (t18) {
                        memmove(((uint8_t *)&gm_l10), ((uint8_t *)&gm_l9), 48);
                    } else {
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    memmove(((uint8_t *)&F.l5), ((uint8_t *)&gm_l10), 48);
                    void * t19 = gm_l0;
                    gm_t428ue2u3hf5g t20 = GM_LD(gm_t428ue2u3hf5g, ((uint8_t *)&gm_l4));
                    gm_f_key_int_pqscuvn((void *)&gm_l11, (void *)NULL, cx, NULL, t19, &t20);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l11))[48] != 0);
                    if (t21) {
                        memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l11), 48);
                    } else {
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    memmove(((uint8_t *)&F.l6), ((uint8_t *)&gm_l12), 48);
                    uint8_t t22 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
                    F.ret = t22;
                    goto gm_fret;
                }
gm_lb4:;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
gm_lb3:;
        }
gm_lb2:;
        F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_bind_nsjnlvq(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_set_bind_nsjnlvq F;
    void * gm_l0; /* binds */
    uint32_t gm_l1; /* p */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l6;
    gm_l4 = ((uint64_t)0ULL);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
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
                        void * t13 = gm_l0;
                        void * t14 = GM_LD(void *, ((uint8_t *)t13));
                        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t13) + 8));
                        uint64_t t16 = gm_l5;
                        if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_srcrorgjlg51brd, 570, 12);
                        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)t14) + (uintptr_t)t16 * 8));
                        uint32_t t18 = gm_l1;
                        uint8_t t19 = ((t17 == t18));
                        if (t19) {
                            void * t20 = gm_l0;
                            void * t21 = GM_LD(void *, ((uint8_t *)t20));
                            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t20) + 8));
                            uint64_t t23 = gm_l5;
                            if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcrorgjlg51brd, 571, 13);
                            gm_t88rcucg1b7uk t24 = gm_l2;
                            GM_ST(gm_t88rcucg1b7uk, ((((uint8_t *)t21) + (uintptr_t)t23 * 8) + 4), t24);
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 569, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t25 = gm_l4;
            uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
            uint64_t t28 = r26;
            gm_l4 = t28;
        }
    }
gm_lb1:;
    void * t29 = gm_l0;
    uint32_t t30 = gm_l1;
    gm_t88rcucg1b7uk t31 = gm_l2;
    gm_tplpbqf3duvn3 t32;
    GM_ST(uint32_t, ((uint8_t *)&t32 + 0), t30);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t32 + 4), t31);
    gm_tftb1odc739t5 t33 = gm_f_push_h69n5fi(t29, GM_CONV(gm_cs7e9ppsd384bj, t32));
    gm_l6 = t33;
    uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcrorgjlg51brd, 575, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_source_location_nc1oola(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_source_location_nc1oola F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* tys */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    void * gm_l4; /* file */
    gm_tcc1rq4n1avb2 gm_l5; /* lc */
    gm_b16_8 gm_l6; /* function */
    GM_STACK_CHECK(&F, 1049u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tkn42u7na3t81 gm_l8;
    gm_t88rcucg1b7uk gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_l2 = t4;
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    gm_tkn42u7na3t81 t7 = gm_f_source_location_ty_slllard((void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t7;
    uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l8))[4] != 0);
    if (t8) {
        gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l8));
        gm_l9 = t9;
    } else {
        void * t10 = gm_l0;
        gm_tl874t2atnlh3 t11 = gm_l1;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s8pjuqfuormq3));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(50));
        gm_b0_1 t13 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t10, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, ((uint8_t *)t14));
        gm_tl874t2atnlh3 t16 = gm_l1;
        void * t17 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t17;
        goto gm_fret;
    }
    gm_t88rcucg1b7uk t18 = gm_l9;
    gm_l3 = t18;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    void * t22 = GM_LD(void *, ((uint8_t *)t21));
    uint32_t t23 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t24 = gm_f_file_o15fqku(t22, t23);
    gm_l4 = t24;
    void * t25 = gm_l4;
    uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_tcc1rq4n1avb2 t27 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t25, t26));
    gm_l5 = t27;
    void * t28 = gm_l0;
    gm_b16_8 t29 = gm_f_location_function_thau9ff(t28);
    gm_l6 = t29;
    gm_trqfvmjtt4sij t30;
    gm_f_list_vp1djv1_2((void *)&t30, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l7 = t30;
    void * t31 = (void *)((uint8_t *)&F.l7);
    void * t32 = gm_l2;
    gm_t88rcucg1b7uk t33 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t34 = gm_l4;
    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)t34));
    gm_tl874t2atnlh3 t36 = gm_l1;
    gm_t2b6tm8c0vgo1 t37;
    memset(&t37, 0, sizeof t37);
    GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)5ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t37 + 8), t35);
    void * t38 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t33, &t37, t36);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t39 = gm_f_push_d943vgp_2(t31, &(*(gm_tcgrjq9toscke *)((uint8_t *)t38)));
    gm_l10 = t39;
    uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcrorgjlg51brd, 762, 5);
    void * t41 = (void *)((uint8_t *)&F.l7);
    void * t42 = gm_l2;
    gm_t88rcucg1b7uk t43 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t42, ((uint8_t)0), ((uint16_t)32ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t44 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
    uint64_t t45 = (uint64_t)t44;
    gm_tbci5ospk5nnn t46;
    gm_f_from_u64_ud13q5v((void *)&t46, (void *)NULL, cx, NULL, t45);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tl874t2atnlh3 t47 = gm_l1;
    gm_t2b6tm8c0vgo1 t48;
    GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t48 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t48 + 8), t46);
    void * t49 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t43, &t48, t47);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t50 = gm_f_push_d943vgp_2(t41, &(*(gm_tcgrjq9toscke *)((uint8_t *)t49)));
    gm_l11 = t50;
    uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcrorgjlg51brd, 763, 5);
    void * t52 = (void *)((uint8_t *)&F.l7);
    void * t53 = gm_l2;
    gm_t88rcucg1b7uk t54 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t53, ((uint8_t)0), ((uint16_t)32ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
    uint64_t t56 = (uint64_t)t55;
    gm_tbci5ospk5nnn t57;
    gm_f_from_u64_ud13q5v((void *)&t57, (void *)NULL, cx, NULL, t56);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tl874t2atnlh3 t58 = gm_l1;
    gm_t2b6tm8c0vgo1 t59;
    GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t59 + 56, 0);
    GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t59 + 8), t57);
    void * t60 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t54, &t59, t58);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t61 = gm_f_push_d943vgp_2(t52, &(*(gm_tcgrjq9toscke *)((uint8_t *)t60)));
    gm_l12 = t61;
    uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t62) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcrorgjlg51brd, 764, 5);
    void * t63 = (void *)((uint8_t *)&F.l7);
    void * t64 = gm_l2;
    gm_t88rcucg1b7uk t65 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t64);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t66 = gm_l6;
    gm_tl874t2atnlh3 t67 = gm_l1;
    gm_t2b6tm8c0vgo1 t68;
    memset(&t68, 0, sizeof t68);
    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)5ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t68 + 8), t66);
    void * t69 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t65, &t68, t67);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tftb1odc739t5 t70 = gm_f_push_d943vgp_2(t63, &(*(gm_tcgrjq9toscke *)((uint8_t *)t69)));
    gm_l13 = t70;
    uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcrorgjlg51brd, 765, 5);
    gm_t88rcucg1b7uk t72 = gm_l3;
    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
    gm_tl874t2atnlh3 t74 = gm_l1;
    gm_t2b6tm8c0vgo1 t75;
    memset(&t75, 0, sizeof t75);
    GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t75 + 8), t73);
    void * t76 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t72, &t75, t74);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t76;
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
inline __attribute__((aligned(4))) uint64_t gm_f_source_position_taggjb0(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_source_position_taggjb0 F;
    gm_b16_8 gm_l0; /* order */
    uint64_t gm_l1; /* i */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* k */
    uint64_t gm_l5; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 250u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t2lqu8jsj511d gm_l6;
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
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t12) + (uintptr_t)t14 * 8));
                gm_t2lqu8jsj511d t16;
                GM_ST(uint64_t, ((uint8_t *)&t16 + 0), t11);
                GM_ST(uint64_t, ((uint8_t *)&t16 + 8), t15);
                gm_l6 = t16;
                {
                    {
                        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
                        gm_l4 = t17;
                        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        gm_l5 = t18;
                        uint64_t t19 = gm_l5;
                        uint64_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (t21) {
                            uint64_t t22 = gm_l4;
                            uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
                            if (o24) gm_trap("integer overflow in `+`", gm_srcrorgjlg51brd, 522, 28);
                            uint64_t t25 = r23;
                            F.ret = t25;
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 521, 5);
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
    F.ret = ((uint64_t)0ULL);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_b16_8 gm_f_span_text_sv6qv18(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_too_many_generic_args_bs99n7s(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_b16_8 a3, gm_b16_8 a4, uint64_t a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_too_many_generic_args_bs99n7s F;
    void * gm_l0; /* cx */
    void * gm_l1; /* sig */
    uint8_t gm_l2; /* has_receiver */
    gm_b16_8 gm_l3; /* named */
    gm_b16_8 gm_l4; /* positional */
    uint64_t gm_l5; /* ntargets */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_b16_8 gm_l7; /* what */
    gm_b16_8 gm_l8; /* msg */
    void * gm_l9; /* first */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 935u);
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
    gm_b16_8 gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tgsvmddj3e85e gm_l16;
    gm_tabb710d9mq2e gm_l17;
    uint8_t t11 = gm_l2;
    if (t11) {
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s26emr7fhbmjr));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(21));
        gm_l14 = t12;
    } else {
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(0));
        gm_l14 = t13;
    }
    gm_b16_8 t14 = gm_l14;
    gm_l7 = t14;
    uint64_t t15 = gm_l5;
    gm_b16_8 t16 = gm_l7;
    gm_tf1bjg81u0glv t17;
    GM_ST(uint64_t, ((uint8_t *)&t17 + 0), t15);
    GM_ST(gm_b16_8, ((uint8_t *)&t17 + 8), t16);
    gm_f_format_dvcvfgj((void *)&gm_l15, (void *)NULL, cx, NULL, &t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcrorgjlg51brd, 288, 12);
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
    gm_l8 = t19;
    void * t20 = GM_LD(void *, ((uint8_t *)&gm_l4));
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t21, gm_srcrorgjlg51brd, 290, 15);
    void * t22 = (void *)(((uint8_t *)t20) + (uintptr_t)((uint64_t)0ULL) * 88);
    gm_l9 = t22;
    {
        {
            void * t23 = gm_l0;
            void * t24 = gm_l9;
            uint8_t t25 = gm_f_clearly_value_f7icrt5(t23, t24);
            uint8_t t26 = (uint8_t)!t25;
            if (!t26) {
                goto gm_lb2;
            }
            void * t27 = gm_l0;
            void * t28 = gm_l9;
            uint8_t t29 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t29) {
                goto gm_lb2;
            }
            void * t30 = gm_l1;
            gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t30) + 40));
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            gm_l10 = t32;
            gm_l11 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t33 = gm_l11;
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint8_t t35 = (t33 < t34);
                    if (!t35) {
                        goto gm_lb3;
                    }
                    {
                        uint64_t t36 = gm_l11;
                        void * t37 = GM_LD(void *, ((uint8_t *)&gm_l10));
                        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                        uint64_t t39 = gm_l11;
                        gm_t5v0c8ia6a5mo t40 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t37) + (uintptr_t)t39 * 56));
                        gm_tgsvmddj3e85e t41;
                        GM_ST(uint64_t, ((uint8_t *)&t41 + 0), t36);
                        GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&t41 + 8), t40);
                        gm_l16 = t41;
                        {
                            {
                                uint64_t t42 = GM_LD(uint64_t, ((uint8_t *)&gm_l16));
                                gm_l12 = t42;
                                memmove(((uint8_t *)&F.l13), (((uint8_t *)&gm_l16) + 8), 56);
                                {
                                    {
                                        void * t43 = gm_l0;
                                        void * t44 = GM_LD(void *, ((uint8_t *)t43));
                                        void * t45 = gm_l1;
                                        uint64_t t46 = gm_l12;
                                        uint8_t t47 = gm_f_inferable_param_f80lrdi(t44, t45, t46);
                                        if (!t47) {
                                            goto gm_lb9;
                                        }
                                        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                                        gm_b16_8 t49 = gm_l3;
                                        uint8_t t50 = 0;
                                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t49) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t49))) + i * 16, ((uint8_t *)&t48))) { t50 = 1; break; }
                                        uint8_t t51 = (uint8_t)(!t50);
                                        if (!t51) {
                                            goto gm_lb9;
                                        }
                                        gm_b16_8 t52 = gm_l8;
                                        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                                        void * t54 = gm_l0;
                                        void * t55 = gm_l9;
                                        gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 72));
                                        gm_b16_8 t57 = gm_f_span_text_sv6qv18(t54, GM_CONV(gm_cseahvg8v7p0vs, t56));
                                        gm_tfeab279c16rr t58;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t52);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 16), t53);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 32), t57);
                                        gm_f_format_41hm2he((void *)&gm_l17, (void *)NULL, cx, NULL, &t58);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                        if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcrorgjlg51brd, 294, 23);
                                        gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                                        gm_l8 = t60;
                                        goto gm_lb3;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcrorgjlg51brd, 292, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t61 = gm_l11;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l11 = t64;
                }
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t65 = gm_l0;
    gm_tl874t2atnlh3 t66 = gm_l6;
    gm_b16_8 t67 = gm_l8;
    gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
    gm_b0_1 t69 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t65, t66, t68);
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
