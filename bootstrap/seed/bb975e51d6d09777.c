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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tcrdb1inj22os;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[1]; uint32_t f5; } gm_th32vq80aukjr;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
typedef struct { gm_tiilmfd16v3fc f0; gm_b16_8 f1; } gm_t1cs2fjnrqap1;
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
typedef struct { uint64_t f0; gm_tsoie1ppgdvcg f1; } gm_t5im2v50m3hk6;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tcrdb1inj22os) == 72 && _Alignof(gm_tcrdb1inj22os) == 8 && offsetof(gm_tcrdb1inj22os, f0) == 0 && offsetof(gm_tcrdb1inj22os, f1) == 16 && offsetof(gm_tcrdb1inj22os, f2) == 24 && offsetof(gm_tcrdb1inj22os, f3) == 40 && offsetof(gm_tcrdb1inj22os, f4) == 56, "layout of `(string, usize, string, string, string)`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th32vq80aukjr) == 32 && _Alignof(gm_th32vq80aukjr) == 8 && offsetof(gm_th32vq80aukjr, f0) == 0 && offsetof(gm_th32vq80aukjr, f1) == 8 && offsetof(gm_th32vq80aukjr, f2) == 24 && offsetof(gm_th32vq80aukjr, f3) == 25 && offsetof(gm_th32vq80aukjr, f4) == 26 && offsetof(gm_th32vq80aukjr, f5) == 28, "layout of `(u8, string, bool, bool, bool, u32)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_trqfvmjtt4sij f1; uint32_t f2; uint8_t pe[4]; } gm_tk0navpiff9ca;
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { gm_tiilmfd16v3fc f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; gm_b16_8 f3; } gm_tvc83pqnepc2e;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1cs2fjnrqap1) == 40 && _Alignof(gm_t1cs2fjnrqap1) == 8 && offsetof(gm_t1cs2fjnrqap1, f0) == 0 && offsetof(gm_t1cs2fjnrqap1, f1) == 24, "layout of `HandlerLit`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
_Static_assert(sizeof(gm_t5im2v50m3hk6) == 48 && _Alignof(gm_t5im2v50m3hk6) == 8 && offsetof(gm_t5im2v50m3hk6, f0) == 0 && offsetof(gm_t5im2v50m3hk6, f1) == 8, "layout of `(i: usize, x: LocalDecl)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
_Static_assert(sizeof(gm_tk0navpiff9ca) == 56 && _Alignof(gm_tk0navpiff9ca) == 8 && offsetof(gm_tk0navpiff9ca, f0) == 0 && offsetof(gm_tk0navpiff9ca, f1) == 8 && offsetof(gm_tk0navpiff9ca, f2) == 48, "layout of `Dump`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tvc83pqnepc2e) == 64 && _Alignof(gm_tvc83pqnepc2e) == 8 && offsetof(gm_tvc83pqnepc2e, f0) == 0 && offsetof(gm_tvc83pqnepc2e, f1) == 24 && offsetof(gm_tvc83pqnepc2e, f2) == 32 && offsetof(gm_tvc83pqnepc2e, f3) == 48, "layout of `HandlerDeclInfo`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; void * f4; } gm_t150deeftog49;
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t150deeftog49) == 96 && _Alignof(gm_t150deeftog49) == 8 && offsetof(gm_t150deeftog49, f0) == 0 && offsetof(gm_t150deeftog49, f1) == 40 && offsetof(gm_t150deeftog49, f2) == 56 && offsetof(gm_t150deeftog49, f3) == 72 && offsetof(gm_t150deeftog49, f4) == 88, "layout of `WriteExpr`");
struct gm_fr_binop_text_t4cv0b4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_builtin_text_od38u7h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_coercion_text_g32oppe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fv6i5s8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_escaped_k5fm4jl {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_flavor_text_3ffnbt7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_label_9hbn37h {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* args */
    gm_b16_8 ret;
};
struct gm_fr_fn_pfpd9oj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_format_02b5heb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_0969kd8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_0gd6afr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_0n7d8jn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_16dq22e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1d88j1c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_208te7b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_2lgoflc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_32bp3b4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3669u6h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3aao56v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_41c1370 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_45lt1qt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4i3nb5n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_556kaa4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5k1v0hr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5m6a3eh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5up8eqa {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6b7mjf6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6h8qssq {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6na8imi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7316bju {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7ejl94l {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_85biid9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8k3m4s1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8vmpj9s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9g7fct9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9nv39ua {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a4vi4f7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aq84o6u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_arnesr1 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_as0b1t3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_booci4l {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c2ann39 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c726p1u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cqr3tc0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cuuc21t {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dva3pbu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ejli3qf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fi775k0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fp763nd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_galn15q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_glqqt6c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gnbipfi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_i37o6qg {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_i4fsdjb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_i7k9v76 {
    gm_frame h;
    gm_tcrdb1inj22os l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tcrdb1inj22os l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_irh7ehn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ka8hqpj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kj3qi5h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lde77u1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lderfbr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lefnhqh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_licpkoa {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_llf38gp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lpj4rro {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nva8q4r {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_oc8ipo6 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_olqcc5i {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p2bh4bb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p7ogfqk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pcmnhpk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pvddll3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qcp7nf9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qhad3qp {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r0u4lpe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r8pfaos {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r9ba7g9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ree2v5u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rvu96av {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sqbp092 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sugh2p3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t0vntpb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t725p24 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vmi8kfe {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vprps7i {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_is_integer_ab2ohk2 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s1kpkpr {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_text_75tuacb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_param_name_rbnop5l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_place_lj97s4p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pf02nv6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5719f1b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rmw_text_te4ol3u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_8uijkbs {
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
struct gm_fr_show_rat_1s624db {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_sink_text_s4o8tr0 {
    gm_frame h;
    gm_th1g69d7vpipg l1; /* sink */
    gm_b16_8 ret;
};
struct gm_fr_soa_op_text_ud90n2l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_sort_by_egbtonb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_spec_text_fr9884d {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sys_text_9qjnpra {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_target_text_4kebb7t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_tir_dump_ppa6e9g {
    gm_frame h;
    void * l0; /* s */
    gm_trqfvmjtt4sij l1; /* order */
    gm_tk0navpiff9ca l6; /* d */
    gm_b16_8 ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_write_4lsqsab {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_7h6pth0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_7ilko9q {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_arm_4vimkqo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_block_q5mpqjf {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* names */
    gm_b0_1 ret;
};
struct gm_fr_write_conds_k9e57un {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_expr_cftucor {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* kids */
    gm_tbci5ospk5nnn l6; /* v */
    gm_t1cs2fjnrqap1 l48; /* lit */
    gm_trqfvmjtt4sij l49; /* labels */
    gm_t710lsrq1m8gk l53; /* w */
    gm_tsth9qu1dv0g3 l69; /* callee */
    gm_t710lsrq1m8gk l110; /* x */
    gm_trqfvmjtt4sij l127; /* h */
    gm_b0_1 ret;
};
struct gm_fr_write_fn_jkakt7l {
    gm_frame h;
    gm_tsoie1ppgdvcg l11; /* l */
    gm_trqfvmjtt4sij l13; /* names */
    gm_b0_1 ret;
};
struct gm_fr_write_if_oqjiok5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_line_d6lkps7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_loop_u0ja07u {
    gm_frame h;
    gm_t710lsrq1m8gk l2; /* x */
    gm_b0_1 ret;
};
struct gm_fr_write_pat_4qu9dcc {
    gm_frame h;
    gm_tbci5ospk5nnn l6; /* lo */
    gm_tbci5ospk5nnn l7; /* hi */
    gm_b0_1 ret;
};
struct gm_fr_write_t5i5r03 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_text_h6e1ibp {
    gm_frame h;
    gm_t150deeftog49 l1; /* w */
    gm_trqfvmjtt4sij l2; /* h */
    gm_b16_8 ret;
};
struct gm_fr_write_uebhmoa {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_v26t591 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_b0_1 ret;
};
struct gm_fr_write_with_j37nil7 {
    gm_frame h;
    gm_t710lsrq1m8gk l2; /* w */
    gm_trqfvmjtt4sij l11; /* names */
    gm_tvc83pqnepc2e l15; /* hd */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_t4cv0b4(void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_builtin_text_od38u7h(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_coercion_text_g32oppe(void *env, gm_node *cx, gm_node *inst, gm_tmn7n0n9raou5 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_bytes_25mo226(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_escaped_k5fm4jl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_flavor_text_3ffnbt7(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_label_9hbn37h(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_fn_pfpd9oj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_02b5heb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_0969kd8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_0gd6afr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
inline __attribute__((aligned(4))) void gm_f_format_0n7d8jn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_16dq22e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
inline __attribute__((aligned(4))) void gm_f_format_1d88j1c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_208te7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_2lgoflc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_32bp3b4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_3669u6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_3aao56v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_41c1370(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_45lt1qt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_4i3nb5n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_toh0ootodhjue a0);
inline __attribute__((aligned(4))) void gm_f_format_556kaa4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5823bl2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_5k1v0hr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_5m6a3eh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5up8eqa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6b7mjf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_6h8qssq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6na8imi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_7316bju(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_74pd3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_7ejl94l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_85biid9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8k3m4s1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8vmpj9s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9g7fct9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_9nv39ua(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_a4vi4f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_aq84o6u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_arnesr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_as0b1t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_format_booci4l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_c2ann39(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
inline __attribute__((aligned(4))) void gm_f_format_c726p1u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_c97t5s6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_cqr3tc0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cuuc21t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dq6oc41(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_dva3pbu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ejli3qf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fi775k0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_fp763nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_galn15q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_glqqt6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_gnbipfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_i37o6qg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_i4fsdjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_i7k9v76(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_irh7ehn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_j9cgp0l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ka8hqpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_kj3qi5h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0);
inline __attribute__((aligned(4))) void gm_f_format_lde77u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lderfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lefnhqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_licpkoa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_llf38gp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lpj4rro(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_nva8q4r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_oc8ipo6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_olqcc5i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_p2bh4bb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_p7ogfqk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_pcmnhpk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_pvddll3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qcp7nf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qhad3qp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_r0u4lpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_r8pfaos(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_r9ba7g9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rc5a5cm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ree2v5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_rvu96av(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_s2bvqvf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_shk2dul(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_toh0ootodhjue a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_sqbp092(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_sugh2p3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_t0vntpb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_t725p24(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vmi8kfe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vprps7i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_41b084m(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
__attribute__((aligned(4))) void gm_f_introsort_l0ks50g(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_local_text_75tuacb(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_place_lj97s4p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_rmw_text_te4ol3u(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_int_ub6npub(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_rat_1s624db(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sink_text_s4o8tr0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_soa_op_text_ud90n2l(void *env, gm_node *cx, gm_node *inst, gm_tesaltf0mhcki a0);
inline __attribute__((aligned(4))) void gm_f_sort_by_egbtonb(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_spec_text_fr9884d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_text_9qjnpra(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_target_text_4kebb7t(void *env, gm_node *cx, gm_node *inst, gm_te9hrk0u35rev a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_tir_dump_ppa6e9g(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_4lsqsab(void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7h6pth0(void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7ilko9q(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_arm_4vimkqo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_block_q5mpqjf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_conds_k9e57un(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e55tuqo(void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_expr_cftucor(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_fn_jkakt7l(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_if_oqjiok5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_line_d6lkps7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_loop_u0ja07u(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_pat_4qu9dcc(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ss9qp4o(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_t5i5r03(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_write_text_h6e1ibp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uebhmoa(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_v26t591(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_with_j37nil7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s000up2l16pib[9];
extern const uint8_t gm_s00pihipe0jga[6];
extern const uint8_t gm_s026u6q948evn[5];
extern const uint8_t gm_s0eqrtq5mqb3a[16];
extern const uint8_t gm_s0iioal6odpu7[11];
extern const uint8_t gm_s0kvnv9teor3q[4];
extern const uint8_t gm_s0lh54534p3vd[10];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0pf2spartj4v[12];
extern const uint8_t gm_s0ve0dba4rtj8[13];
extern const uint8_t gm_s187rb18i1kgk[4];
extern const uint8_t gm_s19069udlvhni[9];
extern const uint8_t gm_s19ndlkgdtviu[6];
extern const uint8_t gm_s1fshvu7vb9i4[16];
extern const uint8_t gm_s1gf327eapo1j[11];
extern const uint8_t gm_s1j5448m6ruti[10];
extern const uint8_t gm_s1jdtclvlju06[9];
extern const uint8_t gm_s1js4dn5e65ja[11];
extern const uint8_t gm_s1n20vngt0mkv[3];
extern const uint8_t gm_s1q2nkavemrqf[9];
extern const uint8_t gm_s1q9ms0kviddj[4];
extern const uint8_t gm_s1s57v200fem0[6];
extern const uint8_t gm_s2289h15tkpi1[12];
extern const uint8_t gm_s24chpb914bep[6];
extern const uint8_t gm_s268n7l3tugli[6];
extern const uint8_t gm_s28ijm46j20ds[8];
extern const uint8_t gm_s2a9t8smkevg0[4];
extern const uint8_t gm_s2fdsodctojm1[10];
extern const uint8_t gm_s2fu5nkek0lrq[7];
extern const uint8_t gm_s2h8cr50573go[8];
extern const uint8_t gm_s2iprgandb78l[8];
extern const uint8_t gm_s2kmbpuutp9qq[5];
extern const uint8_t gm_s2ma5cp41jk1a[7];
extern const uint8_t gm_s2nl2e5anglov[4];
extern const uint8_t gm_s2ot4uojp2s87[7];
extern const uint8_t gm_s2uphqpvb2vhh[4];
extern const uint8_t gm_s2uqljf8sumcn[11];
extern const uint8_t gm_s31o876d6qij5[10];
extern const uint8_t gm_s35gfshei80n5[8];
extern const uint8_t gm_s3avlcjhdh976[10];
extern const uint8_t gm_s3ea3k3og84ri[9];
extern const uint8_t gm_s3ng5nguvd452[5];
extern const uint8_t gm_s3o5gaeteeggu[5];
extern const uint8_t gm_s3ojik1morrbi[9];
extern const uint8_t gm_s3ol5q9br79vf[8];
extern const uint8_t gm_s3opc5ivp6e66[4];
extern const uint8_t gm_s3s50a53ci7rk[7];
extern const uint8_t gm_s49o1oiv7k0po[4];
extern const uint8_t gm_s4bdmssec9aiu[4];
extern const uint8_t gm_s4g3rq58567m6[8];
extern const uint8_t gm_s4gdu7v0gur88[11];
extern const uint8_t gm_s4gilalcmdvu3[8];
extern const uint8_t gm_s4lo69slu4igt[8];
extern const uint8_t gm_s4lt93qhapike[4];
extern const uint8_t gm_s4r2qm241merr[12];
extern const uint8_t gm_s55ccd7do7rbs[11];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s57rgvjb5mj5a[8];
extern const uint8_t gm_s589bsqckia3s[8];
extern const uint8_t gm_s5c5548ll8taj[10];
extern const uint8_t gm_s5dbqnns2epkg[6];
extern const uint8_t gm_s5e0seq0he7ou[10];
extern const uint8_t gm_s5k0vuc678fh1[8];
extern const uint8_t gm_s5n5e484glelv[5];
extern const uint8_t gm_s5udr3eget84q[6];
extern const uint8_t gm_s67i6nrmepi0v[7];
extern const uint8_t gm_s6aamtn65omr1[7];
extern const uint8_t gm_s6ag7edtaqqri[5];
extern const uint8_t gm_s6edba3m7mc9a[6];
extern const uint8_t gm_s6ipt60dh9o1j[11];
extern const uint8_t gm_s6nbs6o2qkgvg[16];
extern const uint8_t gm_s6nm27ehg597q[7];
extern const uint8_t gm_s6ofccoi3ursa[12];
extern const uint8_t gm_s6p9ub6gu603m[7];
extern const uint8_t gm_s6qbnfe2f321h[4];
extern const uint8_t gm_s76ekntop342o[8];
extern const uint8_t gm_s76ud0alkjfse[4];
extern const uint8_t gm_s7d4tu5fkkg12[11];
extern const uint8_t gm_s7dkbj8tvt9e3[7];
extern const uint8_t gm_s7i80akiq0d95[5];
extern const uint8_t gm_s7j04h96r4716[18];
extern const uint8_t gm_s7m3rbk4l87ah[15];
extern const uint8_t gm_s7rltjaeh634d[11];
extern const uint8_t gm_s7u6ij52clodh[7];
extern const uint8_t gm_s85hqf14lbkns[8];
extern const uint8_t gm_s85jtgs7au5b5[9];
extern const uint8_t gm_s86g8amsftemj[8];
extern const uint8_t gm_s87g1ufoaqi74[8];
extern const uint8_t gm_s8d2alh114kri[15];
extern const uint8_t gm_s8gtcifve79ai[9];
extern const uint8_t gm_s8j8keh1qgf0a[7];
extern const uint8_t gm_s8pk5tcnrk6p2[8];
extern const uint8_t gm_s8s6shfnaqahd[7];
extern const uint8_t gm_s8u62e7lr2m4s[13];
extern const uint8_t gm_s920dpvvv6cej[4];
extern const uint8_t gm_s93umj3p0tala[5];
extern const uint8_t gm_s96j6o9smoqdu[8];
extern const uint8_t gm_s9d2hbf54sh7s[22];
extern const uint8_t gm_s9dgf2prvg715[6];
extern const uint8_t gm_s9es2tos1bhfp[5];
extern const uint8_t gm_s9h93c7asacna[7];
extern const uint8_t gm_s9i2ld3a42lap[8];
extern const uint8_t gm_s9o8gb20t6jtp[9];
extern const uint8_t gm_s9v13eck7gubi[4];
extern const uint8_t gm_sa0alba1kin3k[8];
extern const uint8_t gm_sa1pcp92k3aj3[7];
extern const uint8_t gm_sa53epbg9iarq[10];
extern const uint8_t gm_sa5s0jools6a2[9];
extern const uint8_t gm_sa6hsburhm4pn[9];
extern const uint8_t gm_sa8dse4jk5ibv[10];
extern const uint8_t gm_sa902c1tbtg0i[4];
extern const uint8_t gm_sacua9qc75rts[7];
extern const uint8_t gm_sah1jmr01nidj[5];
extern const uint8_t gm_sai3vjkob4s2v[10];
extern const uint8_t gm_sajbccb18c45d[6];
extern const uint8_t gm_sakmuvnjjp217[10];
extern const uint8_t gm_savtbnmf9bjnr[17];
extern const uint8_t gm_sb5h01ig0b9oe[34];
extern const uint8_t gm_sbckpj1jsdt28[13];
extern const uint8_t gm_sbcq15b9em6fm[9];
extern const uint8_t gm_sbek54n4a6h2o[7];
extern const uint8_t gm_sbgepjj6hgjej[4];
extern const uint8_t gm_sbnms71r2jqr5[7];
extern const uint8_t gm_sbqq5iijjahf9[5];
extern const uint8_t gm_sbsb3nmbfgbv2[4];
extern const uint8_t gm_sbuhie0vb9t62[9];
extern const uint8_t gm_sc03pp1oj3q4k[7];
extern const uint8_t gm_sc092629f9on4[15];
extern const uint8_t gm_sc24ftfhihd1h[4];
extern const uint8_t gm_sc5qopgd7dm66[8];
extern const uint8_t gm_sc92utnrdaeo1[12];
extern const uint8_t gm_scb853lmgr58k[6];
extern const uint8_t gm_scd34uli7vuuj[20];
extern const uint8_t gm_scd599tsljaee[5];
extern const uint8_t gm_sdak257j1qgl8[6];
extern const uint8_t gm_sdfl2t7s13ucr[12];
extern const uint8_t gm_sdft8ov6bb7s6[8];
extern const uint8_t gm_sdg7bdfivjinb[3];
extern const uint8_t gm_sdh1dnevn1j10[8];
extern const uint8_t gm_sdl8mgtvebfcs[3];
extern const uint8_t gm_sdq6l7oddjqfe[12];
extern const uint8_t gm_sdrrh4gk917l3[10];
extern const uint8_t gm_sds03accef1j8[6];
extern const uint8_t gm_sdskn4nn8f7q0[5];
extern const uint8_t gm_se0a26478itki[7];
extern const uint8_t gm_se0ivu8cf46o8[12];
extern const uint8_t gm_se19nf1421e37[4];
extern const uint8_t gm_sedrsudco1u16[10];
extern const uint8_t gm_seeo26ergtcha[5];
extern const uint8_t gm_seg9l1usqbs3j[4];
extern const uint8_t gm_segr993uh2crm[11];
extern const uint8_t gm_seh91qmsr2tfr[8];
extern const uint8_t gm_sel2c30qvm74f[7];
extern const uint8_t gm_seodvmll3gsjn[4];
extern const uint8_t gm_sepu95c7o773o[6];
extern const uint8_t gm_ser8ip7vtbv6c[9];
extern const uint8_t gm_setcl7798ov89[4];
extern const uint8_t gm_sf5llhuhdlajp[9];
extern const uint8_t gm_sf90mcj0opih1[8];
extern const uint8_t gm_sf9difklo6omg[17];
extern const uint8_t gm_sfch9mo7lqp8l[3];
extern const uint8_t gm_sfd0mn3sol25u[4];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfggcm2ak7679[20];
extern const uint8_t gm_sfid9en449upv[11];
extern const uint8_t gm_sfsqcni8p8345[42];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgankvlme129s[3];
extern const uint8_t gm_sgfufhs3psibd[4];
extern const uint8_t gm_sgk015tlfp2cu[5];
extern const uint8_t gm_sgm565ktbj9ua[12];
extern const uint8_t gm_sgo00f0mafbnd[4];
extern const uint8_t gm_sgpdmvc9s0d2p[3];
extern const uint8_t gm_sgtriqnjufjbf[10];
extern const uint8_t gm_sh11qqm8nqu68[8];
extern const uint8_t gm_sh4e4kqha06b3[7];
extern const uint8_t gm_sh4niqnmh73co[6];
extern const uint8_t gm_shdnhcpvptc1q[5];
extern const uint8_t gm_shkq4botdoed7[6];
extern const uint8_t gm_shohh3180gu26[7];
extern const uint8_t gm_shtuc6se4lilq[7];
extern const uint8_t gm_shvp36midgf58[10];
extern const uint8_t gm_shvpku7ssg8d7[11];
extern const uint8_t gm_si1m7q6ia0hto[7];
extern const uint8_t gm_si2s3gai2c29i[16];
extern const uint8_t gm_si4k9tiofr8t2[7];
extern const uint8_t gm_si5jb1hd2qqp1[12];
extern const uint8_t gm_si9h59pjqftat[5];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sij2r6jhpquej[12];
extern const uint8_t gm_sim8pu6stc37a[11];
extern const uint8_t gm_sinmpfqtbo9in[5];
extern const uint8_t gm_siqgrmkn8n4vn[8];
extern const uint8_t gm_sismsc98l6u4e[4];
extern const uint8_t gm_sit8u7uppaik9[7];
extern const uint8_t gm_sivsapeh482sm[8];
extern const uint8_t gm_sj1m58fkg956m[8];
extern const uint8_t gm_sj3cmvbmi7cm6[9];
extern const uint8_t gm_sj696i9uo1a9s[14];
extern const uint8_t gm_sjbhp9l7seful[7];
extern const uint8_t gm_sjbl17pjsjm48[5];
extern const uint8_t gm_sjdrosvte680q[12];
extern const uint8_t gm_sjegrp8u9r3n3[8];
extern const uint8_t gm_sjeopa747clpb[4];
extern const uint8_t gm_sjerq54hvns79[11];
extern const uint8_t gm_sjfcbh429vmot[9];
extern const uint8_t gm_sjg1ri23hm753[6];
extern const uint8_t gm_sjijer52q1op6[7];
extern const uint8_t gm_sjk2q3c614jh7[84];
extern const uint8_t gm_sjourjtj9e3to[9];
extern const uint8_t gm_sk2b59ovo43qg[6];
extern const uint8_t gm_sk5s72mtujop8[5];
extern const uint8_t gm_sk698gp5chukg[10];
extern const uint8_t gm_sk721elh8ouv2[11];
extern const uint8_t gm_sk88tp2337lf6[13];
extern const uint8_t gm_skapbb3maknat[5];
extern const uint8_t gm_skbgm82aqj3q0[6];
extern const uint8_t gm_skfdav0cuo9ri[14];
extern const uint8_t gm_skissj10nclhr[6];
extern const uint8_t gm_skl3b78lo1gln[5];
extern const uint8_t gm_skpghu6j8rgck[12];
extern const uint8_t gm_sksprenpp79ih[11];
extern const uint8_t gm_skvqkn3gte5mk[11];
extern const uint8_t gm_sl37uvr16qsu8[11];
extern const uint8_t gm_sl9jmb4urj4ud[13];
extern const uint8_t gm_slgrfk23llffe[13];
extern const uint8_t gm_sligbvojecf44[15];
extern const uint8_t gm_sllcm1pqagh28[6];
extern const uint8_t gm_slmii6hjigrcr[7];
extern const uint8_t gm_sln5qir3tgi7j[10];
extern const uint8_t gm_slnhe1h36jmub[9];
extern const uint8_t gm_slov3heeajhqn[12];
extern const uint8_t gm_slrmmf184fvu8[26];
extern const uint8_t gm_slsbknfpio6sr[9];
extern const uint8_t gm_sltvces0c7l80[14];
extern const uint8_t gm_sluco9qbar70k[10];
extern const uint8_t gm_sm017vofcj044[12];
extern const uint8_t gm_sm0gqinn3n1e9[15];
extern const uint8_t gm_sm0gs6tv176le[8];
extern const uint8_t gm_sm3dios51ece1[10];
extern const uint8_t gm_smgioupbmjajr[14];
extern const uint8_t gm_smmus0krp2svr[6];
extern const uint8_t gm_smpde4t8bsefo[14];
extern const uint8_t gm_smpl91sh0k8v4[9];
extern const uint8_t gm_smsmo0f5ao530[10];
extern const uint8_t gm_smtmd013g2edr[4];
extern const uint8_t gm_smuec3qfsdh82[10];
extern const uint8_t gm_smv20k805bk9c[15];
extern const uint8_t gm_sn0il8abv9oq3[8];
extern const uint8_t gm_sn581p3lbgrjg[9];
extern const uint8_t gm_sn6sqlran7h3r[7];
extern const uint8_t gm_snaq7hvenqsum[4];
extern const uint8_t gm_snbhl8dsqq3ln[7];
extern const uint8_t gm_sncgl1t0uiqr4[17];
extern const uint8_t gm_sngdu4l8vh3l8[5];
extern const uint8_t gm_snm19vfbec4q3[19];
extern const uint8_t gm_snoq0o06l5uns[15];
extern const uint8_t gm_snpp7pgdikfns[10];
extern const uint8_t gm_snq4uaiieftl8[6];
extern const uint8_t gm_snstimc3n04u0[12];
extern const uint8_t gm_so1u9tqjfpsmc[9];
extern const uint8_t gm_so2sjlodq81qn[13];
extern const uint8_t gm_so3oq96gr0k5r[10];
extern const uint8_t gm_so6ip6bkb1lgs[9];
extern const uint8_t gm_so8fkrgrobcsk[9];
extern const uint8_t gm_sobcm12sogdng[5];
extern const uint8_t gm_soe4gvhg9ei97[4];
extern const uint8_t gm_soerl6rds8b9v[12];
extern const uint8_t gm_sofbtko7t8taa[5];
extern const uint8_t gm_sogev0d3re2th[11];
extern const uint8_t gm_soi6iv1hnej40[13];
extern const uint8_t gm_sojpmv6gl0jac[12];
extern const uint8_t gm_solj59tk4ri3k[9];
extern const uint8_t gm_sopbumhmsvcgf[10];
extern const uint8_t gm_sovel03lmfl8u[9];
extern const uint8_t gm_sp2smaof0i4j2[10];
extern const uint8_t gm_sp4uhnkunj2cg[3];
extern const uint8_t gm_spi47lg4tr80j[3];
extern const uint8_t gm_spoh8ft9n8oqt[4];
extern const uint8_t gm_spr274eleu2lj[4];
extern const uint8_t gm_spskhsf5tnedj[10];
extern const uint8_t gm_sq0h7jjc6dtk5[4];
extern const uint8_t gm_sqa0b48i8mvk7[6];
extern const uint8_t gm_sqflguscq1uc5[6];
extern const uint8_t gm_sqfu5kl4vat80[9];
extern const uint8_t gm_sqhhm0h7b3asd[6];
extern const uint8_t gm_sqhkrul9t68sa[8];
extern const uint8_t gm_sqlk0h0kae56e[8];
extern const uint8_t gm_sqrv416075ito[3];
extern const uint8_t gm_squedv51820an[5];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr87tgv3ltq65[13];
extern const uint8_t gm_sr899mamktpvo[7];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const uint8_t gm_src5778gpfoh9[9];
extern const char gm_src6b5gn9iggr1p[];
extern const char gm_src7h8dni243k2v[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srco4ljquuk5d87[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srjd0sq8k3d7s[11];
extern const uint8_t gm_srmrjv19jgvks[11];
extern const uint8_t gm_srntlv7ksi1vk[16];
extern const uint8_t gm_sromjese8e416[14];
extern const uint8_t gm_srri8c6al1v1g[5];
extern const uint8_t gm_ss27a20sarlvg[7];
extern const uint8_t gm_ss308e7joejdo[13];
extern const uint8_t gm_ss6dev5u7tl99[12];
extern const uint8_t gm_ss869lnvmj3m8[6];
extern const uint8_t gm_sse9dgkusbtnf[5];
extern const uint8_t gm_ssgeom7733jik[5];
extern const uint8_t gm_ssgn9mppmu3vl[16];
extern const uint8_t gm_ssigpbgi88it3[4];
extern const uint8_t gm_ssjmv3618n779[10];
extern const uint8_t gm_sstrtuk2b4jdg[10];
extern const uint8_t gm_ssu2vb7lj6d81[21];
extern const uint8_t gm_st02nolqdtiev[9];
extern const uint8_t gm_st0l9v6pp5i7b[14];
extern const uint8_t gm_st1pc9bsku00q[9];
extern const uint8_t gm_st49tt11e1avl[8];
extern const uint8_t gm_st7pul9bg6mqu[8];
extern const uint8_t gm_st87fk7ojs4l8[6];
extern const uint8_t gm_st8ki5f85lc2j[13];
extern const uint8_t gm_stbno5snno6c5[8];
extern const uint8_t gm_std59a9b6lpnn[10];
extern const uint8_t gm_stdefoi5sk91c[12];
extern const uint8_t gm_stehcnejvoo24[6];
extern const uint8_t gm_stft4dc50tdce[8];
extern const uint8_t gm_stgqtu1f7bco8[8];
extern const uint8_t gm_stidgejoe9ul2[11];
extern const uint8_t gm_stivl0smu8eq0[9];
extern const uint8_t gm_stn52kuiq0jk2[12];
extern const uint8_t gm_stou1l3qp844f[10];
extern const uint8_t gm_su3ahsl9p584f[9];
extern const uint8_t gm_sugq7dl55ocon[7];
extern const uint8_t gm_suk88n3of3t2l[6];
extern const uint8_t gm_sukbpodfceoe1[9];
extern const uint8_t gm_sukutl224cgkm[8];
extern const uint8_t gm_sun4qq3op5opf[7];
extern const uint8_t gm_suntqnp3vfhar[16];
extern const uint8_t gm_sup9bfr0np428[9];
extern const uint8_t gm_supnh5d0f56vu[8];
extern const uint8_t gm_supvu1ruutbov[9];
extern const uint8_t gm_sus38q16nfojv[7];
extern const uint8_t gm_sv0fbf330lmcd[8];
extern const uint8_t gm_sv3olmne7h13s[4];
extern const uint8_t gm_sv8lqdk5nsv8i[5];
extern const uint8_t gm_svbhcvlahd4n2[3];
extern const uint8_t gm_svbrm62b64ga9[11];
extern const uint8_t gm_svkrg5dctlh9m[7];
extern const uint8_t gm_svqq3k9j6h1lu[8];
extern const gm_tmpl gm_tpl09kglfpefe8i;
extern const gm_tmpl gm_tpl16petnrgdcd0;
extern const gm_tmpl gm_tpl1f5t4gadpj5n;
extern const gm_tmpl gm_tpl1g7tc1f10noh;
extern const gm_tmpl gm_tpl1hek9uhvdtmo;
extern const gm_tmpl gm_tpl2a7pmi7aq67m;
extern const gm_tmpl gm_tpl2cgrvj4ke42d;
extern const gm_tmpl gm_tpl36bd3ievounm;
extern const gm_tmpl gm_tpl3b00264af9oi;
extern const gm_tmpl gm_tpl3pvc6fn0aqps;
extern const gm_tmpl gm_tpl461nvut9qmgp;
extern const gm_tmpl gm_tpl4bpbmadpmu1i;
extern const gm_tmpl gm_tpl4eg9pptom3ij;
extern const gm_tmpl gm_tpl4nfbp2ikscbk;
extern const gm_tmpl gm_tpl53ihvkdedd31;
extern const gm_tmpl gm_tpl5igv3ckbutrp;
extern const gm_tmpl gm_tpl5rdj46lif6eq;
extern const gm_tmpl gm_tpl77hog51tnj6o;
extern const gm_tmpl gm_tpl7kgo5i3aumd2;
extern const gm_tmpl gm_tpl7net9ifts13e;
extern const gm_tmpl gm_tpl810t9tkid6i2;
extern const gm_tmpl gm_tpl8vbes2v6184u;
extern const gm_tmpl gm_tpl955dep3t38r6;
extern const gm_tmpl gm_tpl9red218f96cc;
extern const gm_tmpl gm_tpl9v7lb7n4a602;
extern const gm_tmpl gm_tpla8nvlakostko;
extern const gm_tmpl gm_tpla93ou7df923g;
extern const gm_tmpl gm_tplb52vln6fobrq;
extern const gm_tmpl gm_tplc3kt858jk6jp;
extern const gm_tmpl gm_tplc6hhdd7vcmvk;
extern const gm_tmpl gm_tplca6pmroagoou;
extern const gm_tmpl gm_tplcddb08maq348;
extern const gm_tmpl gm_tplcic56vj3l629;
extern const gm_tmpl gm_tpldhof7u5v5b2c;
extern const gm_tmpl gm_tpldm0f92v61rgi;
extern const gm_tmpl gm_tpldsg332pn89g6;
extern const gm_tmpl gm_tple87ca4est7gh;
extern const gm_tmpl gm_tplfbtlvu2isf2h;
extern const gm_tmpl gm_tplg4866gcgr837;
extern const gm_tmpl gm_tplge1jgdcvfbq1;
extern const gm_tmpl gm_tplhc78l95r8lfq;
extern const gm_tmpl gm_tplhen8m8tu01kk;
extern const gm_tmpl gm_tplhon5g1o0vuqi;
extern const gm_tmpl gm_tplhp4k6h527j3k;
extern const gm_tmpl gm_tplicg84n3os024;
extern const gm_tmpl gm_tplims1qainr636;
extern const gm_tmpl gm_tplj9vh1qm5m031;
extern const gm_tmpl gm_tplja7aur1oiv70;
extern const gm_tmpl gm_tpljf9iocqf0fr6;
extern const gm_tmpl gm_tpljnlsd7076tqd;
extern const gm_tmpl gm_tplkaran1m5lpqm;
extern const gm_tmpl gm_tplkhnuvgfhr9v5;
extern const gm_tmpl gm_tplknfbq49i74sp;
extern const gm_tmpl gm_tplksrq9kcn68vt;
extern const gm_tmpl gm_tpllsre3itruefn;
extern const gm_tmpl gm_tplm6g8a9odsasi;
extern const gm_tmpl gm_tplme3pul5hs3bf;
extern const gm_tmpl gm_tplmkb4jrr2jqel;
extern const gm_tmpl gm_tplmqflu0tp0eas;
extern const gm_tmpl gm_tpln3j86kdpqd7a;
extern const gm_tmpl gm_tpln7ptjfs3pum5;
extern const gm_tmpl gm_tplntci9ovg2hdj;
extern const gm_tmpl gm_tplob6co4svk3k5;
extern const gm_tmpl gm_tplodlbfsp8tfu3;
extern const gm_tmpl gm_tplokoueqaolfmt;
extern const gm_tmpl gm_tplom6fga2t4kfv;
extern const gm_tmpl gm_tplotas2madniqp;
extern const gm_tmpl gm_tplpj1vfgivuj13;
extern const gm_tmpl gm_tplqm5shuhajt4a;
extern const gm_tmpl gm_tplqnml5avaeikk;
extern const gm_tmpl gm_tplr3ni3ai315ji;
extern const gm_tmpl gm_tplrveq8m992ml7;
extern const gm_tmpl gm_tpls6f1h1hgtqgb;
extern const gm_tmpl gm_tpltrvmjk8jhmh7;
extern const gm_tmpl gm_tplu2g08gg1kb39;
extern const gm_tmpl gm_tpluetglub2muf2;
extern const gm_tmpl gm_tplvbcfr0k6nmhv;
extern const gm_tmpl gm_tplvftnuchaenfj;
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fv6i5s8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fv6i5s8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_escaped_k5fm4jl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_escaped_k5fm4jl F;
    gm_b16_8 gm_l0; /* prefix */
    gm_b16_8 gm_l1; /* bytes */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    gm_trqfvmjtt4sij t2;
    gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t2;
    void * t3 = (void *)((uint8_t *)&F.l2);
    gm_b16_8 t4 = gm_l0;
    gm_tftb1odc739t5 t5 = gm_f_extend_0cs85gs(t3, t4);
    gm_l3 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srco4ljquuk5d87, 428, 5);
    gm_b16_8 t7 = gm_l1;
    void * t8 = (void *)((uint8_t *)&F.l2);
    gm_b0_1 t9 = gm_f_escape_bytes_25mo226((void *)NULL, cx, NULL, t7, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
    F.ret = t11;
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
inline __attribute__((aligned(4))) void gm_f_format_02b5heb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_02b5heb F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjerq54hvns79));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl8vbes2v6184u), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_0969kd8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0969kd8 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s9d2hbf54sh7s));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(21));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5rdj46lif6eq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_0gd6afr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0) {
    struct gm_fr_format_0gd6afr F;
    gm_tcc1rq4n1avb2 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tcc1rq4n1avb2 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tcc1rq4n1avb2 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snoq0o06l5uns));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_j9cgp0l((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplc3kt858jk6jp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_0n7d8jn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_0n7d8jn F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st49tt11e1avl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldhof7u5v5b2c), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_16dq22e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0) {
    struct gm_fr_format_16dq22e F;
    gm_tcc1rq4n1avb2 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tcc1rq4n1avb2 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tcc1rq4n1avb2 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0pf2spartj4v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_j9cgp0l((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhon5g1o0vuqi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_1d88j1c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1d88j1c F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s268n7l3tugli));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2cgrvj4ke42d), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_208te7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_208te7b F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3ojik1morrbi));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3b00264af9oi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_2lgoflc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_2lgoflc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s1s57v200fem0));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplkhnuvgfhr9v5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_32bp3b4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_32bp3b4 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st02nolqdtiev));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl461nvut9qmgp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3669u6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_3669u6h F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc092629f9on4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl2a7pmi7aq67m), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3aao56v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3aao56v F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdrrh4gk917l3));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplob6co4svk3k5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_41c1370(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_41c1370 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slrmmf184fvu8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4nfbp2ikscbk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_45lt1qt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_45lt1qt F;
    gm_t5hdnnftkup6m gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t5hdnnftkup6m gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sukbpodfceoe1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t5hdnnftkup6m t3 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)a0));
    gm_f_format_s2bvqvf((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4bpbmadpmu1i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_4i3nb5n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_toh0ootodhjue a0) {
    struct gm_fr_format_4i3nb5n F;
    gm_toh0ootodhjue gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_toh0ootodhjue gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 26u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toh0ootodhjue t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfsqcni8p8345));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
    gm_f_format_shk2dul((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhc78l95r8lfq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_556kaa4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_556kaa4 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s589bsqckia3s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl53ihvkdedd31), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5m6a3eh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5m6a3eh F;
    gm_tommf4j9upomc gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tommf4j9upomc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_snm19vfbec4q3));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllsre3itruefn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5up8eqa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5up8eqa F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4r2qm241merr));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(11));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplja7aur1oiv70), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6b7mjf6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6b7mjf6 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skpghu6j8rgck));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7net9ifts13e), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_6h8qssq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6h8qssq F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s7j04h96r4716));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(17));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpla93ou7df923g), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6na8imi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_6na8imi F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snpp7pgdikfns));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqm5shuhajt4a), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7316bju(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7316bju F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3ea3k3og84ri));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5igv3ckbutrp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7ejl94l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7ejl94l F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stbno5snno6c5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldsg332pn89g6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_85biid9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_85biid9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sopbumhmsvcgf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmqflu0tp0eas), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8k3m4s1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8k3m4s1 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s67i6nrmepi0v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl810t9tkid6i2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8vmpj9s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8vmpj9s F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8d2alh114kri));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpls6f1h1hgtqgb), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9g7fct9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_9g7fct9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sncgl1t0uiqr4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9red218f96cc), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9nv39ua(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_9nv39ua F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s1jdtclvlju06));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplfbtlvu2isf2h), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_a4vi4f7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a4vi4f7 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suntqnp3vfhar));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj9vh1qm5m031), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_aq84o6u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_aq84o6u F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sltvces0c7l80));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplom6fga2t4kfv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_arnesr1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_arnesr1 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s6nbs6o2qkgvg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(15));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhp4k6h527j3k), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_as0b1t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_as0b1t3 F;
    gm_t9qkh0rsooc8d gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t9qkh0rsooc8d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 20u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_supnh5d0f56vu));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_c97t5s6((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpln3j86kdpqd7a), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_booci4l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_booci4l F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1fshvu7vb9i4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1g7tc1f10noh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_c2ann39(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0) {
    struct gm_fr_format_c2ann39 F;
    gm_tcc1rq4n1avb2 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tcc1rq4n1avb2 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tcc1rq4n1avb2 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb5h01ig0b9oe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_j9cgp0l((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4eg9pptom3ij), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_c726p1u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_c726p1u F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s31o876d6qij5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqnml5avaeikk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cqr3tc0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cqr3tc0 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjijer52q1op6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpltrvmjk8jhmh7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cuuc21t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cuuc21t F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sfggcm2ak7679));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(19));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvbcfr0k6nmhv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_dva3pbu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dva3pbu F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sligbvojecf44));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpluetglub2muf2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ejli3qf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ejli3qf F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2ot4uojp2s87));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplc6hhdd7vcmvk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fi775k0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fi775k0 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3avlcjhdh976));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl36bd3ievounm), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fp763nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fp763nd F;
    gm_tiilmfd16v3fc gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tiilmfd16v3fc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s1s57v200fem0));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_tiilmfd16v3fc t3 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)a0));
    gm_f_format_rc5a5cm((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplkhnuvgfhr9v5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_galn15q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_galn15q F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so6ip6bkb1lgs));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmkb4jrr2jqel), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_glqqt6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_glqqt6c F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8j8keh1qgf0a));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplokoueqaolfmt), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gnbipfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_gnbipfi F;
    gm_tiilmfd16v3fc gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tiilmfd16v3fc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl9jmb4urj4ud));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
    gm_tiilmfd16v3fc t3 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)a0));
    gm_f_format_rc5a5cm((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplknfbq49i74sp), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_i37o6qg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_i37o6qg F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc92utnrdaeo1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl955dep3t38r6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_i4fsdjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_i4fsdjb F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_si2s3gai2c29i));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3pvc6fn0aqps), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_i7k9v76(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_i7k9v76 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smv20k805bk9c));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_tcrdb1inj22os t3 = GM_LD(gm_tcrdb1inj22os, ((uint8_t *)a0));
    gm_f_format_5823bl2((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1hek9uhvdtmo), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_irh7ehn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_irh7ehn F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdskn4nn8f7q0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplge1jgdcvfbq1), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ka8hqpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ka8hqpj F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svbrm62b64ga9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl77hog51tnj6o), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_kj3qi5h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tcc1rq4n1avb2 a0) {
    struct gm_fr_format_kj3qi5h F;
    gm_tcc1rq4n1avb2 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tcc1rq4n1avb2 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tcc1rq4n1avb2 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1s57v200fem0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_f_format_j9cgp0l((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkhnuvgfhr9v5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lde77u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lde77u1 F;
    gm_tiilmfd16v3fc gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tiilmfd16v3fc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_soi6iv1hnej40));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(12));
    gm_tiilmfd16v3fc t3 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)a0));
    gm_f_format_rc5a5cm((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpljnlsd7076tqd), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lderfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lderfbr F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s87g1ufoaqi74));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpln7ptjfs3pum5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lefnhqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_lefnhqh F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skvqkn3gte5mk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplpj1vfgivuj13), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_licpkoa(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_licpkoa F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svkrg5dctlh9m));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7kgo5i3aumd2), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_llf38gp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_llf38gp F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ssu2vb7lj6d81));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(20));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplkaran1m5lpqm), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lpj4rro(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lpj4rro F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sojpmv6gl0jac));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tple87ca4est7gh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_nva8q4r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nva8q4r F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf9difklo6omg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl16petnrgdcd0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_oc8ipo6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_oc8ipo6 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_si5jb1hd2qqp1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(11));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplca6pmroagoou), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_olqcc5i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_olqcc5i F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_seh91qmsr2tfr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplu2g08gg1kb39), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_p2bh4bb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_p2bh4bb F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sup9bfr0np428));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_74pd3in((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9v7lb7n4a602), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_p7ogfqk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_p7ogfqk F;
    gm_tiilmfd16v3fc gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tiilmfd16v3fc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_scd34uli7vuuj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(19));
    gm_tiilmfd16v3fc t3 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)a0));
    gm_f_format_rc5a5cm((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhen8m8tu01kk), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_pcmnhpk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_pcmnhpk F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdfl2t7s13ucr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplksrq9kcn68vt), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_pvddll3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_pvddll3 F;
    gm_tommf4j9upomc gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_tommf4j9upomc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_skfdav0cuo9ri));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(13));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplicg84n3os024), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qcp7nf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qcp7nf9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn0il8abv9oq3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplg4866gcgr837), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qhad3qp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_qhad3qp F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8u62e7lr2m4s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl09kglfpefe8i), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_r0u4lpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_r0u4lpe F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sf90mcj0opih1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(7));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplrveq8m992ml7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_r8pfaos(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r8pfaos F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s000up2l16pib));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplb52vln6fobrq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_r9ba7g9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_r9ba7g9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sm0gqinn3n1e9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplims1qainr636), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ree2v5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_ree2v5u F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk698gp5chukg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldm0f92v61rgi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_rvu96av(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rvu96av F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0iioal6odpu7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcddb08maq348), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sqbp092(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sqbp092 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s86g8amsftemj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplme3pul5hs3bf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_sugh2p3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sugh2p3 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smsmo0f5ao530));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplm6g8a9odsasi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t0vntpb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_t0vntpb F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sus38q16nfojv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcic56vj3l629), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t725p24(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_t725p24 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr87tgv3ltq65));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplr3ni3ai315ji), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vmi8kfe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vmi8kfe F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sogev0d3re2th));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpljf9iocqf0fr6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vprps7i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_vprps7i F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4gdu7v0gur88));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvftnuchaenfj), t3);
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
inline __attribute__((aligned(4))) void gm_f_list_s1kpkpr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s1kpkpr F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fv6i5s8();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pf02nv6(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pf02nv6 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_fv6i5s8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5719f1b(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5719f1b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5719f1b F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_rat_1s624db(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_show_rat_1s624db F;
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_trqfvmjtt4sij t2;
    gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_b0_1 t4 = gm_f_write_v26t591((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l0)), t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
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
inline __attribute__((aligned(4))) void gm_f_sort_by_egbtonb(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_egbtonb F;
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
    gm_f_introsort_l0ks50g(t2, t3);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_4lsqsab(void * a0, gm_t88rcucg1b7uk a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_4lsqsab F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_suk88n3of3t2l));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
    gm_tftb1odc739t5 t5 = gm_f_write_e55tuqo(t2, t3, ((void *)&gm_tpl1f5t4gadpj5n), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7h6pth0(void * a0, gm_t88rcucg1b7uk a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_7h6pth0 F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 51u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_slsbknfpio6sr));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(8));
    gm_tftb1odc739t5 t5 = gm_f_write_e55tuqo(t2, t3, ((void *)&gm_tpla8nvlakostko), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7ilko9q(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_7ilko9q F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_smpde4t8bsefo));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(13));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplotas2madniqp), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_t5i5r03(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_t5i5r03 F;
    void * gm_l0; /* l */
    gm_th32vq80aukjr gm_l1; /* args */
    uint8_t gm_l2; /* failed */
    gm_th32vq80aukjr gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjk2q3c614jh7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(83));
    gm_tftb1odc739t5 t4 = gm_f_write_ss9qp4o(t2, &(*(gm_th32vq80aukjr *)((uint8_t *)a1)), ((void *)&gm_tplodlbfsp8tfu3), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_uebhmoa(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_uebhmoa F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sa6hsburhm4pn));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(8));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplntci9ovg2hdj), t4);
    F.ret = t5;
    goto gm_fret;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_t4cv0b4(void *env, gm_node *cx, gm_node *inst, gm_ts6oqppjh0ba3 a0) {
    struct gm_fr_binop_text_t4cv0b4 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* f */
    gm_t9qkh0rsooc8d gm_l2; /* f */
    gm_t9qkh0rsooc8d gm_l3; /* f */
    gm_t9qkh0rsooc8d gm_l4; /* f */
    GM_STACK_CHECK(&F, 726u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_tabb710d9mq2e gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_b16_8 gm_l10;
    {
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
            if (!t24) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t25 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
            gm_l1 = t25;
            gm_t9qkh0rsooc8d t26 = gm_l1;
            gm_b16_8 t27 = gm_f_flavor_text_3ffnbt7(t26);
            gm_tnpifv20irnph t28;
            GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t27);
            gm_f_format_0n7d8jn((void *)&gm_l6, (void *)NULL, cx, NULL, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srco4ljquuk5d87, 434, 16);
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
            gm_l5 = t31;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)1ULL));
            if (!t33) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t34 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
            gm_l2 = t34;
            gm_t9qkh0rsooc8d t35 = gm_l2;
            gm_b16_8 t36 = gm_f_flavor_text_3ffnbt7(t35);
            gm_tnpifv20irnph t37;
            GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
            gm_f_format_lderfbr((void *)&gm_l7, (void *)NULL, cx, NULL, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srco4ljquuk5d87, 435, 16);
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            gm_l5 = t40;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)2ULL));
            if (!t42) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t43 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
            gm_l3 = t43;
            gm_t9qkh0rsooc8d t44 = gm_l3;
            gm_b16_8 t45 = gm_f_flavor_text_3ffnbt7(t44);
            gm_tnpifv20irnph t46;
            GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
            gm_f_format_olqcc5i((void *)&gm_l8, (void *)NULL, cx, NULL, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srco4ljquuk5d87, 436, 16);
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_l5 = t49;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)3ULL));
            if (!t51) {
                goto gm_lb5;
            }
            gm_t9qkh0rsooc8d t52 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 1));
            gm_l4 = t52;
            gm_t9qkh0rsooc8d t53 = gm_l4;
            gm_b16_8 t54 = gm_f_flavor_text_3ffnbt7(t53);
            gm_tnpifv20irnph t55;
            GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
            gm_f_format_sqbp092((void *)&gm_l9, (void *)NULL, cx, NULL, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srco4ljquuk5d87, 437, 16);
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
            gm_l5 = t58;
            goto gm_lb1;
        }
gm_lb5:;
        {
            {
                {
                    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t60 = (uint8_t)(t59 == ((uint8_t)0ULL));
                    if (!t60) {
                        goto gm_lb8;
                    }
                    gm_b16_8 t61;
                    GM_ST(void *, (uint8_t *)&t61, (void *)(gm_spr274eleu2lj));
                    GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(3));
                    gm_l10 = t61;
                    goto gm_lb7;
                }
gm_lb8:;
                {
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)1ULL));
                    if (!t63) {
                        goto gm_lb9;
                    }
                    gm_b16_8 t64;
                    GM_ST(void *, (uint8_t *)&t64, (void *)(gm_ssigpbgi88it3));
                    GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(3));
                    gm_l10 = t64;
                    goto gm_lb7;
                }
gm_lb9:;
                {
                    uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t66 = (uint8_t)(t65 == ((uint8_t)2ULL));
                    if (!t66) {
                        goto gm_lb10;
                    }
                    gm_b16_8 t67;
                    GM_ST(void *, (uint8_t *)&t67, (void *)(gm_spoh8ft9n8oqt));
                    GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(3));
                    gm_l10 = t67;
                    goto gm_lb7;
                }
gm_lb10:;
                {
                    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t69 = (uint8_t)(t68 == ((uint8_t)3ULL));
                    if (!t69) {
                        goto gm_lb11;
                    }
                    gm_b16_8 t70;
                    GM_ST(void *, (uint8_t *)&t70, (void *)(gm_smtmd013g2edr));
                    GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(3));
                    gm_l10 = t70;
                    goto gm_lb7;
                }
gm_lb11:;
                {
                    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t72 = (uint8_t)(t71 == ((uint8_t)4ULL));
                    if (!t72) {
                        goto gm_lb12;
                    }
                    gm_b16_8 t73;
                    GM_ST(void *, (uint8_t *)&t73, (void *)(gm_s2uphqpvb2vhh));
                    GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(3));
                    gm_l10 = t73;
                    goto gm_lb7;
                }
gm_lb12:;
                {
                    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t75 = (uint8_t)(t74 == ((uint8_t)5ULL));
                    if (!t75) {
                        goto gm_lb13;
                    }
                    gm_b16_8 t76;
                    GM_ST(void *, (uint8_t *)&t76, (void *)(gm_snaq7hvenqsum));
                    GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(3));
                    gm_l10 = t76;
                    goto gm_lb7;
                }
gm_lb13:;
                {
                    uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t78 = (uint8_t)(t77 == ((uint8_t)6ULL));
                    if (!t78) {
                        goto gm_lb14;
                    }
                    gm_b16_8 t79;
                    GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sc24ftfhihd1h));
                    GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(3));
                    gm_l10 = t79;
                    goto gm_lb7;
                }
gm_lb14:;
                {
                    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t81 = (uint8_t)(t80 == ((uint8_t)7ULL));
                    if (!t81) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t82;
                    GM_ST(void *, (uint8_t *)&t82, (void *)(gm_snbhl8dsqq3ln));
                    GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(6));
                    gm_l10 = t82;
                    goto gm_lb7;
                }
gm_lb15:;
                {
                    uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t84 = (uint8_t)(t83 == ((uint8_t)8ULL));
                    if (!t84) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t85;
                    GM_ST(void *, (uint8_t *)&t85, (void *)(gm_snq4uaiieftl8));
                    GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(5));
                    gm_l10 = t85;
                    goto gm_lb7;
                }
gm_lb16:;
                {
                    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t87 = (uint8_t)(t86 == ((uint8_t)9ULL));
                    if (!t87) {
                        goto gm_lb17;
                    }
                    gm_b16_8 t88;
                    GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sel2c30qvm74f));
                    GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(6));
                    gm_l10 = t88;
                    goto gm_lb7;
                }
gm_lb17:;
                {
                    uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t90 = (uint8_t)(t89 == ((uint8_t)10ULL));
                    if (!t90) {
                        goto gm_lb18;
                    }
                    gm_b16_8 t91;
                    GM_ST(void *, (uint8_t *)&t91, (void *)(gm_sqrv416075ito));
                    GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(2));
                    gm_l10 = t91;
                    goto gm_lb7;
                }
gm_lb18:;
                {
                    uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t93 = (uint8_t)(t92 == ((uint8_t)11ULL));
                    if (!t93) {
                        goto gm_lb19;
                    }
                    gm_b16_8 t94;
                    GM_ST(void *, (uint8_t *)&t94, (void *)(gm_s1n20vngt0mkv));
                    GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(2));
                    gm_l10 = t94;
                    goto gm_lb7;
                }
gm_lb19:;
                {
                    uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t96 = (uint8_t)(t95 == ((uint8_t)12ULL));
                    if (!t96) {
                        goto gm_lb20;
                    }
                    gm_b16_8 t97;
                    GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sp4uhnkunj2cg));
                    GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(2));
                    gm_l10 = t97;
                    goto gm_lb7;
                }
gm_lb20:;
                {
                    uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t99 = (uint8_t)(t98 == ((uint8_t)13ULL));
                    if (!t99) {
                        goto gm_lb21;
                    }
                    gm_b16_8 t100;
                    GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sdg7bdfivjinb));
                    GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(2));
                    gm_l10 = t100;
                    goto gm_lb7;
                }
gm_lb21:;
                gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 438, 10);
            }
gm_lb7:;
            gm_b16_8 t101 = gm_l10;
            gm_l5 = t101;
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 433, 58);
    }
gm_lb1:;
    gm_b16_8 t102 = gm_l5;
    F.ret = t102;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_builtin_text_od38u7h(void *env, gm_node *cx, gm_node *inst, gm_tvdg94bk7aa22 a0) {
    struct gm_fr_builtin_text_od38u7h F;
    gm_tvdg94bk7aa22 gm_l0; /* op */
    gm_t9qkh0rsooc8d gm_l1; /* f */
    gm_t9qkh0rsooc8d gm_l2; /* r */
    gm_t9qkh0rsooc8d gm_l3; /* s */
    uint32_t gm_l4; /* outputs */
    uint32_t gm_l5; /* inputs */
    uint32_t gm_l6; /* id */
    uint32_t gm_l7; /* id */
    GM_STACK_CHECK(&F, 2107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_b16_8 gm_l15;
    {
        {
            {
                {
                    uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t94 = (uint8_t)(t93 == ((uint8_t)21ULL));
                    if (!t94) {
                        goto gm_lb2;
                    }
                    gm_t9qkh0rsooc8d t95 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 4));
                    gm_l1 = t95;
                    gm_t9qkh0rsooc8d t96 = gm_l1;
                    gm_b16_8 t97 = gm_f_flavor_text_3ffnbt7(t96);
                    gm_tnpifv20irnph t98;
                    GM_ST(gm_b16_8, ((uint8_t *)&t98 + 0), t97);
                    gm_f_format_qcp7nf9((void *)&gm_l9, (void *)NULL, cx, NULL, t98);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                    if (!t99) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srco4ljquuk5d87, 592, 16);
                    gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                    gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
                    gm_l8 = t101;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t103 = (uint8_t)(t102 == ((uint8_t)68ULL));
                    if (!t103) {
                        goto gm_lb3;
                    }
                    gm_t9qkh0rsooc8d t104 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 4));
                    gm_l2 = t104;
                    gm_t9qkh0rsooc8d t105 = gm_l2;
                    gm_b16_8 t106 = gm_f_rmw_text_te4ol3u(t105);
                    gm_tnpifv20irnph t107;
                    GM_ST(gm_b16_8, ((uint8_t *)&t107 + 0), t106);
                    gm_f_format_aq84o6u((void *)&gm_l10, (void *)NULL, cx, NULL, t107);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t108 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                    if (!t108) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srco4ljquuk5d87, 593, 22);
                    gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                    gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
                    gm_l8 = t110;
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t112 = (uint8_t)(t111 == ((uint8_t)72ULL));
                    if (!t112) {
                        goto gm_lb4;
                    }
                    gm_t9qkh0rsooc8d t113 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l0) + 4));
                    gm_l3 = t113;
                    gm_t9qkh0rsooc8d t114 = gm_l3;
                    gm_b16_8 t115 = gm_f_sys_text_9qjnpra(t114);
                    gm_tnpifv20irnph t116;
                    GM_ST(gm_b16_8, ((uint8_t *)&t116 + 0), t115);
                    gm_f_format_556kaa4((void *)&gm_l11, (void *)NULL, cx, NULL, t116);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                    if (!t117) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srco4ljquuk5d87, 594, 16);
                    gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                    gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
                    gm_l8 = t119;
                    goto gm_lb1;
                }
gm_lb4:;
                {
                    uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t121 = (uint8_t)(t120 == ((uint8_t)79ULL));
                    if (!t121) {
                        goto gm_lb5;
                    }
                    uint32_t t122 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
                    gm_l4 = t122;
                    uint32_t t123 = GM_LD(uint32_t, ((((uint8_t *)&gm_l0) + 4) + 4));
                    gm_l5 = t123;
                    uint32_t t124 = gm_l4;
                    uint32_t t125 = gm_l5;
                    gm_tcc1rq4n1avb2 t126;
                    GM_ST(uint32_t, ((uint8_t *)&t126 + 0), t124);
                    GM_ST(uint32_t, ((uint8_t *)&t126 + 4), t125);
                    gm_f_format_c2ann39((void *)&gm_l12, (void *)NULL, cx, NULL, t126);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                    if (!t127) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srco4ljquuk5d87, 595, 30);
                    gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                    gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                    gm_l8 = t129;
                    goto gm_lb1;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t131 = (uint8_t)(t130 == ((uint8_t)80ULL));
                    if (!t131) {
                        goto gm_lb6;
                    }
                    uint32_t t132 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
                    gm_l6 = t132;
                    uint32_t t133 = gm_l6;
                    gm_t88rcucg1b7uk t134;
                    GM_ST(uint32_t, ((uint8_t *)&t134 + 0), t133);
                    gm_f_format_9g7fct9((void *)&gm_l13, (void *)NULL, cx, NULL, t134);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t135 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                    if (!t135) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srco4ljquuk5d87, 596, 26);
                    gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                    gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
                    gm_l8 = t137;
                    goto gm_lb1;
                }
gm_lb6:;
                {
                    uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t139 = (uint8_t)(t138 == ((uint8_t)81ULL));
                    if (!t139) {
                        goto gm_lb7;
                    }
                    uint32_t t140 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
                    gm_l7 = t140;
                    uint32_t t141 = gm_l7;
                    gm_t88rcucg1b7uk t142;
                    GM_ST(uint32_t, ((uint8_t *)&t142 + 0), t141);
                    gm_f_format_ree2v5u((void *)&gm_l14, (void *)NULL, cx, NULL, t142);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t143 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                    if (!t143) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srco4ljquuk5d87, 597, 19);
                    gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                    gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
                    gm_l8 = t145;
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    {
                        {
                            {
                                {
                                    uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t147 = (uint8_t)(t146 == ((uint8_t)0ULL));
                                    if (!t147) {
                                        goto gm_lb10;
                                    }
                                    gm_b16_8 t148;
                                    GM_ST(void *, (uint8_t *)&t148, (void *)(gm_sivsapeh482sm));
                                    GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(7));
                                    gm_l15 = t148;
                                    goto gm_lb9;
                                }
gm_lb10:;
                                {
                                    uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t150 = (uint8_t)(t149 == ((uint8_t)1ULL));
                                    if (!t150) {
                                        goto gm_lb11;
                                    }
                                    gm_b16_8 t151;
                                    GM_ST(void *, (uint8_t *)&t151, (void *)(gm_sk2b59ovo43qg));
                                    GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(5));
                                    gm_l15 = t151;
                                    goto gm_lb9;
                                }
gm_lb11:;
                                {
                                    uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t153 = (uint8_t)(t152 == ((uint8_t)2ULL));
                                    if (!t153) {
                                        goto gm_lb12;
                                    }
                                    gm_b16_8 t154;
                                    GM_ST(void *, (uint8_t *)&t154, (void *)(gm_s9dgf2prvg715));
                                    GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(5));
                                    gm_l15 = t154;
                                    goto gm_lb9;
                                }
gm_lb12:;
                                {
                                    uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t156 = (uint8_t)(t155 == ((uint8_t)3ULL));
                                    if (!t156) {
                                        goto gm_lb13;
                                    }
                                    gm_b16_8 t157;
                                    GM_ST(void *, (uint8_t *)&t157, (void *)(gm_scb853lmgr58k));
                                    GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(5));
                                    gm_l15 = t157;
                                    goto gm_lb9;
                                }
gm_lb13:;
                                {
                                    uint8_t t158 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t159 = (uint8_t)(t158 == ((uint8_t)4ULL));
                                    if (!t159) {
                                        goto gm_lb14;
                                    }
                                    gm_b16_8 t160;
                                    GM_ST(void *, (uint8_t *)&t160, (void *)(gm_seeo26ergtcha));
                                    GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(4));
                                    gm_l15 = t160;
                                    goto gm_lb9;
                                }
gm_lb14:;
                                {
                                    uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t162 = (uint8_t)(t161 == ((uint8_t)5ULL));
                                    if (!t162) {
                                        goto gm_lb15;
                                    }
                                    gm_b16_8 t163;
                                    GM_ST(void *, (uint8_t *)&t163, (void *)(gm_sdft8ov6bb7s6));
                                    GM_ST(uintptr_t, (uint8_t *)&t163 + 8, (uintptr_t)(7));
                                    gm_l15 = t163;
                                    goto gm_lb9;
                                }
gm_lb15:;
                                {
                                    uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t165 = (uint8_t)(t164 == ((uint8_t)6ULL));
                                    if (!t165) {
                                        goto gm_lb16;
                                    }
                                    gm_b16_8 t166;
                                    GM_ST(void *, (uint8_t *)&t166, (void *)(gm_sv3olmne7h13s));
                                    GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(3));
                                    gm_l15 = t166;
                                    goto gm_lb9;
                                }
gm_lb16:;
                                {
                                    uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t168 = (uint8_t)(t167 == ((uint8_t)7ULL));
                                    if (!t168) {
                                        goto gm_lb17;
                                    }
                                    gm_b16_8 t169;
                                    GM_ST(void *, (uint8_t *)&t169, (void *)(gm_skapbb3maknat));
                                    GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(4));
                                    gm_l15 = t169;
                                    goto gm_lb9;
                                }
gm_lb17:;
                                {
                                    uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t171 = (uint8_t)(t170 == ((uint8_t)8ULL));
                                    if (!t171) {
                                        goto gm_lb18;
                                    }
                                    gm_b16_8 t172;
                                    GM_ST(void *, (uint8_t *)&t172, (void *)(gm_scd599tsljaee));
                                    GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(4));
                                    gm_l15 = t172;
                                    goto gm_lb9;
                                }
gm_lb18:;
                                {
                                    uint8_t t173 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t174 = (uint8_t)(t173 == ((uint8_t)9ULL));
                                    if (!t174) {
                                        goto gm_lb19;
                                    }
                                    gm_b16_8 t175;
                                    GM_ST(void *, (uint8_t *)&t175, (void *)(gm_s2kmbpuutp9qq));
                                    GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(4));
                                    gm_l15 = t175;
                                    goto gm_lb9;
                                }
gm_lb19:;
                                {
                                    uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t177 = (uint8_t)(t176 == ((uint8_t)10ULL));
                                    if (!t177) {
                                        goto gm_lb20;
                                    }
                                    gm_b16_8 t178;
                                    GM_ST(void *, (uint8_t *)&t178, (void *)(gm_s7u6ij52clodh));
                                    GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(6));
                                    gm_l15 = t178;
                                    goto gm_lb9;
                                }
gm_lb20:;
                            }
                            {
                                {
                                    uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t180 = (uint8_t)(t179 == ((uint8_t)11ULL));
                                    if (!t180) {
                                        goto gm_lb21;
                                    }
                                    gm_b16_8 t181;
                                    GM_ST(void *, (uint8_t *)&t181, (void *)(gm_sun4qq3op5opf));
                                    GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(6));
                                    gm_l15 = t181;
                                    goto gm_lb9;
                                }
gm_lb21:;
                                {
                                    uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t183 = (uint8_t)(t182 == ((uint8_t)12ULL));
                                    if (!t183) {
                                        goto gm_lb22;
                                    }
                                    gm_b16_8 t184;
                                    GM_ST(void *, (uint8_t *)&t184, (void *)(gm_s2h8cr50573go));
                                    GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(7));
                                    gm_l15 = t184;
                                    goto gm_lb9;
                                }
gm_lb22:;
                                {
                                    uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t186 = (uint8_t)(t185 == ((uint8_t)13ULL));
                                    if (!t186) {
                                        goto gm_lb23;
                                    }
                                    gm_b16_8 t187;
                                    GM_ST(void *, (uint8_t *)&t187, (void *)(gm_s00pihipe0jga));
                                    GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(5));
                                    gm_l15 = t187;
                                    goto gm_lb9;
                                }
gm_lb23:;
                                {
                                    uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t189 = (uint8_t)(t188 == ((uint8_t)14ULL));
                                    if (!t189) {
                                        goto gm_lb24;
                                    }
                                    gm_b16_8 t190;
                                    GM_ST(void *, (uint8_t *)&t190, (void *)(gm_s3opc5ivp6e66));
                                    GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(3));
                                    gm_l15 = t190;
                                    goto gm_lb9;
                                }
gm_lb24:;
                                {
                                    uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t192 = (uint8_t)(t191 == ((uint8_t)15ULL));
                                    if (!t192) {
                                        goto gm_lb25;
                                    }
                                    gm_b16_8 t193;
                                    GM_ST(void *, (uint8_t *)&t193, (void *)(gm_sj1m58fkg956m));
                                    GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(7));
                                    gm_l15 = t193;
                                    goto gm_lb9;
                                }
gm_lb25:;
                                {
                                    uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t195 = (uint8_t)(t194 == ((uint8_t)16ULL));
                                    if (!t195) {
                                        goto gm_lb26;
                                    }
                                    gm_b16_8 t196;
                                    GM_ST(void *, (uint8_t *)&t196, (void *)(gm_s187rb18i1kgk));
                                    GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(3));
                                    gm_l15 = t196;
                                    goto gm_lb9;
                                }
gm_lb26:;
                                {
                                    uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t198 = (uint8_t)(t197 == ((uint8_t)17ULL));
                                    if (!t198) {
                                        goto gm_lb27;
                                    }
                                    gm_b16_8 t199;
                                    GM_ST(void *, (uint8_t *)&t199, (void *)(gm_sgo00f0mafbnd));
                                    GM_ST(uintptr_t, (uint8_t *)&t199 + 8, (uintptr_t)(3));
                                    gm_l15 = t199;
                                    goto gm_lb9;
                                }
gm_lb27:;
                                {
                                    uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t201 = (uint8_t)(t200 == ((uint8_t)18ULL));
                                    if (!t201) {
                                        goto gm_lb28;
                                    }
                                    gm_b16_8 t202;
                                    GM_ST(void *, (uint8_t *)&t202, (void *)(gm_s5udr3eget84q));
                                    GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(5));
                                    gm_l15 = t202;
                                    goto gm_lb9;
                                }
gm_lb28:;
                                {
                                    uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t204 = (uint8_t)(t203 == ((uint8_t)19ULL));
                                    if (!t204) {
                                        goto gm_lb29;
                                    }
                                    gm_b16_8 t205;
                                    GM_ST(void *, (uint8_t *)&t205, (void *)(gm_sfd0mn3sol25u));
                                    GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(3));
                                    gm_l15 = t205;
                                    goto gm_lb9;
                                }
gm_lb29:;
                                {
                                    uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t207 = (uint8_t)(t206 == ((uint8_t)20ULL));
                                    if (!t207) {
                                        goto gm_lb30;
                                    }
                                    gm_b16_8 t208;
                                    GM_ST(void *, (uint8_t *)&t208, (void *)(gm_s85hqf14lbkns));
                                    GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(7));
                                    gm_l15 = t208;
                                    goto gm_lb9;
                                }
gm_lb30:;
                                {
                                    uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t210 = (uint8_t)(t209 == ((uint8_t)21ULL));
                                    if (!t210) {
                                        goto gm_lb31;
                                    }
                                    gm_b16_8 t211;
                                    GM_ST(void *, (uint8_t *)&t211, (void *)(gm_s6qbnfe2f321h));
                                    GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(3));
                                    gm_l15 = t211;
                                    goto gm_lb9;
                                }
gm_lb31:;
                            }
                            {
                                {
                                    uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t213 = (uint8_t)(t212 == ((uint8_t)22ULL));
                                    if (!t213) {
                                        goto gm_lb32;
                                    }
                                    gm_b16_8 t214;
                                    GM_ST(void *, (uint8_t *)&t214, (void *)(gm_sds03accef1j8));
                                    GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(5));
                                    gm_l15 = t214;
                                    goto gm_lb9;
                                }
gm_lb32:;
                                {
                                    uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t216 = (uint8_t)(t215 == ((uint8_t)23ULL));
                                    if (!t216) {
                                        goto gm_lb33;
                                    }
                                    gm_b16_8 t217;
                                    GM_ST(void *, (uint8_t *)&t217, (void *)(gm_s24chpb914bep));
                                    GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(5));
                                    gm_l15 = t217;
                                    goto gm_lb9;
                                }
gm_lb33:;
                                {
                                    uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t219 = (uint8_t)(t218 == ((uint8_t)24ULL));
                                    if (!t219) {
                                        goto gm_lb34;
                                    }
                                    gm_b16_8 t220;
                                    GM_ST(void *, (uint8_t *)&t220, (void *)(gm_su3ahsl9p584f));
                                    GM_ST(uintptr_t, (uint8_t *)&t220 + 8, (uintptr_t)(8));
                                    gm_l15 = t220;
                                    goto gm_lb9;
                                }
gm_lb34:;
                                {
                                    uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t222 = (uint8_t)(t221 == ((uint8_t)25ULL));
                                    if (!t222) {
                                        goto gm_lb35;
                                    }
                                    gm_b16_8 t223;
                                    GM_ST(void *, (uint8_t *)&t223, (void *)(gm_s28ijm46j20ds));
                                    GM_ST(uintptr_t, (uint8_t *)&t223 + 8, (uintptr_t)(7));
                                    gm_l15 = t223;
                                    goto gm_lb9;
                                }
gm_lb35:;
                                {
                                    uint8_t t224 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t225 = (uint8_t)(t224 == ((uint8_t)26ULL));
                                    if (!t225) {
                                        goto gm_lb36;
                                    }
                                    gm_b16_8 t226;
                                    GM_ST(void *, (uint8_t *)&t226, (void *)(gm_sv8lqdk5nsv8i));
                                    GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(4));
                                    gm_l15 = t226;
                                    goto gm_lb9;
                                }
gm_lb36:;
                                {
                                    uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t228 = (uint8_t)(t227 == ((uint8_t)27ULL));
                                    if (!t228) {
                                        goto gm_lb37;
                                    }
                                    gm_b16_8 t229;
                                    GM_ST(void *, (uint8_t *)&t229, (void *)(gm_s1q9ms0kviddj));
                                    GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(3));
                                    gm_l15 = t229;
                                    goto gm_lb9;
                                }
gm_lb37:;
                                {
                                    uint8_t t230 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t231 = (uint8_t)(t230 == ((uint8_t)28ULL));
                                    if (!t231) {
                                        goto gm_lb38;
                                    }
                                    gm_b16_8 t232;
                                    GM_ST(void *, (uint8_t *)&t232, (void *)(gm_s2nl2e5anglov));
                                    GM_ST(uintptr_t, (uint8_t *)&t232 + 8, (uintptr_t)(3));
                                    gm_l15 = t232;
                                    goto gm_lb9;
                                }
gm_lb38:;
                                {
                                    uint8_t t233 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t234 = (uint8_t)(t233 == ((uint8_t)29ULL));
                                    if (!t234) {
                                        goto gm_lb39;
                                    }
                                    gm_b16_8 t235;
                                    GM_ST(void *, (uint8_t *)&t235, (void *)(gm_s0kvnv9teor3q));
                                    GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(3));
                                    gm_l15 = t235;
                                    goto gm_lb9;
                                }
gm_lb39:;
                                {
                                    uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t237 = (uint8_t)(t236 == ((uint8_t)30ULL));
                                    if (!t237) {
                                        goto gm_lb40;
                                    }
                                    gm_b16_8 t238;
                                    GM_ST(void *, (uint8_t *)&t238, (void *)(gm_solj59tk4ri3k));
                                    GM_ST(uintptr_t, (uint8_t *)&t238 + 8, (uintptr_t)(8));
                                    gm_l15 = t238;
                                    goto gm_lb9;
                                }
gm_lb40:;
                                {
                                    uint8_t t239 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t240 = (uint8_t)(t239 == ((uint8_t)31ULL));
                                    if (!t240) {
                                        goto gm_lb41;
                                    }
                                    gm_b16_8 t241;
                                    GM_ST(void *, (uint8_t *)&t241, (void *)(gm_sqhhm0h7b3asd));
                                    GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(5));
                                    gm_l15 = t241;
                                    goto gm_lb9;
                                }
gm_lb41:;
                                {
                                    uint8_t t242 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t243 = (uint8_t)(t242 == ((uint8_t)32ULL));
                                    if (!t243) {
                                        goto gm_lb42;
                                    }
                                    gm_b16_8 t244;
                                    GM_ST(void *, (uint8_t *)&t244, (void *)(gm_s3ng5nguvd452));
                                    GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)(4));
                                    gm_l15 = t244;
                                    goto gm_lb9;
                                }
gm_lb42:;
                            }
                            {
                                {
                                    uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t246 = (uint8_t)(t245 == ((uint8_t)33ULL));
                                    if (!t246) {
                                        goto gm_lb43;
                                    }
                                    gm_b16_8 t247;
                                    GM_ST(void *, (uint8_t *)&t247, (void *)(gm_sjbl17pjsjm48));
                                    GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(4));
                                    gm_l15 = t247;
                                    goto gm_lb9;
                                }
gm_lb43:;
                                {
                                    uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t249 = (uint8_t)(t248 == ((uint8_t)34ULL));
                                    if (!t249) {
                                        goto gm_lb44;
                                    }
                                    gm_b16_8 t250;
                                    GM_ST(void *, (uint8_t *)&t250, (void *)(gm_sl37uvr16qsu8));
                                    GM_ST(uintptr_t, (uint8_t *)&t250 + 8, (uintptr_t)(10));
                                    gm_l15 = t250;
                                    goto gm_lb9;
                                }
gm_lb44:;
                                {
                                    uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t252 = (uint8_t)(t251 == ((uint8_t)35ULL));
                                    if (!t252) {
                                        goto gm_lb45;
                                    }
                                    gm_b16_8 t253;
                                    GM_ST(void *, (uint8_t *)&t253, (void *)(gm_s1q2nkavemrqf));
                                    GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(8));
                                    gm_l15 = t253;
                                    goto gm_lb9;
                                }
gm_lb45:;
                                {
                                    uint8_t t254 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t255 = (uint8_t)(t254 == ((uint8_t)36ULL));
                                    if (!t255) {
                                        goto gm_lb46;
                                    }
                                    gm_b16_8 t256;
                                    GM_ST(void *, (uint8_t *)&t256, (void *)(gm_s2a9t8smkevg0));
                                    GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(3));
                                    gm_l15 = t256;
                                    goto gm_lb9;
                                }
gm_lb46:;
                                {
                                    uint8_t t257 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t258 = (uint8_t)(t257 == ((uint8_t)37ULL));
                                    if (!t258) {
                                        goto gm_lb47;
                                    }
                                    gm_b16_8 t259;
                                    GM_ST(void *, (uint8_t *)&t259, (void *)(gm_sa1pcp92k3aj3));
                                    GM_ST(uintptr_t, (uint8_t *)&t259 + 8, (uintptr_t)(6));
                                    gm_l15 = t259;
                                    goto gm_lb9;
                                }
gm_lb47:;
                                {
                                    uint8_t t260 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t261 = (uint8_t)(t260 == ((uint8_t)38ULL));
                                    if (!t261) {
                                        goto gm_lb48;
                                    }
                                    gm_b16_8 t262;
                                    GM_ST(void *, (uint8_t *)&t262, (void *)(gm_shkq4botdoed7));
                                    GM_ST(uintptr_t, (uint8_t *)&t262 + 8, (uintptr_t)(5));
                                    gm_l15 = t262;
                                    goto gm_lb9;
                                }
gm_lb48:;
                                {
                                    uint8_t t263 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t264 = (uint8_t)(t263 == ((uint8_t)39ULL));
                                    if (!t264) {
                                        goto gm_lb49;
                                    }
                                    gm_b16_8 t265;
                                    GM_ST(void *, (uint8_t *)&t265, (void *)(gm_sse9dgkusbtnf));
                                    GM_ST(uintptr_t, (uint8_t *)&t265 + 8, (uintptr_t)(4));
                                    gm_l15 = t265;
                                    goto gm_lb9;
                                }
gm_lb49:;
                                {
                                    uint8_t t266 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t267 = (uint8_t)(t266 == ((uint8_t)40ULL));
                                    if (!t267) {
                                        goto gm_lb50;
                                    }
                                    gm_b16_8 t268;
                                    GM_ST(void *, (uint8_t *)&t268, (void *)(gm_si4k9tiofr8t2));
                                    GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(6));
                                    gm_l15 = t268;
                                    goto gm_lb9;
                                }
gm_lb50:;
                                {
                                    uint8_t t269 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t270 = (uint8_t)(t269 == ((uint8_t)41ULL));
                                    if (!t270) {
                                        goto gm_lb51;
                                    }
                                    gm_b16_8 t271;
                                    GM_ST(void *, (uint8_t *)&t271, (void *)(gm_s6aamtn65omr1));
                                    GM_ST(uintptr_t, (uint8_t *)&t271 + 8, (uintptr_t)(6));
                                    gm_l15 = t271;
                                    goto gm_lb9;
                                }
gm_lb51:;
                                {
                                    uint8_t t272 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t273 = (uint8_t)(t272 == ((uint8_t)42ULL));
                                    if (!t273) {
                                        goto gm_lb52;
                                    }
                                    gm_b16_8 t274;
                                    GM_ST(void *, (uint8_t *)&t274, (void *)(gm_s8s6shfnaqahd));
                                    GM_ST(uintptr_t, (uint8_t *)&t274 + 8, (uintptr_t)(6));
                                    gm_l15 = t274;
                                    goto gm_lb9;
                                }
gm_lb52:;
                                {
                                    uint8_t t275 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t276 = (uint8_t)(t275 == ((uint8_t)43ULL));
                                    if (!t276) {
                                        goto gm_lb53;
                                    }
                                    gm_b16_8 t277;
                                    GM_ST(void *, (uint8_t *)&t277, (void *)(gm_sbnms71r2jqr5));
                                    GM_ST(uintptr_t, (uint8_t *)&t277 + 8, (uintptr_t)(6));
                                    gm_l15 = t277;
                                    goto gm_lb9;
                                }
gm_lb53:;
                            }
                            {
                                {
                                    uint8_t t278 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t279 = (uint8_t)(t278 == ((uint8_t)44ULL));
                                    if (!t279) {
                                        goto gm_lb54;
                                    }
                                    gm_b16_8 t280;
                                    GM_ST(void *, (uint8_t *)&t280, (void *)(gm_ss27a20sarlvg));
                                    GM_ST(uintptr_t, (uint8_t *)&t280 + 8, (uintptr_t)(6));
                                    gm_l15 = t280;
                                    goto gm_lb9;
                                }
gm_lb54:;
                                {
                                    uint8_t t281 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t282 = (uint8_t)(t281 == ((uint8_t)45ULL));
                                    if (!t282) {
                                        goto gm_lb55;
                                    }
                                    gm_b16_8 t283;
                                    GM_ST(void *, (uint8_t *)&t283, (void *)(gm_sbek54n4a6h2o));
                                    GM_ST(uintptr_t, (uint8_t *)&t283 + 8, (uintptr_t)(6));
                                    gm_l15 = t283;
                                    goto gm_lb9;
                                }
gm_lb55:;
                                {
                                    uint8_t t284 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t285 = (uint8_t)(t284 == ((uint8_t)46ULL));
                                    if (!t285) {
                                        goto gm_lb56;
                                    }
                                    gm_b16_8 t286;
                                    GM_ST(void *, (uint8_t *)&t286, (void *)(gm_std59a9b6lpnn));
                                    GM_ST(uintptr_t, (uint8_t *)&t286 + 8, (uintptr_t)(9));
                                    gm_l15 = t286;
                                    goto gm_lb9;
                                }
gm_lb56:;
                                {
                                    uint8_t t287 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t288 = (uint8_t)(t287 == ((uint8_t)47ULL));
                                    if (!t288) {
                                        goto gm_lb57;
                                    }
                                    gm_b16_8 t289;
                                    GM_ST(void *, (uint8_t *)&t289, (void *)(gm_ssjmv3618n779));
                                    GM_ST(uintptr_t, (uint8_t *)&t289 + 8, (uintptr_t)(9));
                                    gm_l15 = t289;
                                    goto gm_lb9;
                                }
gm_lb57:;
                                {
                                    uint8_t t290 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t291 = (uint8_t)(t290 == ((uint8_t)48ULL));
                                    if (!t291) {
                                        goto gm_lb58;
                                    }
                                    gm_b16_8 t292;
                                    GM_ST(void *, (uint8_t *)&t292, (void *)(gm_smuec3qfsdh82));
                                    GM_ST(uintptr_t, (uint8_t *)&t292 + 8, (uintptr_t)(9));
                                    gm_l15 = t292;
                                    goto gm_lb9;
                                }
gm_lb58:;
                                {
                                    uint8_t t293 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t294 = (uint8_t)(t293 == ((uint8_t)49ULL));
                                    if (!t294) {
                                        goto gm_lb59;
                                    }
                                    gm_b16_8 t295;
                                    GM_ST(void *, (uint8_t *)&t295, (void *)(gm_s57rgvjb5mj5a));
                                    GM_ST(uintptr_t, (uint8_t *)&t295 + 8, (uintptr_t)(7));
                                    gm_l15 = t295;
                                    goto gm_lb9;
                                }
gm_lb59:;
                                {
                                    uint8_t t296 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t297 = (uint8_t)(t296 == ((uint8_t)50ULL));
                                    if (!t297) {
                                        goto gm_lb60;
                                    }
                                    gm_b16_8 t298;
                                    GM_ST(void *, (uint8_t *)&t298, (void *)(gm_s76ekntop342o));
                                    GM_ST(uintptr_t, (uint8_t *)&t298 + 8, (uintptr_t)(7));
                                    gm_l15 = t298;
                                    goto gm_lb9;
                                }
gm_lb60:;
                                {
                                    uint8_t t299 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t300 = (uint8_t)(t299 == ((uint8_t)51ULL));
                                    if (!t300) {
                                        goto gm_lb61;
                                    }
                                    gm_b16_8 t301;
                                    GM_ST(void *, (uint8_t *)&t301, (void *)(gm_sm3dios51ece1));
                                    GM_ST(uintptr_t, (uint8_t *)&t301 + 8, (uintptr_t)(9));
                                    gm_l15 = t301;
                                    goto gm_lb9;
                                }
gm_lb61:;
                                {
                                    uint8_t t302 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t303 = (uint8_t)(t302 == ((uint8_t)52ULL));
                                    if (!t303) {
                                        goto gm_lb62;
                                    }
                                    gm_b16_8 t304;
                                    GM_ST(void *, (uint8_t *)&t304, (void *)(gm_s9h93c7asacna));
                                    GM_ST(uintptr_t, (uint8_t *)&t304 + 8, (uintptr_t)(6));
                                    gm_l15 = t304;
                                    goto gm_lb9;
                                }
gm_lb62:;
                                {
                                    uint8_t t305 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t306 = (uint8_t)(t305 == ((uint8_t)53ULL));
                                    if (!t306) {
                                        goto gm_lb63;
                                    }
                                    gm_b16_8 t307;
                                    GM_ST(void *, (uint8_t *)&t307, (void *)(gm_s8pk5tcnrk6p2));
                                    GM_ST(uintptr_t, (uint8_t *)&t307 + 8, (uintptr_t)(7));
                                    gm_l15 = t307;
                                    goto gm_lb9;
                                }
gm_lb63:;
                                {
                                    uint8_t t308 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t309 = (uint8_t)(t308 == ((uint8_t)54ULL));
                                    if (!t309) {
                                        goto gm_lb64;
                                    }
                                    gm_b16_8 t310;
                                    GM_ST(void *, (uint8_t *)&t310, (void *)(gm_svqq3k9j6h1lu));
                                    GM_ST(uintptr_t, (uint8_t *)&t310 + 8, (uintptr_t)(7));
                                    gm_l15 = t310;
                                    goto gm_lb9;
                                }
gm_lb64:;
                            }
                            {
                                {
                                    uint8_t t311 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t312 = (uint8_t)(t311 == ((uint8_t)55ULL));
                                    if (!t312) {
                                        goto gm_lb65;
                                    }
                                    gm_b16_8 t313;
                                    GM_ST(void *, (uint8_t *)&t313, (void *)(gm_sdh1dnevn1j10));
                                    GM_ST(uintptr_t, (uint8_t *)&t313 + 8, (uintptr_t)(7));
                                    gm_l15 = t313;
                                    goto gm_lb9;
                                }
gm_lb65:;
                                {
                                    uint8_t t314 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t315 = (uint8_t)(t314 == ((uint8_t)56ULL));
                                    if (!t315) {
                                        goto gm_lb66;
                                    }
                                    gm_b16_8 t316;
                                    GM_ST(void *, (uint8_t *)&t316, (void *)(gm_sij2r6jhpquej));
                                    GM_ST(uintptr_t, (uint8_t *)&t316 + 8, (uintptr_t)(11));
                                    gm_l15 = t316;
                                    goto gm_lb9;
                                }
gm_lb66:;
                                {
                                    uint8_t t317 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t318 = (uint8_t)(t317 == ((uint8_t)57ULL));
                                    if (!t318) {
                                        goto gm_lb67;
                                    }
                                    gm_b16_8 t319;
                                    GM_ST(void *, (uint8_t *)&t319, (void *)(gm_stft4dc50tdce));
                                    GM_ST(uintptr_t, (uint8_t *)&t319 + 8, (uintptr_t)(7));
                                    gm_l15 = t319;
                                    goto gm_lb9;
                                }
gm_lb67:;
                                {
                                    uint8_t t320 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t321 = (uint8_t)(t320 == ((uint8_t)58ULL));
                                    if (!t321) {
                                        goto gm_lb68;
                                    }
                                    gm_b16_8 t322;
                                    GM_ST(void *, (uint8_t *)&t322, (void *)(gm_sa0alba1kin3k));
                                    GM_ST(uintptr_t, (uint8_t *)&t322 + 8, (uintptr_t)(7));
                                    gm_l15 = t322;
                                    goto gm_lb9;
                                }
gm_lb68:;
                                {
                                    uint8_t t323 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t324 = (uint8_t)(t323 == ((uint8_t)59ULL));
                                    if (!t324) {
                                        goto gm_lb69;
                                    }
                                    gm_b16_8 t325;
                                    GM_ST(void *, (uint8_t *)&t325, (void *)(gm_s9o8gb20t6jtp));
                                    GM_ST(uintptr_t, (uint8_t *)&t325 + 8, (uintptr_t)(8));
                                    gm_l15 = t325;
                                    goto gm_lb9;
                                }
gm_lb69:;
                                {
                                    uint8_t t326 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t327 = (uint8_t)(t326 == ((uint8_t)60ULL));
                                    if (!t327) {
                                        goto gm_lb70;
                                    }
                                    gm_b16_8 t328;
                                    GM_ST(void *, (uint8_t *)&t328, (void *)(gm_sh11qqm8nqu68));
                                    GM_ST(uintptr_t, (uint8_t *)&t328 + 8, (uintptr_t)(7));
                                    gm_l15 = t328;
                                    goto gm_lb9;
                                }
gm_lb70:;
                                {
                                    uint8_t t329 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t330 = (uint8_t)(t329 == ((uint8_t)61ULL));
                                    if (!t330) {
                                        goto gm_lb71;
                                    }
                                    gm_b16_8 t331;
                                    GM_ST(void *, (uint8_t *)&t331, (void *)(gm_s2fu5nkek0lrq));
                                    GM_ST(uintptr_t, (uint8_t *)&t331 + 8, (uintptr_t)(6));
                                    gm_l15 = t331;
                                    goto gm_lb9;
                                }
gm_lb71:;
                                {
                                    uint8_t t332 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t333 = (uint8_t)(t332 == ((uint8_t)62ULL));
                                    if (!t333) {
                                        goto gm_lb72;
                                    }
                                    gm_b16_8 t334;
                                    GM_ST(void *, (uint8_t *)&t334, (void *)(gm_s7i80akiq0d95));
                                    GM_ST(uintptr_t, (uint8_t *)&t334 + 8, (uintptr_t)(4));
                                    gm_l15 = t334;
                                    goto gm_lb9;
                                }
gm_lb72:;
                                {
                                    uint8_t t335 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t336 = (uint8_t)(t335 == ((uint8_t)63ULL));
                                    if (!t336) {
                                        goto gm_lb73;
                                    }
                                    gm_b16_8 t337;
                                    GM_ST(void *, (uint8_t *)&t337, (void *)(gm_stn52kuiq0jk2));
                                    GM_ST(uintptr_t, (uint8_t *)&t337 + 8, (uintptr_t)(11));
                                    gm_l15 = t337;
                                    goto gm_lb9;
                                }
gm_lb73:;
                                {
                                    uint8_t t338 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t339 = (uint8_t)(t338 == ((uint8_t)64ULL));
                                    if (!t339) {
                                        goto gm_lb74;
                                    }
                                    gm_b16_8 t340;
                                    GM_ST(void *, (uint8_t *)&t340, (void *)(gm_srjd0sq8k3d7s));
                                    GM_ST(uintptr_t, (uint8_t *)&t340 + 8, (uintptr_t)(10));
                                    gm_l15 = t340;
                                    goto gm_lb9;
                                }
gm_lb74:;
                                {
                                    uint8_t t341 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t342 = (uint8_t)(t341 == ((uint8_t)65ULL));
                                    if (!t342) {
                                        goto gm_lb75;
                                    }
                                    gm_b16_8 t343;
                                    GM_ST(void *, (uint8_t *)&t343, (void *)(gm_sdq6l7oddjqfe));
                                    GM_ST(uintptr_t, (uint8_t *)&t343 + 8, (uintptr_t)(11));
                                    gm_l15 = t343;
                                    goto gm_lb9;
                                }
gm_lb75:;
                            }
                            {
                                {
                                    uint8_t t344 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t345 = (uint8_t)(t344 == ((uint8_t)66ULL));
                                    if (!t345) {
                                        goto gm_lb76;
                                    }
                                    gm_b16_8 t346;
                                    GM_ST(void *, (uint8_t *)&t346, (void *)(gm_s6ipt60dh9o1j));
                                    GM_ST(uintptr_t, (uint8_t *)&t346 + 8, (uintptr_t)(10));
                                    gm_l15 = t346;
                                    goto gm_lb9;
                                }
gm_lb76:;
                                {
                                    uint8_t t347 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t348 = (uint8_t)(t347 == ((uint8_t)67ULL));
                                    if (!t348) {
                                        goto gm_lb77;
                                    }
                                    gm_b16_8 t349;
                                    GM_ST(void *, (uint8_t *)&t349, (void *)(gm_sa53epbg9iarq));
                                    GM_ST(uintptr_t, (uint8_t *)&t349 + 8, (uintptr_t)(9));
                                    gm_l15 = t349;
                                    goto gm_lb9;
                                }
gm_lb77:;
                                {
                                    uint8_t t350 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t351 = (uint8_t)(t350 == ((uint8_t)68ULL));
                                    if (!t351) {
                                        goto gm_lb78;
                                    }
                                    gm_b16_8 t352;
                                    GM_ST(void *, (uint8_t *)&t352, (void *)(gm_s2fdsodctojm1));
                                    GM_ST(uintptr_t, (uint8_t *)&t352 + 8, (uintptr_t)(9));
                                    gm_l15 = t352;
                                    goto gm_lb9;
                                }
gm_lb78:;
                                {
                                    uint8_t t353 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t354 = (uint8_t)(t353 == ((uint8_t)69ULL));
                                    if (!t354) {
                                        goto gm_lb79;
                                    }
                                    gm_b16_8 t355;
                                    GM_ST(void *, (uint8_t *)&t355, (void *)(gm_skbgm82aqj3q0));
                                    GM_ST(uintptr_t, (uint8_t *)&t355 + 8, (uintptr_t)(5));
                                    gm_l15 = t355;
                                    goto gm_lb9;
                                }
gm_lb79:;
                                {
                                    uint8_t t356 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t357 = (uint8_t)(t356 == ((uint8_t)70ULL));
                                    if (!t357) {
                                        goto gm_lb80;
                                    }
                                    gm_b16_8 t358;
                                    GM_ST(void *, (uint8_t *)&t358, (void *)(gm_sk88tp2337lf6));
                                    GM_ST(uintptr_t, (uint8_t *)&t358 + 8, (uintptr_t)(12));
                                    gm_l15 = t358;
                                    goto gm_lb9;
                                }
gm_lb80:;
                                {
                                    uint8_t t359 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t360 = (uint8_t)(t359 == ((uint8_t)71ULL));
                                    if (!t360) {
                                        goto gm_lb81;
                                    }
                                    gm_b16_8 t361;
                                    GM_ST(void *, (uint8_t *)&t361, (void *)(gm_st0l9v6pp5i7b));
                                    GM_ST(uintptr_t, (uint8_t *)&t361 + 8, (uintptr_t)(13));
                                    gm_l15 = t361;
                                    goto gm_lb9;
                                }
gm_lb81:;
                                {
                                    uint8_t t362 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t363 = (uint8_t)(t362 == ((uint8_t)72ULL));
                                    if (!t363) {
                                        goto gm_lb82;
                                    }
                                    gm_b16_8 t364;
                                    GM_ST(void *, (uint8_t *)&t364, (void *)(gm_sa902c1tbtg0i));
                                    GM_ST(uintptr_t, (uint8_t *)&t364 + 8, (uintptr_t)(3));
                                    gm_l15 = t364;
                                    goto gm_lb9;
                                }
gm_lb82:;
                                {
                                    uint8_t t365 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t366 = (uint8_t)(t365 == ((uint8_t)73ULL));
                                    if (!t366) {
                                        goto gm_lb83;
                                    }
                                    gm_b16_8 t367;
                                    GM_ST(void *, (uint8_t *)&t367, (void *)(gm_ss869lnvmj3m8));
                                    GM_ST(uintptr_t, (uint8_t *)&t367 + 8, (uintptr_t)(5));
                                    gm_l15 = t367;
                                    goto gm_lb9;
                                }
gm_lb83:;
                                {
                                    uint8_t t368 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t369 = (uint8_t)(t368 == ((uint8_t)74ULL));
                                    if (!t369) {
                                        goto gm_lb84;
                                    }
                                    gm_b16_8 t370;
                                    GM_ST(void *, (uint8_t *)&t370, (void *)(gm_s1j5448m6ruti));
                                    GM_ST(uintptr_t, (uint8_t *)&t370 + 8, (uintptr_t)(9));
                                    gm_l15 = t370;
                                    goto gm_lb9;
                                }
gm_lb84:;
                                {
                                    uint8_t t371 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t372 = (uint8_t)(t371 == ((uint8_t)75ULL));
                                    if (!t372) {
                                        goto gm_lb85;
                                    }
                                    gm_b16_8 t373;
                                    GM_ST(void *, (uint8_t *)&t373, (void *)(gm_src5778gpfoh9));
                                    GM_ST(uintptr_t, (uint8_t *)&t373 + 8, (uintptr_t)(8));
                                    gm_l15 = t373;
                                    goto gm_lb9;
                                }
gm_lb85:;
                                {
                                    uint8_t t374 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t375 = (uint8_t)(t374 == ((uint8_t)76ULL));
                                    if (!t375) {
                                        goto gm_lb86;
                                    }
                                    gm_b16_8 t376;
                                    GM_ST(void *, (uint8_t *)&t376, (void *)(gm_smpl91sh0k8v4));
                                    GM_ST(uintptr_t, (uint8_t *)&t376 + 8, (uintptr_t)(8));
                                    gm_l15 = t376;
                                    goto gm_lb9;
                                }
gm_lb86:;
                            }
                            {
                                {
                                    uint8_t t377 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t378 = (uint8_t)(t377 == ((uint8_t)77ULL));
                                    if (!t378) {
                                        goto gm_lb87;
                                    }
                                    gm_b16_8 t379;
                                    GM_ST(void *, (uint8_t *)&t379, (void *)(gm_s7rltjaeh634d));
                                    GM_ST(uintptr_t, (uint8_t *)&t379 + 8, (uintptr_t)(10));
                                    gm_l15 = t379;
                                    goto gm_lb9;
                                }
gm_lb87:;
                                {
                                    uint8_t t380 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t381 = (uint8_t)(t380 == ((uint8_t)78ULL));
                                    if (!t381) {
                                        goto gm_lb88;
                                    }
                                    gm_b16_8 t382;
                                    GM_ST(void *, (uint8_t *)&t382, (void *)(gm_s5c5548ll8taj));
                                    GM_ST(uintptr_t, (uint8_t *)&t382 + 8, (uintptr_t)(9));
                                    gm_l15 = t382;
                                    goto gm_lb9;
                                }
gm_lb88:;
                                {
                                    uint8_t t383 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t384 = (uint8_t)(t383 == ((uint8_t)79ULL));
                                    if (!t384) {
                                        goto gm_lb89;
                                    }
                                    gm_b16_8 t385;
                                    GM_ST(void *, (uint8_t *)&t385, (void *)(gm_seodvmll3gsjn));
                                    GM_ST(uintptr_t, (uint8_t *)&t385 + 8, (uintptr_t)(3));
                                    gm_l15 = t385;
                                    goto gm_lb9;
                                }
gm_lb89:;
                                {
                                    uint8_t t386 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t387 = (uint8_t)(t386 == ((uint8_t)80ULL));
                                    if (!t387) {
                                        goto gm_lb90;
                                    }
                                    gm_b16_8 t388;
                                    GM_ST(void *, (uint8_t *)&t388, (void *)(gm_st8ki5f85lc2j));
                                    GM_ST(uintptr_t, (uint8_t *)&t388 + 8, (uintptr_t)(12));
                                    gm_l15 = t388;
                                    goto gm_lb9;
                                }
gm_lb90:;
                                {
                                    uint8_t t389 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                    uint8_t t390 = (uint8_t)(t389 == ((uint8_t)81ULL));
                                    if (!t390) {
                                        goto gm_lb91;
                                    }
                                    gm_b16_8 t391;
                                    GM_ST(void *, (uint8_t *)&t391, (void *)(gm_sepu95c7o773o));
                                    GM_ST(uintptr_t, (uint8_t *)&t391 + 8, (uintptr_t)(5));
                                    gm_l15 = t391;
                                    goto gm_lb9;
                                }
gm_lb91:;
                                gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 598, 10);
                            }
                        }
gm_lb9:;
                    }
                    {
                        gm_b16_8 t392 = gm_l15;
                        gm_l8 = t392;
                        goto gm_lb1;
                    }
                }
gm_lb8:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 591, 62);
            }
        }
gm_lb1:;
    }
    {
        gm_b16_8 t393 = gm_l8;
        F.ret = t393;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_coercion_text_g32oppe(void *env, gm_node *cx, gm_node *inst, gm_tmn7n0n9raou5 a0) {
    struct gm_fr_coercion_text_g32oppe F;
    gm_tmn7n0n9raou5 gm_l0; /* c */
    uint32_t gm_l1; /* v */
    GM_STACK_CHECK(&F, 413u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l2;
    gm_tabb710d9mq2e gm_l3;
    gm_b16_8 gm_l4;
    {
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)4ULL));
            if (!t20) {
                goto gm_lb2;
            }
            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
            gm_l1 = t21;
            uint32_t t22 = gm_l1;
            gm_t88rcucg1b7uk t23;
            GM_ST(uint32_t, ((uint8_t *)&t23 + 0), t22);
            gm_f_format_41c1370((void *)&gm_l3, (void *)NULL, cx, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srco4ljquuk5d87, 444, 20);
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            gm_l2 = t26;
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)0ULL));
                    if (!t28) {
                        goto gm_lb5;
                    }
                    gm_b16_8 t29;
                    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_stivl0smu8eq0));
                    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(8));
                    gm_l4 = t29;
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)1ULL));
                    if (!t31) {
                        goto gm_lb6;
                    }
                    gm_b16_8 t32;
                    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s2uqljf8sumcn));
                    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(10));
                    gm_l4 = t32;
                    goto gm_lb4;
                }
gm_lb6:;
                {
                    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)2ULL));
                    if (!t34) {
                        goto gm_lb7;
                    }
                    gm_b16_8 t35;
                    GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sbckpj1jsdt28));
                    GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(12));
                    gm_l4 = t35;
                    goto gm_lb4;
                }
gm_lb7:;
                {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)3ULL));
                    if (!t37) {
                        goto gm_lb8;
                    }
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s6p9ub6gu603m));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(6));
                    gm_l4 = t38;
                    goto gm_lb4;
                }
gm_lb8:;
                {
                    uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t40 = (uint8_t)(t39 == ((uint8_t)4ULL));
                    if (!t40) {
                        goto gm_lb9;
                    }
                    gm_b16_8 t41;
                    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s4g3rq58567m6));
                    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(7));
                    gm_l4 = t41;
                    goto gm_lb4;
                }
gm_lb9:;
                {
                    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)5ULL));
                    if (!t43) {
                        goto gm_lb10;
                    }
                    gm_b16_8 t44;
                    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_stou1l3qp844f));
                    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(9));
                    gm_l4 = t44;
                    goto gm_lb4;
                }
gm_lb10:;
                {
                    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t46 = (uint8_t)(t45 == ((uint8_t)6ULL));
                    if (!t46) {
                        goto gm_lb11;
                    }
                    gm_b16_8 t47;
                    GM_ST(void *, (uint8_t *)&t47, (void *)(gm_st1pc9bsku00q));
                    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(8));
                    gm_l4 = t47;
                    goto gm_lb4;
                }
gm_lb11:;
                {
                    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)7ULL));
                    if (!t49) {
                        goto gm_lb12;
                    }
                    gm_b16_8 t50;
                    GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s0eqrtq5mqb3a));
                    GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(15));
                    gm_l4 = t50;
                    goto gm_lb4;
                }
gm_lb12:;
                {
                    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t52 = (uint8_t)(t51 == ((uint8_t)8ULL));
                    if (!t52) {
                        goto gm_lb13;
                    }
                    gm_b16_8 t53;
                    GM_ST(void *, (uint8_t *)&t53, (void *)(gm_se0ivu8cf46o8));
                    GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(11));
                    gm_l4 = t53;
                    goto gm_lb4;
                }
gm_lb13:;
                {
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t55 = (uint8_t)(t54 == ((uint8_t)9ULL));
                    if (!t55) {
                        goto gm_lb14;
                    }
                    gm_b16_8 t56;
                    GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s96j6o9smoqdu));
                    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(7));
                    gm_l4 = t56;
                    goto gm_lb4;
                }
gm_lb14:;
                {
                    uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t58 = (uint8_t)(t57 == ((uint8_t)10ULL));
                    if (!t58) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t59;
                    GM_ST(void *, (uint8_t *)&t59, (void *)(gm_srmrjv19jgvks));
                    GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(10));
                    gm_l4 = t59;
                    goto gm_lb4;
                }
gm_lb15:;
                {
                    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t61 = (uint8_t)(t60 == ((uint8_t)11ULL));
                    if (!t61) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t62;
                    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_s55ccd7do7rbs));
                    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(10));
                    gm_l4 = t62;
                    goto gm_lb4;
                }
gm_lb16:;
                {
                    uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t64 = (uint8_t)(t63 == ((uint8_t)12ULL));
                    if (!t64) {
                        goto gm_lb17;
                    }
                    gm_b16_8 t65;
                    GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sim8pu6stc37a));
                    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(10));
                    gm_l4 = t65;
                    goto gm_lb4;
                }
gm_lb17:;
                gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 445, 10);
            }
gm_lb4:;
            gm_b16_8 t66 = gm_l4;
            gm_l2 = t66;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 443, 63);
    }
gm_lb1:;
    gm_b16_8 t67 = gm_l2;
    F.ret = t67;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_escaped_k5fm4jl(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_flavor_text_3ffnbt7(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_flavor_text_3ffnbt7 F;
    gm_t9qkh0rsooc8d gm_l0; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)0ULL));
            if (!t8) {
                goto gm_lb2;
            }
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sdft8ov6bb7s6));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(7));
            gm_l1 = t9;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)1ULL));
            if (!t11) {
                goto gm_lb3;
            }
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_ssgeom7733jik));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(4));
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sv3olmne7h13s));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(3));
            gm_l1 = t15;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)3ULL));
            if (!t17) {
                goto gm_lb5;
            }
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_setcl7798ov89));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
            gm_l1 = t18;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 441, 47);
    }
gm_lb1:;
    gm_b16_8 t19 = gm_l1;
    F.ret = t19;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_fn_label_9hbn37h(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_fn_label_9hbn37h F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* sig */
    gm_tiilmfd16v3fc gm_l3; /* inst */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t88rcucg1b7uk gm_l7; /* a */
    GM_STACK_CHECK(&F, 735u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tabb710d9mq2e gm_l11;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, (((uint8_t *)t9) + 344));
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t9) + 344) + 8));
    uint32_t t12 = gm_l1;
    uint64_t t13 = (uint64_t)t12;
    if ((uintptr_t)t13 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t13, t11, gm_srco4ljquuk5d87, 44, 13);
    void * t14 = (void *)(((uint8_t *)t10) + (uintptr_t)t13 * 136);
    gm_l2 = t14;
    {
        {
            void * t15 = gm_l2;
            uint8_t t16 = (uint8_t)(((((uint8_t *)t15) + 96))[24] != 0);
            if (!t16) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l3), (((uint8_t *)t15) + 96), 24);
            gm_trqfvmjtt4sij t17;
            gm_f_list_epvducg((void *)&t17, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l4 = t17;
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_l5 = t18;
            gm_l6 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t19 = gm_l6;
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint8_t t21 = (t19 < t20);
                    if (!t21) {
                        goto gm_lb3;
                    }
                    {
                        void * t22 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t24 = gm_l6;
                        {
                            {
                                gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t22) + (uintptr_t)t24 * 4));
                                gm_l7 = t25;
                                void * t26 = (void *)((uint8_t *)&F.l4);
                                void * t27 = gm_l0;
                                gm_t88rcucg1b7uk t28 = gm_l7;
                                gm_b16_8 t29 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t27, t28);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t30 = gm_f_push_3ef746b(t26, t29);
                                gm_l8 = t30;
                                uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                                if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srco4ljquuk5d87, 47, 30);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 47, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t32 = gm_l6;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l6 = t35;
                }
            }
gm_lb3:;
            void * t36 = gm_l0;
            uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
            gm_b16_8 t38 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(2));
            gm_f_join_sfknljb((void *)&gm_l9, (void *)NULL, cx, NULL, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srco4ljquuk5d87, 48, 65);
            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_t6j6smj7qoctu t44;
            GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t38);
            GM_ST(gm_b16_8, ((uint8_t *)&t44 + 16), t43);
            gm_f_format_licpkoa((void *)&gm_l10, (void *)NULL, cx, NULL, &t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_srco4ljquuk5d87, 48, 16);
            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
            gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
            F.ret = t47;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t48 = gm_l2;
    gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t48) + 16));
    gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t51 = (gm_eqff85ac10o3(((uint8_t *)&t49), ((uint8_t *)&t50)));
    if (t51) {
        void * t52 = gm_l2;
        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)t52));
        F.ret = t53;
        goto gm_fret;
    }
    void * t54 = gm_l2;
    gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)t54));
    void * t56 = gm_l0;
    void * t57 = gm_l2;
    gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t57) + 16));
    gm_b16_8 t59 = gm_f_place_lj97s4p((void *)NULL, cx, NULL, t56, t58);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t6j6smj7qoctu t60;
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t55);
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 16), t59);
    gm_f_format_1d88j1c((void *)&gm_l11, (void *)NULL, cx, NULL, &t60);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srco4ljquuk5d87, 51, 12);
    gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_pfpd9oj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_pfpd9oj F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    gm_tl874t2atnlh3 gm_l2; /* x */
    gm_tl874t2atnlh3 gm_l3; /* y */
    GM_STACK_CHECK_SHALLOW(&F, 233u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_tir_dump_ppa6e9g *)((&F.h)->up))->l0));
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 344));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 344) + 8));
    void * t5 = gm_l0;
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)t5));
    uint64_t t7 = (uint64_t)t6;
    if ((uintptr_t)t7 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t7, t4, gm_srco4ljquuk5d87, 29, 14);
    gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t3) + (uintptr_t)t7 * 136) + 16));
    gm_l2 = t8;
    void * t9 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_tir_dump_ppa6e9g *)((&F.h)->up))->l0));
    void * t10 = GM_LD(void *, (((uint8_t *)t9) + 344));
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t9) + 344) + 8));
    void * t12 = gm_l1;
    uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)t12));
    uint64_t t14 = (uint64_t)t13;
    if ((uintptr_t)t14 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t14, t11, gm_srco4ljquuk5d87, 30, 14);
    gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t10) + (uintptr_t)t14 * 136) + 16));
    gm_l3 = t15;
    uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
    uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
    uint8_t t18 = (!(t16 == t17));
    if (t18) {
        uint32_t t19 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
        uint32_t t20 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
        uint8_t t21 = (t19 < t20);
        F.ret = t21;
        goto gm_fret;
    }
    uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
    uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
    uint8_t t24 = (!(t22 == t23));
    if (t24) {
        uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        uint8_t t27 = (t25 < t26);
        F.ret = t27;
        goto gm_fret;
    }
    uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
    uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t30 = (!(t28 == t29));
    if (t30) {
        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
        uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t33 = (t31 < t32);
        F.ret = t33;
        goto gm_fret;
    }
    void * t34 = gm_l0;
    uint32_t t35 = GM_LD(uint32_t, ((uint8_t *)t34));
    void * t36 = gm_l1;
    uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)t36));
    uint8_t t38 = (t35 < t37);
    F.ret = t38;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_local_text_75tuacb(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_local_text_75tuacb F;
    void * gm_l0; /* d */
    uint32_t gm_l1; /* l */
    gm_b16_8 gm_l2; /* locals */
    GM_STACK_CHECK(&F, 343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l3;
    gm_tabb710d9mq2e gm_l4;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 384));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 384) + 8));
    void * t6 = gm_l0;
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t6) + 48));
    uint64_t t8 = (uint64_t)t7;
    if ((uintptr_t)t8 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t8, t5, gm_srco4ljquuk5d87, 106, 15);
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + (uintptr_t)t8 * 64) + 24));
    gm_l2 = t9;
    uint32_t t10 = gm_l1;
    uint64_t t11 = (uint64_t)t10;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t13 = (t11 < t12);
    if (t13) {
        uint32_t t14 = gm_l1;
        void * t15 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint32_t t17 = gm_l1;
        uint64_t t18 = (uint64_t)t17;
        if ((uintptr_t)t18 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t18, t16, gm_srco4ljquuk5d87, 107, 62);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t15) + (uintptr_t)t18 * 40));
        gm_tiilmfd16v3fc t20;
        GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t20 + 0), t14);
        GM_ST(gm_b16_8, ((uint8_t *)&t20 + 8), t19);
        gm_f_format_fp763nd((void *)&gm_l3, (void *)NULL, cx, NULL, &t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srco4ljquuk5d87, 107, 39);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        F.ret = t23;
        goto gm_fret;
    }
    uint32_t t24 = gm_l1;
    gm_t88rcucg1b7uk t25;
    GM_ST(uint32_t, ((uint8_t *)&t25 + 0), t24);
    gm_f_format_irh7ehn((void *)&gm_l4, (void *)NULL, cx, NULL, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srco4ljquuk5d87, 108, 12);
    gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
    F.ret = t28;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_place_lj97s4p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_place_lj97s4p F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* file */
    gm_tcc1rq4n1avb2 gm_l3; /* lc */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l4;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t6 = gm_f_file_o15fqku(t4, t5);
    gm_l2 = t6;
    void * t7 = gm_l2;
    uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_tcc1rq4n1avb2 t9 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t7, t8));
    gm_l3 = t9;
    void * t10 = gm_l2;
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)t10));
    uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
    uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
    gm_t5hdnnftkup6m t14;
    GM_ST(gm_b16_8, ((uint8_t *)&t14 + 0), t11);
    GM_ST(uint32_t, ((uint8_t *)&t14 + 16), t12);
    GM_ST(uint32_t, ((uint8_t *)&t14 + 20), t13);
    gm_f_format_45lt1qt((void *)&gm_l4, (void *)NULL, cx, NULL, &t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srco4ljquuk5d87, 58, 12);
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_rmw_text_te4ol3u(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rmw_text_te4ol3u F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 191u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_spr274eleu2lj));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(3));
            gm_l1 = t12;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)1ULL));
            if (!t14) {
                goto gm_lb3;
            }
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_ssigpbgi88it3));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(3));
            gm_l1 = t15;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)2ULL));
            if (!t17) {
                goto gm_lb4;
            }
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s4bdmssec9aiu));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
            gm_l1 = t18;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)3ULL));
            if (!t20) {
                goto gm_lb5;
            }
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sfch9mo7lqp8l));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(2));
            gm_l1 = t21;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)4ULL));
            if (!t23) {
                goto gm_lb6;
            }
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sismsc98l6u4e));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(3));
            gm_l1 = t24;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)5ULL));
            if (!t26) {
                goto gm_lb7;
            }
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sgo00f0mafbnd));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(3));
            gm_l1 = t27;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)6ULL));
            if (!t29) {
                goto gm_lb8;
            }
            gm_b16_8 t30;
            GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s187rb18i1kgk));
            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(3));
            gm_l1 = t30;
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 601, 44);
    }
gm_lb1:;
    gm_b16_8 t31 = gm_l1;
    F.ret = t31;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sink_text_s4o8tr0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_sink_text_s4o8tr0 F;
    void * gm_l0; /* d */
    gm_tiilmfd16v3fc gm_l2; /* e */
    uint32_t gm_l3; /* op */
    GM_STACK_CHECK(&F, 257u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_b16_8 gm_l4;
    gm_tabb710d9mq2e gm_l5;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)0ULL));
            if (!t6) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l2), (((uint8_t *)&F.l1) + 8), 24);
            uint32_t t7 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 8) + 24));
            gm_l3 = t7;
            void * t8 = gm_l0;
            void * t9 = GM_LD(void *, ((uint8_t *)t8));
            void * t10 = (void *)(((uint8_t *)t9) + 8);
            void * t11 = (void *)((uint8_t *)&gm_l2);
            gm_b16_8 t12 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t13 = gm_l3;
            gm_tommf4j9upomc t14;
            GM_ST(uint32_t, (uint8_t *)&t14 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 0), t12);
            GM_ST(uint32_t, ((uint8_t *)&t14 + 16), t13);
            gm_f_format_pvddll3((void *)&gm_l5, (void *)NULL, cx, NULL, &t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srco4ljquuk5d87, 117, 24);
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            gm_l4 = t17;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)1ULL));
            if (!t19) {
                goto gm_lb3;
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s579fb7l5l6df));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(2));
            gm_l4 = t20;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 116, 73);
    }
gm_lb1:;
    gm_b16_8 t21 = gm_l4;
    F.ret = t21;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_soa_op_text_ud90n2l(void *env, gm_node *cx, gm_node *inst, gm_tesaltf0mhcki a0) {
    struct gm_fr_soa_op_text_ud90n2l F;
    gm_tesaltf0mhcki gm_l0; /* op */
    gm_toh0ootodhjue gm_l1; /* x */
    GM_STACK_CHECK(&F, 315u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l2;
    gm_tabb710d9mq2e gm_l3;
    gm_b16_8 gm_l4;
    {
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)3ULL));
            if (!t15) {
                goto gm_lb2;
            }
            gm_toh0ootodhjue t16 = GM_LD(gm_toh0ootodhjue, (((uint8_t *)&gm_l0) + 1));
            gm_l1 = t16;
            uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&gm_l1));
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 1));
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 2));
            gm_toh0ootodhjue t20;
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), t17);
            GM_ST(uint8_t, ((uint8_t *)&t20 + 1), t18);
            GM_ST(uint8_t, ((uint8_t *)&t20 + 2), t19);
            gm_f_format_4i3nb5n((void *)&gm_l3, (void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srco4ljquuk5d87, 606, 18);
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
            gm_l2 = t23;
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t25 = (uint8_t)(t24 == ((uint8_t)0ULL));
                    if (!t25) {
                        goto gm_lb5;
                    }
                    gm_b16_8 t26;
                    GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s2a9t8smkevg0));
                    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(3));
                    gm_l4 = t26;
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t28 = (uint8_t)(t27 == ((uint8_t)1ULL));
                    if (!t28) {
                        goto gm_lb6;
                    }
                    gm_b16_8 t29;
                    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sbgepjj6hgjej));
                    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(3));
                    gm_l4 = t29;
                    goto gm_lb4;
                }
gm_lb6:;
                {
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t31 = (uint8_t)(t30 == ((uint8_t)2ULL));
                    if (!t31) {
                        goto gm_lb7;
                    }
                    gm_b16_8 t32;
                    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s4lt93qhapike));
                    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(3));
                    gm_l4 = t32;
                    goto gm_lb4;
                }
gm_lb7:;
                {
                    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)3ULL));
                    if (!t34) {
                        goto gm_lb8;
                    }
                    gm_b16_8 t35;
                    GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sqa0b48i8mvk7));
                    GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(5));
                    gm_l4 = t35;
                    goto gm_lb4;
                }
gm_lb8:;
                {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)4ULL));
                    if (!t37) {
                        goto gm_lb9;
                    }
                    gm_b16_8 t38;
                    GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s35gfshei80n5));
                    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(7));
                    gm_l4 = t38;
                    goto gm_lb4;
                }
gm_lb9:;
                {
                    uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t40 = (uint8_t)(t39 == ((uint8_t)5ULL));
                    if (!t40) {
                        goto gm_lb10;
                    }
                    gm_b16_8 t41;
                    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s3o5gaeteeggu));
                    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(4));
                    gm_l4 = t41;
                    goto gm_lb4;
                }
gm_lb10:;
                {
                    uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)6ULL));
                    if (!t43) {
                        goto gm_lb11;
                    }
                    gm_b16_8 t44;
                    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sah1jmr01nidj));
                    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(4));
                    gm_l4 = t44;
                    goto gm_lb4;
                }
gm_lb11:;
                {
                    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t46 = (uint8_t)(t45 == ((uint8_t)7ULL));
                    if (!t46) {
                        goto gm_lb12;
                    }
                    gm_b16_8 t47;
                    GM_ST(void *, (uint8_t *)&t47, (void *)(gm_supvu1ruutbov));
                    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(8));
                    gm_l4 = t47;
                    goto gm_lb4;
                }
gm_lb12:;
                gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 607, 10);
            }
gm_lb4:;
            gm_b16_8 t48 = gm_l4;
            gm_l2 = t48;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 605, 59);
    }
gm_lb1:;
    gm_b16_8 t49 = gm_l2;
    F.ret = t49;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_spec_text_fr9884d(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_spec_text_fr9884d F;
    void * gm_l0; /* spec */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* align */
    gm_b16_8 gm_l3; /* kind */
    uint32_t gm_l4; /* p */
    GM_STACK_CHECK(&F, 547u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l5;
    gm_b16_8 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    void * t17 = gm_l0;
    {
        {
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t17) + 1) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
            if (!t19) {
                goto gm_lb2;
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sbqq5iijjahf9));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(4));
            gm_l5 = t20;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t17) + 1) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)1ULL));
            if (!t22) {
                goto gm_lb3;
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s5n5e484glelv));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
            gm_l5 = t23;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t17) + 1) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)2ULL));
            if (!t25) {
                goto gm_lb4;
            }
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s6edba3m7mc9a));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(5));
            gm_l5 = t26;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t17) + 1) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)3ULL));
            if (!t28) {
                goto gm_lb5;
            }
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s2ma5cp41jk1a));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(6));
            gm_l5 = t29;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 450, 14);
    }
gm_lb1:;
    gm_b16_8 t30 = gm_l5;
    gm_l2 = t30;
    void * t31 = gm_l0;
    {
        {
            uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t31) + 20) + 0));
            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)0ULL));
            if (!t33) {
                goto gm_lb7;
            }
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sc5qopgd7dm66));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(7));
            gm_l6 = t34;
            goto gm_lb6;
        }
gm_lb7:;
        {
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t31) + 20) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)1ULL));
            if (!t36) {
                goto gm_lb8;
            }
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sbsb3nmbfgbv2));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(3));
            gm_l6 = t37;
            goto gm_lb6;
        }
gm_lb8:;
        {
            uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t31) + 20) + 0));
            uint8_t t39 = (uint8_t)(t38 == ((uint8_t)2ULL));
            if (!t39) {
                goto gm_lb9;
            }
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s85jtgs7au5b5));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(8));
            gm_l6 = t40;
            goto gm_lb6;
        }
gm_lb9:;
        {
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t31) + 20) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)3ULL));
            if (!t42) {
                goto gm_lb10;
            }
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s49o1oiv7k0po));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(3));
            gm_l6 = t43;
            goto gm_lb6;
        }
gm_lb10:;
        {
            uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t31) + 20) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)4ULL));
            if (!t45) {
                goto gm_lb11;
            }
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_seg9l1usqbs3j));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(3));
            gm_l6 = t46;
            goto gm_lb6;
        }
gm_lb11:;
        {
            uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t31) + 20) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)5ULL));
            if (!t48) {
                goto gm_lb12;
            }
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sgfufhs3psibd));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(3));
            gm_l6 = t49;
            goto gm_lb6;
        }
gm_lb12:;
        {
            uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t31) + 20) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)6ULL));
            if (!t51) {
                goto gm_lb13;
            }
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sajbccb18c45d));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(5));
            gm_l6 = t52;
            goto gm_lb6;
        }
gm_lb13:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 456, 13);
    }
gm_lb6:;
    gm_b16_8 t53 = gm_l6;
    gm_l3 = t53;
    void * t54 = gm_l1;
    void * t55 = gm_l0;
    uint8_t t56 = GM_LD(uint8_t, ((uint8_t *)t55));
    gm_b16_8 t57 = gm_l2;
    void * t58 = gm_l0;
    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)t58) + 2));
    void * t60 = gm_l0;
    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)t60) + 3));
    void * t62 = gm_l0;
    uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)t62) + 4));
    void * t64 = gm_l0;
    uint32_t t65 = GM_LD(uint32_t, (((uint8_t *)t64) + 8));
    gm_th32vq80aukjr t66;
    GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&t66 + 27, 0);
    GM_ST(uint8_t, ((uint8_t *)&t66 + 0), t56);
    GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t57);
    GM_ST(uint8_t, ((uint8_t *)&t66 + 24), t59);
    GM_ST(uint8_t, ((uint8_t *)&t66 + 25), t61);
    GM_ST(uint8_t, ((uint8_t *)&t66 + 26), t63);
    GM_ST(uint32_t, ((uint8_t *)&t66 + 28), t65);
    gm_tftb1odc739t5 t67 = gm_f_write_t5i5r03(t54, &t66);
    gm_l7 = t67;
    uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srco4ljquuk5d87, 465, 5);
    {
        {
            void * t69 = gm_l0;
            uint8_t t70 = (uint8_t)(((((uint8_t *)t69) + 12))[4] != 0);
            if (!t70) {
                goto gm_lb15;
            }
            uint32_t t71 = GM_LD(uint32_t, (((uint8_t *)t69) + 12));
            gm_l4 = t71;
            void * t72 = gm_l1;
            uint32_t t73 = gm_l4;
            gm_t88rcucg1b7uk t74;
            GM_ST(uint32_t, ((uint8_t *)&t74 + 0), t73);
            gm_tftb1odc739t5 t75 = gm_f_write_7h6pth0(t72, t74);
            gm_l8 = t75;
            uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srco4ljquuk5d87, 467, 9);
            goto gm_lb14;
        }
gm_lb15:;
        void * t77 = gm_l1;
        gm_b16_8 t78;
        GM_ST(void *, (uint8_t *)&t78, (void *)(gm_s026u6q948evn));
        GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(4));
        gm_tftb1odc739t5 t79 = gm_f_extend_0cs85gs(t77, t78);
        gm_l9 = t79;
        uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srco4ljquuk5d87, 469, 9);
    }
gm_lb14:;
    void * t81 = gm_l1;
    gm_b16_8 t82 = gm_l3;
    gm_tnpifv20irnph t83;
    GM_ST(gm_b16_8, ((uint8_t *)&t83 + 0), t82);
    gm_tftb1odc739t5 t84 = gm_f_write_7ilko9q(t81, t83);
    gm_l10 = t84;
    uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t85) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srco4ljquuk5d87, 471, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_text_9qjnpra(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sys_text_9qjnpra F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 1055u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            {
                {
                    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t59 = (uint8_t)(t58 == ((uint8_t)0ULL));
                    if (!t59) {
                        goto gm_lb2;
                    }
                    gm_b16_8 t60;
                    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s93umj3p0tala));
                    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(4));
                    gm_l1 = t60;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t62 = (uint8_t)(t61 == ((uint8_t)1ULL));
                    if (!t62) {
                        goto gm_lb3;
                    }
                    gm_b16_8 t63;
                    GM_ST(void *, (uint8_t *)&t63, (void *)(gm_slnhe1h36jmub));
                    GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(8));
                    gm_l1 = t63;
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t65 = (uint8_t)(t64 == ((uint8_t)2ULL));
                    if (!t65) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t66;
                    GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sa8dse4jk5ibv));
                    GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(9));
                    gm_l1 = t66;
                    goto gm_lb1;
                }
gm_lb4:;
                {
                    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t68 = (uint8_t)(t67 == ((uint8_t)3ULL));
                    if (!t68) {
                        goto gm_lb5;
                    }
                    gm_b16_8 t69;
                    GM_ST(void *, (uint8_t *)&t69, (void *)(gm_smmus0krp2svr));
                    GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(5));
                    gm_l1 = t69;
                    goto gm_lb1;
                }
gm_lb5:;
                {
                    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t71 = (uint8_t)(t70 == ((uint8_t)4ULL));
                    if (!t71) {
                        goto gm_lb6;
                    }
                    gm_b16_8 t72;
                    GM_ST(void *, (uint8_t *)&t72, (void *)(gm_sa5s0jools6a2));
                    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(8));
                    gm_l1 = t72;
                    goto gm_lb1;
                }
gm_lb6:;
                {
                    uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t74 = (uint8_t)(t73 == ((uint8_t)5ULL));
                    if (!t74) {
                        goto gm_lb7;
                    }
                    gm_b16_8 t75;
                    GM_ST(void *, (uint8_t *)&t75, (void *)(gm_sacua9qc75rts));
                    GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(6));
                    gm_l1 = t75;
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t77 = (uint8_t)(t76 == ((uint8_t)6ULL));
                    if (!t77) {
                        goto gm_lb8;
                    }
                    gm_b16_8 t78;
                    GM_ST(void *, (uint8_t *)&t78, (void *)(gm_siqgrmkn8n4vn));
                    GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(7));
                    gm_l1 = t78;
                    goto gm_lb1;
                }
gm_lb8:;
                {
                    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t80 = (uint8_t)(t79 == ((uint8_t)7ULL));
                    if (!t80) {
                        goto gm_lb9;
                    }
                    gm_b16_8 t81;
                    GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sc03pp1oj3q4k));
                    GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(6));
                    gm_l1 = t81;
                    goto gm_lb1;
                }
gm_lb9:;
                {
                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)8ULL));
                    if (!t83) {
                        goto gm_lb10;
                    }
                    gm_b16_8 t84;
                    GM_ST(void *, (uint8_t *)&t84, (void *)(gm_s9i2ld3a42lap));
                    GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(7));
                    gm_l1 = t84;
                    goto gm_lb1;
                }
gm_lb10:;
                {
                    uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t86 = (uint8_t)(t85 == ((uint8_t)9ULL));
                    if (!t86) {
                        goto gm_lb11;
                    }
                    gm_b16_8 t87;
                    GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sjbhp9l7seful));
                    GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(6));
                    gm_l1 = t87;
                    goto gm_lb1;
                }
gm_lb11:;
                {
                    uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t89 = (uint8_t)(t88 == ((uint8_t)10ULL));
                    if (!t89) {
                        goto gm_lb12;
                    }
                    gm_b16_8 t90;
                    GM_ST(void *, (uint8_t *)&t90, (void *)(gm_sit8u7uppaik9));
                    GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(6));
                    gm_l1 = t90;
                    goto gm_lb1;
                }
gm_lb12:;
            }
            {
                {
                    uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t92 = (uint8_t)(t91 == ((uint8_t)11ULL));
                    if (!t92) {
                        goto gm_lb13;
                    }
                    gm_b16_8 t93;
                    GM_ST(void *, (uint8_t *)&t93, (void *)(gm_sf5llhuhdlajp));
                    GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(8));
                    gm_l1 = t93;
                    goto gm_lb1;
                }
gm_lb13:;
                {
                    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t95 = (uint8_t)(t94 == ((uint8_t)12ULL));
                    if (!t95) {
                        goto gm_lb14;
                    }
                    gm_b16_8 t96;
                    GM_ST(void *, (uint8_t *)&t96, (void *)(gm_so8fkrgrobcsk));
                    GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(8));
                    gm_l1 = t96;
                    goto gm_lb1;
                }
gm_lb14:;
                {
                    uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t98 = (uint8_t)(t97 == ((uint8_t)13ULL));
                    if (!t98) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t99;
                    GM_ST(void *, (uint8_t *)&t99, (void *)(gm_so3oq96gr0k5r));
                    GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(9));
                    gm_l1 = t99;
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t101 = (uint8_t)(t100 == ((uint8_t)14ULL));
                    if (!t101) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t102;
                    GM_ST(void *, (uint8_t *)&t102, (void *)(gm_snstimc3n04u0));
                    GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(11));
                    gm_l1 = t102;
                    goto gm_lb1;
                }
gm_lb16:;
                {
                    uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t104 = (uint8_t)(t103 == ((uint8_t)15ULL));
                    if (!t104) {
                        goto gm_lb17;
                    }
                    gm_b16_8 t105;
                    GM_ST(void *, (uint8_t *)&t105, (void *)(gm_shvp36midgf58));
                    GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(9));
                    gm_l1 = t105;
                    goto gm_lb1;
                }
gm_lb17:;
                {
                    uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t107 = (uint8_t)(t106 == ((uint8_t)16ULL));
                    if (!t107) {
                        goto gm_lb18;
                    }
                    gm_b16_8 t108;
                    GM_ST(void *, (uint8_t *)&t108, (void *)(gm_slov3heeajhqn));
                    GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(11));
                    gm_l1 = t108;
                    goto gm_lb1;
                }
gm_lb18:;
                {
                    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t110 = (uint8_t)(t109 == ((uint8_t)17ULL));
                    if (!t110) {
                        goto gm_lb19;
                    }
                    gm_b16_8 t111;
                    GM_ST(void *, (uint8_t *)&t111, (void *)(gm_segr993uh2crm));
                    GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(10));
                    gm_l1 = t111;
                    goto gm_lb1;
                }
gm_lb19:;
                {
                    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t113 = (uint8_t)(t112 == ((uint8_t)18ULL));
                    if (!t113) {
                        goto gm_lb20;
                    }
                    gm_b16_8 t114;
                    GM_ST(void *, (uint8_t *)&t114, (void *)(gm_s3ol5q9br79vf));
                    GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(7));
                    gm_l1 = t114;
                    goto gm_lb1;
                }
gm_lb20:;
                {
                    uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t116 = (uint8_t)(t115 == ((uint8_t)19ULL));
                    if (!t116) {
                        goto gm_lb21;
                    }
                    gm_b16_8 t117;
                    GM_ST(void *, (uint8_t *)&t117, (void *)(gm_sk721elh8ouv2));
                    GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(10));
                    gm_l1 = t117;
                    goto gm_lb1;
                }
gm_lb21:;
                {
                    uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t119 = (uint8_t)(t118 == ((uint8_t)20ULL));
                    if (!t119) {
                        goto gm_lb22;
                    }
                    gm_b16_8 t120;
                    GM_ST(void *, (uint8_t *)&t120, (void *)(gm_s6nm27ehg597q));
                    GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(6));
                    gm_l1 = t120;
                    goto gm_lb1;
                }
gm_lb22:;
                {
                    uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t122 = (uint8_t)(t121 == ((uint8_t)21ULL));
                    if (!t122) {
                        goto gm_lb23;
                    }
                    gm_b16_8 t123;
                    GM_ST(void *, (uint8_t *)&t123, (void *)(gm_stehcnejvoo24));
                    GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(5));
                    gm_l1 = t123;
                    goto gm_lb1;
                }
gm_lb23:;
            }
            {
                {
                    uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t125 = (uint8_t)(t124 == ((uint8_t)22ULL));
                    if (!t125) {
                        goto gm_lb24;
                    }
                    gm_b16_8 t126;
                    GM_ST(void *, (uint8_t *)&t126, (void *)(gm_s6ofccoi3ursa));
                    GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(11));
                    gm_l1 = t126;
                    goto gm_lb1;
                }
gm_lb24:;
                {
                    uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t128 = (uint8_t)(t127 == ((uint8_t)23ULL));
                    if (!t128) {
                        goto gm_lb25;
                    }
                    gm_b16_8 t129;
                    GM_ST(void *, (uint8_t *)&t129, (void *)(gm_stidgejoe9ul2));
                    GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(10));
                    gm_l1 = t129;
                    goto gm_lb1;
                }
gm_lb25:;
                {
                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t131 = (uint8_t)(t130 == ((uint8_t)24ULL));
                    if (!t131) {
                        goto gm_lb26;
                    }
                    gm_b16_8 t132;
                    GM_ST(void *, (uint8_t *)&t132, (void *)(gm_sai3vjkob4s2v));
                    GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(9));
                    gm_l1 = t132;
                    goto gm_lb1;
                }
gm_lb26:;
                {
                    uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t134 = (uint8_t)(t133 == ((uint8_t)25ULL));
                    if (!t134) {
                        goto gm_lb27;
                    }
                    gm_b16_8 t135;
                    GM_ST(void *, (uint8_t *)&t135, (void *)(gm_sgtriqnjufjbf));
                    GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(9));
                    gm_l1 = t135;
                    goto gm_lb1;
                }
gm_lb27:;
                {
                    uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t137 = (uint8_t)(t136 == ((uint8_t)26ULL));
                    if (!t137) {
                        goto gm_lb28;
                    }
                    gm_b16_8 t138;
                    GM_ST(void *, (uint8_t *)&t138, (void *)(gm_smgioupbmjajr));
                    GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(13));
                    gm_l1 = t138;
                    goto gm_lb1;
                }
gm_lb28:;
                {
                    uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t140 = (uint8_t)(t139 == ((uint8_t)27ULL));
                    if (!t140) {
                        goto gm_lb29;
                    }
                    gm_b16_8 t141;
                    GM_ST(void *, (uint8_t *)&t141, (void *)(gm_s1js4dn5e65ja));
                    GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(10));
                    gm_l1 = t141;
                    goto gm_lb1;
                }
gm_lb29:;
                {
                    uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t143 = (uint8_t)(t142 == ((uint8_t)28ULL));
                    if (!t143) {
                        goto gm_lb30;
                    }
                    gm_b16_8 t144;
                    GM_ST(void *, (uint8_t *)&t144, (void *)(gm_s5e0seq0he7ou));
                    GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(9));
                    gm_l1 = t144;
                    goto gm_lb1;
                }
gm_lb30:;
                {
                    uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t146 = (uint8_t)(t145 == ((uint8_t)29ULL));
                    if (!t146) {
                        goto gm_lb31;
                    }
                    gm_b16_8 t147;
                    GM_ST(void *, (uint8_t *)&t147, (void *)(gm_so2sjlodq81qn));
                    GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(12));
                    gm_l1 = t147;
                    goto gm_lb1;
                }
gm_lb31:;
                {
                    uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t149 = (uint8_t)(t148 == ((uint8_t)30ULL));
                    if (!t149) {
                        goto gm_lb32;
                    }
                    gm_b16_8 t150;
                    GM_ST(void *, (uint8_t *)&t150, (void *)(gm_spskhsf5tnedj));
                    GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(9));
                    gm_l1 = t150;
                    goto gm_lb1;
                }
gm_lb32:;
                {
                    uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t152 = (uint8_t)(t151 == ((uint8_t)31ULL));
                    if (!t152) {
                        goto gm_lb33;
                    }
                    gm_b16_8 t153;
                    GM_ST(void *, (uint8_t *)&t153, (void *)(gm_sqlk0h0kae56e));
                    GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(7));
                    gm_l1 = t153;
                    goto gm_lb1;
                }
gm_lb33:;
                {
                    uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t155 = (uint8_t)(t154 == ((uint8_t)32ULL));
                    if (!t155) {
                        goto gm_lb34;
                    }
                    gm_b16_8 t156;
                    GM_ST(void *, (uint8_t *)&t156, (void *)(gm_s4gilalcmdvu3));
                    GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(7));
                    gm_l1 = t156;
                    goto gm_lb1;
                }
gm_lb34:;
            }
            {
                {
                    uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t158 = (uint8_t)(t157 == ((uint8_t)33ULL));
                    if (!t158) {
                        goto gm_lb35;
                    }
                    gm_b16_8 t159;
                    GM_ST(void *, (uint8_t *)&t159, (void *)(gm_sjfcbh429vmot));
                    GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)(8));
                    gm_l1 = t159;
                    goto gm_lb1;
                }
gm_lb35:;
                {
                    uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t161 = (uint8_t)(t160 == ((uint8_t)34ULL));
                    if (!t161) {
                        goto gm_lb36;
                    }
                    gm_b16_8 t162;
                    GM_ST(void *, (uint8_t *)&t162, (void *)(gm_soerl6rds8b9v));
                    GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(11));
                    gm_l1 = t162;
                    goto gm_lb1;
                }
gm_lb36:;
                {
                    uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t164 = (uint8_t)(t163 == ((uint8_t)35ULL));
                    if (!t164) {
                        goto gm_lb37;
                    }
                    gm_b16_8 t165;
                    GM_ST(void *, (uint8_t *)&t165, (void *)(gm_s7m3rbk4l87ah));
                    GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(14));
                    gm_l1 = t165;
                    goto gm_lb1;
                }
gm_lb37:;
                {
                    uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t167 = (uint8_t)(t166 == ((uint8_t)36ULL));
                    if (!t167) {
                        goto gm_lb38;
                    }
                    gm_b16_8 t168;
                    GM_ST(void *, (uint8_t *)&t168, (void *)(gm_sm0gs6tv176le));
                    GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(7));
                    gm_l1 = t168;
                    goto gm_lb1;
                }
gm_lb38:;
                {
                    uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t170 = (uint8_t)(t169 == ((uint8_t)37ULL));
                    if (!t170) {
                        goto gm_lb39;
                    }
                    gm_b16_8 t171;
                    GM_ST(void *, (uint8_t *)&t171, (void *)(gm_s1gf327eapo1j));
                    GM_ST(uintptr_t, (uint8_t *)&t171 + 8, (uintptr_t)(10));
                    gm_l1 = t171;
                    goto gm_lb1;
                }
gm_lb39:;
                {
                    uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t173 = (uint8_t)(t172 == ((uint8_t)38ULL));
                    if (!t173) {
                        goto gm_lb40;
                    }
                    gm_b16_8 t174;
                    GM_ST(void *, (uint8_t *)&t174, (void *)(gm_sgm565ktbj9ua));
                    GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(11));
                    gm_l1 = t174;
                    goto gm_lb1;
                }
gm_lb40:;
                {
                    uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t176 = (uint8_t)(t175 == ((uint8_t)39ULL));
                    if (!t176) {
                        goto gm_lb41;
                    }
                    gm_b16_8 t177;
                    GM_ST(void *, (uint8_t *)&t177, (void *)(gm_sjdrosvte680q));
                    GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(11));
                    gm_l1 = t177;
                    goto gm_lb1;
                }
gm_lb41:;
                {
                    uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t179 = (uint8_t)(t178 == ((uint8_t)40ULL));
                    if (!t179) {
                        goto gm_lb42;
                    }
                    gm_b16_8 t180;
                    GM_ST(void *, (uint8_t *)&t180, (void *)(gm_stdefoi5sk91c));
                    GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(11));
                    gm_l1 = t180;
                    goto gm_lb1;
                }
gm_lb42:;
                {
                    uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t182 = (uint8_t)(t181 == ((uint8_t)41ULL));
                    if (!t182) {
                        goto gm_lb43;
                    }
                    gm_b16_8 t183;
                    GM_ST(void *, (uint8_t *)&t183, (void *)(gm_so1u9tqjfpsmc));
                    GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(8));
                    gm_l1 = t183;
                    goto gm_lb1;
                }
gm_lb43:;
                {
                    uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t185 = (uint8_t)(t184 == ((uint8_t)42ULL));
                    if (!t185) {
                        goto gm_lb44;
                    }
                    gm_b16_8 t186;
                    GM_ST(void *, (uint8_t *)&t186, (void *)(gm_s19069udlvhni));
                    GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(8));
                    gm_l1 = t186;
                    goto gm_lb1;
                }
gm_lb44:;
                {
                    uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t188 = (uint8_t)(t187 == ((uint8_t)43ULL));
                    if (!t188) {
                        goto gm_lb45;
                    }
                    gm_b16_8 t189;
                    GM_ST(void *, (uint8_t *)&t189, (void *)(gm_s2289h15tkpi1));
                    GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)(11));
                    gm_l1 = t189;
                    goto gm_lb1;
                }
gm_lb45:;
            }
            {
                {
                    uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t191 = (uint8_t)(t190 == ((uint8_t)44ULL));
                    if (!t191) {
                        goto gm_lb46;
                    }
                    gm_b16_8 t192;
                    GM_ST(void *, (uint8_t *)&t192, (void *)(gm_sqhkrul9t68sa));
                    GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(7));
                    gm_l1 = t192;
                    goto gm_lb1;
                }
gm_lb46:;
                {
                    uint8_t t193 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t194 = (uint8_t)(t193 == ((uint8_t)45ULL));
                    if (!t194) {
                        goto gm_lb47;
                    }
                    gm_b16_8 t195;
                    GM_ST(void *, (uint8_t *)&t195, (void *)(gm_shtuc6se4lilq));
                    GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(6));
                    gm_l1 = t195;
                    goto gm_lb1;
                }
gm_lb47:;
                {
                    uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t197 = (uint8_t)(t196 == ((uint8_t)46ULL));
                    if (!t197) {
                        goto gm_lb48;
                    }
                    gm_b16_8 t198;
                    GM_ST(void *, (uint8_t *)&t198, (void *)(gm_s5k0vuc678fh1));
                    GM_ST(uintptr_t, (uint8_t *)&t198 + 8, (uintptr_t)(7));
                    gm_l1 = t198;
                    goto gm_lb1;
                }
gm_lb48:;
                {
                    uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t200 = (uint8_t)(t199 == ((uint8_t)47ULL));
                    if (!t200) {
                        goto gm_lb49;
                    }
                    gm_b16_8 t201;
                    GM_ST(void *, (uint8_t *)&t201, (void *)(gm_sm017vofcj044));
                    GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(11));
                    gm_l1 = t201;
                    goto gm_lb1;
                }
gm_lb49:;
                {
                    uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t203 = (uint8_t)(t202 == ((uint8_t)48ULL));
                    if (!t203) {
                        goto gm_lb50;
                    }
                    gm_b16_8 t204;
                    GM_ST(void *, (uint8_t *)&t204, (void *)(gm_s7d4tu5fkkg12));
                    GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(10));
                    gm_l1 = t204;
                    goto gm_lb1;
                }
gm_lb50:;
                {
                    uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t206 = (uint8_t)(t205 == ((uint8_t)49ULL));
                    if (!t206) {
                        goto gm_lb51;
                    }
                    gm_b16_8 t207;
                    GM_ST(void *, (uint8_t *)&t207, (void *)(gm_ss308e7joejdo));
                    GM_ST(uintptr_t, (uint8_t *)&t207 + 8, (uintptr_t)(12));
                    gm_l1 = t207;
                    goto gm_lb1;
                }
gm_lb51:;
                {
                    uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t209 = (uint8_t)(t208 == ((uint8_t)50ULL));
                    if (!t209) {
                        goto gm_lb52;
                    }
                    gm_b16_8 t210;
                    GM_ST(void *, (uint8_t *)&t210, (void *)(gm_ss6dev5u7tl99));
                    GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(11));
                    gm_l1 = t210;
                    goto gm_lb1;
                }
gm_lb52:;
                {
                    uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t212 = (uint8_t)(t211 == ((uint8_t)51ULL));
                    if (!t212) {
                        goto gm_lb53;
                    }
                    gm_b16_8 t213;
                    GM_ST(void *, (uint8_t *)&t213, (void *)(gm_sakmuvnjjp217));
                    GM_ST(uintptr_t, (uint8_t *)&t213 + 8, (uintptr_t)(9));
                    gm_l1 = t213;
                    goto gm_lb1;
                }
gm_lb53:;
                {
                    uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t215 = (uint8_t)(t214 == ((uint8_t)52ULL));
                    if (!t215) {
                        goto gm_lb54;
                    }
                    gm_b16_8 t216;
                    GM_ST(void *, (uint8_t *)&t216, (void *)(gm_sln5qir3tgi7j));
                    GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(9));
                    gm_l1 = t216;
                    goto gm_lb1;
                }
gm_lb54:;
                {
                    uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t218 = (uint8_t)(t217 == ((uint8_t)53ULL));
                    if (!t218) {
                        goto gm_lb55;
                    }
                    gm_b16_8 t219;
                    GM_ST(void *, (uint8_t *)&t219, (void *)(gm_sqfu5kl4vat80));
                    GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(8));
                    gm_l1 = t219;
                    goto gm_lb1;
                }
gm_lb55:;
                {
                    uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t221 = (uint8_t)(t220 == ((uint8_t)54ULL));
                    if (!t221) {
                        goto gm_lb56;
                    }
                    gm_b16_8 t222;
                    GM_ST(void *, (uint8_t *)&t222, (void *)(gm_sbcq15b9em6fm));
                    GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)(8));
                    gm_l1 = t222;
                    goto gm_lb1;
                }
gm_lb56:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 603, 44);
            }
        }
gm_lb1:;
    }
    {
        gm_b16_8 t223 = gm_l1;
        F.ret = t223;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_target_text_4kebb7t(void *env, gm_node *cx, gm_node *inst, gm_te9hrk0u35rev a0) {
    struct gm_fr_target_text_4kebb7t F;
    gm_te9hrk0u35rev gm_l0; /* t */
    uint32_t gm_l1; /* levels */
    uint32_t gm_l2; /* label */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l3;
    gm_tabb710d9mq2e gm_l4;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)0ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
            gm_l1 = t7;
            uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&gm_l0) + 4) + 4));
            gm_l2 = t8;
            uint32_t t9 = gm_l1;
            uint32_t t10 = gm_l2;
            gm_tcc1rq4n1avb2 t11;
            GM_ST(uint32_t, ((uint8_t *)&t11 + 0), t9);
            GM_ST(uint32_t, ((uint8_t *)&t11 + 4), t10);
            gm_f_format_kj3qi5h((void *)&gm_l4, (void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
            if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srco4ljquuk5d87, 112, 30);
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
            gm_l3 = t14;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)1ULL));
            if (!t16) {
                goto gm_lb3;
            }
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_srri8c6al1v1g));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
            gm_l3 = t17;
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 111, 59);
    }
gm_lb1:;
    gm_b16_8 t18 = gm_l3;
    F.ret = t18;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_tir_dump_ppa6e9g(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_tir_dump_ppa6e9g F;
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* sig */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* f */
    GM_STACK_CHECK(&F, 685u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_trqfvmjtt4sij t15;
    gm_f_list_go2qg0b((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t15;
    gm_l3 = ((uint64_t)0ULL);
    void * t16 = F.l0;
    uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)t16) + 344) + 8));
    gm_l2 = t17;
    {
        for (;;) {
            uint64_t t19 = gm_l3;
            uint64_t t20 = gm_l2;
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t22 = gm_l3;
                        gm_l4 = t22;
                        void * t23 = F.l0;
                        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 344));
                        uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t23) + 344) + 8));
                        uint64_t t26 = gm_l4;
                        if ((uintptr_t)t26 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t26, t25, gm_srco4ljquuk5d87, 23, 17);
                        void * t27 = (void *)(((uint8_t *)t24) + (uintptr_t)t26 * 136);
                        gm_l5 = t27;
                        void * t28 = gm_l5;
                        gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t28) + 16));
                        gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                        uint8_t t31 = (gm_eqff85ac10o3(((uint8_t *)&t29), ((uint8_t *)&t30)));
                        if (t31) {
                            gm_l11 = ((uint8_t)1);
                        } else {
                            void * t32 = gm_l5;
                            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)t32));
                            gm_b16_8 t34;
                            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_stgqtu1f7bco8));
                            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(7));
                            uint8_t t35 = (gm_eq0iqiubfm7e(((uint8_t *)&t33), ((uint8_t *)&t34)));
                            gm_l11 = t35;
                        }
                        uint8_t t36 = gm_l11;
                        if (t36) {
                            gm_l10 = ((uint8_t)1);
                        } else {
                            void * t37 = gm_l5;
                            {
                                {
                                    uint8_t t38 = (uint8_t)(((((uint8_t *)t37) + 96))[24] != 0);
                                    if (t38) {
                                        goto gm_lb7;
                                    }
                                    gm_l12 = ((uint8_t)0);
                                    goto gm_lb6;
                                }
gm_lb7:;
                                {
                                    gm_l12 = ((uint8_t)1);
                                    goto gm_lb6;
                                }
gm_lb8:;
                                gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 24, 65);
                            }
gm_lb6:;
                            uint8_t t39 = gm_l12;
                            gm_l10 = t39;
                        }
                        uint8_t t40 = gm_l10;
                        if (t40) {
                            goto gm_lb3;
                        }
                        void * t41 = F.l0;
                        void * t42 = GM_LD(void *, ((uint8_t *)t41));
                        void * t43 = GM_LD(void *, ((uint8_t *)t42));
                        void * t44 = gm_l5;
                        uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)t44) + 16));
                        void * t46 = gm_f_file_o15fqku(t43, t45);
                        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)t46));
                        gm_b16_8 t48;
                        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_slmii6hjigrcr));
                        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(6));
                        uint8_t t49 = gm_f_starts_with_6vvt01e(t47, t48);
                        if (t49) {
                            goto gm_lb3;
                        }
                        void * t50 = (void *)((uint8_t *)&F.l1);
                        uint64_t t51 = gm_l4;
                        uint32_t t52 = (uint32_t)t51;
                        gm_tftb1odc739t5 t53 = gm_f_push_im9d4ro(t50, t52);
                        gm_l13 = t53;
                        uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srco4ljquuk5d87, 26, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 22, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t55 = gm_l3;
            uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
            uint64_t t58 = r56;
            gm_l3 = t58;
        }
    }
gm_lb1:;
    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_closure t60 = ((gm_closure){(void *)gm_f_fn_pfpd9oj, (void *)&F.h, cx});
    gm_f_sort_by_egbtonb(t59, t60);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t61 = F.l0;
    gm_trqfvmjtt4sij t62;
    gm_f_list_uvfu1k1((void *)&t62, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tk0navpiff9ca t63;
    GM_ST(uint32_t, (uint8_t *)&t63 + 52, 0);
    GM_ST(void *, ((uint8_t *)&t63 + 0), t61);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t63 + 8), t62);
    GM_ST(uint32_t, ((uint8_t *)&t63 + 48), ((uint32_t)0ULL));
    F.l6 = t63;
    gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_l7 = t64;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t66 = gm_l8;
            uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t68 = (t66 < t67);
            if (!t68) {
                goto gm_lb9;
            }
            {
                void * t69 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t71 = gm_l8;
                {
                    {
                        uint32_t t72 = GM_LD(uint32_t, (((uint8_t *)t69) + (uintptr_t)t71 * 4));
                        gm_l9 = t72;
                        void * t73 = (void *)((uint8_t *)&F.l6);
                        uint32_t t74 = gm_l9;
                        gm_b0_1 t75 = gm_f_write_fn_jkakt7l((void *)NULL, cx, NULL, t73, t74);
                        if (gm_unw.kind) {
                            goto gm_ll65;
                        }
                        goto gm_lb12;
                    }
gm_lb13:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 37, 5);
                }
gm_lb12:;
            }
gm_lb11:;
            uint64_t t76 = gm_l8;
            uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
            uint64_t t79 = r77;
            gm_l8 = t79;
        }
    }
    goto gm_ls80;
gm_ll65:;
    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb9; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb11; }
    }
    goto gm_lad;
gm_ls80:;
gm_lb9:;
    gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 8));
    gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
    F.ret = t82;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_arm_4vimkqo(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_write_arm_4vimkqo F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    void * gm_l2; /* a */
    void * gm_l3; /* g */
    GM_STACK_CHECK(&F, 233u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t4 = gm_l0;
    uint64_t t5 = gm_l1;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s76ud0alkjfse));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(3));
    gm_b0_1 t7 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t4, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t8 = gm_l0;
    uint64_t t9 = gm_l1;
    uint64_t r10; bool o11 = __builtin_add_overflow(t9, ((uint64_t)1ULL), &r10);
    if (o11) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 538, 17);
    uint64_t t12 = r10;
    void * t13 = gm_l2;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    gm_b0_1 t15 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t8, t12, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            void * t16 = gm_l2;
            uint8_t t17 = (uint8_t)(GM_LD(void *, (((uint8_t *)t16) + 8)) != 0);
            if (!t17) {
                goto gm_lb2;
            }
            void * t18 = GM_LD(void *, (((uint8_t *)t16) + 8));
            gm_l3 = t18;
            void * t19 = gm_l0;
            uint64_t t20 = gm_l1;
            uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
            if (o22) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 540, 22);
            uint64_t t23 = r21;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sqflguscq1uc5));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(5));
            gm_b0_1 t25 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t19, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t26 = gm_l0;
            uint64_t t27 = gm_l1;
            uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)2ULL), &r28);
            if (o29) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 541, 22);
            uint64_t t30 = r28;
            void * t31 = gm_l3;
            gm_b0_1 t32 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t26, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t33 = gm_l0;
    uint64_t t34 = gm_l1;
    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
    if (o36) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 543, 18);
    uint64_t t37 = r35;
    void * t38 = gm_l2;
    void * t39 = GM_LD(void *, (((uint8_t *)t38) + 16));
    gm_b0_1 t40 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t33, t37, t39);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_block_q5mpqjf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_write_block_q5mpqjf F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    void * gm_l2; /* b */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_trcmmd12cefb1 gm_l5; /* st */
    uint32_t gm_l6; /* l */
    uint32_t gm_l7; /* l */
    void * gm_l8; /* x */
    gm_b16_8 gm_l9; /* ls */
    void * gm_l10; /* x */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t9805qtonbsag gm_l14; /* l */
    uint32_t gm_l15; /* id */
    void * gm_l16; /* p */
    void * gm_l17; /* v */
    gm_b16_8 gm_l18; /* ps */
    void * gm_l19; /* v */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    void * gm_l22; /* p */
    void * gm_l23; /* x */
    void * gm_l24; /* x */
    uint8_t gm_l25; /* is_err */
    void * gm_l26; /* x */
    void * gm_l27; /* t */
    GM_STACK_CHECK(&F, 1726u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_b16_8 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_b16_8 gm_l34;
    {
        void * t31 = gm_l2;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)t31));
        gm_l3 = t32;
        gm_l4 = ((uint64_t)0ULL);
        {
            for (;;) {
                {
                    uint64_t t33 = gm_l4;
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t35 = (t33 < t34);
                    if (!t35) {
                        goto gm_lb1;
                    }
                    {
                        void * t36 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t38 = gm_l4;
                        {
                            {
                                memmove(((uint8_t *)&gm_l5), (((uint8_t *)t36) + (uintptr_t)t38 * 32), 32);
                                {
                                    {
                                        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                        uint8_t t40 = (uint8_t)(t39 == ((uint8_t)0ULL));
                                        if (!t40) {
                                            goto gm_lb7;
                                        }
                                        uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                                        gm_l6 = t41;
                                        uint8_t t42 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8)) != 0);
                                        if (t42) {
                                            goto gm_lb7;
                                        }
                                        void * t43 = gm_l0;
                                        uint64_t t44 = gm_l1;
                                        void * t45 = gm_l0;
                                        uint32_t t46 = gm_l6;
                                        gm_b16_8 t47 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t45, t46);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tnpifv20irnph t48;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
                                        gm_f_format_cqr3tc0((void *)&gm_l28, (void *)NULL, cx, NULL, t48);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                        if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srco4ljquuk5d87, 477, 50);
                                        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                                        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                                        gm_b0_1 t52 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t43, t44, t51);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb6;
                                    }
gm_lb7:;
                                    {
                                        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                        uint8_t t54 = (uint8_t)(t53 == ((uint8_t)0ULL));
                                        if (!t54) {
                                            goto gm_lb8;
                                        }
                                        uint32_t t55 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                                        gm_l7 = t55;
                                        uint8_t t56 = (uint8_t)(GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8)) != 0);
                                        if (!t56) {
                                            goto gm_lb8;
                                        }
                                        void * t57 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                                        gm_l8 = t57;
                                        void * t58 = gm_l0;
                                        uint64_t t59 = gm_l1;
                                        void * t60 = gm_l0;
                                        uint32_t t61 = gm_l7;
                                        gm_b16_8 t62 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t60, t61);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tnpifv20irnph t63;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t62);
                                        gm_f_format_cqr3tc0((void *)&gm_l29, (void *)NULL, cx, NULL, t63);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                        if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srco4ljquuk5d87, 479, 37);
                                        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                                        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                                        gm_b0_1 t67 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t58, t59, t66);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t68 = gm_l0;
                                        uint64_t t69 = gm_l1;
                                        uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                                        if (o71) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 480, 30);
                                        uint64_t t72 = r70;
                                        void * t73 = gm_l8;
                                        gm_b0_1 t74 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t68, t72, t73);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb6;
                                    }
gm_lb8:;
                                    {
                                        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                        uint8_t t76 = (uint8_t)(t75 == ((uint8_t)1ULL));
                                        if (!t76) {
                                            goto gm_lb9;
                                        }
                                        gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                                        gm_l9 = t77;
                                        void * t78 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 16));
                                        gm_l10 = t78;
                                        gm_trqfvmjtt4sij t79;
                                        gm_f_list_epvducg((void *)&t79, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l11 = t79;
                                        gm_b16_8 t80 = gm_l9;
                                        gm_l12 = t80;
                                        gm_l13 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t81 = gm_l13;
                                                uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                                uint8_t t83 = (t81 < t82);
                                                if (!t83) {
                                                    goto gm_lb10;
                                                }
                                                {
                                                    void * t84 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                                    uint64_t t86 = gm_l13;
                                                    {
                                                        {
                                                            gm_t9805qtonbsag t87 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t84) + (uintptr_t)t86 * 8));
                                                            gm_l14 = t87;
                                                            void * t88 = (void *)((uint8_t *)&F.l11);
                                                            {
                                                                {
                                                                    uint8_t t89 = (uint8_t)((((uint8_t *)&gm_l14))[4] != 0);
                                                                    if (!t89) {
                                                                        goto gm_lb16;
                                                                    }
                                                                    uint32_t t90 = GM_LD(uint32_t, ((uint8_t *)&gm_l14));
                                                                    gm_l15 = t90;
                                                                    void * t91 = gm_l0;
                                                                    uint32_t t92 = gm_l15;
                                                                    gm_b16_8 t93 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t91, t92);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    gm_l30 = t93;
                                                                    goto gm_lb15;
                                                                }
gm_lb16:;
                                                                gm_b16_8 t94;
                                                                GM_ST(void *, (uint8_t *)&t94, (void *)(gm_sfdbfrv46lihd));
                                                                GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(1));
                                                                gm_l30 = t94;
                                                            }
gm_lb15:;
                                                            gm_b16_8 t95 = gm_l30;
                                                            gm_tftb1odc739t5 t96 = gm_f_push_3ef746b(t88, t95);
                                                            gm_l31 = t96;
                                                            uint8_t t97 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                                                            if (!t97) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srco4ljquuk5d87, 484, 31);
                                                            goto gm_lb13;
                                                        }
gm_lb14:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 484, 17);
                                                    }
gm_lb13:;
                                                }
gm_lb12:;
                                                uint64_t t98 = gm_l13;
                                                uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                                                uint64_t t101 = r99;
                                                gm_l13 = t101;
                                            }
                                        }
gm_lb10:;
                                        void * t102 = gm_l0;
                                        uint64_t t103 = gm_l1;
                                        gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                                        gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                                        gm_b16_8 t106;
                                        GM_ST(void *, (uint8_t *)&t106, (void *)(gm_sg5gdnrith2i6));
                                        GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(2));
                                        gm_f_join_sfknljb((void *)&gm_l32, (void *)NULL, cx, NULL, t105, t106);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                        if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_srco4ljquuk5d87, 485, 64);
                                        gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                                        gm_tnpifv20irnph t109;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t109 + 0), t108);
                                        gm_f_format_qhad3qp((void *)&gm_l33, (void *)NULL, cx, NULL, t109);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t110 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                        if (!t110) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srco4ljquuk5d87, 485, 37);
                                        gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                                        gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                                        gm_b0_1 t113 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t102, t103, t112);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t114 = gm_l0;
                                        uint64_t t115 = gm_l1;
                                        uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                                        if (o117) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 486, 30);
                                        uint64_t t118 = r116;
                                        void * t119 = gm_l10;
                                        gm_b0_1 t120 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t114, t118, t119);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb6;
                                    }
gm_lb9:;
                                    {
                                        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                        uint8_t t122 = (uint8_t)(t121 == ((uint8_t)2ULL));
                                        if (!t122) {
                                            goto gm_lb17;
                                        }
                                        void * t123 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                                        gm_l16 = t123;
                                        void * t124 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                                        gm_l17 = t124;
                                        void * t125 = gm_l0;
                                        uint64_t t126 = gm_l1;
                                        gm_b16_8 t127;
                                        GM_ST(void *, (uint8_t *)&t127, (void *)(gm_sugq7dl55ocon));
                                        GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(6));
                                        gm_b0_1 t128 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t125, t126, t127);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t129 = gm_l0;
                                        uint64_t t130 = gm_l1;
                                        uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
                                        if (o132) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 490, 30);
                                        uint64_t t133 = r131;
                                        void * t134 = gm_l16;
                                        gm_b0_1 t135 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t129, t133, t134);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t136 = gm_l0;
                                        uint64_t t137 = gm_l1;
                                        uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                                        if (o139) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 491, 30);
                                        uint64_t t140 = r138;
                                        void * t141 = gm_l17;
                                        gm_b0_1 t142 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t136, t140, t141);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb6;
                                    }
gm_lb17:;
                                    {
                                        uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                        uint8_t t144 = (uint8_t)(t143 == ((uint8_t)3ULL));
                                        if (!t144) {
                                            goto gm_lb18;
                                        }
                                        gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                                        gm_l18 = t145;
                                        void * t146 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 16));
                                        gm_l19 = t146;
                                        void * t147 = gm_l0;
                                        uint64_t t148 = gm_l1;
                                        gm_b16_8 t149;
                                        GM_ST(void *, (uint8_t *)&t149, (void *)(gm_s0ve0dba4rtj8));
                                        GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(12));
                                        gm_b0_1 t150 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t147, t148, t149);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t151 = gm_l18;
                                        gm_l20 = t151;
                                        gm_l21 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t152 = gm_l21;
                                                uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                                uint8_t t154 = (t152 < t153);
                                                if (!t154) {
                                                    goto gm_lb19;
                                                }
                                                {
                                                    void * t155 = GM_LD(void *, ((uint8_t *)&gm_l20));
                                                    uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                                    uint64_t t157 = gm_l21;
                                                    {
                                                        {
                                                            void * t158 = GM_LD(void *, (((uint8_t *)t155) + (uintptr_t)t157 * 8));
                                                            gm_l22 = t158;
                                                            {
                                                                {
                                                                    uint8_t t159 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l22)) != 0);
                                                                    if (!t159) {
                                                                        goto gm_lb25;
                                                                    }
                                                                    void * t160 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                                                    gm_l23 = t160;
                                                                    void * t161 = gm_l0;
                                                                    uint64_t t162 = gm_l1;
                                                                    uint64_t r163; bool o164 = __builtin_add_overflow(t162, ((uint64_t)1ULL), &r163);
                                                                    if (o164) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 497, 38);
                                                                    uint64_t t165 = r163;
                                                                    void * t166 = gm_l23;
                                                                    gm_b0_1 t167 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t161, t165, t166);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    goto gm_lb24;
                                                                }
gm_lb25:;
                                                                void * t168 = gm_l0;
                                                                uint64_t t169 = gm_l1;
                                                                uint64_t r170; bool o171 = __builtin_add_overflow(t169, ((uint64_t)1ULL), &r170);
                                                                if (o171) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 499, 38);
                                                                uint64_t t172 = r170;
                                                                gm_b16_8 t173;
                                                                GM_ST(void *, (uint8_t *)&t173, (void *)(gm_sfdbfrv46lihd));
                                                                GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(1));
                                                                gm_b0_1 t174 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t168, t172, t173);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                            }
gm_lb24:;
                                                            goto gm_lb22;
                                                        }
gm_lb23:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 495, 17);
                                                    }
gm_lb22:;
                                                }
gm_lb21:;
                                                uint64_t t175 = gm_l21;
                                                uint64_t r176; bool o177 = __builtin_add_overflow(t175, ((uint64_t)1ULL), &r176);
                                                uint64_t t178 = r176;
                                                gm_l21 = t178;
                                            }
                                        }
gm_lb19:;
                                        void * t179 = gm_l0;
                                        uint64_t t180 = gm_l1;
                                        uint64_t r181; bool o182 = __builtin_add_overflow(t180, ((uint64_t)1ULL), &r181);
                                        if (o182) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 502, 30);
                                        uint64_t t183 = r181;
                                        void * t184 = gm_l19;
                                        gm_b0_1 t185 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t179, t183, t184);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb6;
                                    }
gm_lb18:;
                                    {
                                        uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                        uint8_t t187 = (uint8_t)(t186 == ((uint8_t)4ULL));
                                        if (!t187) {
                                            goto gm_lb26;
                                        }
                                        void * t188 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                                        gm_l24 = t188;
                                        void * t189 = gm_l0;
                                        uint64_t t190 = gm_l1;
                                        gm_b16_8 t191;
                                        GM_ST(void *, (uint8_t *)&t191, (void *)(gm_sk5s72mtujop8));
                                        GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(4));
                                        gm_b0_1 t192 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t189, t190, t191);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t193 = gm_l0;
                                        uint64_t t194 = gm_l1;
                                        uint64_t r195; bool o196 = __builtin_add_overflow(t194, ((uint64_t)1ULL), &r195);
                                        if (o196) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 506, 30);
                                        uint64_t t197 = r195;
                                        void * t198 = gm_l24;
                                        gm_b0_1 t199 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t193, t197, t198);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb6;
                                    }
gm_lb26:;
                                    {
                                        uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                        uint8_t t201 = (uint8_t)(t200 == ((uint8_t)5ULL));
                                        if (!t201) {
                                            goto gm_lb27;
                                        }
                                        uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 8));
                                        gm_l25 = t202;
                                        void * t203 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                                        gm_l26 = t203;
                                        void * t204 = gm_l0;
                                        uint64_t t205 = gm_l1;
                                        uint8_t t206 = gm_l25;
                                        if (t206) {
                                            gm_b16_8 t207;
                                            GM_ST(void *, (uint8_t *)&t207, (void *)(gm_ser8ip7vtbv6c));
                                            GM_ST(uintptr_t, (uint8_t *)&t207 + 8, (uintptr_t)(8));
                                            gm_l34 = t207;
                                        } else {
                                            gm_b16_8 t208;
                                            GM_ST(void *, (uint8_t *)&t208, (void *)(gm_sllcm1pqagh28));
                                            GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(5));
                                            gm_l34 = t208;
                                        }
                                        gm_b16_8 t209 = gm_l34;
                                        gm_b0_1 t210 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t204, t205, t209);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t211 = gm_l0;
                                        uint64_t t212 = gm_l1;
                                        uint64_t r213; bool o214 = __builtin_add_overflow(t212, ((uint64_t)1ULL), &r213);
                                        if (o214) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 510, 30);
                                        uint64_t t215 = r213;
                                        void * t216 = gm_l26;
                                        gm_b0_1 t217 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t211, t215, t216);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb6;
                                    }
gm_lb27:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 476, 9);
                                }
gm_lb6:;
                                goto gm_lb4;
                            }
gm_lb5:;
                            gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 475, 5);
                        }
gm_lb4:;
                    }
gm_lb3:;
                }
                {
                    uint64_t t218 = gm_l4;
                    uint64_t r219; bool o220 = __builtin_add_overflow(t218, ((uint64_t)1ULL), &r219);
                    uint64_t t221 = r219;
                    gm_l4 = t221;
                }
            }
        }
gm_lb1:;
    }
    {
        {
            {
                void * t222 = gm_l2;
                uint8_t t223 = (uint8_t)(GM_LD(void *, (((uint8_t *)t222) + 16)) != 0);
                if (!t223) {
                    goto gm_lb29;
                }
                void * t224 = GM_LD(void *, (((uint8_t *)t222) + 16));
                gm_l27 = t224;
                void * t225 = gm_l0;
                uint64_t t226 = gm_l1;
                gm_b16_8 t227;
                GM_ST(void *, (uint8_t *)&t227, (void *)(gm_skl3b78lo1gln));
                GM_ST(uintptr_t, (uint8_t *)&t227 + 8, (uintptr_t)(4));
                gm_b0_1 t228 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t225, t226, t227);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t229 = gm_l0;
                uint64_t t230 = gm_l1;
                uint64_t r231; bool o232 = __builtin_add_overflow(t230, ((uint64_t)1ULL), &r231);
                if (o232) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 516, 22);
                uint64_t t233 = r231;
                void * t234 = gm_l27;
                gm_b0_1 t235 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t229, t233, t234);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb28;
            }
gm_lb29:;
        }
gm_lb28:;
        F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_conds_k9e57un(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_write_conds_k9e57un F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l2; /* cond */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tlr9dp305htm7 gm_l5; /* c */
    void * gm_l6; /* x */
    void * gm_l7; /* p */
    void * gm_l8; /* v */
    GM_STACK_CHECK(&F, 365u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t10 = gm_l2;
    gm_l3 = t10;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t11 = gm_l4;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                void * t14 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t16 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t14) + (uintptr_t)t16 * 24), 24);
                        {
                            {
                                uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
                                if (!t18) {
                                    goto gm_lb7;
                                }
                                void * t19 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                                gm_l6 = t19;
                                void * t20 = gm_l0;
                                uint64_t t21 = gm_l1;
                                gm_b16_8 t22;
                                GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sobcm12sogdng));
                                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(4));
                                gm_b0_1 t23 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t20, t21, t22);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t24 = gm_l0;
                                uint64_t t25 = gm_l1;
                                uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                                if (o27) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 525, 30);
                                uint64_t t28 = r26;
                                void * t29 = gm_l6;
                                gm_b0_1 t30 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t24, t28, t29);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                                uint8_t t32 = (uint8_t)(t31 == ((uint8_t)1ULL));
                                if (!t32) {
                                    goto gm_lb8;
                                }
                                void * t33 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                                gm_l7 = t33;
                                void * t34 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                                gm_l8 = t34;
                                void * t35 = gm_l0;
                                uint64_t t36 = gm_l1;
                                gm_b16_8 t37;
                                GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sfid9en449upv));
                                GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(10));
                                gm_b0_1 t38 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t35, t36, t37);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t39 = gm_l0;
                                uint64_t t40 = gm_l1;
                                uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                                if (o42) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 529, 29);
                                uint64_t t43 = r41;
                                void * t44 = gm_l7;
                                gm_b0_1 t45 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t39, t43, t44);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t46 = gm_l0;
                                uint64_t t47 = gm_l1;
                                uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                                if (o49) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 530, 30);
                                uint64_t t50 = r48;
                                void * t51 = gm_l8;
                                gm_b0_1 t52 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t46, t50, t51);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 522, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 521, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t53 = gm_l4;
            uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
            uint64_t t56 = r54;
            gm_l4 = t56;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_expr_cftucor(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_write_expr_cftucor F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    void * gm_l2; /* e */
    void * gm_l3; /* s */
    gm_b16_8 gm_l4; /* ty */
    double gm_l7; /* x */
    void * gm_l8; /* r */
    gm_tandvbt63uk7n gm_l9; /* x */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    void * gm_l13; /* c */
    uint8_t gm_l14; /* b */
    gm_b16_8 gm_l15; /* b */
    gm_b16_8 gm_l16; /* b */
    uint32_t gm_l17; /* p */
    gm_t88rcucg1b7uk gm_l18; /* of */
    uint8_t gm_l19; /* align */
    gm_t88rcucg1b7uk gm_l20; /* of */
    uint8_t gm_l21; /* max */
    gm_t88rcucg1b7uk gm_l22; /* of */
    gm_t88rcucg1b7uk gm_l23; /* a */
    gm_t88rcucg1b7uk gm_l24; /* b */
    uint8_t gm_l25; /* negated */
    gm_b16_8 gm_l26; /* name */
    gm_tl874t2atnlh3 gm_l27; /* span */
    uint32_t gm_l28; /* l */
    gm_b16_8 gm_l29; /* name */
    uint32_t gm_l30; /* levels */
    uint32_t gm_l31; /* l */
    void * gm_l32; /* x */
    void * gm_l33; /* base */
    uint32_t gm_l34; /* index */
    void * gm_l35; /* base */
    void * gm_l36; /* index */
    uint8_t gm_l37; /* checked */
    gm_tao63pk3r18v5 gm_l38; /* sl */
    void * gm_l39; /* x */
    void * gm_l40; /* x */
    void * gm_l41; /* x */
    void * gm_l42; /* x */
    void * gm_l43; /* x */
    void * gm_l44; /* x */
    uint32_t gm_l45; /* f */
    uint32_t gm_l46; /* f */
    uint32_t gm_l47; /* h */
    gm_b16_8 gm_l50; /* <tmp> */
    uint64_t gm_l51; /* <tmp> */
    uint32_t gm_l52; /* c */
    gm_t9qkh0rsooc8d gm_l54; /* op */
    void * gm_l55; /* x */
    gm_ts6oqppjh0ba3 gm_l56; /* op */
    void * gm_l57; /* a */
    void * gm_l58; /* b */
    uint8_t gm_l59; /* is_and */
    void * gm_l60; /* a */
    void * gm_l61; /* b */
    gm_tmn7n0n9raou5 gm_l62; /* kind */
    void * gm_l63; /* x */
    gm_tvdg94bk7aa22 gm_l64; /* op */
    gm_b16_8 gm_l65; /* args */
    uint64_t gm_l66; /* <tmp> */
    uint64_t gm_l67; /* <tmp> */
    uint64_t gm_l68; /* i */
    gm_b16_8 gm_l70; /* args */
    uint32_t gm_l71; /* f */
    gm_tiilmfd16v3fc gm_l72; /* eff */
    uint32_t gm_l73; /* op */
    void * gm_l74; /* c */
    gm_b16_8 gm_l75; /* h */
    uint64_t gm_l76; /* <tmp> */
    uint64_t gm_l77; /* <tmp> */
    uint64_t gm_l78; /* i */
    gm_b16_8 gm_l79; /* xs */
    uint64_t gm_l80; /* <tmp> */
    uint64_t gm_l81; /* <tmp> */
    uint64_t gm_l82; /* i */
    gm_b16_8 gm_l83; /* xs */
    uint64_t gm_l84; /* <tmp> */
    uint64_t gm_l85; /* <tmp> */
    uint64_t gm_l86; /* i */
    void * gm_l87; /* x */
    gm_ttggq7ng4kp3p gm_l88; /* v */
    void * gm_l89; /* p */
    void * gm_l90; /* x */
    gm_t3dcoc7077t1q gm_l91; /* x */
    uint32_t gm_l92; /* c */
    gm_t083kd7b1kkag gm_l93; /* x */
    uint32_t gm_l94; /* v */
    void * gm_l95; /* x */
    uint8_t gm_l96; /* is_result */
    void * gm_l97; /* a */
    void * gm_l98; /* b */
    uint8_t gm_l99; /* negated */
    void * gm_l100; /* a */
    void * gm_l101; /* b */
    uint8_t gm_l102; /* negated */
    void * gm_l103; /* scrutinee */
    gm_b16_8 gm_l104; /* arms */
    gm_b16_8 gm_l105; /* <tmp> */
    uint64_t gm_l106; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l107; /* a */
    void * gm_l108; /* b */
    gm_trl2khmuphncd gm_l109; /* x */
    gm_teo9d4mncjfdu gm_l111; /* x */
    void * gm_l112; /* v */
    gm_te9hrk0u35rev gm_l113; /* t */
    uint32_t gm_l114; /* levels */
    void * gm_l115; /* v */
    void * gm_l116; /* x */
    void * gm_l117; /* k */
    void * gm_l118; /* v */
    void * gm_l119; /* k */
    void * gm_l120; /* w */
    void * gm_l121; /* x */
    void * gm_l122; /* t */
    uint64_t gm_l123; /* <tmp> */
    uint64_t gm_l124; /* <tmp> */
    uint64_t gm_l125; /* i */
    void * gm_l126; /* t */
    uint64_t gm_l128; /* <tmp> */
    uint64_t gm_l129; /* <tmp> */
    uint64_t gm_l130; /* i */
    void * gm_l131; /* w */
    void * gm_l132; /* x */
    void * gm_l133; /* a */
    gm_b16_8 gm_l134; /* head */
    gm_b16_8 gm_l135; /* <tmp> */
    uint64_t gm_l136; /* <tmp> */
    void * gm_l137; /* k */
    GM_STACK_CHECK(&F, 9814u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 gm_l138;
    gm_tabb710d9mq2e gm_l139;
    gm_tabb710d9mq2e gm_l140;
    gm_tabb710d9mq2e gm_l141;
    gm_tftb1odc739t5 gm_l142;
    gm_tabb710d9mq2e gm_l143;
    gm_tabb710d9mq2e gm_l144;
    gm_tabb710d9mq2e gm_l145;
    gm_tabb710d9mq2e gm_l146;
    gm_b16_8 gm_l147;
    gm_tabb710d9mq2e gm_l148;
    gm_b16_8 gm_l149;
    gm_tabb710d9mq2e gm_l150;
    gm_tabb710d9mq2e gm_l151;
    gm_b16_8 gm_l152;
    gm_tabb710d9mq2e gm_l153;
    gm_tabb710d9mq2e gm_l154;
    gm_tabb710d9mq2e gm_l155;
    gm_tabb710d9mq2e gm_l156;
    gm_tabb710d9mq2e gm_l157;
    gm_tftb1odc739t5 gm_l158;
    gm_tftb1odc739t5 gm_l159;
    gm_tabb710d9mq2e gm_l160;
    gm_tftb1odc739t5 gm_l161;
    gm_tftb1odc739t5 gm_l162;
    gm_b16_8 gm_l163;
    gm_tftb1odc739t5 gm_l164;
    gm_tftb1odc739t5 gm_l165;
    gm_tftb1odc739t5 gm_l166;
    gm_b16_8 gm_l167;
    uint8_t gm_l168;
    gm_b16_8 gm_l169;
    uint8_t gm_l170;
    gm_b16_8 gm_l171;
    gm_tabb710d9mq2e gm_l172;
    gm_tftb1odc739t5 gm_l173;
    gm_tftb1odc739t5 gm_l174;
    gm_tftb1odc739t5 gm_l175;
    gm_tftb1odc739t5 gm_l176;
    gm_tabb710d9mq2e gm_l177;
    gm_tabb710d9mq2e gm_l178;
    gm_tftb1odc739t5 gm_l179;
    gm_tabb710d9mq2e gm_l180;
    gm_tabb710d9mq2e gm_l181;
    gm_tftb1odc739t5 gm_l182;
    gm_b16_8 gm_l183;
    gm_tabb710d9mq2e gm_l184;
    gm_tftb1odc739t5 gm_l185;
    gm_tftb1odc739t5 gm_l186;
    gm_tabb710d9mq2e gm_l187;
    gm_tftb1odc739t5 gm_l188;
    gm_tftb1odc739t5 gm_l189;
    gm_b16_8 gm_l190;
    gm_tftb1odc739t5 gm_l191;
    gm_tabb710d9mq2e gm_l192;
    gm_tftb1odc739t5 gm_l193;
    gm_tabb710d9mq2e gm_l194;
    gm_b16_8 gm_l195;
    gm_tabb710d9mq2e gm_l196;
    gm_tabb710d9mq2e gm_l197;
    gm_tftb1odc739t5 gm_l198;
    gm_tftb1odc739t5 gm_l199;
    gm_tftb1odc739t5 gm_l200;
    gm_tftb1odc739t5 gm_l201;
    gm_tftb1odc739t5 gm_l202;
    gm_tftb1odc739t5 gm_l203;
    gm_tabb710d9mq2e gm_l204;
    gm_tftb1odc739t5 gm_l205;
    gm_tftb1odc739t5 gm_l206;
    gm_tftb1odc739t5 gm_l207;
    gm_b16_8 gm_l208;
    gm_tabb710d9mq2e gm_l209;
    gm_tftb1odc739t5 gm_l210;
    gm_b16_8 gm_l211;
    gm_tabb710d9mq2e gm_l212;
    gm_tabb710d9mq2e gm_l213;
    gm_tftb1odc739t5 gm_l214;
    gm_b16_8 gm_l215;
    gm_tftb1odc739t5 gm_l216;
    gm_tftb1odc739t5 gm_l217;
    gm_b16_8 gm_l218;
    gm_tftb1odc739t5 gm_l219;
    gm_tftb1odc739t5 gm_l220;
    gm_b16_8 gm_l221;
    gm_tabb710d9mq2e gm_l222;
    gm_tabb710d9mq2e gm_l223;
    gm_tftb1odc739t5 gm_l224;
    gm_tabb710d9mq2e gm_l225;
    gm_tabb710d9mq2e gm_l226;
    gm_tftb1odc739t5 gm_l227;
    gm_tabb710d9mq2e gm_l228;
    gm_tftb1odc739t5 gm_l229;
    gm_tftb1odc739t5 gm_l230;
    gm_tftb1odc739t5 gm_l231;
    gm_tftb1odc739t5 gm_l232;
    gm_tftb1odc739t5 gm_l233;
    gm_tftb1odc739t5 gm_l234;
    gm_tftb1odc739t5 gm_l235;
    gm_b16_8 gm_l236;
    uint8_t gm_l237;
    gm_tabb710d9mq2e gm_l238;
    gm_tftb1odc739t5 gm_l239;
    gm_tftb1odc739t5 gm_l240;
    gm_tftb1odc739t5 gm_l241;
    gm_tftb1odc739t5 gm_l242;
    gm_tftb1odc739t5 gm_l243;
    gm_tftb1odc739t5 gm_l244;
    gm_tftb1odc739t5 gm_l245;
    gm_tftb1odc739t5 gm_l246;
    gm_tftb1odc739t5 gm_l247;
    gm_tabb710d9mq2e gm_l248;
    gm_tabb710d9mq2e gm_l249;
    {
        void * t150 = gm_l0;
        void * t151 = GM_LD(void *, ((uint8_t *)t150));
        gm_l3 = t151;
        void * t152 = gm_l3;
        void * t153 = gm_l2;
        gm_t88rcucg1b7uk t154 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t153));
        gm_b16_8 t155 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t152, t154);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t155;
        gm_trqfvmjtt4sij t156;
        gm_f_list_s1kpkpr((void *)&t156, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t156;
        void * t157 = gm_l2;
        {
            {
                {
                    uint8_t t158 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t159 = (uint8_t)(t158 == ((uint8_t)0ULL));
                    if (!t159) {
                        goto gm_lb2;
                    }
                    memmove(((uint8_t *)&F.l6), ((((uint8_t *)t157) + 8) + 8), 48);
                    gm_b16_8 t160 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t161;
                    GM_ST(gm_b16_8, ((uint8_t *)&t161 + 0), t160);
                    gm_f_format_ejli3qf((void *)&gm_l139, (void *)NULL, cx, NULL, t161);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t162 = (uint8_t)!((((uint8_t *)&gm_l139))[0] != 0);
                    if (!t162) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l139) + 8, gm_srco4ljquuk5d87, 126, 20);
                    gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l139) + 8));
                    gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&t163));
                    gm_l138 = t164;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t165 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t166 = (uint8_t)(t165 == ((uint8_t)1ULL));
                    if (!t166) {
                        goto gm_lb3;
                    }
                    double t167 = GM_LD(double, ((((uint8_t *)t157) + 8) + 8));
                    gm_l7 = t167;
                    double t168 = gm_l7;
                    uint64_t t169;
                    memset(&t169, 0, sizeof t169); memcpy(&t169, ((uint8_t *)&t168), sizeof t169 < 8 ? sizeof t169 : 8);
                    gm_t4ce0jd3tdro4 t170;
                    GM_ST(uint64_t, ((uint8_t *)&t170 + 0), t169);
                    gm_f_format_booci4l((void *)&gm_l140, (void *)NULL, cx, NULL, t170);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t171 = (uint8_t)!((((uint8_t *)&gm_l140))[0] != 0);
                    if (!t171) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l140) + 8, gm_srco4ljquuk5d87, 127, 22);
                    gm_b16_8 t172 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l140) + 8));
                    gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&t172));
                    gm_l138 = t173;
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t174 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t175 = (uint8_t)(t174 == ((uint8_t)2ULL));
                    if (!t175) {
                        goto gm_lb4;
                    }
                    void * t176 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l8 = t176;
                    void * t177 = gm_l8;
                    gm_b16_8 t178 = gm_f_show_rat_1s624db((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)t177)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t179;
                    GM_ST(gm_b16_8, ((uint8_t *)&t179 + 0), t178);
                    gm_f_format_nva8q4r((void *)&gm_l141, (void *)NULL, cx, NULL, t179);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t180 = (uint8_t)!((((uint8_t *)&gm_l141))[0] != 0);
                    if (!t180) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l141) + 8, gm_srco4ljquuk5d87, 128, 29);
                    gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l141) + 8));
                    gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
                    gm_l138 = t182;
                    goto gm_lb1;
                }
gm_lb4:;
                {
                    uint8_t t183 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t184 = (uint8_t)(t183 == ((uint8_t)58ULL));
                    if (!t184) {
                        goto gm_lb5;
                    }
                    memmove(((uint8_t *)&gm_l9), ((((uint8_t *)t157) + 8) + 8), 24);
                    gm_l11 = ((uint64_t)0ULL);
                    uint64_t t185 = GM_LD(uint64_t, ((((uint8_t *)&gm_l9) + 8) + 8));
                    gm_l10 = t185;
                    {
                        for (;;) {
                            uint64_t t186 = gm_l11;
                            uint64_t t187 = gm_l10;
                            uint8_t t188 = (t186 < t187);
                            if (!t188) {
                                goto gm_lb6;
                            }
                            {
                                {
                                    {
                                        uint64_t t189 = gm_l11;
                                        gm_l12 = t189;
                                        void * t190 = (void *)((uint8_t *)&F.l5);
                                        void * t191 = GM_LD(void *, (((uint8_t *)&gm_l9) + 8));
                                        uint64_t t192 = GM_LD(uint64_t, ((((uint8_t *)&gm_l9) + 8) + 8));
                                        uint64_t t193 = gm_l12;
                                        void * t194 = (void *)(((uint8_t *)t191) + (uintptr_t)t193 * 88);
                                        gm_tftb1odc739t5 t195 = gm_f_push_pf02nv6(t190, t194);
                                        gm_l142 = t195;
                                        uint8_t t196 = (uint8_t)!((((uint8_t *)&gm_l142))[0] != 0);
                                        if (!t196) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l142) + 1, gm_srco4ljquuk5d87, 130, 38);
                                        goto gm_lb9;
                                    }
gm_lb10:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 130, 13);
                                }
gm_lb9:;
                            }
gm_lb8:;
                            uint64_t t197 = gm_l11;
                            uint64_t r198; bool o199 = __builtin_add_overflow(t197, ((uint64_t)1ULL), &r198);
                            uint64_t t200 = r198;
                            gm_l11 = t200;
                        }
                    }
gm_lb6:;
                    gm_tesaltf0mhcki t201 = GM_LD(gm_tesaltf0mhcki, ((uint8_t *)&gm_l9));
                    gm_b16_8 t202 = gm_f_soa_op_text_ud90n2l((void *)NULL, cx, NULL, t201);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t203;
                    GM_ST(gm_b16_8, ((uint8_t *)&t203 + 0), t202);
                    gm_f_format_t0vntpb((void *)&gm_l143, (void *)NULL, cx, NULL, t203);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t204 = (uint8_t)!((((uint8_t *)&gm_l143))[0] != 0);
                    if (!t204) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l143) + 8, gm_srco4ljquuk5d87, 131, 13);
                    gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l143) + 8));
                    gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&t205));
                    gm_l138 = t206;
                    goto gm_lb1;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t207 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t208 = (uint8_t)(t207 == ((uint8_t)3ULL));
                    if (!t208) {
                        goto gm_lb11;
                    }
                    void * t209 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l13 = t209;
                    void * t210 = gm_l13;
                    gm_b16_8 t211 = gm_f_show_rat_1s624db((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)t210)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t212 = gm_l13;
                    gm_b16_8 t213 = gm_f_show_rat_1s624db((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)(((uint8_t *)t212) + 96)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t214;
                    GM_ST(gm_b16_8, ((uint8_t *)&t214 + 0), t211);
                    GM_ST(gm_b16_8, ((uint8_t *)&t214 + 16), t213);
                    gm_f_format_0969kd8((void *)&gm_l144, (void *)NULL, cx, NULL, &t214);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t215 = (uint8_t)!((((uint8_t *)&gm_l144))[0] != 0);
                    if (!t215) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l144) + 8, gm_srco4ljquuk5d87, 133, 31);
                    gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l144) + 8));
                    gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&t216));
                    gm_l138 = t217;
                    goto gm_lb1;
                }
gm_lb11:;
                {
                    uint8_t t218 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t219 = (uint8_t)(t218 == ((uint8_t)4ULL));
                    if (!t219) {
                        goto gm_lb12;
                    }
                    uint8_t t220 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 8));
                    gm_l14 = t220;
                    uint8_t t221 = gm_l14;
                    gm_t9qkh0rsooc8d t222;
                    GM_ST(uint8_t, ((uint8_t *)&t222 + 0), t221);
                    gm_f_format_as0b1t3((void *)&gm_l145, (void *)NULL, cx, NULL, t222);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t223 = (uint8_t)!((((uint8_t *)&gm_l145))[0] != 0);
                    if (!t223) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l145) + 8, gm_srco4ljquuk5d87, 134, 21);
                    gm_b16_8 t224 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l145) + 8));
                    gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&t224));
                    gm_l138 = t225;
                    goto gm_lb1;
                }
gm_lb12:;
                {
                    uint8_t t226 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t227 = (uint8_t)(t226 == ((uint8_t)5ULL));
                    if (!t227) {
                        goto gm_lb13;
                    }
                    gm_b16_8 t228 = GM_LD(gm_b16_8, ((((uint8_t *)t157) + 8) + 8));
                    gm_l15 = t228;
                    gm_b16_8 t229;
                    GM_ST(void *, (uint8_t *)&t229, (void *)(gm_shdnhcpvptc1q));
                    GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(4));
                    gm_b16_8 t230 = gm_l15;
                    gm_b16_8 t231 = gm_f_escaped_k5fm4jl((void *)NULL, cx, NULL, t229, t230);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l138 = t231;
                    goto gm_lb1;
                }
gm_lb13:;
                {
                    uint8_t t232 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t233 = (uint8_t)(t232 == ((uint8_t)6ULL));
                    if (!t233) {
                        goto gm_lb14;
                    }
                    gm_b16_8 t234 = GM_LD(gm_b16_8, ((((uint8_t *)t157) + 8) + 8));
                    gm_l16 = t234;
                    gm_b16_8 t235;
                    GM_ST(void *, (uint8_t *)&t235, (void *)(gm_sdak257j1qgl8));
                    GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(5));
                    gm_b16_8 t236 = gm_l16;
                    gm_b16_8 t237 = gm_f_escaped_k5fm4jl((void *)NULL, cx, NULL, t235, t236);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l138 = t237;
                    goto gm_lb1;
                }
gm_lb14:;
                {
                    uint8_t t238 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t239 = (uint8_t)(t238 == ((uint8_t)7ULL));
                    if (!t239) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t240;
                    GM_ST(void *, (uint8_t *)&t240, (void *)(gm_squedv51820an));
                    GM_ST(uintptr_t, (uint8_t *)&t240 + 8, (uintptr_t)(4));
                    gm_l138 = t240;
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t241 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t242 = (uint8_t)(t241 == ((uint8_t)8ULL));
                    if (!t242) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t243;
                    GM_ST(void *, (uint8_t *)&t243, (void *)(gm_sr899mamktpvo));
                    GM_ST(uintptr_t, (uint8_t *)&t243 + 8, (uintptr_t)(6));
                    gm_l138 = t243;
                    goto gm_lb1;
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t244 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t245 = (uint8_t)(t244 == ((uint8_t)9ULL));
                    if (!t245) {
                        goto gm_lb17;
                    }
                    uint32_t t246 = GM_LD(uint32_t, ((((uint8_t *)t157) + 8) + 8));
                    gm_l17 = t246;
                    void * t247 = gm_l0;
                    void * t248 = GM_LD(void *, ((uint8_t *)t247));
                    void * t249 = (void *)(((uint8_t *)t248) + 8);
                    uint32_t t250 = gm_l17;
                    gm_b16_8 t251 = gm_f_param_name_rbnop5l(t249, t250);
                    gm_tnpifv20irnph t252;
                    GM_ST(gm_b16_8, ((uint8_t *)&t252 + 0), t251);
                    gm_f_format_r9ba7g9((void *)&gm_l146, (void *)NULL, cx, NULL, t252);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t253 = (uint8_t)!((((uint8_t *)&gm_l146))[0] != 0);
                    if (!t253) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l146) + 8, gm_srco4ljquuk5d87, 139, 27);
                    gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l146) + 8));
                    gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                    gm_l138 = t255;
                    goto gm_lb1;
                }
gm_lb17:;
                {
                    uint8_t t256 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t257 = (uint8_t)(t256 == ((uint8_t)10ULL));
                    if (!t257) {
                        goto gm_lb18;
                    }
                    gm_t88rcucg1b7uk t258 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t157) + 8) + 8));
                    gm_l18 = t258;
                    uint8_t t259 = GM_LD(uint8_t, (((((uint8_t *)t157) + 8) + 8) + 4));
                    gm_l19 = t259;
                    uint8_t t260 = gm_l19;
                    if (t260) {
                        gm_b16_8 t261;
                        GM_ST(void *, (uint8_t *)&t261, (void *)(gm_s8gtcifve79ai));
                        GM_ST(uintptr_t, (uint8_t *)&t261 + 8, (uintptr_t)(8));
                        gm_l147 = t261;
                    } else {
                        gm_b16_8 t262;
                        GM_ST(void *, (uint8_t *)&t262, (void *)(gm_sjegrp8u9r3n3));
                        GM_ST(uintptr_t, (uint8_t *)&t262 + 8, (uintptr_t)(7));
                        gm_l147 = t262;
                    }
                    gm_b16_8 t263 = gm_l147;
                    void * t264 = gm_l3;
                    gm_t88rcucg1b7uk t265 = gm_l18;
                    gm_b16_8 t266 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t264, t265);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t267;
                    GM_ST(gm_b16_8, ((uint8_t *)&t267 + 0), t263);
                    GM_ST(gm_b16_8, ((uint8_t *)&t267 + 16), t266);
                    gm_f_format_2lgoflc((void *)&gm_l148, (void *)NULL, cx, NULL, &t267);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t268 = (uint8_t)!((((uint8_t *)&gm_l148))[0] != 0);
                    if (!t268) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l148) + 8, gm_srco4ljquuk5d87, 140, 31);
                    gm_b16_8 t269 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l148) + 8));
                    gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&t269));
                    gm_l138 = t270;
                    goto gm_lb1;
                }
gm_lb18:;
                {
                    uint8_t t271 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t272 = (uint8_t)(t271 == ((uint8_t)11ULL));
                    if (!t272) {
                        goto gm_lb19;
                    }
                    gm_t88rcucg1b7uk t273 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t157) + 8) + 8));
                    gm_l20 = t273;
                    uint8_t t274 = GM_LD(uint8_t, (((((uint8_t *)t157) + 8) + 8) + 4));
                    gm_l21 = t274;
                    uint8_t t275 = gm_l21;
                    if (t275) {
                        gm_b16_8 t276;
                        GM_ST(void *, (uint8_t *)&t276, (void *)(gm_s0lh54534p3vd));
                        GM_ST(uintptr_t, (uint8_t *)&t276 + 8, (uintptr_t)(9));
                        gm_l149 = t276;
                    } else {
                        gm_b16_8 t277;
                        GM_ST(void *, (uint8_t *)&t277, (void *)(gm_sp2smaof0i4j2));
                        GM_ST(uintptr_t, (uint8_t *)&t277 + 8, (uintptr_t)(9));
                        gm_l149 = t277;
                    }
                    gm_b16_8 t278 = gm_l149;
                    void * t279 = gm_l3;
                    gm_t88rcucg1b7uk t280 = gm_l20;
                    gm_b16_8 t281 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t279, t280);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t282;
                    GM_ST(gm_b16_8, ((uint8_t *)&t282 + 0), t278);
                    GM_ST(gm_b16_8, ((uint8_t *)&t282 + 16), t281);
                    gm_f_format_2lgoflc((void *)&gm_l150, (void *)NULL, cx, NULL, &t282);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t283 = (uint8_t)!((((uint8_t *)&gm_l150))[0] != 0);
                    if (!t283) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l150) + 8, gm_srco4ljquuk5d87, 141, 31);
                    gm_b16_8 t284 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l150) + 8));
                    gm_b16_8 t285 = GM_LD(gm_b16_8, ((uint8_t *)&t284));
                    gm_l138 = t285;
                    goto gm_lb1;
                }
gm_lb19:;
            }
            {
                {
                    uint8_t t286 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t287 = (uint8_t)(t286 == ((uint8_t)12ULL));
                    if (!t287) {
                        goto gm_lb20;
                    }
                    gm_t88rcucg1b7uk t288 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t157) + 8) + 8));
                    gm_l22 = t288;
                    void * t289 = gm_l3;
                    gm_t88rcucg1b7uk t290 = gm_l22;
                    gm_b16_8 t291 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t289, t290);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t292;
                    GM_ST(gm_b16_8, ((uint8_t *)&t292 + 0), t291);
                    gm_f_format_t725p24((void *)&gm_l151, (void *)NULL, cx, NULL, t292);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t293 = (uint8_t)!((((uint8_t *)&gm_l151))[0] != 0);
                    if (!t293) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l151) + 8, gm_srco4ljquuk5d87, 142, 26);
                    gm_b16_8 t294 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l151) + 8));
                    gm_b16_8 t295 = GM_LD(gm_b16_8, ((uint8_t *)&t294));
                    gm_l138 = t295;
                    goto gm_lb1;
                }
gm_lb20:;
                {
                    uint8_t t296 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t297 = (uint8_t)(t296 == ((uint8_t)13ULL));
                    if (!t297) {
                        goto gm_lb21;
                    }
                    gm_t88rcucg1b7uk t298 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t157) + 8) + 8));
                    gm_l23 = t298;
                    gm_t88rcucg1b7uk t299 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t157) + 8) + 8) + 4));
                    gm_l24 = t299;
                    uint8_t t300 = GM_LD(uint8_t, (((((uint8_t *)t157) + 8) + 8) + 8));
                    gm_l25 = t300;
                    uint8_t t301 = gm_l25;
                    if (t301) {
                        gm_b16_8 t302;
                        GM_ST(void *, (uint8_t *)&t302, (void *)(gm_sngdu4l8vh3l8));
                        GM_ST(uintptr_t, (uint8_t *)&t302 + 8, (uintptr_t)(4));
                        gm_l152 = t302;
                    } else {
                        gm_b16_8 t303;
                        GM_ST(void *, (uint8_t *)&t303, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t303 + 8, (uintptr_t)(0));
                        gm_l152 = t303;
                    }
                    gm_b16_8 t304 = gm_l152;
                    void * t305 = gm_l3;
                    gm_t88rcucg1b7uk t306 = gm_l23;
                    gm_b16_8 t307 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t305, t306);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t308 = gm_l3;
                    gm_t88rcucg1b7uk t309 = gm_l24;
                    gm_b16_8 t310 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t308, t309);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tfeab279c16rr t311;
                    GM_ST(gm_b16_8, ((uint8_t *)&t311 + 0), t304);
                    GM_ST(gm_b16_8, ((uint8_t *)&t311 + 16), t307);
                    GM_ST(gm_b16_8, ((uint8_t *)&t311 + 32), t310);
                    gm_f_format_arnesr1((void *)&gm_l153, (void *)NULL, cx, NULL, &t311);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t312 = (uint8_t)!((((uint8_t *)&gm_l153))[0] != 0);
                    if (!t312) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l153) + 8, gm_srco4ljquuk5d87, 143, 35);
                    gm_b16_8 t313 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l153) + 8));
                    gm_b16_8 t314 = GM_LD(gm_b16_8, ((uint8_t *)&t313));
                    gm_l138 = t314;
                    goto gm_lb1;
                }
gm_lb21:;
                {
                    uint8_t t315 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t316 = (uint8_t)(t315 == ((uint8_t)14ULL));
                    if (!t316) {
                        goto gm_lb22;
                    }
                    gm_b16_8 t317 = GM_LD(gm_b16_8, ((((uint8_t *)t157) + 8) + 8));
                    gm_l26 = t317;
                    gm_tl874t2atnlh3 t318 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t157) + 8) + 8) + 16));
                    gm_l27 = t318;
                    gm_b16_8 t319 = gm_l26;
                    void * t320 = gm_l3;
                    gm_tl874t2atnlh3 t321 = gm_l27;
                    gm_b16_8 t322 = gm_f_place_lj97s4p((void *)NULL, cx, NULL, t320, t321);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t323;
                    GM_ST(gm_b16_8, ((uint8_t *)&t323 + 0), t319);
                    GM_ST(gm_b16_8, ((uint8_t *)&t323 + 16), t322);
                    gm_f_format_5up8eqa((void *)&gm_l154, (void *)NULL, cx, NULL, &t323);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t324 = (uint8_t)!((((uint8_t *)&gm_l154))[0] != 0);
                    if (!t324) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l154) + 8, gm_srco4ljquuk5d87, 144, 31);
                    gm_b16_8 t325 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l154) + 8));
                    gm_b16_8 t326 = GM_LD(gm_b16_8, ((uint8_t *)&t325));
                    gm_l138 = t326;
                    goto gm_lb1;
                }
gm_lb22:;
            }
            {
                {
                    uint8_t t327 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t328 = (uint8_t)(t327 == ((uint8_t)15ULL));
                    if (!t328) {
                        goto gm_lb23;
                    }
                    uint32_t t329 = GM_LD(uint32_t, ((((uint8_t *)t157) + 8) + 8));
                    gm_l28 = t329;
                    void * t330 = gm_l0;
                    uint32_t t331 = gm_l28;
                    gm_b16_8 t332 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t330, t331);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t333;
                    GM_ST(gm_b16_8, ((uint8_t *)&t333 + 0), t332);
                    gm_f_format_32bp3b4((void *)&gm_l155, (void *)NULL, cx, NULL, t333);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t334 = (uint8_t)!((((uint8_t *)&gm_l155))[0] != 0);
                    if (!t334) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l155) + 8, gm_srco4ljquuk5d87, 145, 22);
                    gm_b16_8 t335 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l155) + 8));
                    gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&t335));
                    gm_l138 = t336;
                    goto gm_lb1;
                }
gm_lb23:;
                {
                    uint8_t t337 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t338 = (uint8_t)(t337 == ((uint8_t)17ULL));
                    if (!t338) {
                        goto gm_lb24;
                    }
                    gm_b16_8 t339 = GM_LD(gm_b16_8, ((((uint8_t *)t157) + 8) + 8));
                    gm_l29 = t339;
                    gm_b16_8 t340 = gm_l29;
                    gm_tnpifv20irnph t341;
                    GM_ST(gm_b16_8, ((uint8_t *)&t341 + 0), t340);
                    gm_f_format_fi775k0((void *)&gm_l156, (void *)NULL, cx, NULL, t341);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t342 = (uint8_t)!((((uint8_t *)&gm_l156))[0] != 0);
                    if (!t342) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l156) + 8, gm_srco4ljquuk5d87, 146, 29);
                    gm_b16_8 t343 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l156) + 8));
                    gm_b16_8 t344 = GM_LD(gm_b16_8, ((uint8_t *)&t343));
                    gm_l138 = t344;
                    goto gm_lb1;
                }
gm_lb24:;
                {
                    uint8_t t345 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t346 = (uint8_t)(t345 == ((uint8_t)16ULL));
                    if (!t346) {
                        goto gm_lb25;
                    }
                    uint32_t t347 = GM_LD(uint32_t, ((((uint8_t *)t157) + 8) + 8));
                    gm_l30 = t347;
                    uint32_t t348 = GM_LD(uint32_t, (((((uint8_t *)t157) + 8) + 8) + 4));
                    gm_l31 = t348;
                    uint32_t t349 = gm_l30;
                    uint32_t t350 = gm_l31;
                    gm_tcc1rq4n1avb2 t351;
                    GM_ST(uint32_t, ((uint8_t *)&t351 + 0), t349);
                    GM_ST(uint32_t, ((uint8_t *)&t351 + 4), t350);
                    gm_f_format_16dq22e((void *)&gm_l157, (void *)NULL, cx, NULL, t351);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t352 = (uint8_t)!((((uint8_t *)&gm_l157))[0] != 0);
                    if (!t352) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l157) + 8, gm_srco4ljquuk5d87, 147, 30);
                    gm_b16_8 t353 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l157) + 8));
                    gm_b16_8 t354 = GM_LD(gm_b16_8, ((uint8_t *)&t353));
                    gm_l138 = t354;
                    goto gm_lb1;
                }
gm_lb25:;
                {
                    uint8_t t355 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t356 = (uint8_t)(t355 == ((uint8_t)18ULL));
                    if (!t356) {
                        goto gm_lb26;
                    }
                    void * t357 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l32 = t357;
                    void * t358 = (void *)((uint8_t *)&F.l5);
                    void * t359 = gm_l32;
                    gm_tftb1odc739t5 t360 = gm_f_push_pf02nv6(t358, t359);
                    gm_l158 = t360;
                    uint8_t t361 = (uint8_t)!((((uint8_t *)&gm_l158))[0] != 0);
                    if (!t361) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l158) + 1, gm_srco4ljquuk5d87, 149, 13);
                    gm_b16_8 t362;
                    GM_ST(void *, (uint8_t *)&t362, (void *)(gm_sh4niqnmh73co));
                    GM_ST(uintptr_t, (uint8_t *)&t362 + 8, (uintptr_t)(5));
                    gm_l138 = t362;
                    goto gm_lb1;
                }
gm_lb26:;
            }
            {
                {
                    uint8_t t363 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t364 = (uint8_t)(t363 == ((uint8_t)19ULL));
                    if (!t364) {
                        goto gm_lb27;
                    }
                    void * t365 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l33 = t365;
                    uint32_t t366 = GM_LD(uint32_t, (((((uint8_t *)t157) + 8) + 8) + 8));
                    gm_l34 = t366;
                    void * t367 = (void *)((uint8_t *)&F.l5);
                    void * t368 = gm_l33;
                    gm_tftb1odc739t5 t369 = gm_f_push_pf02nv6(t367, t368);
                    gm_l159 = t369;
                    uint8_t t370 = (uint8_t)!((((uint8_t *)&gm_l159))[0] != 0);
                    if (!t370) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l159) + 1, gm_srco4ljquuk5d87, 153, 13);
                    uint32_t t371 = gm_l34;
                    gm_t88rcucg1b7uk t372;
                    GM_ST(uint32_t, ((uint8_t *)&t372 + 0), t371);
                    gm_f_format_208te7b((void *)&gm_l160, (void *)NULL, cx, NULL, t372);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t373 = (uint8_t)!((((uint8_t *)&gm_l160))[0] != 0);
                    if (!t373) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l160) + 8, gm_srco4ljquuk5d87, 154, 13);
                    gm_b16_8 t374 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l160) + 8));
                    gm_b16_8 t375 = GM_LD(gm_b16_8, ((uint8_t *)&t374));
                    gm_l138 = t375;
                    goto gm_lb1;
                }
gm_lb27:;
                {
                    uint8_t t376 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t377 = (uint8_t)(t376 == ((uint8_t)20ULL));
                    if (!t377) {
                        goto gm_lb28;
                    }
                    void * t378 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l35 = t378;
                    void * t379 = GM_LD(void *, (((((uint8_t *)t157) + 8) + 8) + 8));
                    gm_l36 = t379;
                    uint8_t t380 = GM_LD(uint8_t, (((((uint8_t *)t157) + 8) + 8) + 16));
                    gm_l37 = t380;
                    void * t381 = (void *)((uint8_t *)&F.l5);
                    void * t382 = gm_l35;
                    gm_tftb1odc739t5 t383 = gm_f_push_pf02nv6(t381, t382);
                    gm_l161 = t383;
                    uint8_t t384 = (uint8_t)!((((uint8_t *)&gm_l161))[0] != 0);
                    if (!t384) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l161) + 1, gm_srco4ljquuk5d87, 157, 13);
                    void * t385 = (void *)((uint8_t *)&F.l5);
                    void * t386 = gm_l36;
                    gm_tftb1odc739t5 t387 = gm_f_push_pf02nv6(t385, t386);
                    gm_l162 = t387;
                    uint8_t t388 = (uint8_t)!((((uint8_t *)&gm_l162))[0] != 0);
                    if (!t388) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l162) + 1, gm_srco4ljquuk5d87, 158, 13);
                    uint8_t t389 = gm_l37;
                    if (t389) {
                        gm_b16_8 t390;
                        GM_ST(void *, (uint8_t *)&t390, (void *)(gm_s5dbqnns2epkg));
                        GM_ST(uintptr_t, (uint8_t *)&t390 + 8, (uintptr_t)(5));
                        gm_l163 = t390;
                    } else {
                        gm_b16_8 t391;
                        GM_ST(void *, (uint8_t *)&t391, (void *)(gm_ssgn9mppmu3vl));
                        GM_ST(uintptr_t, (uint8_t *)&t391 + 8, (uintptr_t)(15));
                        gm_l163 = t391;
                    }
                    gm_b16_8 t392 = gm_l163;
                    gm_l138 = t392;
                    goto gm_lb1;
                }
gm_lb28:;
                {
                    uint8_t t393 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t394 = (uint8_t)(t393 == ((uint8_t)21ULL));
                    if (!t394) {
                        goto gm_lb29;
                    }
                    memmove(((uint8_t *)&gm_l38), ((((uint8_t *)t157) + 8) + 8), 32);
                    void * t395 = (void *)((uint8_t *)&F.l5);
                    void * t396 = GM_LD(void *, ((uint8_t *)&gm_l38));
                    gm_tftb1odc739t5 t397 = gm_f_push_pf02nv6(t395, t396);
                    gm_l164 = t397;
                    uint8_t t398 = (uint8_t)!((((uint8_t *)&gm_l164))[0] != 0);
                    if (!t398) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l164) + 1, gm_srco4ljquuk5d87, 162, 13);
                    {
                        {
                            uint8_t t399 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l38) + 8)) != 0);
                            if (!t399) {
                                goto gm_lb31;
                            }
                            void * t400 = GM_LD(void *, (((uint8_t *)&gm_l38) + 8));
                            gm_l39 = t400;
                            void * t401 = (void *)((uint8_t *)&F.l5);
                            void * t402 = gm_l39;
                            gm_tftb1odc739t5 t403 = gm_f_push_pf02nv6(t401, t402);
                            gm_l165 = t403;
                            uint8_t t404 = (uint8_t)!((((uint8_t *)&gm_l165))[0] != 0);
                            if (!t404) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l165) + 1, gm_srco4ljquuk5d87, 163, 29);
                            goto gm_lb30;
                        }
gm_lb31:;
                    }
gm_lb30:;
                    {
                        {
                            uint8_t t405 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l38) + 16)) != 0);
                            if (!t405) {
                                goto gm_lb33;
                            }
                            void * t406 = GM_LD(void *, (((uint8_t *)&gm_l38) + 16));
                            gm_l40 = t406;
                            void * t407 = (void *)((uint8_t *)&F.l5);
                            void * t408 = gm_l40;
                            gm_tftb1odc739t5 t409 = gm_f_push_pf02nv6(t407, t408);
                            gm_l166 = t409;
                            uint8_t t410 = (uint8_t)!((((uint8_t *)&gm_l166))[0] != 0);
                            if (!t410) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l166) + 1, gm_srco4ljquuk5d87, 164, 29);
                            goto gm_lb32;
                        }
gm_lb33:;
                    }
gm_lb32:;
                    {
                        {
                            uint8_t t411 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l38) + 8)) != 0);
                            if (t411) {
                                goto gm_lb35;
                            }
                            gm_l168 = ((uint8_t)0);
                            goto gm_lb34;
                        }
gm_lb35:;
                        {
                            gm_l168 = ((uint8_t)1);
                            goto gm_lb34;
                        }
gm_lb36:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 165, 42);
                    }
gm_lb34:;
                    uint8_t t412 = gm_l168;
                    if (t412) {
                        gm_b16_8 t413;
                        GM_ST(void *, (uint8_t *)&t413, (void *)(gm_sq0h7jjc6dtk5));
                        GM_ST(uintptr_t, (uint8_t *)&t413 + 8, (uintptr_t)(3));
                        gm_l167 = t413;
                    } else {
                        gm_b16_8 t414;
                        GM_ST(void *, (uint8_t *)&t414, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t414 + 8, (uintptr_t)(0));
                        gm_l167 = t414;
                    }
                    gm_b16_8 t415 = gm_l167;
                    {
                        {
                            uint8_t t416 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l38) + 16)) != 0);
                            if (t416) {
                                goto gm_lb38;
                            }
                            gm_l170 = ((uint8_t)0);
                            goto gm_lb37;
                        }
gm_lb38:;
                        {
                            gm_l170 = ((uint8_t)1);
                            goto gm_lb37;
                        }
gm_lb39:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 165, 86);
                    }
gm_lb37:;
                    uint8_t t417 = gm_l170;
                    if (t417) {
                        gm_b16_8 t418;
                        GM_ST(void *, (uint8_t *)&t418, (void *)(gm_s9v13eck7gubi));
                        GM_ST(uintptr_t, (uint8_t *)&t418 + 8, (uintptr_t)(3));
                        gm_l169 = t418;
                    } else {
                        gm_b16_8 t419;
                        GM_ST(void *, (uint8_t *)&t419, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t419 + 8, (uintptr_t)(0));
                        gm_l169 = t419;
                    }
                    gm_b16_8 t420 = gm_l169;
                    uint8_t t421 = GM_LD(uint8_t, (((uint8_t *)&gm_l38) + 24));
                    if (t421) {
                        gm_b16_8 t422;
                        GM_ST(void *, (uint8_t *)&t422, (void *)(gm_sksprenpp79ih));
                        GM_ST(uintptr_t, (uint8_t *)&t422 + 8, (uintptr_t)(10));
                        gm_l171 = t422;
                    } else {
                        gm_b16_8 t423;
                        GM_ST(void *, (uint8_t *)&t423, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t423 + 8, (uintptr_t)(0));
                        gm_l171 = t423;
                    }
                    gm_b16_8 t424 = gm_l171;
                    gm_tfeab279c16rr t425;
                    GM_ST(gm_b16_8, ((uint8_t *)&t425 + 0), t415);
                    GM_ST(gm_b16_8, ((uint8_t *)&t425 + 16), t420);
                    GM_ST(gm_b16_8, ((uint8_t *)&t425 + 32), t424);
                    gm_f_format_oc8ipo6((void *)&gm_l172, (void *)NULL, cx, NULL, &t425);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t426 = (uint8_t)!((((uint8_t *)&gm_l172))[0] != 0);
                    if (!t426) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l172) + 8, gm_srco4ljquuk5d87, 165, 13);
                    gm_b16_8 t427 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l172) + 8));
                    gm_b16_8 t428 = GM_LD(gm_b16_8, ((uint8_t *)&t427));
                    gm_l138 = t428;
                    goto gm_lb1;
                }
gm_lb29:;
            }
            {
                {
                    uint8_t t429 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t430 = (uint8_t)(t429 == ((uint8_t)22ULL));
                    if (!t430) {
                        goto gm_lb40;
                    }
                    void * t431 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l41 = t431;
                    void * t432 = (void *)((uint8_t *)&F.l5);
                    void * t433 = gm_l41;
                    gm_tftb1odc739t5 t434 = gm_f_push_pf02nv6(t432, t433);
                    gm_l173 = t434;
                    uint8_t t435 = (uint8_t)!((((uint8_t *)&gm_l173))[0] != 0);
                    if (!t435) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l173) + 1, gm_srco4ljquuk5d87, 168, 13);
                    gm_b16_8 t436;
                    GM_ST(void *, (uint8_t *)&t436, (void *)(gm_sstrtuk2b4jdg));
                    GM_ST(uintptr_t, (uint8_t *)&t436 + 8, (uintptr_t)(9));
                    gm_l138 = t436;
                    goto gm_lb1;
                }
gm_lb40:;
                {
                    uint8_t t437 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t438 = (uint8_t)(t437 == ((uint8_t)23ULL));
                    if (!t438) {
                        goto gm_lb41;
                    }
                    void * t439 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l42 = t439;
                    void * t440 = (void *)((uint8_t *)&F.l5);
                    void * t441 = gm_l42;
                    gm_tftb1odc739t5 t442 = gm_f_push_pf02nv6(t440, t441);
                    gm_l174 = t442;
                    uint8_t t443 = (uint8_t)!((((uint8_t *)&gm_l174))[0] != 0);
                    if (!t443) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l174) + 1, gm_srco4ljquuk5d87, 172, 13);
                    gm_b16_8 t444;
                    GM_ST(void *, (uint8_t *)&t444, (void *)(gm_sedrsudco1u16));
                    GM_ST(uintptr_t, (uint8_t *)&t444 + 8, (uintptr_t)(9));
                    gm_l138 = t444;
                    goto gm_lb1;
                }
gm_lb41:;
                {
                    uint8_t t445 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t446 = (uint8_t)(t445 == ((uint8_t)24ULL));
                    if (!t446) {
                        goto gm_lb42;
                    }
                    void * t447 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l43 = t447;
                    void * t448 = (void *)((uint8_t *)&F.l5);
                    void * t449 = gm_l43;
                    gm_tftb1odc739t5 t450 = gm_f_push_pf02nv6(t448, t449);
                    gm_l175 = t450;
                    uint8_t t451 = (uint8_t)!((((uint8_t *)&gm_l175))[0] != 0);
                    if (!t451) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l175) + 1, gm_srco4ljquuk5d87, 176, 13);
                    gm_b16_8 t452;
                    GM_ST(void *, (uint8_t *)&t452, (void *)(gm_sukutl224cgkm));
                    GM_ST(uintptr_t, (uint8_t *)&t452 + 8, (uintptr_t)(7));
                    gm_l138 = t452;
                    goto gm_lb1;
                }
gm_lb42:;
                {
                    uint8_t t453 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t454 = (uint8_t)(t453 == ((uint8_t)25ULL));
                    if (!t454) {
                        goto gm_lb43;
                    }
                    void * t455 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l44 = t455;
                    void * t456 = (void *)((uint8_t *)&F.l5);
                    void * t457 = gm_l44;
                    gm_tftb1odc739t5 t458 = gm_f_push_pf02nv6(t456, t457);
                    gm_l176 = t458;
                    uint8_t t459 = (uint8_t)!((((uint8_t *)&gm_l176))[0] != 0);
                    if (!t459) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l176) + 1, gm_srco4ljquuk5d87, 180, 13);
                    gm_b16_8 t460;
                    GM_ST(void *, (uint8_t *)&t460, (void *)(gm_slgrfk23llffe));
                    GM_ST(uintptr_t, (uint8_t *)&t460 + 8, (uintptr_t)(12));
                    gm_l138 = t460;
                    goto gm_lb1;
                }
gm_lb43:;
                {
                    uint8_t t461 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t462 = (uint8_t)(t461 == ((uint8_t)26ULL));
                    if (!t462) {
                        goto gm_lb44;
                    }
                    uint32_t t463 = GM_LD(uint32_t, ((((uint8_t *)t157) + 8) + 8));
                    gm_l45 = t463;
                    void * t464 = gm_l3;
                    uint32_t t465 = gm_l45;
                    gm_b16_8 t466 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t464, t465);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t467;
                    GM_ST(gm_b16_8, ((uint8_t *)&t467 + 0), t466);
                    gm_f_format_sugh2p3((void *)&gm_l177, (void *)NULL, cx, NULL, t467);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t468 = (uint8_t)!((((uint8_t *)&gm_l177))[0] != 0);
                    if (!t468) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l177) + 8, gm_srco4ljquuk5d87, 183, 22);
                    gm_b16_8 t469 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l177) + 8));
                    gm_b16_8 t470 = GM_LD(gm_b16_8, ((uint8_t *)&t469));
                    gm_l138 = t470;
                    goto gm_lb1;
                }
gm_lb44:;
            }
            {
                {
                    uint8_t t471 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t472 = (uint8_t)(t471 == ((uint8_t)27ULL));
                    if (!t472) {
                        goto gm_lb45;
                    }
                    uint32_t t473 = GM_LD(uint32_t, ((((uint8_t *)t157) + 8) + 8));
                    gm_l46 = t473;
                    void * t474 = gm_l3;
                    uint32_t t475 = gm_l46;
                    gm_b16_8 t476 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t474, t475);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t477;
                    GM_ST(gm_b16_8, ((uint8_t *)&t477 + 0), t476);
                    gm_f_format_ka8hqpj((void *)&gm_l178, (void *)NULL, cx, NULL, t477);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t478 = (uint8_t)!((((uint8_t *)&gm_l178))[0] != 0);
                    if (!t478) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l178) + 8, gm_srco4ljquuk5d87, 184, 24);
                    gm_b16_8 t479 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l178) + 8));
                    gm_b16_8 t480 = GM_LD(gm_b16_8, ((uint8_t *)&t479));
                    gm_l138 = t480;
                    goto gm_lb1;
                }
gm_lb45:;
                {
                    uint8_t t481 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t482 = (uint8_t)(t481 == ((uint8_t)28ULL));
                    if (!t482) {
                        goto gm_lb46;
                    }
                    uint32_t t483 = GM_LD(uint32_t, ((((uint8_t *)t157) + 8) + 8));
                    gm_l47 = t483;
                    void * t484 = gm_l3;
                    void * t485 = GM_LD(void *, (((uint8_t *)t484) + 504));
                    uint64_t t486 = GM_LD(uint64_t, ((((uint8_t *)t484) + 504) + 8));
                    uint32_t t487 = gm_l47;
                    uint64_t t488 = (uint64_t)t487;
                    if ((uintptr_t)t488 >= (uintptr_t)(t486)) gm_trap_index((uintptr_t)t488, t486, gm_srco4ljquuk5d87, 186, 20);
                    memmove(((uint8_t *)&F.l48), (((uint8_t *)t485) + (uintptr_t)t488 * 40), 40);
                    gm_trqfvmjtt4sij t489;
                    gm_f_list_epvducg((void *)&t489, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l49 = t489;
                    gm_b16_8 t490 = GM_LD(gm_b16_8, (((uint8_t *)&F.l48) + 24));
                    gm_l50 = t490;
                    gm_l51 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t491 = gm_l51;
                            uint64_t t492 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                            uint8_t t493 = (t491 < t492);
                            if (!t493) {
                                goto gm_lb47;
                            }
                            {
                                void * t494 = GM_LD(void *, ((uint8_t *)&gm_l50));
                                uint64_t t495 = GM_LD(uint64_t, (((uint8_t *)&gm_l50) + 8));
                                uint64_t t496 = gm_l51;
                                {
                                    {
                                        uint32_t t497 = GM_LD(uint32_t, (((uint8_t *)t494) + (uintptr_t)t496 * 4));
                                        gm_l52 = t497;
                                        void * t498 = (void *)((uint8_t *)&F.l49);
                                        void * t499 = gm_l3;
                                        uint32_t t500 = gm_l52;
                                        gm_b16_8 t501 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t499, t500);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t502 = gm_f_push_3ef746b(t498, t501);
                                        gm_l179 = t502;
                                        uint8_t t503 = (uint8_t)!((((uint8_t *)&gm_l179))[0] != 0);
                                        if (!t503) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l179) + 1, gm_srco4ljquuk5d87, 188, 36);
                                        goto gm_lb50;
                                    }
gm_lb51:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 188, 13);
                                }
gm_lb50:;
                            }
gm_lb49:;
                            uint64_t t504 = gm_l51;
                            uint64_t r505; bool o506 = __builtin_add_overflow(t504, ((uint64_t)1ULL), &r505);
                            uint64_t t507 = r505;
                            gm_l51 = t507;
                        }
                    }
gm_lb47:;
                    void * t508 = gm_l3;
                    void * t509 = (void *)(((uint8_t *)t508) + 8);
                    void * t510 = (void *)((uint8_t *)&F.l48);
                    gm_b16_8 t511 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t509, t510);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t512 = GM_LD(gm_b16_8, ((uint8_t *)&F.l49));
                    gm_b16_8 t513 = GM_LD(gm_b16_8, ((uint8_t *)&t512));
                    gm_b16_8 t514;
                    GM_ST(void *, (uint8_t *)&t514, (void *)(gm_sg5gdnrith2i6));
                    GM_ST(uintptr_t, (uint8_t *)&t514 + 8, (uintptr_t)(2));
                    gm_f_join_sfknljb((void *)&gm_l180, (void *)NULL, cx, NULL, t513, t514);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t515 = (uint8_t)!((((uint8_t *)&gm_l180))[0] != 0);
                    if (!t515) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l180) + 8, gm_srco4ljquuk5d87, 189, 77);
                    gm_b16_8 t516 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l180) + 8));
                    gm_t6j6smj7qoctu t517;
                    GM_ST(gm_b16_8, ((uint8_t *)&t517 + 0), t511);
                    GM_ST(gm_b16_8, ((uint8_t *)&t517 + 16), t516);
                    gm_f_format_cuuc21t((void *)&gm_l181, (void *)NULL, cx, NULL, &t517);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t518 = (uint8_t)!((((uint8_t *)&gm_l181))[0] != 0);
                    if (!t518) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l181) + 8, gm_srco4ljquuk5d87, 189, 13);
                    gm_b16_8 t519 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l181) + 8));
                    gm_b16_8 t520 = GM_LD(gm_b16_8, ((uint8_t *)&t519));
                    gm_l138 = t520;
                    goto gm_lb1;
                }
gm_lb46:;
            }
            {
                {
                    uint8_t t521 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t522 = (uint8_t)(t521 == ((uint8_t)59ULL));
                    if (!t522) {
                        goto gm_lb52;
                    }
                    memmove(((uint8_t *)&F.l53), ((((uint8_t *)t157) + 8) + 8), 40);
                    void * t523 = gm_l0;
                    uint64_t t524 = gm_l1;
                    gm_b16_8 t525 = gm_l4;
                    gm_b0_1 t526 = gm_f_write_with_j37nil7((void *)NULL, cx, NULL, t523, t524, &(*(gm_t710lsrq1m8gk *)((uint8_t *)&F.l53)), t525);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb52:;
                {
                    uint8_t t527 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t528 = (uint8_t)(t527 == ((uint8_t)29ULL));
                    if (!t528) {
                        goto gm_lb53;
                    }
                    gm_t9qkh0rsooc8d t529 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t157) + 8) + 8));
                    gm_l54 = t529;
                    void * t530 = GM_LD(void *, (((((uint8_t *)t157) + 8) + 8) + 8));
                    gm_l55 = t530;
                    void * t531 = (void *)((uint8_t *)&F.l5);
                    void * t532 = gm_l55;
                    gm_tftb1odc739t5 t533 = gm_f_push_pf02nv6(t531, t532);
                    gm_l182 = t533;
                    uint8_t t534 = (uint8_t)!((((uint8_t *)&gm_l182))[0] != 0);
                    if (!t534) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l182) + 1, gm_srco4ljquuk5d87, 196, 13);
                    {
                        {
                            uint8_t t535 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
                            uint8_t t536 = (uint8_t)(t535 == ((uint8_t)0ULL));
                            if (!t536) {
                                goto gm_lb55;
                            }
                            gm_b16_8 t537;
                            GM_ST(void *, (uint8_t *)&t537, (void *)(gm_se19nf1421e37));
                            GM_ST(uintptr_t, (uint8_t *)&t537 + 8, (uintptr_t)(3));
                            gm_l183 = t537;
                            goto gm_lb54;
                        }
gm_lb55:;
                        {
                            uint8_t t538 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
                            uint8_t t539 = (uint8_t)(t538 == ((uint8_t)1ULL));
                            if (!t539) {
                                goto gm_lb56;
                            }
                            gm_b16_8 t540;
                            GM_ST(void *, (uint8_t *)&t540, (void *)(gm_shohh3180gu26));
                            GM_ST(uintptr_t, (uint8_t *)&t540 + 8, (uintptr_t)(6));
                            gm_l183 = t540;
                            goto gm_lb54;
                        }
gm_lb56:;
                        {
                            uint8_t t541 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
                            uint8_t t542 = (uint8_t)(t541 == ((uint8_t)2ULL));
                            if (!t542) {
                                goto gm_lb57;
                            }
                            gm_b16_8 t543;
                            GM_ST(void *, (uint8_t *)&t543, (void *)(gm_soe4gvhg9ei97));
                            GM_ST(uintptr_t, (uint8_t *)&t543 + 8, (uintptr_t)(3));
                            gm_l183 = t543;
                            goto gm_lb54;
                        }
gm_lb57:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 197, 36);
                    }
gm_lb54:;
                    gm_b16_8 t544 = gm_l183;
                    gm_tnpifv20irnph t545;
                    GM_ST(gm_b16_8, ((uint8_t *)&t545 + 0), t544);
                    gm_f_format_r8pfaos((void *)&gm_l184, (void *)NULL, cx, NULL, t545);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t546 = (uint8_t)!((((uint8_t *)&gm_l184))[0] != 0);
                    if (!t546) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l184) + 8, gm_srco4ljquuk5d87, 197, 13);
                    gm_b16_8 t547 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l184) + 8));
                    gm_b16_8 t548 = GM_LD(gm_b16_8, ((uint8_t *)&t547));
                    gm_l138 = t548;
                    goto gm_lb1;
                }
gm_lb53:;
                {
                    uint8_t t549 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t550 = (uint8_t)(t549 == ((uint8_t)30ULL));
                    if (!t550) {
                        goto gm_lb58;
                    }
                    gm_ts6oqppjh0ba3 t551 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)t157) + 8) + 8));
                    gm_l56 = t551;
                    void * t552 = GM_LD(void *, (((((uint8_t *)t157) + 8) + 8) + 8));
                    gm_l57 = t552;
                    void * t553 = GM_LD(void *, (((((uint8_t *)t157) + 8) + 8) + 16));
                    gm_l58 = t553;
                    void * t554 = (void *)((uint8_t *)&F.l5);
                    void * t555 = gm_l57;
                    gm_tftb1odc739t5 t556 = gm_f_push_pf02nv6(t554, t555);
                    gm_l185 = t556;
                    uint8_t t557 = (uint8_t)!((((uint8_t *)&gm_l185))[0] != 0);
                    if (!t557) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l185) + 1, gm_srco4ljquuk5d87, 204, 13);
                    void * t558 = (void *)((uint8_t *)&F.l5);
                    void * t559 = gm_l58;
                    gm_tftb1odc739t5 t560 = gm_f_push_pf02nv6(t558, t559);
                    gm_l186 = t560;
                    uint8_t t561 = (uint8_t)!((((uint8_t *)&gm_l186))[0] != 0);
                    if (!t561) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l186) + 1, gm_srco4ljquuk5d87, 205, 13);
                    gm_ts6oqppjh0ba3 t562 = gm_l56;
                    gm_b16_8 t563 = gm_f_binop_text_t4cv0b4((void *)NULL, cx, NULL, t562);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t564;
                    GM_ST(gm_b16_8, ((uint8_t *)&t564 + 0), t563);
                    gm_f_format_85biid9((void *)&gm_l187, (void *)NULL, cx, NULL, t564);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t565 = (uint8_t)!((((uint8_t *)&gm_l187))[0] != 0);
                    if (!t565) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l187) + 8, gm_srco4ljquuk5d87, 206, 13);
                    gm_b16_8 t566 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l187) + 8));
                    gm_b16_8 t567 = GM_LD(gm_b16_8, ((uint8_t *)&t566));
                    gm_l138 = t567;
                    goto gm_lb1;
                }
gm_lb58:;
            }
            {
                {
                    uint8_t t568 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t569 = (uint8_t)(t568 == ((uint8_t)31ULL));
                    if (!t569) {
                        goto gm_lb59;
                    }
                    uint8_t t570 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 8));
                    gm_l59 = t570;
                    void * t571 = GM_LD(void *, (((((uint8_t *)t157) + 8) + 8) + 8));
                    gm_l60 = t571;
                    void * t572 = GM_LD(void *, (((((uint8_t *)t157) + 8) + 8) + 16));
                    gm_l61 = t572;
                    void * t573 = (void *)((uint8_t *)&F.l5);
                    void * t574 = gm_l60;
                    gm_tftb1odc739t5 t575 = gm_f_push_pf02nv6(t573, t574);
                    gm_l188 = t575;
                    uint8_t t576 = (uint8_t)!((((uint8_t *)&gm_l188))[0] != 0);
                    if (!t576) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l188) + 1, gm_srco4ljquuk5d87, 209, 13);
                    void * t577 = (void *)((uint8_t *)&F.l5);
                    void * t578 = gm_l61;
                    gm_tftb1odc739t5 t579 = gm_f_push_pf02nv6(t577, t578);
                    gm_l189 = t579;
                    uint8_t t580 = (uint8_t)!((((uint8_t *)&gm_l189))[0] != 0);
                    if (!t580) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l189) + 1, gm_srco4ljquuk5d87, 210, 13);
                    uint8_t t581 = gm_l59;
                    if (t581) {
                        gm_b16_8 t582;
                        GM_ST(void *, (uint8_t *)&t582, (void *)(gm_s920dpvvv6cej));
                        GM_ST(uintptr_t, (uint8_t *)&t582 + 8, (uintptr_t)(3));
                        gm_l190 = t582;
                    } else {
                        gm_b16_8 t583;
                        GM_ST(void *, (uint8_t *)&t583, (void *)(gm_spi47lg4tr80j));
                        GM_ST(uintptr_t, (uint8_t *)&t583 + 8, (uintptr_t)(2));
                        gm_l190 = t583;
                    }
                    gm_b16_8 t584 = gm_l190;
                    gm_l138 = t584;
                    goto gm_lb1;
                }
gm_lb59:;
                {
                    uint8_t t585 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t586 = (uint8_t)(t585 == ((uint8_t)32ULL));
                    if (!t586) {
                        goto gm_lb60;
                    }
                    gm_tmn7n0n9raou5 t587 = GM_LD(gm_tmn7n0n9raou5, ((((uint8_t *)t157) + 8) + 8));
                    gm_l62 = t587;
                    void * t588 = GM_LD(void *, (((((uint8_t *)t157) + 8) + 8) + 8));
                    gm_l63 = t588;
                    void * t589 = (void *)((uint8_t *)&F.l5);
                    void * t590 = gm_l63;
                    gm_tftb1odc739t5 t591 = gm_f_push_pf02nv6(t589, t590);
                    gm_l191 = t591;
                    uint8_t t592 = (uint8_t)!((((uint8_t *)&gm_l191))[0] != 0);
                    if (!t592) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l191) + 1, gm_srco4ljquuk5d87, 214, 13);
                    gm_tmn7n0n9raou5 t593 = gm_l62;
                    gm_b16_8 t594 = gm_f_coercion_text_g32oppe((void *)NULL, cx, NULL, t593);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t595;
                    GM_ST(gm_b16_8, ((uint8_t *)&t595 + 0), t594);
                    gm_f_format_c726p1u((void *)&gm_l192, (void *)NULL, cx, NULL, t595);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t596 = (uint8_t)!((((uint8_t *)&gm_l192))[0] != 0);
                    if (!t596) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l192) + 8, gm_srco4ljquuk5d87, 215, 13);
                    gm_b16_8 t597 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l192) + 8));
                    gm_b16_8 t598 = GM_LD(gm_b16_8, ((uint8_t *)&t597));
                    gm_l138 = t598;
                    goto gm_lb1;
                }
gm_lb60:;
                {
                    uint8_t t599 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t600 = (uint8_t)(t599 == ((uint8_t)33ULL));
                    if (!t600) {
                        goto gm_lb61;
                    }
                    gm_tvdg94bk7aa22 t601 = GM_LD(gm_tvdg94bk7aa22, ((((uint8_t *)t157) + 8) + 8));
                    gm_l64 = t601;
                    gm_b16_8 t602 = GM_LD(gm_b16_8, (((((uint8_t *)t157) + 8) + 8) + 16));
                    gm_l65 = t602;
                    gm_l67 = ((uint64_t)0ULL);
                    uint64_t t603 = GM_LD(uint64_t, (((uint8_t *)&gm_l65) + 8));
                    gm_l66 = t603;
                    {
                        for (;;) {
                            uint64_t t604 = gm_l67;
                            uint64_t t605 = gm_l66;
                            uint8_t t606 = (t604 < t605);
                            if (!t606) {
                                goto gm_lb62;
                            }
                            {
                                {
                                    {
                                        uint64_t t607 = gm_l67;
                                        gm_l68 = t607;
                                        void * t608 = (void *)((uint8_t *)&F.l5);
                                        void * t609 = GM_LD(void *, ((uint8_t *)&gm_l65));
                                        uint64_t t610 = GM_LD(uint64_t, (((uint8_t *)&gm_l65) + 8));
                                        uint64_t t611 = gm_l68;
                                        void * t612 = (void *)(((uint8_t *)t609) + (uintptr_t)t611 * 88);
                                        gm_tftb1odc739t5 t613 = gm_f_push_pf02nv6(t608, t612);
                                        gm_l193 = t613;
                                        uint8_t t614 = (uint8_t)!((((uint8_t *)&gm_l193))[0] != 0);
                                        if (!t614) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l193) + 1, gm_srco4ljquuk5d87, 218, 36);
                                        goto gm_lb65;
                                    }
gm_lb66:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 218, 13);
                                }
gm_lb65:;
                            }
gm_lb64:;
                            uint64_t t615 = gm_l67;
                            uint64_t r616; bool o617 = __builtin_add_overflow(t615, ((uint64_t)1ULL), &r616);
                            uint64_t t618 = r616;
                            gm_l67 = t618;
                        }
                    }
gm_lb62:;
                    gm_tvdg94bk7aa22 t619 = gm_l64;
                    gm_b16_8 t620 = gm_f_builtin_text_od38u7h((void *)NULL, cx, NULL, t619);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t621;
                    GM_ST(gm_b16_8, ((uint8_t *)&t621 + 0), t620);
                    gm_f_format_02b5heb((void *)&gm_l194, (void *)NULL, cx, NULL, t621);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t622 = (uint8_t)!((((uint8_t *)&gm_l194))[0] != 0);
                    if (!t622) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l194) + 8, gm_srco4ljquuk5d87, 219, 13);
                    gm_b16_8 t623 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l194) + 8));
                    gm_b16_8 t624 = GM_LD(gm_b16_8, ((uint8_t *)&t623));
                    gm_l138 = t624;
                    goto gm_lb1;
                }
gm_lb61:;
            }
            {
                {
                    uint8_t t625 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t626 = (uint8_t)(t625 == ((uint8_t)34ULL));
                    if (!t626) {
                        goto gm_lb67;
                    }
                    memmove(((uint8_t *)&F.l69), ((((uint8_t *)t157) + 8) + 8), 40);
                    gm_b16_8 t627 = GM_LD(gm_b16_8, (((((uint8_t *)t157) + 8) + 8) + 40));
                    gm_l70 = t627;
                    {
                        {
                            uint8_t t628 = GM_LD(uint8_t, (((uint8_t *)&F.l69) + 0));
                            uint8_t t629 = (uint8_t)(t628 == ((uint8_t)0ULL));
                            if (!t629) {
                                goto gm_lb69;
                            }
                            uint32_t t630 = GM_LD(uint32_t, (((uint8_t *)&F.l69) + 8));
                            gm_l71 = t630;
                            void * t631 = gm_l3;
                            uint32_t t632 = gm_l71;
                            gm_b16_8 t633 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t631, t632);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t634;
                            GM_ST(gm_b16_8, ((uint8_t *)&t634 + 0), t633);
                            gm_f_format_vmi8kfe((void *)&gm_l196, (void *)NULL, cx, NULL, t634);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t635 = (uint8_t)!((((uint8_t *)&gm_l196))[0] != 0);
                            if (!t635) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l196) + 8, gm_srco4ljquuk5d87, 223, 27);
                            gm_b16_8 t636 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l196) + 8));
                            gm_b16_8 t637 = GM_LD(gm_b16_8, ((uint8_t *)&t636));
                            gm_l195 = t637;
                            goto gm_lb68;
                        }
gm_lb69:;
                        {
                            uint8_t t638 = GM_LD(uint8_t, (((uint8_t *)&F.l69) + 0));
                            uint8_t t639 = (uint8_t)(t638 == ((uint8_t)1ULL));
                            if (!t639) {
                                goto gm_lb70;
                            }
                            memmove(((uint8_t *)&gm_l72), (((uint8_t *)&F.l69) + 8), 24);
                            uint32_t t640 = GM_LD(uint32_t, ((((uint8_t *)&F.l69) + 8) + 24));
                            gm_l73 = t640;
                            void * t641 = gm_l3;
                            void * t642 = (void *)(((uint8_t *)t641) + 8);
                            void * t643 = (void *)((uint8_t *)&gm_l72);
                            gm_b16_8 t644 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t642, t643);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint32_t t645 = gm_l73;
                            gm_tommf4j9upomc t646;
                            GM_ST(uint32_t, (uint8_t *)&t646 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t646 + 0), t644);
                            GM_ST(uint32_t, ((uint8_t *)&t646 + 16), t645);
                            gm_f_format_5m6a3eh((void *)&gm_l197, (void *)NULL, cx, NULL, &t646);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t647 = (uint8_t)!((((uint8_t *)&gm_l197))[0] != 0);
                            if (!t647) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l197) + 8, gm_srco4ljquuk5d87, 224, 38);
                            gm_b16_8 t648 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l197) + 8));
                            gm_b16_8 t649 = GM_LD(gm_b16_8, ((uint8_t *)&t648));
                            gm_l195 = t649;
                            goto gm_lb68;
                        }
gm_lb70:;
                        {
                            uint8_t t650 = GM_LD(uint8_t, (((uint8_t *)&F.l69) + 0));
                            uint8_t t651 = (uint8_t)(t650 == ((uint8_t)2ULL));
                            if (!t651) {
                                goto gm_lb71;
                            }
                            void * t652 = GM_LD(void *, (((uint8_t *)&F.l69) + 8));
                            gm_l74 = t652;
                            void * t653 = (void *)((uint8_t *)&F.l5);
                            void * t654 = gm_l74;
                            gm_tftb1odc739t5 t655 = gm_f_push_pf02nv6(t653, t654);
                            gm_l198 = t655;
                            uint8_t t656 = (uint8_t)!((((uint8_t *)&gm_l198))[0] != 0);
                            if (!t656) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l198) + 1, gm_srco4ljquuk5d87, 226, 21);
                            gm_b16_8 t657;
                            GM_ST(void *, (uint8_t *)&t657, (void *)(gm_sj696i9uo1a9s));
                            GM_ST(uintptr_t, (uint8_t *)&t657 + 8, (uintptr_t)(13));
                            gm_l195 = t657;
                            goto gm_lb68;
                        }
gm_lb71:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 222, 25);
                    }
gm_lb68:;
                    gm_b16_8 t658 = gm_l195;
                    gm_l75 = t658;
                    gm_l77 = ((uint64_t)0ULL);
                    uint64_t t659 = GM_LD(uint64_t, (((uint8_t *)&gm_l70) + 8));
                    gm_l76 = t659;
                    {
                        for (;;) {
                            uint64_t t660 = gm_l77;
                            uint64_t t661 = gm_l76;
                            uint8_t t662 = (t660 < t661);
                            if (!t662) {
                                goto gm_lb72;
                            }
                            {
                                {
                                    {
                                        uint64_t t663 = gm_l77;
                                        gm_l78 = t663;
                                        void * t664 = (void *)((uint8_t *)&F.l5);
                                        void * t665 = GM_LD(void *, ((uint8_t *)&gm_l70));
                                        uint64_t t666 = GM_LD(uint64_t, (((uint8_t *)&gm_l70) + 8));
                                        uint64_t t667 = gm_l78;
                                        void * t668 = (void *)(((uint8_t *)t665) + (uintptr_t)t667 * 88);
                                        gm_tftb1odc739t5 t669 = gm_f_push_pf02nv6(t664, t668);
                                        gm_l199 = t669;
                                        uint8_t t670 = (uint8_t)!((((uint8_t *)&gm_l199))[0] != 0);
                                        if (!t670) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l199) + 1, gm_srco4ljquuk5d87, 230, 36);
                                        goto gm_lb75;
                                    }
gm_lb76:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 230, 13);
                                }
gm_lb75:;
                            }
gm_lb74:;
                            uint64_t t671 = gm_l77;
                            uint64_t r672; bool o673 = __builtin_add_overflow(t671, ((uint64_t)1ULL), &r672);
                            uint64_t t674 = r672;
                            gm_l77 = t674;
                        }
                    }
gm_lb72:;
                    gm_b16_8 t675 = gm_l75;
                    gm_l138 = t675;
                    goto gm_lb1;
                }
gm_lb67:;
            }
            {
                {
                    uint8_t t676 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t677 = (uint8_t)(t676 == ((uint8_t)35ULL));
                    if (!t677) {
                        goto gm_lb77;
                    }
                    gm_b16_8 t678 = GM_LD(gm_b16_8, ((((uint8_t *)t157) + 8) + 8));
                    gm_l79 = t678;
                    gm_l81 = ((uint64_t)0ULL);
                    uint64_t t679 = GM_LD(uint64_t, (((uint8_t *)&gm_l79) + 8));
                    gm_l80 = t679;
                    {
                        for (;;) {
                            uint64_t t680 = gm_l81;
                            uint64_t t681 = gm_l80;
                            uint8_t t682 = (t680 < t681);
                            if (!t682) {
                                goto gm_lb78;
                            }
                            {
                                {
                                    {
                                        uint64_t t683 = gm_l81;
                                        gm_l82 = t683;
                                        void * t684 = (void *)((uint8_t *)&F.l5);
                                        void * t685 = GM_LD(void *, ((uint8_t *)&gm_l79));
                                        uint64_t t686 = GM_LD(uint64_t, (((uint8_t *)&gm_l79) + 8));
                                        uint64_t t687 = gm_l82;
                                        void * t688 = (void *)(((uint8_t *)t685) + (uintptr_t)t687 * 88);
                                        gm_tftb1odc739t5 t689 = gm_f_push_pf02nv6(t684, t688);
                                        gm_l200 = t689;
                                        uint8_t t690 = (uint8_t)!((((uint8_t *)&gm_l200))[0] != 0);
                                        if (!t690) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l200) + 1, gm_srco4ljquuk5d87, 234, 34);
                                        goto gm_lb81;
                                    }
gm_lb82:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 234, 13);
                                }
gm_lb81:;
                            }
gm_lb80:;
                            uint64_t t691 = gm_l81;
                            uint64_t r692; bool o693 = __builtin_add_overflow(t691, ((uint64_t)1ULL), &r692);
                            uint64_t t694 = r692;
                            gm_l81 = t694;
                        }
                    }
gm_lb78:;
                    gm_b16_8 t695;
                    GM_ST(void *, (uint8_t *)&t695, (void *)(gm_sluco9qbar70k));
                    GM_ST(uintptr_t, (uint8_t *)&t695 + 8, (uintptr_t)(9));
                    gm_l138 = t695;
                    goto gm_lb1;
                }
gm_lb77:;
                {
                    uint8_t t696 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t697 = (uint8_t)(t696 == ((uint8_t)36ULL));
                    if (!t697) {
                        goto gm_lb83;
                    }
                    gm_b16_8 t698 = GM_LD(gm_b16_8, ((((uint8_t *)t157) + 8) + 8));
                    gm_l83 = t698;
                    gm_l85 = ((uint64_t)0ULL);
                    uint64_t t699 = GM_LD(uint64_t, (((uint8_t *)&gm_l83) + 8));
                    gm_l84 = t699;
                    {
                        for (;;) {
                            uint64_t t700 = gm_l85;
                            uint64_t t701 = gm_l84;
                            uint8_t t702 = (t700 < t701);
                            if (!t702) {
                                goto gm_lb84;
                            }
                            {
                                {
                                    {
                                        uint64_t t703 = gm_l85;
                                        gm_l86 = t703;
                                        void * t704 = (void *)((uint8_t *)&F.l5);
                                        void * t705 = GM_LD(void *, ((uint8_t *)&gm_l83));
                                        uint64_t t706 = GM_LD(uint64_t, (((uint8_t *)&gm_l83) + 8));
                                        uint64_t t707 = gm_l86;
                                        void * t708 = (void *)(((uint8_t *)t705) + (uintptr_t)t707 * 88);
                                        gm_tftb1odc739t5 t709 = gm_f_push_pf02nv6(t704, t708);
                                        gm_l201 = t709;
                                        uint8_t t710 = (uint8_t)!((((uint8_t *)&gm_l201))[0] != 0);
                                        if (!t710) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l201) + 1, gm_srco4ljquuk5d87, 238, 34);
                                        goto gm_lb87;
                                    }
gm_lb88:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 238, 13);
                                }
gm_lb87:;
                            }
gm_lb86:;
                            uint64_t t711 = gm_l85;
                            uint64_t r712; bool o713 = __builtin_add_overflow(t711, ((uint64_t)1ULL), &r712);
                            uint64_t t714 = r712;
                            gm_l85 = t714;
                        }
                    }
gm_lb84:;
                    gm_b16_8 t715;
                    GM_ST(void *, (uint8_t *)&t715, (void *)(gm_st87fk7ojs4l8));
                    GM_ST(uintptr_t, (uint8_t *)&t715 + 8, (uintptr_t)(5));
                    gm_l138 = t715;
                    goto gm_lb1;
                }
gm_lb83:;
            }
            {
                {
                    uint8_t t716 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t717 = (uint8_t)(t716 == ((uint8_t)37ULL));
                    if (!t717) {
                        goto gm_lb89;
                    }
                    void * t718 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l87 = t718;
                    void * t719 = (void *)((uint8_t *)&F.l5);
                    void * t720 = gm_l87;
                    gm_tftb1odc739t5 t721 = gm_f_push_pf02nv6(t719, t720);
                    gm_l202 = t721;
                    uint8_t t722 = (uint8_t)!((((uint8_t *)&gm_l202))[0] != 0);
                    if (!t722) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l202) + 1, gm_srco4ljquuk5d87, 242, 13);
                    gm_b16_8 t723;
                    GM_ST(void *, (uint8_t *)&t723, (void *)(gm_shvpku7ssg8d7));
                    GM_ST(uintptr_t, (uint8_t *)&t723 + 8, (uintptr_t)(10));
                    gm_l138 = t723;
                    goto gm_lb1;
                }
gm_lb89:;
                {
                    uint8_t t724 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t725 = (uint8_t)(t724 == ((uint8_t)38ULL));
                    if (!t725) {
                        goto gm_lb90;
                    }
                    gm_ttggq7ng4kp3p t726 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t157) + 8) + 8));
                    gm_l88 = t726;
                    {
                        {
                            uint8_t t727 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l88) + 8)) != 0);
                            if (!t727) {
                                goto gm_lb92;
                            }
                            void * t728 = GM_LD(void *, (((uint8_t *)&gm_l88) + 8));
                            gm_l89 = t728;
                            void * t729 = (void *)((uint8_t *)&F.l5);
                            void * t730 = gm_l89;
                            gm_tftb1odc739t5 t731 = gm_f_push_pf02nv6(t729, t730);
                            gm_l203 = t731;
                            uint8_t t732 = (uint8_t)!((((uint8_t *)&gm_l203))[0] != 0);
                            if (!t732) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l203) + 1, gm_srco4ljquuk5d87, 246, 33);
                            goto gm_lb91;
                        }
gm_lb92:;
                    }
gm_lb91:;
                    uint32_t t733 = GM_LD(uint32_t, ((uint8_t *)&gm_l88));
                    gm_t88rcucg1b7uk t734;
                    GM_ST(uint32_t, ((uint8_t *)&t734 + 0), t733);
                    gm_f_format_lefnhqh((void *)&gm_l204, (void *)NULL, cx, NULL, t734);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t735 = (uint8_t)!((((uint8_t *)&gm_l204))[0] != 0);
                    if (!t735) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l204) + 8, gm_srco4ljquuk5d87, 247, 13);
                    gm_b16_8 t736 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l204) + 8));
                    gm_b16_8 t737 = GM_LD(gm_b16_8, ((uint8_t *)&t736));
                    gm_l138 = t737;
                    goto gm_lb1;
                }
gm_lb90:;
                {
                    uint8_t t738 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t739 = (uint8_t)(t738 == ((uint8_t)39ULL));
                    if (!t739) {
                        goto gm_lb93;
                    }
                    void * t740 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l90 = t740;
                    void * t741 = (void *)((uint8_t *)&F.l5);
                    void * t742 = gm_l90;
                    gm_tftb1odc739t5 t743 = gm_f_push_pf02nv6(t741, t742);
                    gm_l205 = t743;
                    uint8_t t744 = (uint8_t)!((((uint8_t *)&gm_l205))[0] != 0);
                    if (!t744) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l205) + 1, gm_srco4ljquuk5d87, 250, 13);
                    gm_b16_8 t745;
                    GM_ST(void *, (uint8_t *)&t745, (void *)(gm_sn581p3lbgrjg));
                    GM_ST(uintptr_t, (uint8_t *)&t745 + 8, (uintptr_t)(8));
                    gm_l138 = t745;
                    goto gm_lb1;
                }
gm_lb93:;
                {
                    uint8_t t746 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t747 = (uint8_t)(t746 == ((uint8_t)41ULL));
                    if (!t747) {
                        goto gm_lb94;
                    }
                    memmove(((uint8_t *)&gm_l91), ((((uint8_t *)t157) + 8) + 8), 24);
                    void * t748 = (void *)((uint8_t *)&F.l5);
                    void * t749 = GM_LD(void *, ((uint8_t *)&gm_l91));
                    gm_tftb1odc739t5 t750 = gm_f_push_pf02nv6(t748, t749);
                    gm_l206 = t750;
                    uint8_t t751 = (uint8_t)!((((uint8_t *)&gm_l206))[0] != 0);
                    if (!t751) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l206) + 1, gm_srco4ljquuk5d87, 254, 13);
                    void * t752 = (void *)((uint8_t *)&F.l5);
                    void * t753 = GM_LD(void *, (((uint8_t *)&gm_l91) + 16));
                    gm_tftb1odc739t5 t754 = gm_f_push_pf02nv6(t752, t753);
                    gm_l207 = t754;
                    uint8_t t755 = (uint8_t)!((((uint8_t *)&gm_l207))[0] != 0);
                    if (!t755) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l207) + 1, gm_srco4ljquuk5d87, 255, 13);
                    {
                        {
                            uint8_t t756 = (uint8_t)(((((uint8_t *)&gm_l91) + 8))[4] != 0);
                            if (!t756) {
                                goto gm_lb96;
                            }
                            uint32_t t757 = GM_LD(uint32_t, (((uint8_t *)&gm_l91) + 8));
                            gm_l92 = t757;
                            void * t758 = gm_l0;
                            uint32_t t759 = gm_l92;
                            gm_b16_8 t760 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t758, t759);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t761;
                            GM_ST(gm_b16_8, ((uint8_t *)&t761 + 0), t760);
                            gm_f_format_i4fsdjb((void *)&gm_l209, (void *)NULL, cx, NULL, t761);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t762 = (uint8_t)!((((uint8_t *)&gm_l209))[0] != 0);
                            if (!t762) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l209) + 8, gm_srco4ljquuk5d87, 256, 33);
                            gm_b16_8 t763 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l209) + 8));
                            gm_b16_8 t764 = GM_LD(gm_b16_8, ((uint8_t *)&t763));
                            gm_l208 = t764;
                            goto gm_lb95;
                        }
gm_lb96:;
                        gm_b16_8 t765;
                        GM_ST(void *, (uint8_t *)&t765, (void *)(gm_s9es2tos1bhfp));
                        GM_ST(uintptr_t, (uint8_t *)&t765 + 8, (uintptr_t)(4));
                        gm_l208 = t765;
                    }
gm_lb95:;
                    gm_b16_8 t766 = gm_l208;
                    gm_l138 = t766;
                    goto gm_lb1;
                }
gm_lb94:;
            }
            {
                {
                    uint8_t t767 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t768 = (uint8_t)(t767 == ((uint8_t)42ULL));
                    if (!t768) {
                        goto gm_lb97;
                    }
                    memmove(((uint8_t *)&gm_l93), ((((uint8_t *)t157) + 8) + 8), 24);
                    void * t769 = (void *)((uint8_t *)&F.l5);
                    void * t770 = GM_LD(void *, ((uint8_t *)&gm_l93));
                    gm_tftb1odc739t5 t771 = gm_f_push_pf02nv6(t769, t770);
                    gm_l210 = t771;
                    uint8_t t772 = (uint8_t)!((((uint8_t *)&gm_l210))[0] != 0);
                    if (!t772) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l210) + 1, gm_srco4ljquuk5d87, 259, 13);
                    {
                        {
                            uint8_t t773 = (uint8_t)(((((uint8_t *)&gm_l93) + 8))[4] != 0);
                            if (!t773) {
                                goto gm_lb99;
                            }
                            uint32_t t774 = GM_LD(uint32_t, (((uint8_t *)&gm_l93) + 8));
                            gm_l94 = t774;
                            uint32_t t775 = GM_LD(uint32_t, (((uint8_t *)&gm_l93) + 16));
                            uint32_t t776 = gm_l94;
                            gm_tcc1rq4n1avb2 t777;
                            GM_ST(uint32_t, ((uint8_t *)&t777 + 0), t775);
                            GM_ST(uint32_t, ((uint8_t *)&t777 + 4), t776);
                            gm_f_format_0gd6afr((void *)&gm_l212, (void *)NULL, cx, NULL, t777);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t778 = (uint8_t)!((((uint8_t *)&gm_l212))[0] != 0);
                            if (!t778) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l212) + 8, gm_srco4ljquuk5d87, 260, 30);
                            gm_b16_8 t779 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l212) + 8));
                            gm_b16_8 t780 = GM_LD(gm_b16_8, ((uint8_t *)&t779));
                            gm_l211 = t780;
                            goto gm_lb98;
                        }
gm_lb99:;
                        uint32_t t781 = GM_LD(uint32_t, (((uint8_t *)&gm_l93) + 16));
                        gm_t88rcucg1b7uk t782;
                        GM_ST(uint32_t, ((uint8_t *)&t782 + 0), t781);
                        gm_f_format_glqqt6c((void *)&gm_l213, (void *)NULL, cx, NULL, t782);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t783 = (uint8_t)!((((uint8_t *)&gm_l213))[0] != 0);
                        if (!t783) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l213) + 8, gm_srco4ljquuk5d87, 260, 82);
                        gm_b16_8 t784 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l213) + 8));
                        gm_b16_8 t785 = GM_LD(gm_b16_8, ((uint8_t *)&t784));
                        gm_l211 = t785;
                    }
gm_lb98:;
                    gm_b16_8 t786 = gm_l211;
                    gm_l138 = t786;
                    goto gm_lb1;
                }
gm_lb97:;
                {
                    uint8_t t787 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t788 = (uint8_t)(t787 == ((uint8_t)40ULL));
                    if (!t788) {
                        goto gm_lb100;
                    }
                    void * t789 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l95 = t789;
                    uint8_t t790 = GM_LD(uint8_t, (((((uint8_t *)t157) + 8) + 8) + 8));
                    gm_l96 = t790;
                    void * t791 = (void *)((uint8_t *)&F.l5);
                    void * t792 = gm_l95;
                    gm_tftb1odc739t5 t793 = gm_f_push_pf02nv6(t791, t792);
                    gm_l214 = t793;
                    uint8_t t794 = (uint8_t)!((((uint8_t *)&gm_l214))[0] != 0);
                    if (!t794) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l214) + 1, gm_srco4ljquuk5d87, 263, 13);
                    uint8_t t795 = gm_l96;
                    if (t795) {
                        gm_b16_8 t796;
                        GM_ST(void *, (uint8_t *)&t796, (void *)(gm_sromjese8e416));
                        GM_ST(uintptr_t, (uint8_t *)&t796 + 8, (uintptr_t)(13));
                        gm_l215 = t796;
                    } else {
                        gm_b16_8 t797;
                        GM_ST(void *, (uint8_t *)&t797, (void *)(gm_srntlv7ksi1vk));
                        GM_ST(uintptr_t, (uint8_t *)&t797 + 8, (uintptr_t)(15));
                        gm_l215 = t797;
                    }
                    gm_b16_8 t798 = gm_l215;
                    gm_l138 = t798;
                    goto gm_lb1;
                }
gm_lb100:;
                {
                    uint8_t t799 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t800 = (uint8_t)(t799 == ((uint8_t)43ULL));
                    if (!t800) {
                        goto gm_lb101;
                    }
                    void * t801 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l97 = t801;
                    void * t802 = GM_LD(void *, (((((uint8_t *)t157) + 8) + 8) + 8));
                    gm_l98 = t802;
                    uint8_t t803 = GM_LD(uint8_t, (((((uint8_t *)t157) + 8) + 8) + 16));
                    gm_l99 = t803;
                    void * t804 = (void *)((uint8_t *)&F.l5);
                    void * t805 = gm_l97;
                    gm_tftb1odc739t5 t806 = gm_f_push_pf02nv6(t804, t805);
                    gm_l216 = t806;
                    uint8_t t807 = (uint8_t)!((((uint8_t *)&gm_l216))[0] != 0);
                    if (!t807) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l216) + 1, gm_srco4ljquuk5d87, 267, 13);
                    void * t808 = (void *)((uint8_t *)&F.l5);
                    void * t809 = gm_l98;
                    gm_tftb1odc739t5 t810 = gm_f_push_pf02nv6(t808, t809);
                    gm_l217 = t810;
                    uint8_t t811 = (uint8_t)!((((uint8_t *)&gm_l217))[0] != 0);
                    if (!t811) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l217) + 1, gm_srco4ljquuk5d87, 268, 13);
                    uint8_t t812 = gm_l99;
                    if (t812) {
                        gm_b16_8 t813;
                        GM_ST(void *, (uint8_t *)&t813, (void *)(gm_s3s50a53ci7rk));
                        GM_ST(uintptr_t, (uint8_t *)&t813 + 8, (uintptr_t)(6));
                        gm_l218 = t813;
                    } else {
                        gm_b16_8 t814;
                        GM_ST(void *, (uint8_t *)&t814, (void *)(gm_sgpdmvc9s0d2p));
                        GM_ST(uintptr_t, (uint8_t *)&t814 + 8, (uintptr_t)(2));
                        gm_l218 = t814;
                    }
                    gm_b16_8 t815 = gm_l218;
                    gm_l138 = t815;
                    goto gm_lb1;
                }
gm_lb101:;
            }
            {
                {
                    uint8_t t816 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t817 = (uint8_t)(t816 == ((uint8_t)45ULL));
                    if (!t817) {
                        goto gm_lb102;
                    }
                    void * t818 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l100 = t818;
                    void * t819 = GM_LD(void *, (((((uint8_t *)t157) + 8) + 8) + 8));
                    gm_l101 = t819;
                    uint8_t t820 = GM_LD(uint8_t, (((((uint8_t *)t157) + 8) + 8) + 16));
                    gm_l102 = t820;
                    void * t821 = (void *)((uint8_t *)&F.l5);
                    void * t822 = gm_l100;
                    gm_tftb1odc739t5 t823 = gm_f_push_pf02nv6(t821, t822);
                    gm_l219 = t823;
                    uint8_t t824 = (uint8_t)!((((uint8_t *)&gm_l219))[0] != 0);
                    if (!t824) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l219) + 1, gm_srco4ljquuk5d87, 272, 13);
                    void * t825 = (void *)((uint8_t *)&F.l5);
                    void * t826 = gm_l101;
                    gm_tftb1odc739t5 t827 = gm_f_push_pf02nv6(t825, t826);
                    gm_l220 = t827;
                    uint8_t t828 = (uint8_t)!((((uint8_t *)&gm_l220))[0] != 0);
                    if (!t828) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l220) + 1, gm_srco4ljquuk5d87, 273, 13);
                    uint8_t t829 = gm_l102;
                    if (t829) {
                        gm_b16_8 t830;
                        GM_ST(void *, (uint8_t *)&t830, (void *)(gm_sh4e4kqha06b3));
                        GM_ST(uintptr_t, (uint8_t *)&t830 + 8, (uintptr_t)(6));
                        gm_l221 = t830;
                    } else {
                        gm_b16_8 t831;
                        GM_ST(void *, (uint8_t *)&t831, (void *)(gm_svbhcvlahd4n2));
                        GM_ST(uintptr_t, (uint8_t *)&t831 + 8, (uintptr_t)(2));
                        gm_l221 = t831;
                    }
                    gm_b16_8 t832 = gm_l221;
                    gm_l138 = t832;
                    goto gm_lb1;
                }
gm_lb102:;
                {
                    uint8_t t833 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t834 = (uint8_t)(t833 == ((uint8_t)44ULL));
                    if (!t834) {
                        goto gm_lb103;
                    }
                    void * t835 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l103 = t835;
                    gm_b16_8 t836 = GM_LD(gm_b16_8, (((((uint8_t *)t157) + 8) + 8) + 8));
                    gm_l104 = t836;
                    void * t837 = gm_l0;
                    uint64_t t838 = gm_l1;
                    gm_b16_8 t839 = gm_l4;
                    gm_tnpifv20irnph t840;
                    GM_ST(gm_b16_8, ((uint8_t *)&t840 + 0), t839);
                    gm_f_format_vprps7i((void *)&gm_l222, (void *)NULL, cx, NULL, t840);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t841 = (uint8_t)!((((uint8_t *)&gm_l222))[0] != 0);
                    if (!t841) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l222) + 8, gm_srco4ljquuk5d87, 277, 33);
                    gm_b16_8 t842 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l222) + 8));
                    gm_b16_8 t843 = GM_LD(gm_b16_8, ((uint8_t *)&t842));
                    gm_b0_1 t844 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t837, t838, t843);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t845 = gm_l0;
                    uint64_t t846 = gm_l1;
                    uint64_t r847; bool o848 = __builtin_add_overflow(t846, ((uint64_t)1ULL), &r847);
                    if (o848) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 278, 26);
                    uint64_t t849 = r847;
                    void * t850 = gm_l103;
                    gm_b0_1 t851 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t845, t849, t850);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t852 = gm_l104;
                    gm_l105 = t852;
                    gm_l106 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t853 = gm_l106;
                            uint64_t t854 = GM_LD(uint64_t, (((uint8_t *)&gm_l105) + 8));
                            uint8_t t855 = (t853 < t854);
                            if (!t855) {
                                goto gm_lb104;
                            }
                            {
                                void * t856 = GM_LD(void *, ((uint8_t *)&gm_l105));
                                uint64_t t857 = GM_LD(uint64_t, (((uint8_t *)&gm_l105) + 8));
                                uint64_t t858 = gm_l106;
                                {
                                    {
                                        memmove(((uint8_t *)&gm_l107), (((uint8_t *)t856) + (uintptr_t)t858 * 24), 24);
                                        void * t859 = gm_l0;
                                        uint64_t t860 = gm_l1;
                                        uint64_t r861; bool o862 = __builtin_add_overflow(t860, ((uint64_t)1ULL), &r861);
                                        if (o862) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 279, 41);
                                        uint64_t t863 = r861;
                                        void * t864 = (void *)((uint8_t *)&gm_l107);
                                        gm_b0_1 t865 = gm_f_write_arm_4vimkqo((void *)NULL, cx, NULL, t859, t863, t864);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb107;
                                    }
gm_lb108:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 279, 13);
                                }
gm_lb107:;
                            }
gm_lb106:;
                            uint64_t t866 = gm_l106;
                            uint64_t r867; bool o868 = __builtin_add_overflow(t866, ((uint64_t)1ULL), &r867);
                            uint64_t t869 = r867;
                            gm_l106 = t869;
                        }
                    }
gm_lb104:;
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb103:;
            }
            {
                {
                    uint8_t t870 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t871 = (uint8_t)(t870 == ((uint8_t)46ULL));
                    if (!t871) {
                        goto gm_lb109;
                    }
                    void * t872 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l108 = t872;
                    void * t873 = gm_l0;
                    uint64_t t874 = gm_l1;
                    gm_b16_8 t875 = gm_l4;
                    gm_tnpifv20irnph t876;
                    GM_ST(gm_b16_8, ((uint8_t *)&t876 + 0), t875);
                    gm_f_format_rvu96av((void *)&gm_l223, (void *)NULL, cx, NULL, t876);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t877 = (uint8_t)!((((uint8_t *)&gm_l223))[0] != 0);
                    if (!t877) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l223) + 8, gm_srco4ljquuk5d87, 283, 33);
                    gm_b16_8 t878 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l223) + 8));
                    gm_b16_8 t879 = GM_LD(gm_b16_8, ((uint8_t *)&t878));
                    gm_b0_1 t880 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t873, t874, t879);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t881 = gm_l0;
                    uint64_t t882 = gm_l1;
                    uint64_t r883; bool o884 = __builtin_add_overflow(t882, ((uint64_t)1ULL), &r883);
                    if (o884) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 284, 27);
                    uint64_t t885 = r883;
                    void * t886 = gm_l108;
                    gm_b0_1 t887 = gm_f_write_block_q5mpqjf((void *)NULL, cx, NULL, t881, t885, t886);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb109:;
                {
                    uint8_t t888 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t889 = (uint8_t)(t888 == ((uint8_t)47ULL));
                    if (!t889) {
                        goto gm_lb110;
                    }
                    memmove(((uint8_t *)&gm_l109), ((((uint8_t *)t157) + 8) + 8), 32);
                    void * t890 = gm_l0;
                    uint64_t t891 = gm_l1;
                    gm_b16_8 t892 = gm_l4;
                    gm_b0_1 t893 = gm_f_write_if_oqjiok5((void *)NULL, cx, NULL, t890, t891, &(*(gm_trl2khmuphncd *)((uint8_t *)&gm_l109)), t892);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb110:;
                {
                    uint8_t t894 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t895 = (uint8_t)(t894 == ((uint8_t)48ULL));
                    if (!t895) {
                        goto gm_lb111;
                    }
                    memmove(((uint8_t *)&F.l110), ((((uint8_t *)t157) + 8) + 8), 40);
                    void * t896 = gm_l0;
                    uint64_t t897 = gm_l1;
                    gm_b16_8 t898 = gm_l4;
                    gm_b0_1 t899 = gm_f_write_loop_u0ja07u((void *)NULL, cx, NULL, t896, t897, &(*(gm_t710lsrq1m8gk *)((uint8_t *)&F.l110)), t898);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb111:;
                {
                    uint8_t t900 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t901 = (uint8_t)(t900 == ((uint8_t)49ULL));
                    if (!t901) {
                        goto gm_lb112;
                    }
                    memmove(((uint8_t *)&gm_l111), ((((uint8_t *)t157) + 8) + 8), 24);
                    {
                        {
                            uint8_t t902 = (uint8_t)(GM_LD(void *, (((uint8_t *)&gm_l111) + 16)) != 0);
                            if (!t902) {
                                goto gm_lb114;
                            }
                            void * t903 = GM_LD(void *, (((uint8_t *)&gm_l111) + 16));
                            gm_l112 = t903;
                            void * t904 = (void *)((uint8_t *)&F.l5);
                            void * t905 = gm_l112;
                            gm_tftb1odc739t5 t906 = gm_f_push_pf02nv6(t904, t905);
                            gm_l224 = t906;
                            uint8_t t907 = (uint8_t)!((((uint8_t *)&gm_l224))[0] != 0);
                            if (!t907) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l224) + 1, gm_srco4ljquuk5d87, 296, 31);
                            goto gm_lb113;
                        }
gm_lb114:;
                    }
gm_lb113:;
                    gm_te9hrk0u35rev t908 = GM_LD(gm_te9hrk0u35rev, ((uint8_t *)&gm_l111));
                    gm_b16_8 t909 = gm_f_target_text_4kebb7t((void *)NULL, cx, NULL, t908);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t910;
                    GM_ST(gm_b16_8, ((uint8_t *)&t910 + 0), t909);
                    gm_f_format_galn15q((void *)&gm_l225, (void *)NULL, cx, NULL, t910);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t911 = (uint8_t)!((((uint8_t *)&gm_l225))[0] != 0);
                    if (!t911) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l225) + 8, gm_srco4ljquuk5d87, 297, 13);
                    gm_b16_8 t912 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l225) + 8));
                    gm_b16_8 t913 = GM_LD(gm_b16_8, ((uint8_t *)&t912));
                    gm_l138 = t913;
                    goto gm_lb1;
                }
gm_lb112:;
            }
            {
                {
                    uint8_t t914 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t915 = (uint8_t)(t914 == ((uint8_t)50ULL));
                    if (!t915) {
                        goto gm_lb115;
                    }
                    gm_te9hrk0u35rev t916 = GM_LD(gm_te9hrk0u35rev, ((((uint8_t *)t157) + 8) + 8));
                    gm_l113 = t916;
                    gm_te9hrk0u35rev t917 = gm_l113;
                    gm_b16_8 t918 = gm_f_target_text_4kebb7t((void *)NULL, cx, NULL, t917);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t919;
                    GM_ST(gm_b16_8, ((uint8_t *)&t919 + 0), t918);
                    gm_f_format_pcmnhpk((void *)&gm_l226, (void *)NULL, cx, NULL, t919);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t920 = (uint8_t)!((((uint8_t *)&gm_l226))[0] != 0);
                    if (!t920) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l226) + 8, gm_srco4ljquuk5d87, 299, 25);
                    gm_b16_8 t921 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l226) + 8));
                    gm_b16_8 t922 = GM_LD(gm_b16_8, ((uint8_t *)&t921));
                    gm_l138 = t922;
                    goto gm_lb1;
                }
gm_lb115:;
                {
                    uint8_t t923 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t924 = (uint8_t)(t923 == ((uint8_t)51ULL));
                    if (!t924) {
                        goto gm_lb116;
                    }
                    uint32_t t925 = GM_LD(uint32_t, ((((uint8_t *)t157) + 8) + 8));
                    gm_l114 = t925;
                    void * t926 = GM_LD(void *, (((((uint8_t *)t157) + 8) + 8) + 8));
                    gm_l115 = t926;
                    void * t927 = (void *)((uint8_t *)&F.l5);
                    void * t928 = gm_l115;
                    gm_tftb1odc739t5 t929 = gm_f_push_pf02nv6(t927, t928);
                    gm_l227 = t929;
                    uint8_t t930 = (uint8_t)!((((uint8_t *)&gm_l227))[0] != 0);
                    if (!t930) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l227) + 1, gm_srco4ljquuk5d87, 301, 13);
                    uint32_t t931 = gm_l114;
                    gm_t88rcucg1b7uk t932;
                    GM_ST(uint32_t, ((uint8_t *)&t932 + 0), t931);
                    gm_f_format_6na8imi((void *)&gm_l228, (void *)NULL, cx, NULL, t932);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t933 = (uint8_t)!((((uint8_t *)&gm_l228))[0] != 0);
                    if (!t933) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l228) + 8, gm_srco4ljquuk5d87, 302, 13);
                    gm_b16_8 t934 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l228) + 8));
                    gm_b16_8 t935 = GM_LD(gm_b16_8, ((uint8_t *)&t934));
                    gm_l138 = t935;
                    goto gm_lb1;
                }
gm_lb116:;
                {
                    uint8_t t936 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t937 = (uint8_t)(t936 == ((uint8_t)52ULL));
                    if (!t937) {
                        goto gm_lb117;
                    }
                    void * t938 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l116 = t938;
                    void * t939 = (void *)((uint8_t *)&F.l5);
                    void * t940 = gm_l116;
                    gm_tftb1odc739t5 t941 = gm_f_push_pf02nv6(t939, t940);
                    gm_l229 = t941;
                    uint8_t t942 = (uint8_t)!((((uint8_t *)&gm_l229))[0] != 0);
                    if (!t942) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l229) + 1, gm_srco4ljquuk5d87, 305, 13);
                    gm_b16_8 t943;
                    GM_ST(void *, (uint8_t *)&t943, (void *)(gm_sinmpfqtbo9in));
                    GM_ST(uintptr_t, (uint8_t *)&t943 + 8, (uintptr_t)(4));
                    gm_l138 = t943;
                    goto gm_lb1;
                }
gm_lb117:;
                {
                    uint8_t t944 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t945 = (uint8_t)(t944 == ((uint8_t)53ULL));
                    if (!t945) {
                        goto gm_lb118;
                    }
                    void * t946 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l117 = t946;
                    void * t947 = GM_LD(void *, (((((uint8_t *)t157) + 8) + 8) + 8));
                    gm_l118 = t947;
                    void * t948 = (void *)((uint8_t *)&F.l5);
                    void * t949 = gm_l117;
                    gm_tftb1odc739t5 t950 = gm_f_push_pf02nv6(t948, t949);
                    gm_l230 = t950;
                    uint8_t t951 = (uint8_t)!((((uint8_t *)&gm_l230))[0] != 0);
                    if (!t951) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l230) + 1, gm_srco4ljquuk5d87, 309, 13);
                    void * t952 = (void *)((uint8_t *)&F.l5);
                    void * t953 = gm_l118;
                    gm_tftb1odc739t5 t954 = gm_f_push_pf02nv6(t952, t953);
                    gm_l231 = t954;
                    uint8_t t955 = (uint8_t)!((((uint8_t *)&gm_l231))[0] != 0);
                    if (!t955) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l231) + 1, gm_srco4ljquuk5d87, 310, 13);
                    gm_b16_8 t956;
                    GM_ST(void *, (uint8_t *)&t956, (void *)(gm_sn6sqlran7h3r));
                    GM_ST(uintptr_t, (uint8_t *)&t956 + 8, (uintptr_t)(6));
                    gm_l138 = t956;
                    goto gm_lb1;
                }
gm_lb118:;
            }
            {
                {
                    uint8_t t957 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t958 = (uint8_t)(t957 == ((uint8_t)54ULL));
                    if (!t958) {
                        goto gm_lb119;
                    }
                    void * t959 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l119 = t959;
                    void * t960 = (void *)((uint8_t *)&F.l5);
                    void * t961 = gm_l119;
                    gm_tftb1odc739t5 t962 = gm_f_push_pf02nv6(t960, t961);
                    gm_l232 = t962;
                    uint8_t t963 = (uint8_t)!((((uint8_t *)&gm_l232))[0] != 0);
                    if (!t963) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l232) + 1, gm_srco4ljquuk5d87, 314, 13);
                    gm_b16_8 t964;
                    GM_ST(void *, (uint8_t *)&t964, (void *)(gm_si1m7q6ia0hto));
                    GM_ST(uintptr_t, (uint8_t *)&t964 + 8, (uintptr_t)(6));
                    gm_l138 = t964;
                    goto gm_lb1;
                }
gm_lb119:;
                {
                    uint8_t t965 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t966 = (uint8_t)(t965 == ((uint8_t)55ULL));
                    if (!t966) {
                        goto gm_lb120;
                    }
                    void * t967 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l120 = t967;
                    {
                        {
                            void * t968 = gm_l120;
                            uint8_t t969 = GM_LD(uint8_t, (((uint8_t *)t968) + 0));
                            uint8_t t970 = (uint8_t)(t969 == ((uint8_t)1ULL));
                            if (!t970) {
                                goto gm_lb122;
                            }
                            void * t971 = GM_LD(void *, (((uint8_t *)t968) + 8));
                            gm_l121 = t971;
                            void * t972 = (void *)((uint8_t *)&F.l5);
                            void * t973 = gm_l121;
                            gm_tftb1odc739t5 t974 = gm_f_push_pf02nv6(t972, t973);
                            gm_l233 = t974;
                            uint8_t t975 = (uint8_t)!((((uint8_t *)&gm_l233))[0] != 0);
                            if (!t975) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l233) + 1, gm_srco4ljquuk5d87, 318, 35);
                            goto gm_lb121;
                        }
gm_lb122:;
                    }
gm_lb121:;
                    {
                        {
                            void * t976 = gm_l120;
                            uint8_t t977 = (uint8_t)(GM_LD(void *, (((uint8_t *)t976) + 88)) != 0);
                            if (!t977) {
                                goto gm_lb124;
                            }
                            void * t978 = GM_LD(void *, (((uint8_t *)t976) + 88));
                            gm_l122 = t978;
                            void * t979 = (void *)((uint8_t *)&F.l5);
                            void * t980 = gm_l122;
                            gm_tftb1odc739t5 t981 = gm_f_push_pf02nv6(t979, t980);
                            gm_l234 = t981;
                            uint8_t t982 = (uint8_t)!((((uint8_t *)&gm_l234))[0] != 0);
                            if (!t982) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l234) + 1, gm_srco4ljquuk5d87, 319, 31);
                            goto gm_lb123;
                        }
gm_lb124:;
                    }
gm_lb123:;
                    gm_l124 = ((uint64_t)0ULL);
                    void * t983 = gm_l120;
                    uint64_t t984 = GM_LD(uint64_t, ((((uint8_t *)t983) + 72) + 8));
                    gm_l123 = t984;
                    {
                        for (;;) {
                            uint64_t t985 = gm_l124;
                            uint64_t t986 = gm_l123;
                            uint8_t t987 = (t985 < t986);
                            if (!t987) {
                                goto gm_lb125;
                            }
                            {
                                {
                                    {
                                        uint64_t t988 = gm_l124;
                                        gm_l125 = t988;
                                        void * t989 = (void *)((uint8_t *)&F.l5);
                                        void * t990 = gm_l120;
                                        void * t991 = GM_LD(void *, (((uint8_t *)t990) + 72));
                                        uint64_t t992 = GM_LD(uint64_t, ((((uint8_t *)t990) + 72) + 8));
                                        uint64_t t993 = gm_l125;
                                        if ((uintptr_t)t993 >= (uintptr_t)(t992)) gm_trap_index((uintptr_t)t993, t992, gm_srco4ljquuk5d87, 320, 49);
                                        void * t994 = (void *)(((uint8_t *)t991) + (uintptr_t)t993 * 88);
                                        gm_tftb1odc739t5 t995 = gm_f_push_pf02nv6(t989, t994);
                                        gm_l235 = t995;
                                        uint8_t t996 = (uint8_t)!((((uint8_t *)&gm_l235))[0] != 0);
                                        if (!t996) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l235) + 1, gm_srco4ljquuk5d87, 320, 38);
                                        goto gm_lb128;
                                    }
gm_lb129:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 320, 13);
                                }
gm_lb128:;
                            }
gm_lb127:;
                            uint64_t t997 = gm_l124;
                            uint64_t r998; bool o999 = __builtin_add_overflow(t997, ((uint64_t)1ULL), &r998);
                            uint64_t t1000 = r998;
                            gm_l124 = t1000;
                        }
                    }
gm_lb125:;
                    void * t1001 = gm_l120;
                    {
                        {
                            uint8_t t1002 = (uint8_t)(GM_LD(void *, (((uint8_t *)t1001) + 88)) != 0);
                            if (t1002) {
                                goto gm_lb131;
                            }
                            gm_l237 = ((uint8_t)1);
                            goto gm_lb130;
                        }
gm_lb131:;
                        {
                            gm_l237 = ((uint8_t)0);
                            goto gm_lb130;
                        }
gm_lb132:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 321, 16);
                    }
gm_lb130:;
                    uint8_t t1003 = gm_l237;
                    if (t1003) {
                        void * t1004 = gm_l0;
                        void * t1005 = gm_l120;
                        gm_b16_8 t1006 = gm_f_write_text_h6e1ibp((void *)NULL, cx, NULL, t1004, &(*(gm_t150deeftog49 *)((uint8_t *)t1005)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l236 = t1006;
                    } else {
                        void * t1007 = gm_l0;
                        void * t1008 = gm_l120;
                        gm_b16_8 t1009 = gm_f_sink_text_s4o8tr0((void *)NULL, cx, NULL, t1007, &(*(gm_th1g69d7vpipg *)((uint8_t *)t1008)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t1010;
                        GM_ST(gm_b16_8, ((uint8_t *)&t1010 + 0), t1009);
                        gm_f_format_dva3pbu((void *)&gm_l238, (void *)NULL, cx, NULL, t1010);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t1011 = (uint8_t)!((((uint8_t *)&gm_l238))[0] != 0);
                        if (!t1011) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l238) + 8, gm_srco4ljquuk5d87, 321, 60);
                        gm_b16_8 t1012 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l238) + 8));
                        gm_b16_8 t1013 = GM_LD(gm_b16_8, ((uint8_t *)&t1012));
                        gm_l236 = t1013;
                    }
                    gm_b16_8 t1014 = gm_l236;
                    gm_l138 = t1014;
                    goto gm_lb1;
                }
gm_lb120:;
            }
            {
                {
                    uint8_t t1015 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t1016 = (uint8_t)(t1015 == ((uint8_t)57ULL));
                    if (!t1016) {
                        goto gm_lb133;
                    }
                    void * t1017 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l126 = t1017;
                    gm_trqfvmjtt4sij t1018;
                    gm_f_list_uvfu1k1((void *)&t1018, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l127 = t1018;
                    void * t1019 = (void *)((uint8_t *)&F.l127);
                    gm_b16_8 t1020;
                    GM_ST(void *, (uint8_t *)&t1020, (void *)(gm_sj3cmvbmi7cm6));
                    GM_ST(uintptr_t, (uint8_t *)&t1020 + 8, (uintptr_t)(8));
                    gm_tftb1odc739t5 t1021 = gm_f_extend_0cs85gs(t1019, t1020);
                    gm_l239 = t1021;
                    uint8_t t1022 = (uint8_t)!((((uint8_t *)&gm_l239))[0] != 0);
                    if (!t1022) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l239) + 1, gm_srco4ljquuk5d87, 325, 13);
                    gm_l129 = ((uint64_t)0ULL);
                    void * t1023 = gm_l126;
                    uint64_t t1024 = GM_LD(uint64_t, (((uint8_t *)t1023) + 8));
                    gm_l128 = t1024;
                    {
                        for (;;) {
                            uint64_t t1025 = gm_l129;
                            uint64_t t1026 = gm_l128;
                            uint8_t t1027 = (t1025 < t1026);
                            if (!t1027) {
                                goto gm_lb134;
                            }
                            {
                                {
                                    {
                                        uint64_t t1028 = gm_l129;
                                        gm_l130 = t1028;
                                        void * t1029 = (void *)((uint8_t *)&F.l127);
                                        gm_tftb1odc739t5 t1030 = gm_f_push_t83rg2m(t1029, ((uint8_t)32ULL));
                                        gm_l240 = t1030;
                                        uint8_t t1031 = (uint8_t)!((((uint8_t *)&gm_l240))[0] != 0);
                                        if (!t1031) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l240) + 1, gm_srco4ljquuk5d87, 327, 17);
                                        void * t1032 = gm_l126;
                                        void * t1033 = GM_LD(void *, ((uint8_t *)t1032));
                                        uint64_t t1034 = GM_LD(uint64_t, (((uint8_t *)t1032) + 8));
                                        uint64_t t1035 = gm_l130;
                                        if ((uintptr_t)t1035 >= (uintptr_t)(t1034)) gm_trap_index((uintptr_t)t1035, t1034, gm_srco4ljquuk5d87, 328, 37);
                                        gm_b16_8 t1036 = GM_LD(gm_b16_8, (((uint8_t *)t1033) + (uintptr_t)t1035 * 16));
                                        void * t1037 = (void *)((uint8_t *)&F.l127);
                                        gm_b0_1 t1038 = gm_f_escape_bytes_25mo226((void *)NULL, cx, NULL, t1036, t1037);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint64_t t1039 = gm_l130;
                                        void * t1040 = gm_l126;
                                        uint64_t t1041 = GM_LD(uint64_t, ((((uint8_t *)t1040) + 16) + 8));
                                        uint8_t t1042 = (t1039 < t1041);
                                        if (t1042) {
                                            void * t1043 = (void *)((uint8_t *)&F.l127);
                                            void * t1044 = gm_l126;
                                            void * t1045 = GM_LD(void *, (((uint8_t *)t1044) + 32));
                                            uint64_t t1046 = GM_LD(uint64_t, ((((uint8_t *)t1044) + 32) + 8));
                                            uint64_t t1047 = gm_l130;
                                            if ((uintptr_t)t1047 >= (uintptr_t)(t1046)) gm_trap_index((uintptr_t)t1047, t1046, gm_srco4ljquuk5d87, 330, 38);
                                            uint32_t t1048 = GM_LD(uint32_t, (((uint8_t *)t1045) + (uintptr_t)t1047 * 4));
                                            gm_t88rcucg1b7uk t1049;
                                            GM_ST(uint32_t, ((uint8_t *)&t1049 + 0), t1048);
                                            gm_tftb1odc739t5 t1050 = gm_f_write_4lsqsab(t1043, t1049);
                                            gm_l241 = t1050;
                                            uint8_t t1051 = (uint8_t)!((((uint8_t *)&gm_l241))[0] != 0);
                                            if (!t1051) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l241) + 1, gm_srco4ljquuk5d87, 330, 21);
                                            void * t1052 = gm_l126;
                                            void * t1053 = GM_LD(void *, (((uint8_t *)t1052) + 16));
                                            uint64_t t1054 = GM_LD(uint64_t, ((((uint8_t *)t1052) + 16) + 8));
                                            uint64_t t1055 = gm_l130;
                                            if ((uintptr_t)t1055 >= (uintptr_t)(t1054)) gm_trap_index((uintptr_t)t1055, t1054, gm_srco4ljquuk5d87, 331, 45);
                                            void * t1056 = (void *)(((uint8_t *)t1053) + (uintptr_t)t1055 * 24);
                                            uint8_t t1057 = gm_f_is_default_spec_cdd8jng(t1056);
                                            if (t1057) {
                                                void * t1058 = (void *)((uint8_t *)&F.l127);
                                                gm_tftb1odc739t5 t1059 = gm_f_push_t83rg2m(t1058, ((uint8_t)125ULL));
                                                gm_l242 = t1059;
                                                uint8_t t1060 = (uint8_t)!((((uint8_t *)&gm_l242))[0] != 0);
                                                if (!t1060) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l242) + 1, gm_srco4ljquuk5d87, 332, 25);
                                            } else {
                                                void * t1061 = (void *)((uint8_t *)&F.l127);
                                                gm_tftb1odc739t5 t1062 = gm_f_push_t83rg2m(t1061, ((uint8_t)32ULL));
                                                gm_l243 = t1062;
                                                uint8_t t1063 = (uint8_t)!((((uint8_t *)&gm_l243))[0] != 0);
                                                if (!t1063) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l243) + 1, gm_srco4ljquuk5d87, 334, 25);
                                                void * t1064 = gm_l126;
                                                void * t1065 = GM_LD(void *, (((uint8_t *)t1064) + 16));
                                                uint64_t t1066 = GM_LD(uint64_t, ((((uint8_t *)t1064) + 16) + 8));
                                                uint64_t t1067 = gm_l130;
                                                if ((uintptr_t)t1067 >= (uintptr_t)(t1066)) gm_trap_index((uintptr_t)t1067, t1066, gm_srco4ljquuk5d87, 335, 36);
                                                void * t1068 = (void *)(((uint8_t *)t1065) + (uintptr_t)t1067 * 24);
                                                void * t1069 = (void *)((uint8_t *)&F.l127);
                                                gm_b0_1 t1070 = gm_f_spec_text_fr9884d((void *)NULL, cx, NULL, t1068, t1069);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t1071 = (void *)((uint8_t *)&F.l127);
                                                gm_tftb1odc739t5 t1072 = gm_f_push_t83rg2m(t1071, ((uint8_t)125ULL));
                                                gm_l244 = t1072;
                                                uint8_t t1073 = (uint8_t)!((((uint8_t *)&gm_l244))[0] != 0);
                                                if (!t1073) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l244) + 1, gm_srco4ljquuk5d87, 336, 25);
                                            }
                                        }
                                        goto gm_lb137;
                                    }
gm_lb138:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 326, 13);
                                }
gm_lb137:;
                            }
gm_lb136:;
                            uint64_t t1074 = gm_l129;
                            uint64_t r1075; bool o1076 = __builtin_add_overflow(t1074, ((uint64_t)1ULL), &r1075);
                            uint64_t t1077 = r1075;
                            gm_l129 = t1077;
                        }
                    }
gm_lb134:;
                    gm_b16_8 t1078 = GM_LD(gm_b16_8, ((uint8_t *)&F.l127));
                    gm_b16_8 t1079 = GM_LD(gm_b16_8, ((uint8_t *)&t1078));
                    gm_l138 = t1079;
                    goto gm_lb1;
                }
gm_lb133:;
            }
            {
                {
                    uint8_t t1080 = GM_LD(uint8_t, ((((uint8_t *)t157) + 8) + 0));
                    uint8_t t1081 = (uint8_t)(t1080 == ((uint8_t)56ULL));
                    if (!t1081) {
                        goto gm_lb139;
                    }
                    void * t1082 = GM_LD(void *, ((((uint8_t *)t157) + 8) + 8));
                    gm_l131 = t1082;
                    {
                        {
                            void * t1083 = gm_l131;
                            uint8_t t1084 = GM_LD(uint8_t, (((uint8_t *)t1083) + 0));
                            uint8_t t1085 = (uint8_t)(t1084 == ((uint8_t)1ULL));
                            if (!t1085) {
                                goto gm_lb141;
                            }
                            void * t1086 = GM_LD(void *, (((uint8_t *)t1083) + 8));
                            gm_l132 = t1086;
                            void * t1087 = (void *)((uint8_t *)&F.l5);
                            void * t1088 = gm_l132;
                            gm_tftb1odc739t5 t1089 = gm_f_push_pf02nv6(t1087, t1088);
                            gm_l245 = t1089;
                            uint8_t t1090 = (uint8_t)!((((uint8_t *)&gm_l245))[0] != 0);
                            if (!t1090) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l245) + 1, gm_srco4ljquuk5d87, 343, 35);
                            goto gm_lb140;
                        }
gm_lb141:;
                    }
gm_lb140:;
                    void * t1091 = (void *)((uint8_t *)&F.l5);
                    void * t1092 = gm_l131;
                    void * t1093 = GM_LD(void *, (((uint8_t *)t1092) + 40));
                    gm_tftb1odc739t5 t1094 = gm_f_push_pf02nv6(t1091, t1093);
                    gm_l246 = t1094;
                    uint8_t t1095 = (uint8_t)!((((uint8_t *)&gm_l246))[0] != 0);
                    if (!t1095) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l246) + 1, gm_srco4ljquuk5d87, 344, 13);
                    {
                        {
                            void * t1096 = gm_l131;
                            uint8_t t1097 = (uint8_t)(GM_LD(void *, (((uint8_t *)t1096) + 48)) != 0);
                            if (!t1097) {
                                goto gm_lb143;
                            }
                            void * t1098 = GM_LD(void *, (((uint8_t *)t1096) + 48));
                            gm_l133 = t1098;
                            void * t1099 = (void *)((uint8_t *)&F.l5);
                            void * t1100 = gm_l133;
                            gm_tftb1odc739t5 t1101 = gm_f_push_pf02nv6(t1099, t1100);
                            gm_l247 = t1101;
                            uint8_t t1102 = (uint8_t)!((((uint8_t *)&gm_l247))[0] != 0);
                            if (!t1102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l247) + 1, gm_srco4ljquuk5d87, 345, 30);
                            goto gm_lb142;
                        }
gm_lb143:;
                    }
gm_lb142:;
                    void * t1103 = gm_l0;
                    void * t1104 = gm_l131;
                    gm_b16_8 t1105 = gm_f_sink_text_s4o8tr0((void *)NULL, cx, NULL, t1103, &(*(gm_th1g69d7vpipg *)((uint8_t *)t1104)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t1106 = gm_l0;
                    void * t1107 = gm_l131;
                    uint32_t t1108 = GM_LD(uint32_t, (((uint8_t *)t1107) + 56));
                    gm_b16_8 t1109 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t1106, t1108);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t1110;
                    GM_ST(gm_b16_8, ((uint8_t *)&t1110 + 0), t1105);
                    GM_ST(gm_b16_8, ((uint8_t *)&t1110 + 16), t1109);
                    gm_f_format_llf38gp((void *)&gm_l248, (void *)NULL, cx, NULL, &t1110);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t1111 = (uint8_t)!((((uint8_t *)&gm_l248))[0] != 0);
                    if (!t1111) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l248) + 8, gm_srco4ljquuk5d87, 346, 13);
                    gm_b16_8 t1112 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l248) + 8));
                    gm_b16_8 t1113 = GM_LD(gm_b16_8, ((uint8_t *)&t1112));
                    gm_l138 = t1113;
                    goto gm_lb1;
                }
gm_lb139:;
                gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 125, 20);
            }
        }
gm_lb1:;
    }
    {
        gm_b16_8 t1114 = gm_l138;
        gm_l134 = t1114;
        void * t1115 = gm_l0;
        uint64_t t1116 = gm_l1;
        gm_b16_8 t1117 = gm_l134;
        gm_b16_8 t1118 = gm_l4;
        gm_t6j6smj7qoctu t1119;
        GM_ST(gm_b16_8, ((uint8_t *)&t1119 + 0), t1117);
        GM_ST(gm_b16_8, ((uint8_t *)&t1119 + 16), t1118);
        gm_f_format_r0u4lpe((void *)&gm_l249, (void *)NULL, cx, NULL, &t1119);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t1120 = (uint8_t)!((((uint8_t *)&gm_l249))[0] != 0);
        if (!t1120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l249) + 8, gm_srco4ljquuk5d87, 349, 25);
        gm_b16_8 t1121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l249) + 8));
        gm_b16_8 t1122 = GM_LD(gm_b16_8, ((uint8_t *)&t1121));
        gm_b0_1 t1123 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t1115, t1116, t1122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t1124 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_l135 = t1124;
        gm_l136 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t1125 = gm_l136;
                uint64_t t1126 = GM_LD(uint64_t, (((uint8_t *)&gm_l135) + 8));
                uint8_t t1127 = (t1125 < t1126);
                if (!t1127) {
                    goto gm_lb144;
                }
                {
                    void * t1128 = GM_LD(void *, ((uint8_t *)&gm_l135));
                    uint64_t t1129 = GM_LD(uint64_t, (((uint8_t *)&gm_l135) + 8));
                    uint64_t t1130 = gm_l136;
                    {
                        {
                            void * t1131 = GM_LD(void *, (((uint8_t *)t1128) + (uintptr_t)t1130 * 8));
                            gm_l137 = t1131;
                            void * t1132 = gm_l0;
                            uint64_t t1133 = gm_l1;
                            uint64_t r1134; bool o1135 = __builtin_add_overflow(t1133, ((uint64_t)1ULL), &r1134);
                            if (o1135) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 350, 40);
                            uint64_t t1136 = r1134;
                            void * t1137 = gm_l137;
                            gm_b0_1 t1138 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t1132, t1136, t1137);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb147;
                        }
gm_lb148:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 350, 5);
                    }
gm_lb147:;
                }
gm_lb146:;
                uint64_t t1139 = gm_l136;
                uint64_t r1140; bool o1141 = __builtin_add_overflow(t1139, ((uint64_t)1ULL), &r1140);
                uint64_t t1142 = r1140;
                gm_l136 = t1142;
            }
        }
gm_lb144:;
        F.ret = GM_ZERO(gm_b0_1);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_fn_jkakt7l(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_write_fn_jkakt7l F;
    void * gm_l0; /* d */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* s */
    void * gm_l3; /* sig */
    void * gm_l4; /* def */
    gm_b16_8 gm_l5; /* kind */
    gm_b16_8 gm_l6; /* head */
    uint32_t gm_l7; /* p */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint8_t gm_l12; /* param */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    void * gm_l17; /* b */
    GM_STACK_CHECK(&F, 1850u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_t5im2v50m3hk6 gm_l21;
    uint8_t gm_l22;
    gm_tf5srh4mp4b3i gm_l23;
    gm_b16_8 gm_l24;
    gm_b16_8 gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_tabb710d9mq2e gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_tabb710d9mq2e gm_l30;
    void * t24 = gm_l0;
    uint32_t t25 = gm_l1;
    GM_ST(uint32_t, (((uint8_t *)t24) + 48), t25);
    void * t26 = gm_l0;
    void * t27 = GM_LD(void *, ((uint8_t *)t26));
    gm_l2 = t27;
    void * t28 = gm_l2;
    void * t29 = GM_LD(void *, (((uint8_t *)t28) + 344));
    uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t28) + 344) + 8));
    uint32_t t31 = gm_l1;
    uint64_t t32 = (uint64_t)t31;
    if ((uintptr_t)t32 >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)t32, t30, gm_srco4ljquuk5d87, 77, 13);
    void * t33 = (void *)(((uint8_t *)t29) + (uintptr_t)t32 * 136);
    gm_l3 = t33;
    void * t34 = gm_l2;
    void * t35 = GM_LD(void *, (((uint8_t *)t34) + 384));
    uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)t34) + 384) + 8));
    uint32_t t37 = gm_l1;
    uint64_t t38 = (uint64_t)t37;
    if ((uintptr_t)t38 >= (uintptr_t)(t36)) gm_trap_index((uintptr_t)t38, t36, gm_srco4ljquuk5d87, 78, 13);
    void * t39 = (void *)(((uint8_t *)t35) + (uintptr_t)t38 * 64);
    gm_l4 = t39;
    void * t40 = gm_l4;
    {
        {
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)t40) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)0ULL));
            if (!t42) {
                goto gm_lb2;
            }
            gm_b16_8 t43;
            GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s19ndlkgdtviu));
            GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(5));
            gm_l18 = t43;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)t40) + 0));
            uint8_t t45 = (uint8_t)(t44 == ((uint8_t)1ULL));
            if (!t45) {
                goto gm_lb3;
            }
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s4lo69slu4igt));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(7));
            gm_l18 = t46;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)t40) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)2ULL));
            if (!t48) {
                goto gm_lb4;
            }
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s7dkbj8tvt9e3));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(6));
            gm_l18 = t49;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)t40) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)3ULL));
            if (!t51) {
                goto gm_lb5;
            }
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_sv0fbf330lmcd));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(7));
            gm_l18 = t52;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 79, 13);
    }
gm_lb1:;
    gm_b16_8 t53 = gm_l18;
    gm_l5 = t53;
    void * t54 = gm_l2;
    uint32_t t55 = gm_l1;
    gm_b16_8 t56 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t54, t55);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t57 = gm_l5;
    gm_t6j6smj7qoctu t58;
    GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t56);
    GM_ST(gm_b16_8, ((uint8_t *)&t58 + 16), t57);
    gm_f_format_9nv39ua((void *)&gm_l19, (void *)NULL, cx, NULL, &t58);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
    if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srco4ljquuk5d87, 85, 13);
    gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
    gm_l6 = t60;
    {
        {
            void * t61 = gm_l4;
            uint8_t t62 = (uint8_t)(((((uint8_t *)t61) + 48))[4] != 0);
            if (!t62) {
                goto gm_lb7;
            }
            uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)t61) + 48));
            gm_l7 = t63;
            gm_b16_8 t64 = gm_l6;
            void * t65 = gm_l2;
            uint32_t t66 = gm_l7;
            gm_b16_8 t67 = gm_f_fn_label_9hbn37h((void *)NULL, cx, NULL, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t68;
            GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t64);
            GM_ST(gm_b16_8, ((uint8_t *)&t68 + 16), t67);
            gm_f_format_p2bh4bb((void *)&gm_l20, (void *)NULL, cx, NULL, &t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srco4ljquuk5d87, 86, 33);
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
            gm_l6 = t70;
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    void * t71 = gm_l0;
    gm_b16_8 t72 = gm_l6;
    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
    gm_b0_1 t74 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t71, ((uint64_t)0ULL), t73);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t75 = gm_l4;
    gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)t75) + 24));
    gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
    gm_l8 = t77;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t78 = gm_l9;
            uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t80 = (t78 < t79);
            if (!t80) {
                goto gm_lb8;
            }
            {
                uint64_t t81 = gm_l9;
                void * t82 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t84 = gm_l9;
                gm_tsoie1ppgdvcg t85 = GM_LD(gm_tsoie1ppgdvcg, (((uint8_t *)t82) + (uintptr_t)t84 * 40));
                gm_t5im2v50m3hk6 t86;
                GM_ST(uint64_t, ((uint8_t *)&t86 + 0), t81);
                GM_ST(gm_tsoie1ppgdvcg, ((uint8_t *)&t86 + 8), t85);
                gm_l21 = t86;
                {
                    {
                        uint64_t t87 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
                        gm_l10 = t87;
                        memmove(((uint8_t *)&F.l11), (((uint8_t *)&gm_l21) + 8), 40);
                        void * t88 = gm_l4;
                        gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)t88) + 8));
                        gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                        uint64_t t91 = gm_l10;
                        uint32_t t92 = (uint32_t)t91;
                        gm_tf5srh4mp4b3i t93 = gm_f_index_of_41b084m(t90, t92);
                        gm_l23 = t93;
                        {
                            {
                                uint8_t t94 = (uint8_t)((((uint8_t *)&gm_l23))[8] != 0);
                                if (t94) {
                                    goto gm_lb14;
                                }
                                gm_l22 = ((uint8_t)0);
                                goto gm_lb13;
                            }
gm_lb14:;
                            {
                                gm_l22 = ((uint8_t)1);
                                goto gm_lb13;
                            }
gm_lb15:;
                            gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 89, 18);
                        }
gm_lb13:;
                        uint8_t t95 = gm_l22;
                        gm_l12 = t95;
                        void * t96 = gm_l0;
                        uint8_t t97 = gm_l12;
                        if (t97) {
                            gm_b16_8 t98;
                            GM_ST(void *, (uint8_t *)&t98, (void *)(gm_skissj10nclhr));
                            GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(5));
                            gm_l24 = t98;
                        } else {
                            gm_b16_8 t99;
                            GM_ST(void *, (uint8_t *)&t99, (void *)(gm_sjg1ri23hm753));
                            GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(5));
                            gm_l24 = t99;
                        }
                        gm_b16_8 t100 = gm_l24;
                        uint64_t t101 = gm_l10;
                        gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                        void * t103 = gm_l2;
                        gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l11) + 16));
                        gm_b16_8 t105 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t103, t104);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 20));
                        if (t106) {
                            gm_b16_8 t107;
                            GM_ST(void *, (uint8_t *)&t107, (void *)(gm_s6ag7edtaqqri));
                            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(4));
                            gm_l25 = t107;
                        } else {
                            gm_b16_8 t108;
                            GM_ST(void *, (uint8_t *)&t108, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(0));
                            gm_l25 = t108;
                        }
                        gm_b16_8 t109 = gm_l25;
                        gm_tcrdb1inj22os t110;
                        GM_ST(gm_b16_8, ((uint8_t *)&t110 + 0), t100);
                        GM_ST(uint64_t, ((uint8_t *)&t110 + 16), t101);
                        GM_ST(gm_b16_8, ((uint8_t *)&t110 + 24), t102);
                        GM_ST(gm_b16_8, ((uint8_t *)&t110 + 40), t105);
                        GM_ST(gm_b16_8, ((uint8_t *)&t110 + 56), t109);
                        gm_f_format_i7k9v76((void *)&gm_l26, (void *)NULL, cx, NULL, &t110);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t111 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                        if (!t111) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_srco4ljquuk5d87, 90, 25);
                        gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                        gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                        gm_b0_1 t114 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t96, ((uint64_t)1ULL), t113);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb11;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 88, 5);
                }
gm_lb11:;
            }
gm_lb10:;
            uint64_t t115 = gm_l9;
            uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
            uint64_t t118 = r116;
            gm_l9 = t118;
        }
    }
gm_lb8:;
    void * t119 = gm_l0;
    void * t120 = gm_l2;
    void * t121 = gm_l3;
    gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t121) + 56));
    gm_b16_8 t123 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t120, t122);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t124;
    GM_ST(gm_b16_8, ((uint8_t *)&t124 + 0), t123);
    gm_f_format_8k3m4s1((void *)&gm_l27, (void *)NULL, cx, NULL, t124);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t125 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
    if (!t125) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_srco4ljquuk5d87, 92, 21);
    gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
    gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
    gm_b0_1 t128 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t119, ((uint64_t)1ULL), t127);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t129 = gm_l3;
    uint64_t t130 = GM_LD(uint64_t, ((((uint8_t *)t129) + 64) + 8));
    uint8_t t131 = (t130 > ((uint64_t)0ULL));
    if (t131) {
        gm_trqfvmjtt4sij t132;
        gm_f_list_epvducg((void *)&t132, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t132;
        gm_l15 = ((uint64_t)0ULL);
        void * t133 = gm_l3;
        uint64_t t134 = GM_LD(uint64_t, ((((uint8_t *)t133) + 64) + 8));
        gm_l14 = t134;
        {
            for (;;) {
                uint64_t t135 = gm_l15;
                uint64_t t136 = gm_l14;
                uint8_t t137 = (t135 < t136);
                if (!t137) {
                    goto gm_lb16;
                }
                {
                    {
                        {
                            uint64_t t138 = gm_l15;
                            gm_l16 = t138;
                            void * t139 = (void *)((uint8_t *)&F.l13);
                            void * t140 = gm_l2;
                            void * t141 = (void *)(((uint8_t *)t140) + 8);
                            void * t142 = gm_l3;
                            void * t143 = GM_LD(void *, (((uint8_t *)t142) + 64));
                            uint64_t t144 = GM_LD(uint64_t, ((((uint8_t *)t142) + 64) + 8));
                            uint64_t t145 = gm_l16;
                            if ((uintptr_t)t145 >= (uintptr_t)(t144)) gm_trap_index((uintptr_t)t145, t144, gm_srco4ljquuk5d87, 95, 74);
                            void * t146 = (void *)(((uint8_t *)t143) + (uintptr_t)t145 * 24);
                            gm_b16_8 t147 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t141, t146);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t148 = gm_f_push_3ef746b(t139, t147);
                            gm_l28 = t148;
                            uint8_t t149 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                            if (!t149) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srco4ljquuk5d87, 95, 47);
                            goto gm_lb19;
                        }
gm_lb20:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 95, 9);
                    }
gm_lb19:;
                }
gm_lb18:;
                uint64_t t150 = gm_l15;
                uint64_t r151; bool o152 = __builtin_add_overflow(t150, ((uint64_t)1ULL), &r151);
                uint64_t t153 = r151;
                gm_l15 = t153;
            }
        }
gm_lb16:;
        void * t154 = gm_l0;
        gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
        gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&t155));
        gm_b16_8 t157;
        GM_ST(void *, (uint8_t *)&t157, (void *)(gm_s0m0qaokk51g0));
        GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(3));
        gm_f_join_sfknljb((void *)&gm_l29, (void *)NULL, cx, NULL, t156, t157);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t158 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
        if (!t158) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srco4ljquuk5d87, 96, 48);
        gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
        gm_tnpifv20irnph t160;
        GM_ST(gm_b16_8, ((uint8_t *)&t160 + 0), t159);
        gm_f_format_7316bju((void *)&gm_l30, (void *)NULL, cx, NULL, t160);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t161 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
        if (!t161) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srco4ljquuk5d87, 96, 25);
        gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
        gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&t162));
        gm_b0_1 t164 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t154, ((uint64_t)1ULL), t163);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t165 = gm_l4;
            uint8_t t166 = (uint8_t)(GM_LD(void *, (((uint8_t *)t165) + 40)) != 0);
            if (!t166) {
                goto gm_lb22;
            }
            void * t167 = GM_LD(void *, (((uint8_t *)t165) + 40));
            gm_l17 = t167;
            void * t168 = gm_l0;
            void * t169 = gm_l17;
            gm_b0_1 t170 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t168, ((uint64_t)1ULL), t169);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb21;
        }
gm_lb22:;
        void * t171 = gm_l0;
        gm_b16_8 t172;
        GM_ST(void *, (uint8_t *)&t172, (void *)(gm_st7pul9bg6mqu));
        GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(7));
        gm_b0_1 t173 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t171, ((uint64_t)1ULL), t172);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
gm_lb21:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_if_oqjiok5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3) {
    struct gm_fr_write_if_oqjiok5 F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_trl2khmuphncd gm_l2; /* x */
    gm_b16_8 gm_l3; /* ty */
    void * gm_l4; /* el */
    GM_STACK_CHECK(&F, 386u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l5;
    void * t4 = gm_l0;
    uint64_t t5 = gm_l1;
    gm_b16_8 t6 = gm_l3;
    gm_tnpifv20irnph t7;
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
    gm_f_format_7ejl94l((void *)&gm_l5, (void *)NULL, cx, NULL, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srco4ljquuk5d87, 384, 25);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    gm_b0_1 t11 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t4, t5, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t12 = gm_l0;
    uint64_t t13 = gm_l1;
    uint64_t r14; bool o15 = __builtin_add_overflow(t13, ((uint64_t)1ULL), &r14);
    if (o15) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 385, 19);
    uint64_t t16 = r14;
    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)a2));
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
    gm_b0_1 t19 = gm_f_write_conds_k9e57un((void *)NULL, cx, NULL, t12, t16, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t20 = gm_l0;
    uint64_t t21 = gm_l1;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    if (o23) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 386, 18);
    uint64_t t24 = r22;
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sofbtko7t8taa));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(4));
    gm_b0_1 t26 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t20, t24, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t27 = gm_l0;
    uint64_t t28 = gm_l1;
    uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)2ULL), &r29);
    if (o30) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 387, 18);
    uint64_t t31 = r29;
    void * t32 = GM_LD(void *, (((uint8_t *)a2) + 16));
    gm_b0_1 t33 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t27, t31, t32);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t34 = (uint8_t)(GM_LD(void *, (((uint8_t *)a2) + 24)) != 0);
            if (!t34) {
                goto gm_lb2;
            }
            void * t35 = GM_LD(void *, (((uint8_t *)a2) + 24));
            gm_l4 = t35;
            void * t36 = gm_l0;
            uint64_t t37 = gm_l1;
            uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
            if (o39) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 389, 22);
            uint64_t t40 = r38;
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s9es2tos1bhfp));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(4));
            gm_b0_1 t42 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t36, t40, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t43 = gm_l0;
            uint64_t t44 = gm_l1;
            uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)2ULL), &r45);
            if (o46) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 390, 22);
            uint64_t t47 = r45;
            void * t48 = gm_l4;
            gm_b0_1 t49 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t43, t47, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_line_d6lkps7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_write_line_d6lkps7 F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l2; /* text */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t7 = gm_l1;
    gm_l3 = t7;
    {
        for (;;) {
            uint64_t t8 = gm_l4;
            uint64_t t9 = gm_l3;
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t11 = gm_l0;
                        void * t12 = (void *)(((uint8_t *)t11) + 8);
                        gm_b16_8 t13;
                        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sgankvlme129s));
                        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t14 = gm_f_extend_0cs85gs(t12, t13);
                        gm_l5 = t14;
                        uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                        if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srco4ljquuk5d87, 69, 25);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 69, 5);
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
    void * t20 = gm_l0;
    void * t21 = (void *)(((uint8_t *)t20) + 8);
    gm_b16_8 t22 = gm_l2;
    gm_tftb1odc739t5 t23 = gm_f_extend_0cs85gs(t21, t22);
    gm_l6 = t23;
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srco4ljquuk5d87, 70, 5);
    void * t25 = gm_l0;
    void * t26 = (void *)(((uint8_t *)t25) + 8);
    gm_tftb1odc739t5 t27 = gm_f_push_t83rg2m(t26, ((uint8_t)10ULL));
    gm_l7 = t27;
    uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srco4ljquuk5d87, 71, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_loop_u0ja07u(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3) {
    struct gm_fr_write_loop_u0ja07u F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l3; /* ty */
    void * gm_l4; /* st */
    GM_STACK_CHECK(&F, 366u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l5;
    void * t4 = gm_l0;
    uint64_t t5 = gm_l1;
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)a2));
    gm_b16_8 t7 = gm_l3;
    gm_tiilmfd16v3fc t8;
    GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t8 + 0), t6);
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 8), t7);
    gm_f_format_lde77u1((void *)&gm_l5, (void *)NULL, cx, NULL, &t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srco4ljquuk5d87, 396, 25);
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
    gm_b0_1 t12 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t4, t5, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t13 = gm_l0;
    uint64_t t14 = gm_l1;
    uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 397, 19);
    uint64_t t17 = r15;
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
    gm_b0_1 t20 = gm_f_write_conds_k9e57un((void *)NULL, cx, NULL, t13, t17, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t21 = gm_l0;
    uint64_t t22 = gm_l1;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    if (o24) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 398, 18);
    uint64_t t25 = r23;
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sgk015tlfp2cu));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(4));
    gm_b0_1 t27 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t21, t25, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t28 = gm_l0;
    uint64_t t29 = gm_l1;
    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)2ULL), &r30);
    if (o31) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 399, 19);
    uint64_t t32 = r30;
    void * t33 = GM_LD(void *, (((uint8_t *)a2) + 24));
    gm_b0_1 t34 = gm_f_write_block_q5mpqjf((void *)NULL, cx, NULL, t28, t32, t33);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t35 = (uint8_t)(GM_LD(void *, (((uint8_t *)a2) + 32)) != 0);
            if (!t35) {
                goto gm_lb2;
            }
            void * t36 = GM_LD(void *, (((uint8_t *)a2) + 32));
            gm_l4 = t36;
            void * t37 = gm_l0;
            uint64_t t38 = gm_l1;
            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
            if (o40) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 401, 22);
            uint64_t t41 = r39;
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_si9h59pjqftat));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(4));
            gm_b0_1 t43 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t37, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t44 = gm_l0;
            uint64_t t45 = gm_l1;
            uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)2ULL), &r46);
            if (o47) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 402, 22);
            uint64_t t48 = r46;
            void * t49 = gm_l4;
            gm_b0_1 t50 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t44, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_pat_4qu9dcc(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_write_pat_4qu9dcc F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    void * gm_l2; /* p */
    gm_b16_8 gm_l3; /* ty */
    uint32_t gm_l4; /* l */
    void * gm_l5; /* x */
    uint8_t gm_l8; /* inclusive */
    void * gm_l9; /* q */
    void * gm_l10; /* q */
    void * gm_l11; /* q */
    gm_b16_8 gm_l12; /* name */
    uint32_t gm_l13; /* v */
    uint32_t gm_l14; /* v */
    void * gm_l15; /* q */
    gm_b16_8 gm_l16; /* fs */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l19; /* f */
    gm_b16_8 gm_l20; /* ps */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    void * gm_l23; /* q */
    gm_b16_8 gm_l24; /* head */
    GM_STACK_CHECK(&F, 2041u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_tabb710d9mq2e gm_l27;
    gm_b16_8 gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_b16_8 gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_tabb710d9mq2e gm_l37;
    {
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, ((uint8_t *)t31));
        void * t33 = gm_l2;
        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t33));
        gm_b16_8 t35 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t32, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t35;
        void * t36 = gm_l2;
        {
            {
                {
                    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t38 = (uint8_t)(t37 == ((uint8_t)0ULL));
                    if (!t38) {
                        goto gm_lb2;
                    }
                    gm_b16_8 t39;
                    GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sbuhie0vb9t62));
                    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(8));
                    gm_l25 = t39;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)1ULL));
                    if (!t41) {
                        goto gm_lb3;
                    }
                    uint32_t t42 = GM_LD(uint32_t, ((((uint8_t *)t36) + 8) + 8));
                    gm_l4 = t42;
                    void * t43 = gm_l0;
                    uint32_t t44 = gm_l4;
                    gm_b16_8 t45 = gm_f_local_text_75tuacb((void *)NULL, cx, NULL, t43, t44);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t46;
                    GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
                    gm_f_format_lpj4rro((void *)&gm_l26, (void *)NULL, cx, NULL, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                    if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_srco4ljquuk5d87, 550, 21);
                    gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                    gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                    gm_l25 = t49;
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)2ULL));
                    if (!t51) {
                        goto gm_lb4;
                    }
                    void * t52 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
                    gm_l5 = t52;
                    void * t53 = gm_l0;
                    uint64_t t54 = gm_l1;
                    gm_b16_8 t55 = gm_l3;
                    gm_tnpifv20irnph t56;
                    GM_ST(gm_b16_8, ((uint8_t *)&t56 + 0), t55);
                    gm_f_format_8vmpj9s((void *)&gm_l27, (void *)NULL, cx, NULL, t56);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                    if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_srco4ljquuk5d87, 552, 33);
                    gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
                    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                    gm_b0_1 t60 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t53, t54, t59);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t61 = gm_l0;
                    uint64_t t62 = gm_l1;
                    uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                    if (o64) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 553, 26);
                    uint64_t t65 = r63;
                    void * t66 = gm_l5;
                    gm_b0_1 t67 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t61, t65, t66);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb4:;
                {
                    uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t69 = (uint8_t)(t68 == ((uint8_t)3ULL));
                    if (!t69) {
                        goto gm_lb5;
                    }
                    memmove(((uint8_t *)&F.l6), ((((uint8_t *)t36) + 8) + 8), 48);
                    memmove(((uint8_t *)&F.l7), (((((uint8_t *)t36) + 8) + 8) + 48), 48);
                    uint8_t t70 = GM_LD(uint8_t, (((((uint8_t *)t36) + 8) + 8) + 96));
                    gm_l8 = t70;
                    gm_b16_8 t71 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t72 = gm_f_show_int_ub6npub((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t73 = gm_l8;
                    if (t73) {
                        gm_b16_8 t74;
                        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sksprenpp79ih));
                        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(10));
                        gm_l28 = t74;
                    } else {
                        gm_b16_8 t75;
                        GM_ST(void *, (uint8_t *)&t75, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(0));
                        gm_l28 = t75;
                    }
                    gm_b16_8 t76 = gm_l28;
                    gm_tfeab279c16rr t77;
                    GM_ST(gm_b16_8, ((uint8_t *)&t77 + 0), t71);
                    GM_ST(gm_b16_8, ((uint8_t *)&t77 + 16), t72);
                    GM_ST(gm_b16_8, ((uint8_t *)&t77 + 32), t76);
                    gm_f_format_6h8qssq((void *)&gm_l29, (void *)NULL, cx, NULL, &t77);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                    if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srco4ljquuk5d87, 556, 38);
                    gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                    gm_l25 = t80;
                    goto gm_lb1;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t82 = (uint8_t)(t81 == ((uint8_t)4ULL));
                    if (!t82) {
                        goto gm_lb6;
                    }
                    gm_b16_8 t83;
                    GM_ST(void *, (uint8_t *)&t83, (void *)(gm_sovel03lmfl8u));
                    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(8));
                    gm_l25 = t83;
                    goto gm_lb1;
                }
gm_lb6:;
                {
                    {
                        {
                            uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                            uint8_t t85 = (uint8_t)(t84 == ((uint8_t)5ULL));
                            if (!t85) {
                                goto gm_lb9;
                            }
                            void * t86 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
                            gm_l9 = t86;
                            goto gm_lb8;
                        }
gm_lb9:;
                        {
                            uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                            uint8_t t88 = (uint8_t)(t87 == ((uint8_t)6ULL));
                            if (!t88) {
                                goto gm_lb10;
                            }
                            void * t89 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
                            gm_l9 = t89;
                            goto gm_lb8;
                        }
gm_lb10:;
                        {
                            uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                            uint8_t t91 = (uint8_t)(t90 == ((uint8_t)7ULL));
                            if (!t91) {
                                goto gm_lb11;
                            }
                            void * t92 = GM_LD(void *, ((((uint8_t *)t36) + 8) + 8));
                            gm_l9 = t92;
                            goto gm_lb8;
                        }
gm_lb11:;
                        goto gm_lb7;
                    }
gm_lb8:;
                    void * t93 = gm_l2;
                    {
                        {
                            uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t93) + 8) + 0));
                            uint8_t t95 = (uint8_t)(t94 == ((uint8_t)5ULL));
                            if (!t95) {
                                goto gm_lb13;
                            }
                            gm_b16_8 t96;
                            GM_ST(void *, (uint8_t *)&t96, (void *)(gm_sjourjtj9e3to));
                            GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(8));
                            gm_l30 = t96;
                            goto gm_lb12;
                        }
gm_lb13:;
                        {
                            uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t93) + 8) + 0));
                            uint8_t t98 = (uint8_t)(t97 == ((uint8_t)6ULL));
                            if (!t98) {
                                goto gm_lb14;
                            }
                            gm_b16_8 t99;
                            GM_ST(void *, (uint8_t *)&t99, (void *)(gm_se0a26478itki));
                            GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(6));
                            gm_l30 = t99;
                            goto gm_lb12;
                        }
gm_lb14:;
                        {
                            gm_b16_8 t100;
                            GM_ST(void *, (uint8_t *)&t100, (void *)(gm_s2iprgandb78l));
                            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(7));
                            gm_l30 = t100;
                            goto gm_lb12;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 559, 21);
                    }
gm_lb12:;
                    gm_b16_8 t101 = gm_l30;
                    gm_l12 = t101;
                    void * t102 = gm_l0;
                    uint64_t t103 = gm_l1;
                    gm_b16_8 t104 = gm_l12;
                    gm_b16_8 t105 = gm_l3;
                    gm_t6j6smj7qoctu t106;
                    GM_ST(gm_b16_8, ((uint8_t *)&t106 + 0), t104);
                    GM_ST(gm_b16_8, ((uint8_t *)&t106 + 16), t105);
                    gm_f_format_r0u4lpe((void *)&gm_l31, (void *)NULL, cx, NULL, &t106);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                    if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srco4ljquuk5d87, 564, 33);
                    gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                    gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
                    gm_b0_1 t110 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t102, t103, t109);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t111 = gm_l0;
                    uint64_t t112 = gm_l1;
                    uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                    if (o114) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 565, 25);
                    uint64_t t115 = r113;
                    void * t116 = gm_l9;
                    gm_b0_1 t117 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t111, t115, t116);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb7:;
            }
            {
                {
                    uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t119 = (uint8_t)(t118 == ((uint8_t)8ULL));
                    if (!t119) {
                        goto gm_lb16;
                    }
                    uint32_t t120 = GM_LD(uint32_t, ((((uint8_t *)t36) + 8) + 8));
                    gm_l13 = t120;
                    uint8_t t121 = (uint8_t)(GM_LD(void *, (((((uint8_t *)t36) + 8) + 8) + 8)) != 0);
                    if (t121) {
                        goto gm_lb16;
                    }
                    uint32_t t122 = gm_l13;
                    gm_t88rcucg1b7uk t123;
                    GM_ST(uint32_t, ((uint8_t *)&t123 + 0), t122);
                    gm_f_format_3669u6h((void *)&gm_l32, (void *)NULL, cx, NULL, t123);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                    if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_srco4ljquuk5d87, 568, 30);
                    gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                    gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
                    gm_l25 = t126;
                    goto gm_lb1;
                }
gm_lb16:;
                {
                    uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t128 = (uint8_t)(t127 == ((uint8_t)8ULL));
                    if (!t128) {
                        goto gm_lb17;
                    }
                    uint32_t t129 = GM_LD(uint32_t, ((((uint8_t *)t36) + 8) + 8));
                    gm_l14 = t129;
                    uint8_t t130 = (uint8_t)(GM_LD(void *, (((((uint8_t *)t36) + 8) + 8) + 8)) != 0);
                    if (!t130) {
                        goto gm_lb17;
                    }
                    void * t131 = GM_LD(void *, (((((uint8_t *)t36) + 8) + 8) + 8));
                    gm_l15 = t131;
                    void * t132 = gm_l0;
                    uint64_t t133 = gm_l1;
                    uint32_t t134 = gm_l14;
                    gm_b16_8 t135 = gm_l3;
                    gm_tiilmfd16v3fc t136;
                    GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t136 + 0), t134);
                    GM_ST(gm_b16_8, ((uint8_t *)&t136 + 8), t135);
                    gm_f_format_p7ogfqk((void *)&gm_l33, (void *)NULL, cx, NULL, &t136);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t137 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                    if (!t137) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srco4ljquuk5d87, 570, 33);
                    gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                    gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
                    gm_b0_1 t140 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t132, t133, t139);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t141 = gm_l0;
                    uint64_t t142 = gm_l1;
                    uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                    if (o144) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 571, 25);
                    uint64_t t145 = r143;
                    void * t146 = gm_l15;
                    gm_b0_1 t147 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t141, t145, t146);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb17:;
                {
                    uint8_t t148 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t149 = (uint8_t)(t148 == ((uint8_t)9ULL));
                    if (!t149) {
                        goto gm_lb18;
                    }
                    gm_b16_8 t150 = GM_LD(gm_b16_8, ((((uint8_t *)t36) + 8) + 8));
                    gm_l16 = t150;
                    void * t151 = gm_l0;
                    uint64_t t152 = gm_l1;
                    gm_b16_8 t153 = gm_l3;
                    gm_tnpifv20irnph t154;
                    GM_ST(gm_b16_8, ((uint8_t *)&t154 + 0), t153);
                    gm_f_format_a4vi4f7((void *)&gm_l34, (void *)NULL, cx, NULL, t154);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t155 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                    if (!t155) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_srco4ljquuk5d87, 575, 33);
                    gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                    gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                    gm_b0_1 t158 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t151, t152, t157);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t159 = gm_l16;
                    gm_l17 = t159;
                    gm_l18 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t160 = gm_l18;
                            uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                            uint8_t t162 = (t160 < t161);
                            if (!t162) {
                                goto gm_lb19;
                            }
                            {
                                void * t163 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                uint64_t t165 = gm_l18;
                                {
                                    {
                                        gm_ttggq7ng4kp3p t166 = GM_LD(gm_ttggq7ng4kp3p, (((uint8_t *)t163) + (uintptr_t)t165 * 16));
                                        gm_l19 = t166;
                                        void * t167 = gm_l0;
                                        uint64_t t168 = gm_l1;
                                        uint64_t r169; bool o170 = __builtin_add_overflow(t168, ((uint64_t)1ULL), &r169);
                                        if (o170) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 577, 30);
                                        uint64_t t171 = r169;
                                        uint32_t t172 = GM_LD(uint32_t, ((uint8_t *)&gm_l19));
                                        gm_t88rcucg1b7uk t173;
                                        GM_ST(uint32_t, ((uint8_t *)&t173 + 0), t172);
                                        gm_f_format_208te7b((void *)&gm_l35, (void *)NULL, cx, NULL, t173);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t174 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                        if (!t174) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_srco4ljquuk5d87, 577, 41);
                                        gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                        gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&t175));
                                        gm_b0_1 t177 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t167, t171, t176);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t178 = gm_l0;
                                        uint64_t t179 = gm_l1;
                                        uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)2ULL), &r180);
                                        if (o181) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 578, 29);
                                        uint64_t t182 = r180;
                                        void * t183 = GM_LD(void *, (((uint8_t *)&gm_l19) + 8));
                                        gm_b0_1 t184 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t178, t182, t183);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb22;
                                    }
gm_lb23:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 576, 13);
                                }
gm_lb22:;
                            }
gm_lb21:;
                            uint64_t t185 = gm_l18;
                            uint64_t r186; bool o187 = __builtin_add_overflow(t185, ((uint64_t)1ULL), &r186);
                            uint64_t t188 = r186;
                            gm_l18 = t188;
                        }
                    }
gm_lb19:;
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb18:;
            }
            {
                {
                    uint8_t t189 = GM_LD(uint8_t, ((((uint8_t *)t36) + 8) + 0));
                    uint8_t t190 = (uint8_t)(t189 == ((uint8_t)10ULL));
                    if (!t190) {
                        goto gm_lb24;
                    }
                    gm_b16_8 t191 = GM_LD(gm_b16_8, ((((uint8_t *)t36) + 8) + 8));
                    gm_l20 = t191;
                    void * t192 = gm_l0;
                    uint64_t t193 = gm_l1;
                    gm_b16_8 t194 = gm_l3;
                    gm_tnpifv20irnph t195;
                    GM_ST(gm_b16_8, ((uint8_t *)&t195 + 0), t194);
                    gm_f_format_6b7mjf6((void *)&gm_l36, (void *)NULL, cx, NULL, t195);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t196 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                    if (!t196) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_srco4ljquuk5d87, 583, 33);
                    gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                    gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)&t197));
                    gm_b0_1 t199 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t192, t193, t198);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t200 = gm_l20;
                    gm_l21 = t200;
                    gm_l22 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t201 = gm_l22;
                            uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                            uint8_t t203 = (t201 < t202);
                            if (!t203) {
                                goto gm_lb25;
                            }
                            {
                                void * t204 = GM_LD(void *, ((uint8_t *)&gm_l21));
                                uint64_t t205 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                uint64_t t206 = gm_l22;
                                {
                                    {
                                        void * t207 = GM_LD(void *, (((uint8_t *)t204) + (uintptr_t)t206 * 8));
                                        gm_l23 = t207;
                                        void * t208 = gm_l0;
                                        uint64_t t209 = gm_l1;
                                        uint64_t r210; bool o211 = __builtin_add_overflow(t209, ((uint64_t)1ULL), &r210);
                                        if (o211) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 584, 39);
                                        uint64_t t212 = r210;
                                        void * t213 = gm_l23;
                                        gm_b0_1 t214 = gm_f_write_pat_4qu9dcc((void *)NULL, cx, NULL, t208, t212, t213);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb28;
                                    }
gm_lb29:;
                                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 584, 13);
                                }
gm_lb28:;
                            }
gm_lb27:;
                            uint64_t t215 = gm_l22;
                            uint64_t r216; bool o217 = __builtin_add_overflow(t215, ((uint64_t)1ULL), &r216);
                            uint64_t t218 = r216;
                            gm_l22 = t218;
                        }
                    }
gm_lb25:;
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb24:;
                gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 548, 20);
            }
        }
gm_lb1:;
    }
    {
        gm_b16_8 t219 = gm_l25;
        gm_l24 = t219;
        void * t220 = gm_l0;
        uint64_t t221 = gm_l1;
        gm_b16_8 t222 = gm_l24;
        gm_b16_8 t223 = gm_l3;
        gm_t6j6smj7qoctu t224;
        GM_ST(gm_b16_8, ((uint8_t *)&t224 + 0), t222);
        GM_ST(gm_b16_8, ((uint8_t *)&t224 + 16), t223);
        gm_f_format_r0u4lpe((void *)&gm_l37, (void *)NULL, cx, NULL, &t224);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t225 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
        if (!t225) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_srco4ljquuk5d87, 588, 25);
        gm_b16_8 t226 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
        gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&t226));
        gm_b0_1 t228 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t220, t221, t227);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_write_text_h6e1ibp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_write_text_h6e1ibp F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* p */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* spec */
    GM_STACK_CHECK(&F, 511u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_trqfvmjtt4sij t12;
    gm_f_list_uvfu1k1((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t12;
    void * t13 = (void *)((uint8_t *)&F.l2);
    void * t14 = gm_l0;
    gm_b16_8 t15 = gm_f_sink_text_s4o8tr0((void *)NULL, cx, NULL, t14, &(*(gm_th1g69d7vpipg *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t16;
    GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t15);
    gm_tftb1odc739t5 t17 = gm_f_write_uebhmoa(t13, t16);
    gm_l10 = t17;
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srco4ljquuk5d87, 409, 5);
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 40));
    gm_l3 = t19;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t20 = gm_l4;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t25 = gm_l4;
                {
                    {
                        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t23) + (uintptr_t)t25 * 16));
                        gm_l5 = t26;
                        void * t27 = (void *)((uint8_t *)&F.l2);
                        gm_tftb1odc739t5 t28 = gm_f_push_t83rg2m(t27, ((uint8_t)32ULL));
                        gm_l11 = t28;
                        uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srco4ljquuk5d87, 411, 9);
                        gm_b16_8 t30 = gm_l5;
                        void * t31 = (void *)((uint8_t *)&F.l2);
                        gm_b0_1 t32 = gm_f_escape_bytes_25mo226((void *)NULL, cx, NULL, t30, t31);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 410, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t33 = gm_l4;
            uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
            uint64_t t36 = r34;
            gm_l4 = t36;
        }
    }
gm_lb1:;
    gm_l7 = ((uint64_t)0ULL);
    uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)&F.l1) + 56) + 8));
    gm_l6 = t37;
    {
        for (;;) {
            uint64_t t38 = gm_l7;
            uint64_t t39 = gm_l6;
            uint8_t t40 = (t38 < t39);
            if (!t40) {
                goto gm_lb6;
            }
            {
                {
                    {
                        uint64_t t41 = gm_l7;
                        gm_l8 = t41;
                        void * t42 = GM_LD(void *, (((uint8_t *)&F.l1) + 56));
                        uint64_t t43 = GM_LD(uint64_t, ((((uint8_t *)&F.l1) + 56) + 8));
                        uint64_t t44 = gm_l8;
                        void * t45 = (void *)(((uint8_t *)t42) + (uintptr_t)t44 * 24);
                        gm_l9 = t45;
                        void * t46 = gm_l9;
                        uint8_t t47 = gm_f_is_default_spec_cdd8jng(t46);
                        if (t47) {
                            void * t48 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t49;
                            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sjeopa747clpb));
                            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(3));
                            gm_tftb1odc739t5 t50 = gm_f_extend_0cs85gs(t48, t49);
                            gm_l12 = t50;
                            uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                            if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srco4ljquuk5d87, 417, 13);
                        } else {
                            void * t52 = (void *)((uint8_t *)&F.l2);
                            gm_tftb1odc739t5 t53 = gm_f_push_t83rg2m(t52, ((uint8_t)32ULL));
                            gm_l13 = t53;
                            uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                            if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srco4ljquuk5d87, 419, 13);
                            void * t55 = gm_l9;
                            void * t56 = (void *)((uint8_t *)&F.l2);
                            gm_b0_1 t57 = gm_f_spec_text_fr9884d((void *)NULL, cx, NULL, t55, t56);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 414, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t58 = gm_l7;
            uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
            uint64_t t61 = r59;
            gm_l7 = t61;
        }
    }
gm_lb6:;
    gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_with_j37nil7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_b16_8 a3) {
    struct gm_fr_write_with_j37nil7 F;
    void * gm_l0; /* d */
    uint64_t gm_l1; /* depth */
    gm_b16_8 gm_l3; /* ty */
    void * gm_l4; /* s */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t4lnf71mruqpt gm_l7; /* h */
    void * gm_l8; /* x */
    gm_b16_8 gm_l9; /* decls */
    void * gm_l10; /* x */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint32_t gm_l14; /* i */
    void * gm_l16; /* x */
    GM_STACK_CHECK(&F, 1172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tabb710d9mq2e gm_l21;
    void * t17 = gm_l0;
    void * t18 = GM_LD(void *, ((uint8_t *)t17));
    gm_l4 = t18;
    void * t19 = gm_l0;
    uint64_t t20 = gm_l1;
    uint32_t t21 = GM_LD(uint32_t, ((uint8_t *)a2));
    gm_b16_8 t22 = gm_l3;
    gm_tiilmfd16v3fc t23;
    GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t23 + 0), t21);
    GM_ST(gm_b16_8, ((uint8_t *)&t23 + 8), t22);
    gm_f_format_gnbipfi((void *)&gm_l17, (void *)NULL, cx, NULL, &t23);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srco4ljquuk5d87, 356, 25);
    gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
    gm_b0_1 t27 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t19, t20, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
    gm_l5 = t28;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t29 = gm_l6;
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t31 = (t29 < t30);
            if (!t31) {
                goto gm_lb1;
            }
            {
                void * t32 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t34 = gm_l6;
                {
                    {
                        memmove(((uint8_t *)&gm_l7), (((uint8_t *)t32) + (uintptr_t)t34 * 32), 32);
                        {
                            {
                                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)0ULL));
                                if (!t36) {
                                    goto gm_lb7;
                                }
                                void * t37 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                                gm_l8 = t37;
                                void * t38 = gm_l0;
                                uint64_t t39 = gm_l1;
                                uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                                if (o41) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 360, 30);
                                uint64_t t42 = r40;
                                gm_b16_8 t43;
                                GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sic5vsema0r36));
                                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(7));
                                gm_b0_1 t44 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t38, t42, t43);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t45 = gm_l0;
                                uint64_t t46 = gm_l1;
                                uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)2ULL), &r47);
                                if (o48) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 361, 30);
                                uint64_t t49 = r47;
                                void * t50 = gm_l8;
                                gm_b0_1 t51 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t45, t49, t50);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                                uint8_t t53 = (uint8_t)(t52 == ((uint8_t)1ULL));
                                if (!t53) {
                                    goto gm_lb8;
                                }
                                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                                gm_l9 = t54;
                                void * t55 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 16));
                                gm_l10 = t55;
                                gm_trqfvmjtt4sij t56;
                                gm_f_list_epvducg((void *)&t56, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l11 = t56;
                                gm_b16_8 t57 = gm_l9;
                                gm_l12 = t57;
                                gm_l13 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t58 = gm_l13;
                                        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                        uint8_t t60 = (t58 < t59);
                                        if (!t60) {
                                            goto gm_lb9;
                                        }
                                        {
                                            void * t61 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                            uint64_t t63 = gm_l13;
                                            {
                                                {
                                                    uint32_t t64 = GM_LD(uint32_t, (((uint8_t *)t61) + (uintptr_t)t63 * 4));
                                                    gm_l14 = t64;
                                                    void * t65 = gm_l4;
                                                    void * t66 = GM_LD(void *, (((uint8_t *)t65) + 544));
                                                    uint64_t t67 = GM_LD(uint64_t, ((((uint8_t *)t65) + 544) + 8));
                                                    uint32_t t68 = gm_l14;
                                                    uint64_t t69 = (uint64_t)t68;
                                                    if ((uintptr_t)t69 >= (uintptr_t)(t67)) gm_trap_index((uintptr_t)t69, t67, gm_srco4ljquuk5d87, 366, 27);
                                                    memmove(((uint8_t *)&F.l15), (((uint8_t *)t66) + (uintptr_t)t69 * 64), 64);
                                                    void * t70 = (void *)((uint8_t *)&F.l11);
                                                    void * t71 = gm_l4;
                                                    void * t72 = (void *)(((uint8_t *)t71) + 8);
                                                    void * t73 = (void *)((uint8_t *)&F.l15);
                                                    gm_b16_8 t74 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t72, t73);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t75 = gm_l4;
                                                    gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 24));
                                                    gm_b16_8 t77 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t75, t76);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_t6j6smj7qoctu t78;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t78 + 0), t74);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t78 + 16), t77);
                                                    gm_f_format_3aao56v((void *)&gm_l18, (void *)NULL, cx, NULL, &t78);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                                    if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srco4ljquuk5d87, 367, 32);
                                                    gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                                                    gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
                                                    gm_tftb1odc739t5 t82 = gm_f_push_3ef746b(t70, t81);
                                                    gm_l19 = t82;
                                                    uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                                    if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srco4ljquuk5d87, 367, 21);
                                                    goto gm_lb12;
                                                }
gm_lb13:;
                                                gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 365, 17);
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
                                void * t88 = gm_l0;
                                uint64_t t89 = gm_l1;
                                uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                                if (o91) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 369, 30);
                                uint64_t t92 = r90;
                                gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                                gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                                gm_b16_8 t95;
                                GM_ST(void *, (uint8_t *)&t95, (void *)(gm_sg5gdnrith2i6));
                                GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(2));
                                gm_f_join_sfknljb((void *)&gm_l20, (void *)NULL, cx, NULL, t94, t95);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srco4ljquuk5d87, 369, 67);
                                gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                                gm_tnpifv20irnph t98;
                                GM_ST(gm_b16_8, ((uint8_t *)&t98 + 0), t97);
                                gm_f_format_i37o6qg((void *)&gm_l21, (void *)NULL, cx, NULL, t98);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                if (!t99) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_srco4ljquuk5d87, 369, 41);
                                gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                                gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
                                gm_b0_1 t102 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t88, t92, t101);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t103 = gm_l0;
                                uint64_t t104 = gm_l1;
                                uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)2ULL), &r105);
                                if (o106) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 370, 30);
                                uint64_t t107 = r105;
                                void * t108 = gm_l10;
                                gm_b0_1 t109 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t103, t107, t108);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 358, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srco4ljquuk5d87, 357, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t110 = gm_l6;
            uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
            uint64_t t113 = r111;
            gm_l6 = t113;
        }
    }
gm_lb1:;
    {
        {
            uint8_t t114 = (uint8_t)(GM_LD(void *, (((uint8_t *)a2) + 24)) != 0);
            if (!t114) {
                goto gm_lb15;
            }
            void * t115 = GM_LD(void *, (((uint8_t *)a2) + 24));
            gm_l16 = t115;
            void * t116 = gm_l0;
            uint64_t t117 = gm_l1;
            uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
            if (o119) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 375, 22);
            uint64_t t120 = r118;
            gm_b16_8 t121;
            GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sdl8mgtvebfcs));
            GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(2));
            gm_b0_1 t122 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t116, t120, t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t123 = gm_l0;
            uint64_t t124 = gm_l1;
            uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)2ULL), &r125);
            if (o126) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 376, 22);
            uint64_t t127 = r125;
            void * t128 = gm_l16;
            gm_b0_1 t129 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t123, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb14;
        }
gm_lb15:;
    }
gm_lb14:;
    void * t130 = gm_l0;
    uint64_t t131 = gm_l1;
    uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
    if (o133) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 378, 18);
    uint64_t t134 = r132;
    gm_b16_8 t135;
    GM_ST(void *, (uint8_t *)&t135, (void *)(gm_sgk015tlfp2cu));
    GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(4));
    gm_b0_1 t136 = gm_f_write_line_d6lkps7((void *)NULL, cx, NULL, t130, t134, t135);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t137 = gm_l0;
    uint64_t t138 = gm_l1;
    uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)2ULL), &r139);
    if (o140) gm_trap("integer overflow in `+`", gm_srco4ljquuk5d87, 379, 18);
    uint64_t t141 = r139;
    void * t142 = GM_LD(void *, (((uint8_t *)a2) + 32));
    gm_b0_1 t143 = gm_f_write_expr_cftucor((void *)NULL, cx, NULL, t137, t141, t142);
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
