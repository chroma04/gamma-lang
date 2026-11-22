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
struct gm_fr_for_body_1avb45k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_leaves_directory_kq5llq4 {
    gm_frame h;
    uint8_t ret;
    gm_b0_1 w0;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1avb45k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_cbj68cn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_n6686bi(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tdd1e60(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern uint8_t gm_c0n6vj9dmoi[8] __attribute__((aligned(8)));
extern uint8_t gm_c0nre4icjau[1] __attribute__((aligned(1)));
extern uint8_t gm_c0ocv2md26k[9] __attribute__((aligned(1)));
extern uint8_t gm_c1id0n0l1pf[8] __attribute__((aligned(8)));
extern uint8_t gm_c2a4s61n0q1[11] __attribute__((aligned(1)));
extern uint8_t gm_c2ftoibr66a[8] __attribute__((aligned(8)));
extern uint8_t gm_c2im5cngga6[7] __attribute__((aligned(1)));
extern uint8_t gm_c3oglifus73[18] __attribute__((aligned(1)));
extern uint8_t gm_c3tekh5hk29[12] __attribute__((aligned(1)));
extern uint8_t gm_c433keo1ggm[5] __attribute__((aligned(1)));
extern uint8_t gm_c4mn1ob3j1k[17] __attribute__((aligned(1)));
extern uint8_t gm_c51kem98eiq[6] __attribute__((aligned(1)));
extern uint8_t gm_c64sa63uljg[1] __attribute__((aligned(1)));
extern uint8_t gm_c66tv9kq8na[208] __attribute__((aligned(8)));
extern uint8_t gm_c6fqpnlmjmc[1] __attribute__((aligned(8)));
extern uint8_t gm_c7983l5vkhl[16] __attribute__((aligned(8)));
extern uint8_t gm_c79qbqmpu02[32] __attribute__((aligned(1)));
extern uint8_t gm_c8lsr2952en[14] __attribute__((aligned(1)));
extern uint8_t gm_c9b1o5ceti8[13] __attribute__((aligned(1)));
extern uint8_t gm_c9paf88rqhe[8] __attribute__((aligned(8)));
extern uint8_t gm_cbemal9c6bf[8] __attribute__((aligned(8)));
extern uint8_t gm_cbg3jjml4ui[5] __attribute__((aligned(1)));
extern uint8_t gm_cbvo0qov0pu[13] __attribute__((aligned(1)));
extern uint8_t gm_cc0f9pvdvgk[12] __attribute__((aligned(1)));
extern uint8_t gm_ccbbgs9ri4m[2] __attribute__((aligned(1)));
extern uint8_t gm_ccgj59b20un[15] __attribute__((aligned(1)));
extern uint8_t gm_cdeh13viquh[4] __attribute__((aligned(1)));
extern uint8_t gm_ce9kahcvfp7[1] __attribute__((aligned(8)));
extern uint8_t gm_cecnl9dg9cm[7] __attribute__((aligned(1)));
extern uint8_t gm_cel6p901dc1[1] __attribute__((aligned(1)));
extern uint8_t gm_cgsmfcq937e_2[16] __attribute__((aligned(8)));
extern uint8_t gm_cgt0a5n48ha[16] __attribute__((aligned(1)));
extern uint8_t gm_chkfraubo1r[4] __attribute__((aligned(4)));
extern uint8_t gm_cigjk56aans[16] __attribute__((aligned(1)));
extern uint8_t gm_cj5e0v8gk2i[20] __attribute__((aligned(1)));
extern uint8_t gm_cjajqd9smnj[12] __attribute__((aligned(1)));
extern uint8_t gm_cjg96tg3e0f[11] __attribute__((aligned(1)));
extern uint8_t gm_cjidvhvshci[19] __attribute__((aligned(1)));
extern uint8_t gm_cke4ts1najg[4] __attribute__((aligned(4)));
extern uint8_t gm_ckgo58jroh8[8] __attribute__((aligned(1)));
extern uint8_t gm_cl2be5u9pqk[16] __attribute__((aligned(8)));
extern uint8_t gm_clcchfce70r[32] __attribute__((aligned(1)));
extern uint8_t gm_cljjip91gu8[13] __attribute__((aligned(1)));
extern uint8_t gm_clkdg6jbli4[16] __attribute__((aligned(8)));
extern uint8_t gm_cm7k8v105sj[1] __attribute__((aligned(4)));
extern uint8_t gm_cmr3530gn23[13] __attribute__((aligned(1)));
extern uint8_t gm_cn8o84dqbf8[12] __attribute__((aligned(1)));
extern uint8_t gm_cn9bf8d1ktm[16] __attribute__((aligned(8)));
extern uint8_t gm_cnbt0kfagfe[2] __attribute__((aligned(1)));
extern uint8_t gm_cnkg4pj33ln[1] __attribute__((aligned(1)));
extern uint8_t gm_coeovpdckud[2] __attribute__((aligned(1)));
extern uint8_t gm_comi4sk42dt[8] __attribute__((aligned(1)));
extern uint8_t gm_cov7mr7mfr5[10] __attribute__((aligned(1)));
extern uint8_t gm_cp0utnbgndu[16] __attribute__((aligned(8)));
extern uint8_t gm_cphj36bitbq[688] __attribute__((aligned(8)));
extern uint8_t gm_cqacvlglfdh[7] __attribute__((aligned(1)));
extern uint8_t gm_cqq41hlun1j[5] __attribute__((aligned(1)));
extern uint8_t gm_cs5hilqh977[13] __attribute__((aligned(1)));
extern uint8_t gm_csmeedg7pnp[80] __attribute__((aligned(1)));
extern uint8_t gm_ct1fm60hrci[6] __attribute__((aligned(1)));
extern uint8_t gm_ctdm11uuaj6[16] __attribute__((aligned(8)));
extern uint8_t gm_ctpnchchr9h[8] __attribute__((aligned(1)));
extern void *const gm_hl7iqllvekj4[];
extern void *const gm_hlib8i5oaffv[];
extern void *const gm_hlucu919n5n4[];
extern void *const gm_hlvop6ag413a[];
extern uint8_t gm_k0ddgrnig17[40] __attribute__((aligned(8)));
extern uint8_t gm_k1ndla1vg9c[40] __attribute__((aligned(8)));
extern uint8_t gm_k2kjji37v2m[40] __attribute__((aligned(8)));
extern uint8_t gm_k6m9oif038q[40] __attribute__((aligned(8)));
extern uint8_t gm_k9pi0h2o5i2[40] __attribute__((aligned(8)));
extern uint8_t gm_kebji8epnem[40] __attribute__((aligned(8)));
extern uint8_t gm_khdoei7bkbm[40] __attribute__((aligned(8)));
extern uint8_t gm_khikhlemh0i[40] __attribute__((aligned(8)));
extern uint8_t gm_kjqpf95jhcm[40] __attribute__((aligned(8)));
extern uint8_t gm_kmlih2j5ae5[40] __attribute__((aligned(8)));
extern uint8_t gm_kmnip3ksasl[40] __attribute__((aligned(8)));
extern uint8_t gm_koniosmdv9m[40] __attribute__((aligned(8)));
extern uint8_t gm_kovthkfk7ta[40] __attribute__((aligned(8)));
extern uint8_t gm_kplh4t9vgh7[40] __attribute__((aligned(8)));
extern uint8_t gm_kq9hk6r9m7a[40] __attribute__((aligned(8)));
extern uint8_t gm_ktv0eonmebe[40] __attribute__((aligned(8)));
extern uint8_t gm_kv5f4tk0m2l[40] __attribute__((aligned(8)));
extern const uint8_t gm_s000up2l16pib[9];
extern const uint8_t gm_s01ekgjjrfehv[13];
extern const uint8_t gm_s01k0p56k10gk[29];
extern const uint8_t gm_s02aodrl2fpra[12];
extern const uint8_t gm_s07uhsrd798kj[9];
extern const uint8_t gm_s0adus7j7p0g0[41];
extern const uint8_t gm_s0b795193rvs2[7];
extern const uint8_t gm_s0cf8090s33vc[30];
extern const uint8_t gm_s0dhnude2n5fe[8];
extern const uint8_t gm_s0eudsv4chfi5[44];
extern const uint8_t gm_s0hcm1974rbe7[34];
extern const uint8_t gm_s0k04b3r99uqd[10];
extern const uint8_t gm_s0kbj4moi16fu[16];
extern const uint8_t gm_s0kf3vtbitlnb[58];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0m3gh47554ab[6];
extern const uint8_t gm_s0pi5sf493jqq[41];
extern const uint8_t gm_s0s3f0v5p7674[27];
extern const uint8_t gm_s0t5omnh472sf[21];
extern const uint8_t gm_s0ucactj6hj0a[4];
extern const uint8_t gm_s0uoh0jkm28kv[24];
extern const uint8_t gm_s12b39d0rgdtc[56];
extern const uint8_t gm_s13jru0todls1[38];
extern const uint8_t gm_s19069udlvhni[9];
extern const uint8_t gm_s199bfpbr16jo[44];
extern const uint8_t gm_s19d3jvdogm6m[7];
extern const uint8_t gm_s1af5e7vkvjv9[8];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1b80v2hb8d4l[72];
extern const uint8_t gm_s1bp6pdherlbe[41];
extern const uint8_t gm_s1ec711cn6c5f[33];
extern const uint8_t gm_s1eo6mkdbgh2b[38];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1gm12po6spua[18];
extern const uint8_t gm_s1jkh3mpunvd6[64];
extern const uint8_t gm_s1lh34nabv9g2[5];
extern const uint8_t gm_s1m38bo3naic1[8];
extern const uint8_t gm_s1o778dp7d59d[43];
extern const uint8_t gm_s1p0thm43h6d6[21];
extern const uint8_t gm_s1smed2pig5ts[35];
extern const uint8_t gm_s1vcu5tk6vbma[67];
extern const uint8_t gm_s1vlc7kpvelfb[25];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s23t712hkqlmp[5];
extern const uint8_t gm_s2470an4q44lj[25];
extern const uint8_t gm_s24s9g8r9gc0v[7];
extern const uint8_t gm_s25qdppmkh7s6[7];
extern const uint8_t gm_s268n7l3tugli[6];
extern const uint8_t gm_s283bs8hsleb3[9];
extern const uint8_t gm_s29avse796bo5[18];
extern const uint8_t gm_s2bnc7q68giqi[17];
extern const uint8_t gm_s2c5ns5u9qs90[35];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2dog0ijtvmd2[32];
extern const uint8_t gm_s2ef11ime8o01[42];
extern const uint8_t gm_s2kvk8bnjk52t[12];
extern const uint8_t gm_s2m4r2316r14d[11];
extern const uint8_t gm_s2ogeh8fp9da3[12];
extern const uint8_t gm_s2okn0skhqnvu[32];
extern const uint8_t gm_s2rbapbbig1pe[19];
extern const uint8_t gm_s2rk6mdib4mtv[17];
extern const uint8_t gm_s2sfjihvaabi3[9];
extern const uint8_t gm_s2u7k18iev3v2[4];
extern const uint8_t gm_s2umangg4hn1g[15];
extern const uint8_t gm_s31g9ftlb6lgj[11];
extern const uint8_t gm_s32blj9c3tc60[11];
extern const uint8_t gm_s32rhj249n222[10];
extern const uint8_t gm_s33ft5ck7snhl[4];
extern const uint8_t gm_s33h2lce46bnm[9];
extern const uint8_t gm_s38cjdjl7n99k[10];
extern const uint8_t gm_s39jha51ulivf[27];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3hb2q98sfsao[5];
extern const uint8_t gm_s3jflb7gdk32k[32];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3p65h6c1goi8[8];
extern const uint8_t gm_s3plkb3in5251[9];
extern const uint8_t gm_s3pmm119d0kjq[16];
extern const uint8_t gm_s3qu37je1jc8f[46];
extern const uint8_t gm_s3ss0hcg1h7js[7];
extern const uint8_t gm_s3t2d4i4t34ct[74];
extern const uint8_t gm_s3tr540lff6s6[11];
extern const uint8_t gm_s3udvgirbbq3p[41];
extern const uint8_t gm_s41047ga9d9tq[26];
extern const uint8_t gm_s418vrbemh247[49];
extern const uint8_t gm_s41uc8v0dkl4a[15];
extern const uint8_t gm_s463po27vtmnn[15];
extern const uint8_t gm_s46ki8g7j9f6v[6];
extern const uint8_t gm_s47db08p2n3lj[3];
extern const uint8_t gm_s489g6hodd4uq[44];
extern const uint8_t gm_s48hq6ubqo1rd[10];
extern const uint8_t gm_s48vlq6lddb8m[29];
extern const uint8_t gm_s4a980fahtblo[3];
extern const uint8_t gm_s4acc6d5101lp[45];
extern const uint8_t gm_s4dgh055v8im1[43];
extern const uint8_t gm_s4fhghepo9qb6[13];
extern const uint8_t gm_s4g35ls4ma80g[13];
extern const uint8_t gm_s4hf5j4eu72mv[66];
extern const uint8_t gm_s4hv5fsmnuq3r[15];
extern const uint8_t gm_s4lmdtjb9rnah[70];
extern const uint8_t gm_s4m4ttv1faeb1[175];
extern const uint8_t gm_s4nikb38gk9t0[22];
extern const uint8_t gm_s4ok0uoiq1nu9[97];
extern const uint8_t gm_s4pk73sg6h8qk[6];
extern const uint8_t gm_s4r4tmb9bg65q[41];
extern const uint8_t gm_s4tgvb7s2dbuj[20];
extern const uint8_t gm_s4v9747a9b85a[11];
extern const uint8_t gm_s527dqlmmaier[6];
extern const uint8_t gm_s52bclt1m0fbl[76];
extern const uint8_t gm_s55ccd7do7rbs[11];
extern const uint8_t gm_s592b618tctb1[15];
extern const uint8_t gm_s5cbc2v9e254r[37];
extern const uint8_t gm_s5cfou479ljhn[29];
extern const uint8_t gm_s5el4up5fp2qc[45];
extern const uint8_t gm_s5f9u0o3duhns[13];
extern const uint8_t gm_s5gkjub7hejc1[9];
extern const uint8_t gm_s5hqjuhiq2fc2[41];
extern const uint8_t gm_s5rqsv8c71bds[11];
extern const uint8_t gm_s5vakuk6ui4kj[9];
extern const uint8_t gm_s5vb1b1mnli2b[72];
extern const uint8_t gm_s5ve9r6iab8ga[26];
extern const uint8_t gm_s5veo86cvpdae[86];
extern const uint8_t gm_s60adpphft7s5[5];
extern const uint8_t gm_s61i2o7ldibjh[35];
extern const uint8_t gm_s61jd5hctkcmt[38];
extern const uint8_t gm_s63011l92pvfr[40];
extern const uint8_t gm_s63e7t279sbjq[47];
extern const uint8_t gm_s64fvfprf3827[63];
extern const uint8_t gm_s64skgbd4ii1k[10];
extern const uint8_t gm_s6899ib4u9fpt[24];
extern const uint8_t gm_s6cmbp3c9g09j[61];
extern const uint8_t gm_s6eefft9fser8[9];
extern const uint8_t gm_s6g2mpli2mu72[11];
extern const uint8_t gm_s6i74fbmndo4a[15];
extern const uint8_t gm_s6khfdbr5aof9[34];
extern const uint8_t gm_s6mmuahmmssv6[17];
extern const uint8_t gm_s6n3pd0tmqbqc[10];
extern const uint8_t gm_s6nqhdcqlaqpu[44];
extern const uint8_t gm_s6pgabt9io52p[132];
extern const uint8_t gm_s6qf5dtv8rcdo[13];
extern const uint8_t gm_s6rbdi4l8b3ii[18];
extern const uint8_t gm_s6rj805fm39qm[65];
extern const uint8_t gm_s6s4ngfaj87vv[5];
extern const uint8_t gm_s6u49d04cqlg1[17];
extern const uint8_t gm_s6vghnf0vdf2t[51];
extern const uint8_t gm_s70iivik802q4[26];
extern const uint8_t gm_s70j71mp70pdq[14];
extern const uint8_t gm_s76ekntop342o[8];
extern const uint8_t gm_s783a88c5t0ve[67];
extern const uint8_t gm_s78naq2blbmbt[32];
extern const uint8_t gm_s7an5gtrdgen2[14];
extern const uint8_t gm_s7b8bvi5kqaqt[7];
extern const uint8_t gm_s7ed59qo09cfg[41];
extern const uint8_t gm_s7fdjhp9ga5oo[8];
extern const uint8_t gm_s7m3rbk4l87ah[15];
extern const uint8_t gm_s7mfvroqmll1f[3];
extern const uint8_t gm_s7qcnd652ae6d[25];
extern const uint8_t gm_s816n04ro16nc[48];
extern const uint8_t gm_s81csu1l86dmv[52];
extern const uint8_t gm_s81f5igr577he[14];
extern const uint8_t gm_s86kdga2ul6h4[9];
extern const uint8_t gm_s87isqt5t284q[9];
extern const uint8_t gm_s87p55h1isc6m[6];
extern const uint8_t gm_s89gtqa9dnss0[35];
extern const uint8_t gm_s8d2alh114kri[15];
extern const uint8_t gm_s8g92mbmiiva2[48];
extern const uint8_t gm_s8jclq8b6mi27[17];
extern const uint8_t gm_s8l22p9l388io[67];
extern const uint8_t gm_s8l7qb8s3942u[35];
extern const uint8_t gm_s8m12els4res5[26];
extern const uint8_t gm_s8o6lm1cn4eo5[3];
extern const uint8_t gm_s8p032eorpf80[3];
extern const uint8_t gm_s8ps5ka0vnf6d[42];
extern const uint8_t gm_s8qkarceaerkr[22];
extern const uint8_t gm_s8rs2s65rnmku[18];
extern const uint8_t gm_s8u62e7lr2m4s[13];
extern const uint8_t gm_s8vthjuk0el8j[63];
extern const uint8_t gm_s903nhsp8hiug[14];
extern const uint8_t gm_s92n4v7htp6pi[11];
extern const uint8_t gm_s96j6o9smoqdu[8];
extern const uint8_t gm_s9dh9d8gs6a16[9];
extern const uint8_t gm_s9dudft75oi1c[33];
extern const uint8_t gm_s9ehkuquejmat[45];
extern const uint8_t gm_s9eqqrnq18n3l[31];
extern const uint8_t gm_s9g30d9lksra5[3];
extern const uint8_t gm_s9gk1b0ve64mt[10];
extern const uint8_t gm_s9h8lp3gkedth[29];
extern const uint8_t gm_s9i2ld3a42lap[8];
extern const uint8_t gm_s9impgj2b5t1c[50];
extern const uint8_t gm_s9indmpqvpug4[58];
extern const uint8_t gm_s9k65bfq9n6l0[14];
extern const uint8_t gm_s9kou5ia9uri9[6];
extern const uint8_t gm_s9m6kd6gec368[14];
extern const uint8_t gm_s9p1knha37i5h[27];
extern const uint8_t gm_s9p37ubghllhn[64];
extern const uint8_t gm_s9qjpsba9cv0i[10];
extern const uint8_t gm_s9rk6ipadg2pq[111];
extern const uint8_t gm_s9sjpa1f1m1tr[24];
extern const uint8_t gm_s9uf4t24m7c3t[31];
extern const uint8_t gm_sa16a135q4am6[13];
extern const uint8_t gm_sa3k87epai61e[25];
extern const uint8_t gm_sa5lo120id503[26];
extern const uint8_t gm_sa5m6gegug17p[40];
extern const uint8_t gm_sa5s0jools6a2[9];
extern const uint8_t gm_sa6hsburhm4pn[9];
extern const uint8_t gm_sa7q2bqmn8lgb[9];
extern const uint8_t gm_sa8gjfvck9d40[30];
extern const uint8_t gm_sa8k1i3b5phk8[24];
extern const uint8_t gm_sa9e77lq9q04p[16];
extern const uint8_t gm_sadg245agtuao[45];
extern const uint8_t gm_sadj3uigeodiu[29];
extern const uint8_t gm_sadphckf7ra7f[4707];
extern const uint8_t gm_saempncqkf91r[3];
extern const uint8_t gm_sagd3osht35og[13];
extern const uint8_t gm_sahd19pngn4vn[14];
extern const uint8_t gm_sahim4484f9l7[3];
extern const uint8_t gm_sak3h3rlsvn4o[10];
extern const uint8_t gm_salbpgh5m2h5m[15];
extern const uint8_t gm_salpeh0v5rqdf[8];
extern const uint8_t gm_sao4mumvadoct[4];
extern const uint8_t gm_saovklm35s57t[184];
extern const uint8_t gm_saq8ri2pmi7o5[19];
extern const uint8_t gm_sasea0sobfkrs[8];
extern const uint8_t gm_sat505mlqphna[26];
extern const uint8_t gm_sauu2s1pbjqsh[11];
extern const uint8_t gm_sb21gep34rmm9[36];
extern const uint8_t gm_sb3sf0ef436fi[40];
extern const uint8_t gm_sb5b70djeqhg8[16];
extern const uint8_t gm_sb5h01ig0b9oe[34];
extern const uint8_t gm_sb641udihu5hs[10];
extern const uint8_t gm_sb8b1gf0gtt49[43];
extern const uint8_t gm_sb8qu1qqed6pt[85];
extern const uint8_t gm_sbbgtkch3q4n5[3];
extern const uint8_t gm_sbdfikdtgvq9q[28];
extern const uint8_t gm_sbht6tj2u5vfl[8];
extern const uint8_t gm_sbjshone12eeu[158];
extern const uint8_t gm_sbjuojbqo51av[36];
extern const uint8_t gm_sbkd9ldfr86ri[5];
extern const uint8_t gm_sbl7p8tv4j986[17];
extern const uint8_t gm_sblomia8fb3mu[20];
extern const uint8_t gm_sbm0k1981kgpb[40];
extern const uint8_t gm_sbm19bhbgtvrj[129];
extern const uint8_t gm_sbmtphtelgust[29];
extern const uint8_t gm_sbo8mn3eri83c[23];
extern const uint8_t gm_sbqoiitqtfv75[11];
extern const uint8_t gm_sbssn254n4fs2[25];
extern const uint8_t gm_sbtq9nj91tefv[10];
extern const uint8_t gm_sbvi33ihvufj1[20];
extern const uint8_t gm_sbvo1o52epaqq[4];
extern const uint8_t gm_sc103i8mmkjaq[41];
extern const uint8_t gm_sc1nadatuanhc[5];
extern const uint8_t gm_sc46nvblu52f3[18];
extern const uint8_t gm_sc4t0u3ku1fbu[23];
extern const uint8_t gm_sc559v8etqgda[19];
extern const uint8_t gm_sc6pb1tb5tnnv[34];
extern const uint8_t gm_scai7a7ojvpjh[19];
extern const uint8_t gm_scavc1uf2hs8l[7];
extern const uint8_t gm_scc5n7285iivu[13];
extern const uint8_t gm_sccadmt684uri[23];
extern const uint8_t gm_sccpc5t5dl8ni[51];
extern const uint8_t gm_scd1bb5dijnis[15];
extern const uint8_t gm_sce79e7st00ad[4];
extern const uint8_t gm_scgm14ju6d0p8[15];
extern const uint8_t gm_scgvr0isof71e[9];
extern const uint8_t gm_scj5qlamj2bll[39];
extern const uint8_t gm_scllp0lpbljde[15];
extern const uint8_t gm_scobqq9qv6nnk[6];
extern const uint8_t gm_scotq6054g01h[6];
extern const uint8_t gm_scp8gj1kjauec[29];
extern const uint8_t gm_scq06n33arhgv[5];
extern const uint8_t gm_scqqkvgm1j3cs[65];
extern const uint8_t gm_scrnhm4ha22vj[18];
extern const uint8_t gm_scs1evojsik8q[4];
extern const uint8_t gm_scs3asa1395s7[6];
extern const uint8_t gm_scvsibgrrt3sc[13];
extern const uint8_t gm_sd2a8ckkfb013[26];
extern const uint8_t gm_sd2iiq8cuaks3[5];
extern const uint8_t gm_sd5g4r2t5gcbk[25];
extern const uint8_t gm_sd5giovmg2s7p[17];
extern const uint8_t gm_sdak0upmjiivc[15];
extern const uint8_t gm_sdak257j1qgl8[6];
extern const uint8_t gm_sdbgs2fic3oib[11];
extern const uint8_t gm_sdbtbup89cn84[19];
extern const uint8_t gm_sdcf6v5v0oma5[6];
extern const uint8_t gm_sddniufplfkku[66];
extern const uint8_t gm_sdealia24icjj[40];
extern const uint8_t gm_sdg7s5c89ve6u[64];
extern const uint8_t gm_sdhddtlu453l5[13];
extern const uint8_t gm_sdl70rjr9en4m[16];
extern const uint8_t gm_sdle4vcf3dhti[3];
extern const uint8_t gm_sdm98dp52j8ep[16];
extern const uint8_t gm_sdnh9sm5pppq1[40];
extern const uint8_t gm_sdprvvqv16trj[61];
extern const uint8_t gm_sdq66cp51pg1r[11];
extern const uint8_t gm_sdubr1na84odv[12];
extern const uint8_t gm_se09jntm6upln[4];
extern const uint8_t gm_se0oi28sapu01[11];
extern const uint8_t gm_se5r6p8tqdo82[31];
extern const uint8_t gm_se7oqjo2sonp9[9];
extern const uint8_t gm_seajfhgtfmhv7[12];
extern const uint8_t gm_seak1jbr9qmd1[9];
extern const uint8_t gm_sebe0816umkgp[54];
extern const uint8_t gm_sebk4v8jac58q[43];
extern const uint8_t gm_seflbetisd1ui[3];
extern const uint8_t gm_seg7e1ca8e64l[5];
extern const uint8_t gm_segm7or15hs5h[20];
extern const uint8_t gm_sei6ib477d1ds[4];
extern const uint8_t gm_sekdm5lqg7176[43];
extern const uint8_t gm_sekmurq2ua2sf[6];
extern const uint8_t gm_sennl2fjkcbdb[4];
extern const uint8_t gm_senuk3dngdds0[17];
extern const uint8_t gm_sepq106jdsjea[11];
extern const uint8_t gm_seqap6hh2h8p5[5];
extern const uint8_t gm_seqji52qmi3ea[4];
extern const uint8_t gm_sequ7jfvtub5t[10];
extern const uint8_t gm_serasf77ce9g3[52];
extern const uint8_t gm_serke084a3lbj[41];
extern const uint8_t gm_sf51sbqtper4f[74];
extern const uint8_t gm_sf9b70aed3bu1[21];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfil4j1onqa90[14];
extern const uint8_t gm_sfinup9etdjip[4];
extern const uint8_t gm_sfiqishktrbg2[27];
extern const uint8_t gm_sfjrpo4i6b0at[24];
extern const uint8_t gm_sfknvqdnjah7l[18];
extern const uint8_t gm_sfm8ps2e26dh1[3];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfnq22hc2kms9[14];
extern const uint8_t gm_sfpdckbabsquc[18];
extern const uint8_t gm_sfpnied4dp46h[15];
extern const uint8_t gm_sfrdbcc53tuv9[18];
extern const uint8_t gm_sfs5rrntfu3mi[25];
extern const uint8_t gm_sft51gvrcleut[16];
extern const uint8_t gm_sfu0in59pituf[62];
extern const uint8_t gm_sfurr787jvkcg[6];
extern const uint8_t gm_sfusuld9q5ekv[44];
extern const uint8_t gm_sg0tidc1tpl0e[33];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg8a1r64kvpgo[3];
extern const uint8_t gm_sgc7crh43q9tj[6];
extern const uint8_t gm_sgd9tn7p5ihgk[10];
extern const uint8_t gm_sge2cgjaklnf2[5];
extern const uint8_t gm_sge6a56sqn6kj[21];
extern const uint8_t gm_sgfqei77bsia8[4];
extern const uint8_t gm_sggm36eg9tkga[10];
extern const uint8_t gm_sgjkgsqdq34nu[37];
extern const uint8_t gm_sgli4lbivdt1d[38];
extern const uint8_t gm_sgls4l5hb5j8p[75];
extern const uint8_t gm_sgo00f0mafbnd[4];
extern const uint8_t gm_sgovtffcl93pk[15];
extern const uint8_t gm_sgp3f4mk5jmsl[41];
extern const uint8_t gm_sgq1v4nk059uv[10];
extern const uint8_t gm_sgq6hvrcda4sp[37];
extern const uint8_t gm_sgs7af5k634cn[11];
extern const uint8_t gm_sgu3j1te831tl[24];
extern const uint8_t gm_sguhoqhco0rfk[9];
extern const uint8_t gm_sh1emun6fmppt[4];
extern const uint8_t gm_sh1scn6dg8mpr[14];
extern const uint8_t gm_sh5lbh5j6oqpf[36];
extern const uint8_t gm_sh75rh8dk51gf[12];
extern const uint8_t gm_sh8houjk34evm[15];
extern const uint8_t gm_sh8lvmk1ev2kd[16];
extern const uint8_t gm_shdnhcpvptc1q[5];
extern const uint8_t gm_shf8e0v8f47ml[17];
extern const uint8_t gm_shg1pn1r3sjgi[13];
extern const uint8_t gm_shgjlb2nrn5fk[22];
extern const uint8_t gm_shhrbleiv8o3j[14];
extern const uint8_t gm_shjpff93a0f25[7];
extern const uint8_t gm_shmgu63rvoce3[36];
extern const uint8_t gm_shp2btrg6e14g[29];
extern const uint8_t gm_shpgcu8us1vm6[11];
extern const uint8_t gm_shq9t6lutffbf[4];
extern const uint8_t gm_shrelr880pj0m[7];
extern const uint8_t gm_sht1158338t3d[11];
extern const uint8_t gm_shtrdthev5fjn[14];
extern const uint8_t gm_shttb5kqse295[12];
extern const uint8_t gm_shu6vhibjovbk[18];
extern const uint8_t gm_si68mvbt2sjam[19];
extern const uint8_t gm_si698b9virvoh[7];
extern const uint8_t gm_siaeuo6l48ehh[15];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sic6g350stk7h[21];
extern const uint8_t gm_siddm9qkt382a[15];
extern const uint8_t gm_sie82oc0ua5dm[3];
extern const uint8_t gm_sifqqc9n3eem2[6];
extern const uint8_t gm_sihb07k8fk2nc[35];
extern const uint8_t gm_sihlhf2982kip[12];
extern const uint8_t gm_sii9tamh3vah6[32];
extern const uint8_t gm_sil41urphp1ol[12];
extern const uint8_t gm_sio8t6i3tf974[11];
extern const uint8_t gm_sipfpcphbcub1[34];
extern const uint8_t gm_sipi0b0c6ms62[6];
extern const uint8_t gm_sirn08c749sdd[28];
extern const uint8_t gm_sirpcjgkmlehr[8];
extern const uint8_t gm_sisa53plcfedi[14];
extern const uint8_t gm_sisd1n433forg[52];
extern const uint8_t gm_sismsc98l6u4e[4];
extern const uint8_t gm_sisn94tbdoeof[65];
extern const uint8_t gm_sium1s93t7o1a[19];
extern const uint8_t gm_sivsapeh482sm[8];
extern const uint8_t gm_sj0e9fo4a625j[80];
extern const uint8_t gm_sj0ur1o7a5584[16];
extern const uint8_t gm_sj3lfuqd4f1np[37];
extern const uint8_t gm_sj62oio2kg58e[20];
extern const uint8_t gm_sj718thiqudru[10];
extern const uint8_t gm_sj83d90uvga1k[8];
extern const uint8_t gm_sj97id02v9fh3[6];
extern const uint8_t gm_sj9co2vp4uudk[13];
extern const uint8_t gm_sjcdr1cibbeq6[64];
extern const uint8_t gm_sjcocjnsa7u7g[85];
extern const uint8_t gm_sjdk7e5fs4top[6];
extern const uint8_t gm_sjds8o3l7mmnl[5];
extern const uint8_t gm_sjf1r2hmheh87[29];
extern const uint8_t gm_sjkbtepn8bqeb[24];
extern const uint8_t gm_sjmhvdjct2lq7[19];
extern const uint8_t gm_sjpml17j24nb4[64];
extern const uint8_t gm_sjrtlfuv3os00[75];
extern const uint8_t gm_sjsv6e8j9nikr[7];
extern const uint8_t gm_sjt1hcgrgj8be[36];
extern const uint8_t gm_sjvb17co9jlff[15];
extern const uint8_t gm_sk36cj5av4kuu[17];
extern const uint8_t gm_sk5vbnunk2sec[16];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_sk7bri4eh56ps[17];
extern const uint8_t gm_sk83con0mdsfe[6];
extern const uint8_t gm_sk8hc0njn930q[43];
extern const uint8_t gm_sk9at358cmauo[27];
extern const uint8_t gm_ska00mcp5pgpq[53];
extern const uint8_t gm_skfdo9qggtrbp[6];
extern const uint8_t gm_skfks8nrdfdgv[5];
extern const uint8_t gm_skfo9c0nr3nrd[50];
extern const uint8_t gm_skgulapvvrhkp[36];
extern const uint8_t gm_skjfb4faajq73[13];
extern const uint8_t gm_skkog7g7r3edb[8];
extern const uint8_t gm_skl1tisbdbp2m[13];
extern const uint8_t gm_skme3gpq5egsp[40];
extern const uint8_t gm_skq9qlj43eei8[41];
extern const uint8_t gm_skr6p9lfc5ftp[11];
extern const uint8_t gm_skrdd0u45d86g[17];
extern const uint8_t gm_sksgae11pvk6p[2];
extern const uint8_t gm_skt7g4v99o4j0[42];
extern const uint8_t gm_skusvrbuvvp37[13];
extern const uint8_t gm_sl1f8ibfoc1i1[13];
extern const uint8_t gm_sl1kcdemlk6n0[10];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl4ll6nrpr5rj[33];
extern const uint8_t gm_sl628i1qguhss[13];
extern const uint8_t gm_sl7pe4skltkbu[9];
extern const uint8_t gm_sl80cge5ts0aj[14];
extern const uint8_t gm_sl815v6oegv7n[14];
extern const uint8_t gm_sl935l3jslu0o[28];
extern const uint8_t gm_slc3rjglch387[50];
extern const uint8_t gm_slfddg7c1u1oc[62];
extern const uint8_t gm_slfinpjh0406b[47];
extern const uint8_t gm_slfk2il1skugc[13];
extern const uint8_t gm_slg6o1a142s3f[44];
extern const uint8_t gm_slgiebdce3332[8];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slgu49nv3o8e7[12];
extern const uint8_t gm_sli291jkgotin[31];
extern const uint8_t gm_sljdair45oqft[31];
extern const uint8_t gm_slm25hfg2udo4[8];
extern const uint8_t gm_slmeqnp27emg7[9];
extern const uint8_t gm_slmnp81e6ev88[10];
extern const uint8_t gm_sln5qir3tgi7j[10];
extern const uint8_t gm_slnqcu8l15gll[12];
extern const uint8_t gm_slpi6dabivg2e[29];
extern const uint8_t gm_sluvntf7gk99f[3];
extern const uint8_t gm_sm0f8bs4gd2ma[18];
extern const uint8_t gm_sm34bm8ke9n7b[22];
extern const uint8_t gm_sm5ffrmin8340[10];
extern const uint8_t gm_sm6gmoq645v8o[70];
extern const uint8_t gm_sm732n1u78vvc[16];
extern const uint8_t gm_sm7cboss659pv[51];
extern const uint8_t gm_sm922ui8k0oca[9];
extern const uint8_t gm_smaq5hrff6a5d[56];
extern const uint8_t gm_smbfsgie9fgg4[4];
extern const uint8_t gm_smf8ll9cm8fgf[26];
extern const uint8_t gm_smh12ad93nju6[45];
extern const uint8_t gm_smhve5pq35drr[9];
extern const uint8_t gm_smi31ifpstnmf[9];
extern const uint8_t gm_smiaoqkobhu81[54];
extern const uint8_t gm_smlcnjfj6t3mc[6];
extern const uint8_t gm_smnkiec8dekcp[8];
extern const uint8_t gm_smoi1nh7b1b6o[65];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smp6ue9h98hbc[31];
extern const uint8_t gm_smpb9qb8m3meo[7];
extern const uint8_t gm_smrc1u3r4i5hd[7];
extern const uint8_t gm_smsmo0f5ao530[10];
extern const uint8_t gm_sn2e6t65fstoj[19];
extern const uint8_t gm_sn6vmked7l7pf[7];
extern const uint8_t gm_sn8d077r4fgpq[26];
extern const uint8_t gm_snaq7hvenqsum[4];
extern const uint8_t gm_snb1pvju20aqn[7];
extern const uint8_t gm_snbque26o7lbi[7];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snf877edekrff[11];
extern const uint8_t gm_snf9ak4s55usv[18];
extern const uint8_t gm_snfsrvfr6vnkt[8];
extern const uint8_t gm_sngckqdbegtlc[26];
extern const uint8_t gm_snh0sl2cdagdn[13];
extern const uint8_t gm_snhamhc7ig6ce[25];
extern const uint8_t gm_snhkhrbd8tjtf[45];
extern const uint8_t gm_snja4gioias3h[19];
extern const uint8_t gm_snku1611u1u7p[9];
extern const uint8_t gm_snm19vfbec4q3[19];
extern const uint8_t gm_snmrruc88a739[10];
extern const uint8_t gm_snpp5o7gj4ij7[10];
extern const uint8_t gm_snqbh8eqcn135[17];
extern const uint8_t gm_snr5o5tak6405[4];
extern const uint8_t gm_snritti8butc6[41];
extern const uint8_t gm_snrk30ug2qmu2[8];
extern const uint8_t gm_sns1hl83hdlcn[5];
extern const uint8_t gm_snsqui8o89u28[15];
extern const uint8_t gm_snstimc3n04u0[12];
extern const uint8_t gm_snuadcbi5e786[15];
extern const uint8_t gm_snukl6jqep6jf[4];
extern const uint8_t gm_snvodad5t43r7[11];
extern const uint8_t gm_so1m4qmhi2fvm[93];
extern const uint8_t gm_so3bl4q7kp6gl[21];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so8g4o8hee3cb[7];
extern const uint8_t gm_so91sclkgrupd[33];
extern const uint8_t gm_so94vinsu4uib[50];
extern const uint8_t gm_so9fi6r28770a[3];
extern const uint8_t gm_sob1n7uh7kpps[14];
extern const uint8_t gm_sodg3e7aummvt[8];
extern const uint8_t gm_soeo46rfuspd5[32];
extern const uint8_t gm_soi6iv1hnej40[13];
extern const uint8_t gm_solguqm0afvc4[44];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_sor8flejv9lfq[67];
extern const uint8_t gm_sorb6gi9d49ic[9];
extern const uint8_t gm_souep3pirutur[55];
extern const uint8_t gm_sovlckpk2gctr[16];
extern const uint8_t gm_sp1784q48t8s2[4];
extern const uint8_t gm_sp2a19d61vbe0[4];
extern const uint8_t gm_sp4skfj2bn2ut[7];
extern const uint8_t gm_sp4uhnkunj2cg[3];
extern const uint8_t gm_sp8h6qkr4b6hg[12];
extern const uint8_t gm_spbhhmdbp335u[4];
extern const uint8_t gm_spbv5h8c0ukao[5];
extern const uint8_t gm_spffvcegd6os6[6];
extern const uint8_t gm_spfleivdeduud[4];
extern const uint8_t gm_spm9ae4g2b2fd[14];
extern const uint8_t gm_spni6j7splqto[8];
extern const uint8_t gm_spnr89edksdqp[4];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sppcsr92ukfol[64];
extern const uint8_t gm_sppeomacjvkpm[7];
extern const uint8_t gm_spqlqjii789ln[57];
extern const uint8_t gm_sprj2pveuq1p8[13];
extern const uint8_t gm_spsbmlrti644m[11];
extern const uint8_t gm_sptnm8c6c52dr[7];
extern const uint8_t gm_spts1f8sg22km[18];
extern const uint8_t gm_sq05ai6ts8qlk[17];
extern const uint8_t gm_sq1mn6f7h111r[3];
extern const uint8_t gm_sq3r7vbcbs4ll[26];
extern const uint8_t gm_sq4jcg2uujqmj[16];
extern const uint8_t gm_sq7c24l9r5rti[118];
extern const uint8_t gm_sq89uknu8mulu[8];
extern const uint8_t gm_sq95m8isfa7vd[2];
extern const uint8_t gm_sq9jk07jm2tgi[10];
extern const uint8_t gm_sq9org4qoda29[12];
extern const uint8_t gm_sqfu5kl4vat80[9];
extern const uint8_t gm_sqin9fcebg38h[7];
extern const uint8_t gm_sqjh1si5sbfvd[41];
extern const uint8_t gm_sqkcsn1qihjbl[41];
extern const uint8_t gm_sqn4031c223rk[13];
extern const uint8_t gm_sqrh6i8djv6b4[7];
extern const uint8_t gm_sqsb0jek355o9[17];
extern const uint8_t gm_sqssndasqkc4l[17];
extern const uint8_t gm_squr90ahaf9ht[13];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sqviis18fpvj7[5];
extern const uint8_t gm_sr00p3fdg5qn3[19];
extern const uint8_t gm_sr1lb1m0kmmrh[46];
extern const uint8_t gm_sr26j6lp0gfqo[11];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr6b2mblh2jam[223];
extern const uint8_t gm_sr8f1b5ta7294[9];
extern const char gm_src0s0aq2lhn254[];
extern const char gm_src2n2ugogq2cq2[];
extern const char gm_src361hl447ggcv[];
extern const char gm_src3a4t4bi1ba85[];
extern const char gm_src3s76fqo1a9jk[];
extern const char gm_src41vqrhgekac9[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srci09ctgf0djoe[];
extern const char gm_srciagdq75um1d5[];
extern const char gm_srcl43af23eher3[];
extern const char gm_srcn6q5fuum979p[];
extern const char gm_srco9vbah234t6e[];
extern const char gm_srcqp133sovb148[];
extern const char gm_srcrorgjlg51brd[];
extern const char gm_srcush70170kjg3[];
extern const uint8_t gm_srd5daqbo6ilc[58];
extern const uint8_t gm_sre51cnt7pim3[35];
extern const uint8_t gm_srehvgd1sod2m[18];
extern const uint8_t gm_srfobl8913gvl[3];
extern const uint8_t gm_sri78umnjbra8[11];
extern const uint8_t gm_srjb0lv60gms5[62];
extern const uint8_t gm_srjkl5qvdutgq[5];
extern const uint8_t gm_srkbv7g2spehb[33];
extern const uint8_t gm_srkol0piugptj[14];
extern const uint8_t gm_srltdqer7h3t7[55];
extern const uint8_t gm_srok3f5iht8q8[23];
extern const uint8_t gm_srqehfttj6ngk[4];
extern const uint8_t gm_sruk98mc5gf6c[44];
extern const uint8_t gm_srv4cua6q51nq[5];
extern const uint8_t gm_srv4dcfccuset[3];
extern const uint8_t gm_ss3kdmf5g7qmv[82];
extern const uint8_t gm_ss558hlhv1kah[31];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_ss8vfi8kj31rc[6];
extern const uint8_t gm_ss9d6rvgfqq53[13];
extern const uint8_t gm_ss9rpos7stctm[34];
extern const uint8_t gm_ssb6m4k22290v[17];
extern const uint8_t gm_ssbqeok4607ju[25];
extern const uint8_t gm_sscmtmpbttmhb[93];
extern const uint8_t gm_sseb1k7dlt7ge[67];
extern const uint8_t gm_ssffver18si54[25];
extern const uint8_t gm_ssift49n290vj[5];
extern const uint8_t gm_ssjk8k45afgjb[11];
extern const uint8_t gm_ssmuvuejgvj6b[27];
extern const uint8_t gm_ssnbkpbck3vbo[22];
extern const uint8_t gm_ssp208k4afn15[23];
extern const uint8_t gm_ssqifgr2icd6s[3];
extern const uint8_t gm_ssud7c614kqjt[5];
extern const uint8_t gm_st2vlvkf674c2[250];
extern const uint8_t gm_st35tcurhvabs[31];
extern const uint8_t gm_st4gqcsm35nph[90];
extern const uint8_t gm_st52k2850n5r5[5];
extern const uint8_t gm_st5ngsmk07040[8];
extern const uint8_t gm_st8h6qvqu7ddb[30];
extern const uint8_t gm_st9djk4oc14um[67];
extern const uint8_t gm_st9gkvtvi92qe[17];
extern const uint8_t gm_staulpjprqbul[155];
extern const uint8_t gm_stbno5snno6c5[8];
extern const uint8_t gm_stbrououb87po[51];
extern const uint8_t gm_stfd80s46313k[38];
extern const uint8_t gm_stgra1esdnhlk[12];
extern const uint8_t gm_stjq3hbrk4g4r[4];
extern const uint8_t gm_stk9qd6rl9jdj[21];
extern const uint8_t gm_stkm2m8f3okk8[18];
extern const uint8_t gm_stl4dqpj6esk9[14];
extern const uint8_t gm_stlamkovo4l42[4];
extern const uint8_t gm_stna0ubmss8p6[48];
extern const uint8_t gm_stocunr1upos5[9];
extern const uint8_t gm_sts2e9lggtpr2[47];
extern const uint8_t gm_stshdb5m6rl63[5];
extern const uint8_t gm_stt50isac07pf[46];
extern const uint8_t gm_su0gvqh5vvvlj[37];
extern const uint8_t gm_su0l53i30raa8[6];
extern const uint8_t gm_su28d8kir9fan[10];
extern const uint8_t gm_su4j8ek23cevi[51];
extern const uint8_t gm_su6ipjjrnjs7p[10];
extern const uint8_t gm_su6oav9vpqcnu[17];
extern const uint8_t gm_su71itml70ikf[5];
extern const uint8_t gm_suafpipqeom8q[7];
extern const uint8_t gm_suc2pvoh9ktdi[6];
extern const uint8_t gm_sud7vj6tst3lp[99];
extern const uint8_t gm_sudpbkssneeek[11];
extern const uint8_t gm_sueo2nge7h013[14];
extern const uint8_t gm_suevj69t71tjc[35];
extern const uint8_t gm_sufoqrq4onvgh[16];
extern const uint8_t gm_suftl0kath3th[21];
extern const uint8_t gm_suhl0206dfbdm[5];
extern const uint8_t gm_suiq4ou0s86ho[14];
extern const uint8_t gm_suj1i29321vmu[10];
extern const uint8_t gm_suod5ocif7o5v[5];
extern const uint8_t gm_suqchf5vh2hqp[5];
extern const uint8_t gm_sv0fbf330lmcd[8];
extern const uint8_t gm_sv1f0l9ks3lvu[25];
extern const uint8_t gm_svbrm62b64ga9[11];
extern const uint8_t gm_svdi2pk0uienp[45];
extern const uint8_t gm_sveol5dvm5sd7[20];
extern const uint8_t gm_svgaurqm99100[48];
extern const uint8_t gm_sviu03g0f5s30[43];
extern const uint8_t gm_svk3u470blc4u[9];
extern const uint8_t gm_svldd4tfe8pf3[41];
extern const uint8_t gm_svo0v47tsnb8b[14];
extern const uint8_t gm_svp314e9ficjs[14];
extern const uint8_t gm_svshci6b9lbnq[9];
extern const uint8_t gm_svth7kg659g2i[13];
extern const uint8_t gm_svtkcim3vundp[18];
extern const uint8_t gm_svv63hse0pejb[18];
extern const gm_tmpl_piece gm_tpl01fkj9ukhaa3_p[];
extern const gm_spec *const gm_tpl02lckpq8ae01_s[];
extern const gm_spec *const gm_tpl04hr2npsr626_s[];
extern const gm_spec *const gm_tpl06vq09a1bq44_s[];
extern const gm_tmpl_piece gm_tpl07e4gjk0ub6d_p[];
extern const gm_tmpl_piece gm_tpl09kglfpefe8i_p[];
extern const uint32_t gm_tpl0e4rd6ag9tqa_i[];
extern const uint32_t gm_tpl0elencma14kh_i[];
extern const gm_spec *const gm_tpl0eln6evo1vf9_s[];
extern const gm_spec *const gm_tpl0lggla435q5r_s[];
extern const uint32_t gm_tpl0scna75sco5f_i[];
extern const uint32_t gm_tpl0stjtt2gdn50_i[];
extern const gm_spec *const gm_tpl0uott3s3afvp_s[];
extern const uint32_t gm_tpl10tenob4hk5k_i[];
extern const gm_tmpl_piece gm_tpl10tenob4hk5k_p[];
extern const uint32_t gm_tpl128vde47jcsu_i[];
extern const gm_tmpl gm_tpl12e8u6hvkejb;
extern const uint32_t gm_tpl12e8u6hvkejb_i[];
extern const gm_tmpl_piece gm_tpl12e8u6hvkejb_p[];
extern const gm_spec *const gm_tpl12e8u6hvkejb_s[];
extern const gm_tmpl gm_tpl13c887f2o944;
extern const uint32_t gm_tpl13c887f2o944_i[];
extern const gm_tmpl_piece gm_tpl13c887f2o944_p[];
extern const gm_spec *const gm_tpl13c887f2o944_s[];
extern const gm_tmpl gm_tpl149k5asebpko;
extern const uint32_t gm_tpl149k5asebpko_i[];
extern const gm_tmpl_piece gm_tpl149k5asebpko_p[];
extern const gm_spec *const gm_tpl149k5asebpko_s[];
extern const gm_spec *const gm_tpl14n6ndgi62j1_s[];
extern const gm_spec *const gm_tpl150n44984e0j_s[];
extern const gm_tmpl_piece gm_tpl16gsv91msflc_p[];
extern const gm_tmpl gm_tpl17l9mok2be4a;
extern const uint32_t gm_tpl17l9mok2be4a_i[];
extern const gm_tmpl_piece gm_tpl17l9mok2be4a_p[];
extern const gm_spec *const gm_tpl17l9mok2be4a_s[];
extern const gm_tmpl gm_tpl18roma1gvhpk;
extern const uint32_t gm_tpl18roma1gvhpk_i[];
extern const gm_tmpl_piece gm_tpl18roma1gvhpk_p[];
extern const gm_spec *const gm_tpl18roma1gvhpk_s[];
extern const gm_tmpl_piece gm_tpl1a3usdkusvtq_p[];
extern const uint32_t gm_tpl1blafg62e77e_i[];
extern const uint32_t gm_tpl1buo4rn7srlb_i[];
extern const gm_spec *const gm_tpl1f5t4gadpj5n_s[];
extern const gm_tmpl gm_tpl1f7c1o805fpf;
extern const uint32_t gm_tpl1f7c1o805fpf_i[];
extern const gm_tmpl_piece gm_tpl1f7c1o805fpf_p[];
extern const gm_spec *const gm_tpl1f7c1o805fpf_s[];
extern const uint32_t gm_tpl1hek9uhvdtmo_i[];
extern const gm_spec *const gm_tpl1jgrbdjt2sm3_s[];
extern const uint32_t gm_tpl1nqa0en23gvp_i[];
extern const gm_tmpl_piece gm_tpl1q6o13b940dg_p[];
extern const gm_tmpl gm_tpl1sdu3l536hkg;
extern const uint32_t gm_tpl1sdu3l536hkg_i[];
extern const gm_tmpl_piece gm_tpl1sdu3l536hkg_p[];
extern const gm_spec *const gm_tpl1sdu3l536hkg_s[];
extern const gm_tmpl_piece gm_tpl1umecooe9oti_p[];
extern const gm_tmpl gm_tpl29pahnav2feq;
extern const uint32_t gm_tpl29pahnav2feq_i[];
extern const gm_tmpl_piece gm_tpl29pahnav2feq_p[];
extern const gm_spec *const gm_tpl29pahnav2feq_s[];
extern const uint32_t gm_tpl2a8tmpvk11vt_i[];
extern const gm_spec *const gm_tpl2d8hdhnr24pd_s[];
extern const gm_spec *const gm_tpl2jsq89buriv9_s[];
extern const gm_spec *const gm_tpl2lqr5lnbpsvm_s[];
extern const uint32_t gm_tpl2od99j0nh2ah_i[];
extern const gm_spec *const gm_tpl2od99j0nh2ah_s[];
extern const gm_tmpl gm_tpl2qrjfa8j0fl5;
extern const uint32_t gm_tpl2qrjfa8j0fl5_i[];
extern const gm_tmpl_piece gm_tpl2qrjfa8j0fl5_p[];
extern const gm_spec *const gm_tpl2qrjfa8j0fl5_s[];
extern const gm_tmpl_piece gm_tpl2r84u6vnc58k_p[];
extern const gm_spec *const gm_tpl2r84u6vnc58k_s[];
extern const gm_tmpl gm_tpl324tc5bgmdkb;
extern const uint32_t gm_tpl324tc5bgmdkb_i[];
extern const gm_tmpl_piece gm_tpl324tc5bgmdkb_p[];
extern const gm_spec *const gm_tpl324tc5bgmdkb_s[];
extern const uint32_t gm_tpl32o7ct8vibbi_i[];
extern const gm_tmpl_piece gm_tpl33em4maossan_p[];
extern const gm_tmpl gm_tpl34g57qfcenfh;
extern const uint32_t gm_tpl34g57qfcenfh_i[];
extern const gm_tmpl_piece gm_tpl34g57qfcenfh_p[];
extern const gm_spec *const gm_tpl34g57qfcenfh_s[];
extern const uint32_t gm_tpl34tavcs9635o_i[];
extern const gm_tmpl_piece gm_tpl34tavcs9635o_p[];
extern const gm_tmpl_piece gm_tpl386j11hdkrib_p[];
extern const gm_tmpl gm_tpl3bnq2anocte9;
extern const uint32_t gm_tpl3bnq2anocte9_i[];
extern const gm_tmpl_piece gm_tpl3bnq2anocte9_p[];
extern const gm_spec *const gm_tpl3bnq2anocte9_s[];
extern const uint32_t gm_tpl3cldid2v21cr_i[];
extern const gm_spec *const gm_tpl3dghmndqn708_s[];
extern const gm_tmpl_piece gm_tpl3ergg6mce23h_p[];
extern const uint32_t gm_tpl3jlrgt0cfgj3_i[];
extern const gm_tmpl gm_tpl3melp777ubs8;
extern const uint32_t gm_tpl3melp777ubs8_i[];
extern const gm_tmpl_piece gm_tpl3melp777ubs8_p[];
extern const gm_spec *const gm_tpl3melp777ubs8_s[];
extern const gm_tmpl_piece gm_tpl3pvc6fn0aqps_p[];
extern const uint32_t gm_tpl3qt6ss8l9cgf_i[];
extern const gm_tmpl_piece gm_tpl43224j8old0s_p[];
extern const gm_tmpl_piece gm_tpl461nvut9qmgp_p[];
extern const uint32_t gm_tpl46u099ch4aa2_i[];
extern const gm_tmpl_piece gm_tpl46u099ch4aa2_p[];
extern const gm_tmpl_piece gm_tpl499l2fj2qs8e_p[];
extern const gm_spec *const gm_tpl499l2fj2qs8e_s[];
extern const gm_tmpl gm_tpl4bj6lgqq92ev;
extern const uint32_t gm_tpl4bj6lgqq92ev_i[];
extern const gm_tmpl_piece gm_tpl4bj6lgqq92ev_p[];
extern const gm_spec *const gm_tpl4bj6lgqq92ev_s[];
extern const gm_tmpl_piece gm_tpl4clb07vpsm45_p[];
extern const gm_tmpl gm_tpl4esqdh06rpvt;
extern const uint32_t gm_tpl4esqdh06rpvt_i[];
extern const gm_tmpl_piece gm_tpl4esqdh06rpvt_p[];
extern const gm_spec *const gm_tpl4esqdh06rpvt_s[];
extern const gm_tmpl_piece gm_tpl4ht4dp62es1n_p[];
extern const gm_tmpl_piece gm_tpl4iu2nfn1okgb_p[];
extern const uint32_t gm_tpl4j60geuv9mvj_i[];
extern const gm_spec *const gm_tpl4lt7abhi3ej4_s[];
extern const uint32_t gm_tpl4sdckuvlf4dl_i[];
extern const uint32_t gm_tpl500h9j75csqa_i[];
extern const gm_spec *const gm_tpl5536lp5da6eq_s[];
extern const gm_spec *const gm_tpl56vmcuv96cg9_s[];
extern const gm_tmpl gm_tpl570sljg0ivt5;
extern const uint32_t gm_tpl570sljg0ivt5_i[];
extern const gm_tmpl_piece gm_tpl570sljg0ivt5_p[];
extern const gm_spec *const gm_tpl570sljg0ivt5_s[];
extern const gm_tmpl gm_tpl58k6ai39p40h;
extern const uint32_t gm_tpl58k6ai39p40h_i[];
extern const gm_tmpl_piece gm_tpl58k6ai39p40h_p[];
extern const gm_spec *const gm_tpl58k6ai39p40h_s[];
extern const gm_spec *const gm_tpl5fhcgp5i42cd_s[];
extern const gm_tmpl gm_tpl5g8f9c4f30p4;
extern const uint32_t gm_tpl5g8f9c4f30p4_i[];
extern const gm_tmpl_piece gm_tpl5g8f9c4f30p4_p[];
extern const gm_spec *const gm_tpl5g8f9c4f30p4_s[];
extern const gm_tmpl gm_tpl5gt902348obb;
extern const uint32_t gm_tpl5gt902348obb_i[];
extern const gm_tmpl_piece gm_tpl5gt902348obb_p[];
extern const gm_spec *const gm_tpl5gt902348obb_s[];
extern const uint32_t gm_tpl5jb9rn2u95g6_i[];
extern const uint32_t gm_tpl5jffirbo3a3v_i[];
extern const gm_tmpl_piece gm_tpl5kl598jq5h12_p[];
extern const gm_tmpl gm_tpl5m0ku1mi16i0;
extern const uint32_t gm_tpl5m0ku1mi16i0_i[];
extern const gm_tmpl_piece gm_tpl5m0ku1mi16i0_p[];
extern const gm_spec *const gm_tpl5m0ku1mi16i0_s[];
extern const gm_spec *const gm_tpl5prdhjdrg194_s[];
extern const gm_spec *const gm_tpl5rdj46lif6eq_s[];
extern const gm_tmpl gm_tpl5svfna5ij5ui;
extern const uint32_t gm_tpl5svfna5ij5ui_i[];
extern const gm_tmpl_piece gm_tpl5svfna5ij5ui_p[];
extern const gm_spec *const gm_tpl5svfna5ij5ui_s[];
extern const gm_spec *const gm_tpl5uqj3jbiq5f0_s[];
extern const uint32_t gm_tpl5vbp9fc0mpc1_i[];
extern const gm_spec *const gm_tpl6104ee3qfshr_s[];
extern const uint32_t gm_tpl6186pbdul6ge_i[];
extern const gm_spec *const gm_tpl63iuq3je4sbr_s[];
extern const gm_tmpl_piece gm_tpl64ltc7suc5qg_p[];
extern const gm_spec *const gm_tpl65nnuqtjd24o_s[];
extern const gm_tmpl gm_tpl6bjoajp2fh2n;
extern const uint32_t gm_tpl6bjoajp2fh2n_i[];
extern const gm_tmpl_piece gm_tpl6bjoajp2fh2n_p[];
extern const gm_spec *const gm_tpl6bjoajp2fh2n_s[];
extern const gm_tmpl gm_tpl6boglt2rkmh4;
extern const uint32_t gm_tpl6boglt2rkmh4_i[];
extern const gm_tmpl_piece gm_tpl6boglt2rkmh4_p[];
extern const gm_spec *const gm_tpl6boglt2rkmh4_s[];
extern const gm_tmpl gm_tpl6cs6ch4uvrua;
extern const uint32_t gm_tpl6cs6ch4uvrua_i[];
extern const gm_tmpl_piece gm_tpl6cs6ch4uvrua_p[];
extern const gm_spec *const gm_tpl6cs6ch4uvrua_s[];
extern const gm_tmpl_piece gm_tpl6gp425a13qgr_p[];
extern const gm_tmpl gm_tpl6jo4mmkdq2l1;
extern const uint32_t gm_tpl6jo4mmkdq2l1_i[];
extern const gm_tmpl_piece gm_tpl6jo4mmkdq2l1_p[];
extern const gm_spec *const gm_tpl6jo4mmkdq2l1_s[];
extern const gm_tmpl gm_tpl6qj65gvm8vp3;
extern const uint32_t gm_tpl6qj65gvm8vp3_i[];
extern const gm_tmpl_piece gm_tpl6qj65gvm8vp3_p[];
extern const gm_spec *const gm_tpl6qj65gvm8vp3_s[];
extern const gm_tmpl gm_tpl6r9reg6pnruq;
extern const uint32_t gm_tpl6r9reg6pnruq_i[];
extern const gm_tmpl_piece gm_tpl6r9reg6pnruq_p[];
extern const gm_spec *const gm_tpl6r9reg6pnruq_s[];
extern const gm_tmpl_piece gm_tpl6salfcvi63n2_p[];
extern const gm_tmpl_piece gm_tpl737bm31hf71o_p[];
extern const gm_tmpl gm_tpl74mr52kbqst5;
extern const uint32_t gm_tpl74mr52kbqst5_i[];
extern const gm_tmpl_piece gm_tpl74mr52kbqst5_p[];
extern const gm_spec *const gm_tpl74mr52kbqst5_s[];
extern const gm_tmpl gm_tpl77gpruiccsl2;
extern const uint32_t gm_tpl77gpruiccsl2_i[];
extern const gm_tmpl_piece gm_tpl77gpruiccsl2_p[];
extern const gm_spec *const gm_tpl77gpruiccsl2_s[];
extern const uint32_t gm_tpl77hog51tnj6o_i[];
extern const gm_tmpl_piece gm_tpl7bbfeces83t7_p[];
extern const gm_tmpl_piece gm_tpl7dh44jmr11le_p[];
extern const gm_spec *const gm_tpl7dh44jmr11le_s[];
extern const gm_spec *const gm_tpl7hvv6i3t3o0k_s[];
extern const gm_tmpl_piece gm_tpl7kp6007qjdtb_p[];
extern const uint32_t gm_tpl7ll3166bkl17_i[];
extern const uint32_t gm_tpl7ufg84gs1lc7_i[];
extern const gm_tmpl_piece gm_tpl7ut0r685hcaa_p[];
extern const uint32_t gm_tpl82nt5tav1er7_i[];
extern const gm_spec *const gm_tpl83abvjjf3qd1_s[];
extern const gm_spec *const gm_tpl8lt5kipnmd64_s[];
extern const gm_spec *const gm_tpl8nmc259qeh4g_s[];
extern const gm_spec *const gm_tpl8ocve2bpsu72_s[];
extern const uint32_t gm_tpl8qng452btr2a_i[];
extern const gm_tmpl gm_tpl8s8bnem30e1s;
extern const uint32_t gm_tpl8s8bnem30e1s_i[];
extern const gm_tmpl_piece gm_tpl8s8bnem30e1s_p[];
extern const gm_spec *const gm_tpl8s8bnem30e1s_s[];
extern const gm_tmpl gm_tpl8tri2h8bpkpg;
extern const uint32_t gm_tpl8tri2h8bpkpg_i[];
extern const gm_tmpl_piece gm_tpl8tri2h8bpkpg_p[];
extern const gm_spec *const gm_tpl8tri2h8bpkpg_s[];
extern const gm_spec *const gm_tpl8vbes2v6184u_s[];
extern const gm_tmpl_piece gm_tpl941gb7vu8kgv_p[];
extern const gm_tmpl gm_tpl955dep3t38r6;
extern const uint32_t gm_tpl955dep3t38r6_i[];
extern const gm_tmpl_piece gm_tpl955dep3t38r6_p[];
extern const gm_spec *const gm_tpl955dep3t38r6_s[];
extern const uint32_t gm_tpl9jrfnomsrfk1_i[];
extern const gm_tmpl_piece gm_tpl9kch76oh20pa_p[];
extern const uint32_t gm_tpl9kqhb5r0uube_i[];
extern const gm_tmpl_piece gm_tpl9kqhb5r0uube_p[];
extern const uint32_t gm_tpl9kt3nn7p3a92_i[];
extern const gm_tmpl gm_tpl9ne0ajfec16n;
extern const uint32_t gm_tpl9ne0ajfec16n_i[];
extern const gm_tmpl_piece gm_tpl9ne0ajfec16n_p[];
extern const gm_spec *const gm_tpl9ne0ajfec16n_s[];
extern const gm_tmpl_piece gm_tpl9red218f96cc_p[];
extern const gm_tmpl gm_tpl9sifsarv7t06;
extern const uint32_t gm_tpl9sifsarv7t06_i[];
extern const gm_tmpl_piece gm_tpl9sifsarv7t06_p[];
extern const gm_spec *const gm_tpl9sifsarv7t06_s[];
extern const gm_tmpl_piece gm_tpl9v8qcr7jilen_p[];
extern const uint32_t gm_tpla67mmq1id2h3_i[];
extern const gm_tmpl gm_tpla843kfhqtvit;
extern const uint32_t gm_tpla843kfhqtvit_i[];
extern const gm_tmpl_piece gm_tpla843kfhqtvit_p[];
extern const gm_spec *const gm_tpla843kfhqtvit_s[];
extern const gm_spec *const gm_tpla93ou7df923g_s[];
extern const gm_spec *const gm_tplaahjt316h1v3_s[];
extern const gm_tmpl gm_tplablvqji1bpu7;
extern const uint32_t gm_tplablvqji1bpu7_i[];
extern const gm_tmpl_piece gm_tplablvqji1bpu7_p[];
extern const gm_spec *const gm_tplablvqji1bpu7_s[];
extern const gm_spec *const gm_tplaemahh05v026_s[];
extern const gm_spec *const gm_tplaggdk85jgg2m_s[];
extern const gm_spec *const gm_tplahe50smef36f_s[];
extern const uint32_t gm_tplai4t67ppan77_i[];
extern const uint32_t gm_tplalu8vhij0mma_i[];
extern const gm_tmpl gm_tplancpnb63qiq5;
extern const uint32_t gm_tplancpnb63qiq5_i[];
extern const gm_tmpl_piece gm_tplancpnb63qiq5_p[];
extern const gm_spec *const gm_tplancpnb63qiq5_s[];
extern const gm_spec *const gm_tplaoj5r4tauejk_s[];
extern const gm_tmpl gm_tplaos6tmpdtrie;
extern const uint32_t gm_tplaos6tmpdtrie_i[];
extern const gm_tmpl_piece gm_tplaos6tmpdtrie_p[];
extern const gm_spec *const gm_tplaos6tmpdtrie_s[];
extern const gm_tmpl gm_tplaq7jlj8i0s87;
extern const uint32_t gm_tplaq7jlj8i0s87_i[];
extern const gm_tmpl_piece gm_tplaq7jlj8i0s87_p[];
extern const gm_spec *const gm_tplaq7jlj8i0s87_s[];
extern const uint32_t gm_tplau4cbc1piuvn_i[];
extern const uint32_t gm_tplav4294nbj9c7_i[];
extern const uint32_t gm_tplb043ibme104j_i[];
extern const gm_tmpl gm_tplb05kqqtmbu7v;
extern const uint32_t gm_tplb05kqqtmbu7v_i[];
extern const gm_tmpl_piece gm_tplb05kqqtmbu7v_p[];
extern const gm_spec *const gm_tplb05kqqtmbu7v_s[];
extern const gm_tmpl_piece gm_tplb2gsdj8iioqj_p[];
extern const gm_tmpl gm_tplb52vln6fobrq;
extern const uint32_t gm_tplb52vln6fobrq_i[];
extern const gm_tmpl_piece gm_tplb52vln6fobrq_p[];
extern const gm_spec *const gm_tplb52vln6fobrq_s[];
extern const gm_spec *const gm_tplb899rpghqa93_s[];
extern const uint32_t gm_tplb9i7jfun8rvg_i[];
extern const gm_tmpl gm_tplbds86ojmcoik;
extern const uint32_t gm_tplbds86ojmcoik_i[];
extern const gm_tmpl_piece gm_tplbds86ojmcoik_p[];
extern const gm_spec *const gm_tplbds86ojmcoik_s[];
extern const gm_tmpl_piece gm_tplbfdkthp45ai4_p[];
extern const gm_tmpl_piece gm_tplbh7305mc9stu_p[];
extern const gm_tmpl_piece gm_tplbj3gvifc61li_p[];
extern const gm_tmpl gm_tplbk03g4ml40ad;
extern const uint32_t gm_tplbk03g4ml40ad_i[];
extern const gm_tmpl_piece gm_tplbk03g4ml40ad_p[];
extern const gm_spec *const gm_tplbk03g4ml40ad_s[];
extern const gm_tmpl_piece gm_tplbk128im3qlnl_p[];
extern const gm_tmpl gm_tplbk1k026jl879;
extern const uint32_t gm_tplbk1k026jl879_i[];
extern const gm_tmpl_piece gm_tplbk1k026jl879_p[];
extern const gm_spec *const gm_tplbk1k026jl879_s[];
extern const uint32_t gm_tplbo3vrh9egnst_i[];
extern const gm_tmpl_piece gm_tplbr0fi0201tmv_p[];
extern const uint32_t gm_tplbu8sthucqj1b_i[];
extern const gm_spec *const gm_tplbu8sthucqj1b_s[];
extern const gm_spec *const gm_tplc0d2186db2jt_s[];
extern const uint32_t gm_tplc0jhs849b438_i[];
extern const gm_tmpl gm_tplc1emmpcosf7b;
extern const uint32_t gm_tplc1emmpcosf7b_i[];
extern const gm_tmpl_piece gm_tplc1emmpcosf7b_p[];
extern const gm_spec *const gm_tplc1emmpcosf7b_s[];
extern const uint32_t gm_tplc1tlvqjnir84_i[];
extern const gm_spec *const gm_tplc1tlvqjnir84_s[];
extern const uint32_t gm_tplceihv99elj2t_i[];
extern const gm_tmpl_piece gm_tplcn6c0fsph4j2_p[];
extern const gm_spec *const gm_tplcn6c0fsph4j2_s[];
extern const uint32_t gm_tplcouqld11oeil_i[];
extern const uint32_t gm_tplcttkqubjcaqf_i[];
extern const gm_tmpl_piece gm_tpld1hhk0kef14l_p[];
extern const uint32_t gm_tpld9ts015fint3_i[];
extern const gm_tmpl gm_tpldftt3jdrahlt;
extern const uint32_t gm_tpldftt3jdrahlt_i[];
extern const gm_tmpl_piece gm_tpldftt3jdrahlt_p[];
extern const gm_spec *const gm_tpldftt3jdrahlt_s[];
extern const gm_spec *const gm_tpldjbe17jhbkpg_s[];
extern const uint32_t gm_tpldknbfev12jrb_i[];
extern const gm_spec *const gm_tpldlrsentitfg3_s[];
extern const gm_tmpl_piece gm_tpldm0f92v61rgi_p[];
extern const gm_tmpl_piece gm_tpldn8tr7ddtik6_p[];
extern const gm_tmpl gm_tpldq0sm30o8mph;
extern const uint32_t gm_tpldq0sm30o8mph_i[];
extern const gm_tmpl_piece gm_tpldq0sm30o8mph_p[];
extern const gm_spec *const gm_tpldq0sm30o8mph_s[];
extern const gm_tmpl_piece gm_tpldqjt9kdk4ntt_p[];
extern const gm_tmpl gm_tpldqr3c69jgnqi;
extern const uint32_t gm_tpldqr3c69jgnqi_i[];
extern const gm_tmpl_piece gm_tpldqr3c69jgnqi_p[];
extern const gm_spec *const gm_tpldqr3c69jgnqi_s[];
extern const gm_tmpl_piece gm_tpldt4nur6h705h_p[];
extern const gm_spec *const gm_tpldtv4ooqtkvgm_s[];
extern const gm_tmpl_piece gm_tple2rp5rj93mkj_p[];
extern const gm_tmpl gm_tple377dkn6pj7u;
extern const uint32_t gm_tple377dkn6pj7u_i[];
extern const gm_tmpl_piece gm_tple377dkn6pj7u_p[];
extern const gm_spec *const gm_tple377dkn6pj7u_s[];
extern const gm_tmpl_piece gm_tple4r1aumd134l_p[];
extern const gm_tmpl_piece gm_tple75bobdc6q0g_p[];
extern const uint32_t gm_tple7i0r0sg6sjs_i[];
extern const gm_tmpl gm_tple7qj0fr2tlub;
extern const uint32_t gm_tple7qj0fr2tlub_i[];
extern const gm_tmpl_piece gm_tple7qj0fr2tlub_p[];
extern const gm_spec *const gm_tple7qj0fr2tlub_s[];
extern const gm_tmpl gm_tple87ca4est7gh;
extern const uint32_t gm_tple87ca4est7gh_i[];
extern const gm_tmpl_piece gm_tple87ca4est7gh_p[];
extern const gm_spec *const gm_tple87ca4est7gh_s[];
extern const gm_spec *const gm_tplenpf051q0m4n_s[];
extern const gm_tmpl gm_tpleonu21sc9fi7;
extern const uint32_t gm_tpleonu21sc9fi7_i[];
extern const gm_tmpl_piece gm_tpleonu21sc9fi7_p[];
extern const gm_spec *const gm_tpleonu21sc9fi7_s[];
extern const gm_spec *const gm_tpleoop2177apvc_s[];
extern const gm_tmpl gm_tplep2l66hmuvrc;
extern const uint32_t gm_tplep2l66hmuvrc_i[];
extern const gm_tmpl_piece gm_tplep2l66hmuvrc_p[];
extern const gm_spec *const gm_tplep2l66hmuvrc_s[];
extern const uint32_t gm_tplepdvp8ofjal7_i[];
extern const uint32_t gm_tples1m1m9iv6b0_i[];
extern const uint32_t gm_tplf0gekbh2mtku_i[];
extern const gm_tmpl_piece gm_tplf3e41f3siie3_p[];
extern const gm_spec *const gm_tplf3e41f3siie3_s[];
extern const gm_tmpl_piece gm_tplf48rihumtlpr_p[];
extern const gm_spec *const gm_tplf579fe8ol0qp_s[];
extern const gm_tmpl_piece gm_tplf9434bnahqi1_p[];
extern const uint32_t gm_tplf9jp9gfh0eik_i[];
extern const gm_tmpl gm_tplfcgbc9s4hmn7;
extern const uint32_t gm_tplfcgbc9s4hmn7_i[];
extern const gm_tmpl_piece gm_tplfcgbc9s4hmn7_p[];
extern const gm_spec *const gm_tplfcgbc9s4hmn7_s[];
extern const gm_tmpl_piece gm_tplfcj32ttpsd6a_p[];
extern const gm_spec *const gm_tplfcj32ttpsd6a_s[];
extern const gm_tmpl_piece gm_tplfjobpnodgc6e_p[];
extern const gm_tmpl_piece gm_tplfjoq6lkhl94q_p[];
extern const gm_tmpl gm_tplfo19v5itil8t;
extern const uint32_t gm_tplfo19v5itil8t_i[];
extern const gm_tmpl_piece gm_tplfo19v5itil8t_p[];
extern const gm_spec *const gm_tplfo19v5itil8t_s[];
extern const gm_tmpl gm_tplfoosl93e8k5t;
extern const uint32_t gm_tplfoosl93e8k5t_i[];
extern const gm_tmpl_piece gm_tplfoosl93e8k5t_p[];
extern const gm_spec *const gm_tplfoosl93e8k5t_s[];
extern const gm_spec *const gm_tplfqk35ejq39la_s[];
extern const gm_spec *const gm_tplfrm74l83ht8l_s[];
extern const uint32_t gm_tplft0ecs37svcn_i[];
extern const gm_tmpl gm_tplfuin6hfnfo1c;
extern const uint32_t gm_tplfuin6hfnfo1c_i[];
extern const gm_tmpl_piece gm_tplfuin6hfnfo1c_p[];
extern const gm_spec *const gm_tplfuin6hfnfo1c_s[];
extern const gm_tmpl_piece gm_tplfvv1qe6os0le_p[];
extern const gm_tmpl gm_tplg21rd4as3ph6;
extern const uint32_t gm_tplg21rd4as3ph6_i[];
extern const gm_tmpl_piece gm_tplg21rd4as3ph6_p[];
extern const gm_spec *const gm_tplg21rd4as3ph6_s[];
extern const gm_tmpl gm_tplg4866gcgr837;
extern const uint32_t gm_tplg4866gcgr837_i[];
extern const gm_tmpl_piece gm_tplg4866gcgr837_p[];
extern const gm_spec *const gm_tplg4866gcgr837_s[];
extern const gm_tmpl gm_tplgclcdp539fpt;
extern const uint32_t gm_tplgclcdp539fpt_i[];
extern const gm_tmpl_piece gm_tplgclcdp539fpt_p[];
extern const gm_spec *const gm_tplgclcdp539fpt_s[];
extern const gm_tmpl gm_tplgcrr0tu9874k;
extern const uint32_t gm_tplgcrr0tu9874k_i[];
extern const gm_tmpl_piece gm_tplgcrr0tu9874k_p[];
extern const gm_spec *const gm_tplgcrr0tu9874k_s[];
extern const gm_spec *const gm_tplge1jgdcvfbq1_s[];
extern const gm_tmpl_piece gm_tplggnuionimjk9_p[];
extern const gm_tmpl_piece gm_tplgism5t1v67p6_p[];
extern const gm_spec *const gm_tplgj9d3igk4i62_s[];
extern const gm_tmpl gm_tplgnobkmf75b9i;
extern const uint32_t gm_tplgnobkmf75b9i_i[];
extern const gm_tmpl_piece gm_tplgnobkmf75b9i_p[];
extern const gm_spec *const gm_tplgnobkmf75b9i_s[];
extern const gm_tmpl gm_tplgursfbit7uc3;
extern const uint32_t gm_tplgursfbit7uc3_i[];
extern const gm_tmpl_piece gm_tplgursfbit7uc3_p[];
extern const gm_spec *const gm_tplgursfbit7uc3_s[];
extern const gm_tmpl gm_tplgv9va17kt7ll;
extern const uint32_t gm_tplgv9va17kt7ll_i[];
extern const gm_tmpl_piece gm_tplgv9va17kt7ll_p[];
extern const gm_spec *const gm_tplgv9va17kt7ll_s[];
extern const gm_tmpl_piece gm_tplh64dsjnr7d93_p[];
extern const uint32_t gm_tplh6c513o0038j_i[];
extern const gm_tmpl_piece gm_tplh7231rl9trm2_p[];
extern const uint32_t gm_tplhciekfbsqng7_i[];
extern const gm_spec *const gm_tplhgdgs8lv4h7d_s[];
extern const gm_tmpl_piece gm_tplhghj93ore8u2_p[];
extern const uint32_t gm_tplhkjcpttbsqt0_i[];
extern const uint32_t gm_tplhos3sjrj7v9v_i[];
extern const gm_tmpl_piece gm_tplht122d0ji4km_p[];
extern const uint32_t gm_tpli2ivqo4qj5ok_i[];
extern const gm_tmpl_piece gm_tpli2ivqo4qj5ok_p[];
extern const uint32_t gm_tpli2qk88n6tll4_i[];
extern const gm_tmpl_piece gm_tpli4rfoi75onhj_p[];
extern const gm_tmpl_piece gm_tpli7hldrrhg7sr_p[];
extern const gm_tmpl_piece gm_tplicuqpjvsak60_p[];
extern const gm_spec *const gm_tplicuqpjvsak60_s[];
extern const uint32_t gm_tplidcjkf79g8le_i[];
extern const gm_spec *const gm_tplidcjkf79g8le_s[];
extern const gm_spec *const gm_tpliig5tr0gjn5a_s[];
extern const uint32_t gm_tplikisll89mgrm_i[];
extern const gm_spec *const gm_tplilne6f8eq90i_s[];
extern const uint32_t gm_tplim7pg0q4u4tg_i[];
extern const gm_tmpl gm_tplin004j5gtkru;
extern const uint32_t gm_tplin004j5gtkru_i[];
extern const gm_tmpl_piece gm_tplin004j5gtkru_p[];
extern const gm_spec *const gm_tplin004j5gtkru_s[];
extern const gm_tmpl_piece gm_tplisurri6t3aun_p[];
extern const gm_tmpl gm_tpliucrr7pqi5es;
extern const uint32_t gm_tpliucrr7pqi5es_i[];
extern const gm_tmpl_piece gm_tpliucrr7pqi5es_p[];
extern const gm_spec *const gm_tpliucrr7pqi5es_s[];
extern const uint32_t gm_tplivip1s1762gt_i[];
extern const gm_tmpl gm_tplj78420ela1cg;
extern const uint32_t gm_tplj78420ela1cg_i[];
extern const gm_tmpl_piece gm_tplj78420ela1cg_p[];
extern const gm_spec *const gm_tplj78420ela1cg_s[];
extern const gm_spec *const gm_tpljc7kqi31f4d2_s[];
extern const gm_spec *const gm_tpljf9iocqf0fr6_s[];
extern const gm_tmpl gm_tpljg9idve5dgca;
extern const uint32_t gm_tpljg9idve5dgca_i[];
extern const gm_tmpl_piece gm_tpljg9idve5dgca_p[];
extern const gm_spec *const gm_tpljg9idve5dgca_s[];
extern const gm_spec *const gm_tpljnlsd7076tqd_s[];
extern const gm_tmpl_piece gm_tpljp2husef3b2l_p[];
extern const gm_tmpl_piece gm_tpljpjg7sv41er0_p[];
extern const uint32_t gm_tpljt2av4h7c132_i[];
extern const gm_tmpl gm_tpljucl1tv02je2;
extern const uint32_t gm_tpljucl1tv02je2_i[];
extern const gm_tmpl_piece gm_tpljucl1tv02je2_p[];
extern const gm_spec *const gm_tpljucl1tv02je2_s[];
extern const gm_tmpl_piece gm_tplk1dctbs79mmk_p[];
extern const gm_tmpl gm_tplk5o80h990o81;
extern const uint32_t gm_tplk5o80h990o81_i[];
extern const gm_tmpl_piece gm_tplk5o80h990o81_p[];
extern const gm_spec *const gm_tplk5o80h990o81_s[];
extern const gm_tmpl_piece gm_tplk5t5jhqqvnff_p[];
extern const gm_spec *const gm_tplkjc1g73er3ub_s[];
extern const uint32_t gm_tplkji0sd9phcb9_i[];
extern const uint32_t gm_tplkv0rjh54m5gn_i[];
extern const uint32_t gm_tpll487d6t52m4g_i[];
extern const gm_spec *const gm_tpll6udkhd3c609_s[];
extern const gm_spec *const gm_tpll733hu18149l_s[];
extern const gm_tmpl gm_tplld08lp5kl07i;
extern const uint32_t gm_tplld08lp5kl07i_i[];
extern const gm_tmpl_piece gm_tplld08lp5kl07i_p[];
extern const gm_spec *const gm_tplld08lp5kl07i_s[];
extern const gm_tmpl gm_tplld9eu0oenqb7;
extern const uint32_t gm_tplld9eu0oenqb7_i[];
extern const gm_tmpl_piece gm_tplld9eu0oenqb7_p[];
extern const gm_spec *const gm_tplld9eu0oenqb7_s[];
extern const gm_spec *const gm_tpllhk8pmjfso1j_s[];
extern const gm_tmpl gm_tplljgqo309tg8i;
extern const uint32_t gm_tplljgqo309tg8i_i[];
extern const gm_tmpl_piece gm_tplljgqo309tg8i_p[];
extern const gm_spec *const gm_tplljgqo309tg8i_s[];
extern const uint32_t gm_tpllnpq25q3a5a4_i[];
extern const gm_tmpl gm_tplloue1uueuiig;
extern const uint32_t gm_tplloue1uueuiig_i[];
extern const gm_tmpl_piece gm_tplloue1uueuiig_p[];
extern const gm_spec *const gm_tplloue1uueuiig_s[];
extern const gm_tmpl_piece gm_tpllp752obmvr55_p[];
extern const gm_spec *const gm_tplm2m182mt5ljj_s[];
extern const uint32_t gm_tplm3a458dq976v_i[];
extern const gm_tmpl gm_tplm3f7baid20me;
extern const uint32_t gm_tplm3f7baid20me_i[];
extern const gm_tmpl_piece gm_tplm3f7baid20me_p[];
extern const gm_spec *const gm_tplm3f7baid20me_s[];
extern const uint32_t gm_tplm3t9qdrf37sd_i[];
extern const gm_tmpl gm_tplm5g4n5l4k6ck;
extern const uint32_t gm_tplm5g4n5l4k6ck_i[];
extern const gm_tmpl_piece gm_tplm5g4n5l4k6ck_p[];
extern const gm_spec *const gm_tplm5g4n5l4k6ck_s[];
extern const uint32_t gm_tplm61d8mlvmepm_i[];
extern const gm_spec *const gm_tplm61d8mlvmepm_s[];
extern const gm_tmpl_piece gm_tplm7tetbq1snr9_p[];
extern const uint32_t gm_tplm9hj31tk9ip4_i[];
extern const gm_spec *const gm_tplmekan10d5buf_s[];
extern const gm_spec *const gm_tplmjku4vg3eupf_s[];
extern const gm_tmpl gm_tplmkgblgbo7imb;
extern const uint32_t gm_tplmkgblgbo7imb_i[];
extern const gm_tmpl_piece gm_tplmkgblgbo7imb_p[];
extern const gm_spec *const gm_tplmkgblgbo7imb_s[];
extern const gm_tmpl_piece gm_tplmm5mgnjalbfr_p[];
extern const gm_tmpl gm_tplmpsv5qagthkk;
extern const uint32_t gm_tplmpsv5qagthkk_i[];
extern const gm_tmpl_piece gm_tplmpsv5qagthkk_p[];
extern const gm_spec *const gm_tplmpsv5qagthkk_s[];
extern const gm_tmpl_piece gm_tpln4hlddpv31vi_p[];
extern const gm_spec *const gm_tplnc4en2s3t0da_s[];
extern const gm_tmpl_piece gm_tplncm14tpa4cdg_p[];
extern const gm_tmpl gm_tplngekt6vfvnsv;
extern const uint32_t gm_tplngekt6vfvnsv_i[];
extern const gm_tmpl_piece gm_tplngekt6vfvnsv_p[];
extern const gm_spec *const gm_tplngekt6vfvnsv_s[];
extern const gm_tmpl_piece gm_tplngsd5kcuu6fv_p[];
extern const gm_tmpl gm_tplnitaca1dp5li;
extern const uint32_t gm_tplnitaca1dp5li_i[];
extern const gm_tmpl_piece gm_tplnitaca1dp5li_p[];
extern const gm_spec *const gm_tplnitaca1dp5li_s[];
extern const gm_tmpl gm_tplnko388ar3ptd;
extern const uint32_t gm_tplnko388ar3ptd_i[];
extern const gm_tmpl_piece gm_tplnko388ar3ptd_p[];
extern const gm_spec *const gm_tplnko388ar3ptd_s[];
extern const gm_spec *const gm_tplnlqa25lecq48_s[];
extern const uint32_t gm_tplnnqg0hvtcqac_i[];
extern const gm_tmpl_piece gm_tplnnqg0hvtcqac_p[];
extern const gm_tmpl gm_tplnrfprs01gfmj;
extern const uint32_t gm_tplnrfprs01gfmj_i[];
extern const gm_tmpl_piece gm_tplnrfprs01gfmj_p[];
extern const gm_spec *const gm_tplnrfprs01gfmj_s[];
extern const gm_tmpl gm_tplntci9ovg2hdj;
extern const uint32_t gm_tplntci9ovg2hdj_i[];
extern const gm_tmpl_piece gm_tplntci9ovg2hdj_p[];
extern const gm_spec *const gm_tplntci9ovg2hdj_s[];
extern const gm_tmpl gm_tplo203818ggfa8;
extern const uint32_t gm_tplo203818ggfa8_i[];
extern const gm_tmpl_piece gm_tplo203818ggfa8_p[];
extern const gm_spec *const gm_tplo203818ggfa8_s[];
extern const uint32_t gm_tplo2fstigc8bcf_i[];
extern const gm_tmpl_piece gm_tplo2fstigc8bcf_p[];
extern const gm_tmpl gm_tplo4cs7gli8ts2;
extern const uint32_t gm_tplo4cs7gli8ts2_i[];
extern const gm_tmpl_piece gm_tplo4cs7gli8ts2_p[];
extern const gm_spec *const gm_tplo4cs7gli8ts2_s[];
extern const gm_tmpl gm_tplo5614u5649ka;
extern const uint32_t gm_tplo5614u5649ka_i[];
extern const gm_tmpl_piece gm_tplo5614u5649ka_p[];
extern const gm_spec *const gm_tplo5614u5649ka_s[];
extern const gm_spec *const gm_tplo6lbijqleti1_s[];
extern const uint32_t gm_tplo867268s51oj_i[];
extern const gm_tmpl gm_tploa995k06npbj;
extern const uint32_t gm_tploa995k06npbj_i[];
extern const gm_tmpl_piece gm_tploa995k06npbj_p[];
extern const gm_spec *const gm_tploa995k06npbj_s[];
extern const uint32_t gm_tplod1bbc3c9s5j_i[];
extern const gm_tmpl gm_tplodcipjau6eqr;
extern const uint32_t gm_tplodcipjau6eqr_i[];
extern const gm_tmpl_piece gm_tplodcipjau6eqr_p[];
extern const gm_spec *const gm_tplodcipjau6eqr_s[];
extern const gm_tmpl gm_tplogq0vb9du4ii;
extern const uint32_t gm_tplogq0vb9du4ii_i[];
extern const gm_tmpl_piece gm_tplogq0vb9du4ii_p[];
extern const gm_spec *const gm_tplogq0vb9du4ii_s[];
extern const uint32_t gm_tploh8l9phm91d3_i[];
extern const gm_tmpl gm_tploj4uaptg6qnk;
extern const uint32_t gm_tploj4uaptg6qnk_i[];
extern const gm_tmpl_piece gm_tploj4uaptg6qnk_p[];
extern const gm_spec *const gm_tploj4uaptg6qnk_s[];
extern const gm_tmpl_piece gm_tplom6fga2t4kfv_p[];
extern const gm_tmpl gm_tplomrgvu31uqof;
extern const uint32_t gm_tplomrgvu31uqof_i[];
extern const gm_tmpl_piece gm_tplomrgvu31uqof_p[];
extern const gm_spec *const gm_tplomrgvu31uqof_s[];
extern const gm_spec *const gm_tplorj2ck0ovl7t_s[];
extern const gm_tmpl_piece gm_tplovlajhkvntql_p[];
extern const gm_spec *const gm_tplovoda71kp04v_s[];
extern const uint32_t gm_tplp4f8nloo6fdo_i[];
extern const uint32_t gm_tplp93m8c6u3nrp_i[];
extern const gm_tmpl_piece gm_tplp98fsht4k706_p[];
extern const uint32_t gm_tplpe5hifub5ms7_i[];
extern const gm_tmpl gm_tplpehi2hdi7j7m;
extern const uint32_t gm_tplpehi2hdi7j7m_i[];
extern const gm_tmpl_piece gm_tplpehi2hdi7j7m_p[];
extern const gm_spec *const gm_tplpehi2hdi7j7m_s[];
extern const gm_tmpl gm_tplpgeniqejbt43;
extern const uint32_t gm_tplpgeniqejbt43_i[];
extern const gm_tmpl_piece gm_tplpgeniqejbt43_p[];
extern const gm_spec *const gm_tplpgeniqejbt43_s[];
extern const uint32_t gm_tplpjjba8pqbhe2_i[];
extern const uint32_t gm_tplpkvu14cg936d_i[];
extern const gm_tmpl gm_tplplkqcb01ocrd;
extern const uint32_t gm_tplplkqcb01ocrd_i[];
extern const gm_tmpl_piece gm_tplplkqcb01ocrd_p[];
extern const gm_spec *const gm_tplplkqcb01ocrd_s[];
extern const uint32_t gm_tplpntqck8i8f5b_i[];
extern const gm_tmpl_piece gm_tplpodff9060oeo_p[];
extern const gm_tmpl_piece gm_tplpqtqehhhq7oj_p[];
extern const gm_tmpl_piece gm_tplpvmvg9ce66cu_p[];
extern const uint32_t gm_tplq11qqgpg1tv1_i[];
extern const gm_tmpl gm_tplq2jm9n7vekeo;
extern const uint32_t gm_tplq2jm9n7vekeo_i[];
extern const gm_tmpl_piece gm_tplq2jm9n7vekeo_p[];
extern const gm_spec *const gm_tplq2jm9n7vekeo_s[];
extern const uint32_t gm_tplq75gqr5tvlmd_i[];
extern const gm_spec *const gm_tplqbtvqd3a4g25_s[];
extern const gm_spec *const gm_tplqd2g84l5ne39_s[];
extern const gm_tmpl_piece gm_tplqg9a6t74ffg7_p[];
extern const gm_spec *const gm_tplqhugo4i82obv_s[];
extern const gm_spec *const gm_tplqjo10upi8bs6_s[];
extern const gm_tmpl_piece gm_tplqnau1elkkogs_p[];
extern const gm_tmpl gm_tplqp3jlt332oq5;
extern const uint32_t gm_tplqp3jlt332oq5_i[];
extern const gm_tmpl_piece gm_tplqp3jlt332oq5_p[];
extern const gm_spec *const gm_tplqp3jlt332oq5_s[];
extern const gm_tmpl gm_tplqpg9790rv4ki;
extern const uint32_t gm_tplqpg9790rv4ki_i[];
extern const gm_tmpl_piece gm_tplqpg9790rv4ki_p[];
extern const gm_spec *const gm_tplqpg9790rv4ki_s[];
extern const uint32_t gm_tplqstpsk3jkk1c_i[];
extern const gm_tmpl gm_tplr1if0cs3oolt;
extern const uint32_t gm_tplr1if0cs3oolt_i[];
extern const gm_tmpl_piece gm_tplr1if0cs3oolt_p[];
extern const gm_spec *const gm_tplr1if0cs3oolt_s[];
extern const gm_tmpl gm_tplr7vr7aubja28;
extern const uint32_t gm_tplr7vr7aubja28_i[];
extern const gm_tmpl_piece gm_tplr7vr7aubja28_p[];
extern const gm_spec *const gm_tplr7vr7aubja28_s[];
extern const uint32_t gm_tplr8vkqgn7raqo_i[];
extern const gm_tmpl_piece gm_tplrcurrnquacuf_p[];
extern const gm_spec *const gm_tplre7l7cu4fpbv_s[];
extern const gm_spec *const gm_tplrivm1rlf1ps3_s[];
extern const gm_tmpl_piece gm_tplrkvt0j8tqlqg_p[];
extern const gm_spec *const gm_tplrmoc4ag4eqrb_s[];
extern const uint32_t gm_tplrodjuj3fgmsa_i[];
extern const gm_tmpl_piece gm_tplrqgv2l0evbdg_p[];
extern const uint32_t gm_tplrsdjn4cifhr9_i[];
extern const gm_tmpl gm_tplrvl1hqu8hbkh;
extern const uint32_t gm_tplrvl1hqu8hbkh_i[];
extern const gm_tmpl_piece gm_tplrvl1hqu8hbkh_p[];
extern const gm_spec *const gm_tplrvl1hqu8hbkh_s[];
extern const uint32_t gm_tpls2ou5a11vcn7_i[];
extern const gm_tmpl gm_tpls5kpq7mskrde;
extern const uint32_t gm_tpls5kpq7mskrde_i[];
extern const gm_tmpl_piece gm_tpls5kpq7mskrde_p[];
extern const gm_spec *const gm_tpls5kpq7mskrde_s[];
extern const gm_spec *const gm_tpls80s4u4ujepk_s[];
extern const uint32_t gm_tpls8octb03drbp_i[];
extern const uint32_t gm_tplsa4s74k8e6ci_i[];
extern const gm_tmpl_piece gm_tplsa4s74k8e6ci_p[];
extern const gm_tmpl_piece gm_tplsb492k540eai_p[];
extern const uint32_t gm_tplsc9pqkp31v7e_i[];
extern const gm_spec *const gm_tplsdamq31ts61q_s[];
extern const gm_tmpl gm_tplsfrupsaoln3v;
extern const uint32_t gm_tplsfrupsaoln3v_i[];
extern const gm_tmpl_piece gm_tplsfrupsaoln3v_p[];
extern const gm_spec *const gm_tplsfrupsaoln3v_s[];
extern const gm_tmpl gm_tplsil8jb9ng0ao;
extern const uint32_t gm_tplsil8jb9ng0ao_i[];
extern const gm_tmpl_piece gm_tplsil8jb9ng0ao_p[];
extern const gm_spec *const gm_tplsil8jb9ng0ao_s[];
extern const gm_tmpl_piece gm_tplska2t1f86rr8_p[];
extern const uint32_t gm_tplsnlr78cvfrao_i[];
extern const gm_tmpl gm_tplsogg2sp0m9p9;
extern const uint32_t gm_tplsogg2sp0m9p9_i[];
extern const gm_tmpl_piece gm_tplsogg2sp0m9p9_p[];
extern const gm_spec *const gm_tplsogg2sp0m9p9_s[];
extern const gm_spec *const gm_tplsrms2036pbmr_s[];
extern const gm_tmpl_piece gm_tplsrr2cfplv9ff_p[];
extern const gm_spec *const gm_tplsrr43aau9tgs_s[];
extern const gm_tmpl_piece gm_tplt1555ki49he5_p[];
extern const uint32_t gm_tplt320ru9bfpdd_i[];
extern const gm_tmpl gm_tplt392n8hqfhn9;
extern const uint32_t gm_tplt392n8hqfhn9_i[];
extern const gm_tmpl_piece gm_tplt392n8hqfhn9_p[];
extern const gm_spec *const gm_tplt392n8hqfhn9_s[];
extern const gm_spec *const gm_tplt7tm5142l0mh_s[];
extern const gm_tmpl gm_tpltdh9l4qjtpek;
extern const uint32_t gm_tpltdh9l4qjtpek_i[];
extern const gm_tmpl_piece gm_tpltdh9l4qjtpek_p[];
extern const gm_spec *const gm_tpltdh9l4qjtpek_s[];
extern const uint32_t gm_tplteu0ijrjjsvv_i[];
extern const uint32_t gm_tpltnckk4rc73ud_i[];
extern const gm_tmpl gm_tpltng0q64u6p80;
extern const uint32_t gm_tpltng0q64u6p80_i[];
extern const gm_tmpl_piece gm_tpltng0q64u6p80_p[];
extern const gm_spec *const gm_tpltng0q64u6p80_s[];
extern const gm_tmpl gm_tpltnu092iqmfdd;
extern const uint32_t gm_tpltnu092iqmfdd_i[];
extern const gm_tmpl_piece gm_tpltnu092iqmfdd_p[];
extern const gm_spec *const gm_tpltnu092iqmfdd_s[];
extern const gm_spec *const gm_tplts1d4nefj5gf_s[];
extern const gm_tmpl_piece gm_tplu0ijdfgp8vs3_p[];
extern const gm_tmpl gm_tplu272587ugnvi;
extern const uint32_t gm_tplu272587ugnvi_i[];
extern const gm_tmpl_piece gm_tplu272587ugnvi_p[];
extern const gm_spec *const gm_tplu272587ugnvi_s[];
extern const uint32_t gm_tplu49icuspir05_i[];
extern const gm_spec *const gm_tplu9669cm9mqja_s[];
extern const gm_spec *const gm_tpluc9sbr66n218_s[];
extern const gm_tmpl_piece gm_tplucd2r10hh63v_p[];
extern const gm_spec *const gm_tplucd2r10hh63v_s[];
extern const uint32_t gm_tpludt9aee4n14r_i[];
extern const gm_tmpl_piece gm_tpluf7u812t4p3d_p[];
extern const gm_tmpl gm_tplugm36655v1ns;
extern const uint32_t gm_tplugm36655v1ns_i[];
extern const gm_tmpl_piece gm_tplugm36655v1ns_p[];
extern const gm_spec *const gm_tplugm36655v1ns_s[];
extern const gm_spec *const gm_tpluhfd3hvppdji_s[];
extern const gm_spec *const gm_tpluoji9akrnj9c_s[];
extern const gm_spec *const gm_tpluue9jh13g9df_s[];
extern const uint32_t gm_tplv8s8fgl2lggu_i[];
extern const gm_spec *const gm_tplvc30qsg5hfc6_s[];
extern const gm_spec *const gm_tplvcccam8nulr1_s[];
extern const uint32_t gm_tplvcuursopmobk_i[];
extern const gm_tmpl gm_tplvi7pmohm1ihu;
extern const uint32_t gm_tplvi7pmohm1ihu_i[];
extern const gm_tmpl_piece gm_tplvi7pmohm1ihu_p[];
extern const gm_spec *const gm_tplvi7pmohm1ihu_s[];
extern const gm_tmpl_piece gm_tplvkf7arb107q2_p[];
extern const gm_tmpl_piece gm_tplvl1jqbb33ag2_p[];
extern const gm_spec *const gm_tplvonunpuh0kn8_s[];
extern const uint32_t gm_tplvqpq1chrhvno_i[];
extern const gm_type gm_ty467d5cm1d5;
extern const gm_variant gm_tybqb4gb1d92_v[];
extern const gm_type gm_tyehhdtpeti1;
extern const gm_variant gm_tyehhdtpeti1_v[];
extern const gm_type gm_tyg63o53ar52;
extern const gm_variant gm_tyg63o53ar52_v[];
extern const gm_type gm_tyqqajgod2bb;
extern const gm_variant gm_tysgeom7733j_v[];
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1avb45k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_1avb45k F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 82u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            gm_b16_8 t5 = gm_l1;
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_snenkhfev166s));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(2));
            uint8_t t7 = (gm_eq0iqiubfm7e(((uint8_t *)&t5), ((uint8_t *)&t6)));
            if (t7) {
                uint8_t t8 = 0;
                ((struct gm_fr_leaves_directory_kq5llq4 *)((&F.h)->up))->ret = ((uint8_t)1);
                gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t8;
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 131, 5);
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
const char gm_src0s0aq2lhn254[] = "compiler/load/core.gm";
const char gm_src2n2ugogq2cq2[] = "compiler/repl/report.gm";
const char gm_src361hl447ggcv[] = "compiler/fix/fix.gm";
const char gm_src3a4t4bi1ba85[] = "compiler/check/index.gm";
const char gm_src41vqrhgekac9[] = "compiler/syntax/token.gm";
const char gm_srcgfglnv8p059r[] = "compiler/diag/diag.gm";
const char gm_srcht195tfrfuqo[] = "compiler/cgen/names.gm";
const char gm_srci09ctgf0djoe[] = "compiler/check/mathvec.gm";
const char gm_srciagdq75um1d5[] = "compiler/doc/collect.gm";
const char gm_srcl43af23eher3[] = "compiler/check/fnlit.gm";
const char gm_srcn6q5fuum979p[] = "compiler/syntax/dump.gm";
const char gm_srco9vbah234t6e[] = "compiler/lint/checked.gm";
const char gm_srcqp133sovb148[] = "compiler/repl/session.gm";
const char gm_srcrorgjlg51brd[] = "compiler/check/generic.gm";
const char gm_srcush70170kjg3[] = "compiler/sema/dump.gm";
const gm_spec *const gm_tpl02lckpq8ae01_s[] = {NULL, NULL};
const gm_spec *const gm_tpl04hr2npsr626_s[] = {NULL};
const gm_spec *const gm_tpl06vq09a1bq44_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl0eln6evo1vf9_s[] = {NULL};
const gm_spec *const gm_tpl0lggla435q5r_s[] = {NULL};
const gm_spec *const gm_tpl0uott3s3afvp_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl14n6ndgi62j1_s[] = {NULL, NULL};
const gm_spec *const gm_tpl150n44984e0j_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl1f5t4gadpj5n_s[] = {NULL};
const gm_spec *const gm_tpl1jgrbdjt2sm3_s[] = {NULL};
const gm_spec *const gm_tpl2d8hdhnr24pd_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl2jsq89buriv9_s[] = {NULL};
const gm_spec *const gm_tpl2lqr5lnbpsvm_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl2od99j0nh2ah_s[] = {NULL};
const gm_spec *const gm_tpl2qrjfa8j0fl5_s[] = {NULL, NULL};
const gm_spec *const gm_tpl2r84u6vnc58k_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl3dghmndqn708_s[] = {NULL};
const gm_spec *const gm_tpl499l2fj2qs8e_s[] = {NULL};
const gm_spec *const gm_tpl4lt7abhi3ej4_s[] = {NULL};
const gm_spec *const gm_tpl5536lp5da6eq_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl56vmcuv96cg9_s[] = {NULL, NULL};
const gm_spec *const gm_tpl5fhcgp5i42cd_s[] = {NULL};
const gm_spec *const gm_tpl5prdhjdrg194_s[] = {&(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1}, NULL};
const gm_spec *const gm_tpl5rdj46lif6eq_s[] = {NULL, NULL};
const gm_spec *const gm_tpl5uqj3jbiq5f0_s[] = {NULL};
const gm_spec *const gm_tpl6104ee3qfshr_s[] = {NULL};
const gm_spec *const gm_tpl63iuq3je4sbr_s[] = {NULL};
const gm_spec *const gm_tpl65nnuqtjd24o_s[] = {NULL};
const gm_spec *const gm_tpl7dh44jmr11le_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl7hvv6i3t3o0k_s[] = {NULL, NULL};
const gm_spec *const gm_tpl83abvjjf3qd1_s[] = {NULL, NULL};
const gm_spec *const gm_tpl8lt5kipnmd64_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl8nmc259qeh4g_s[] = {NULL, NULL};
const gm_spec *const gm_tpl8ocve2bpsu72_s[] = {NULL};
const gm_spec *const gm_tpl8vbes2v6184u_s[] = {NULL};
const gm_spec *const gm_tpla93ou7df923g_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplaahjt316h1v3_s[] = {NULL};
const gm_spec *const gm_tplaemahh05v026_s[] = {NULL};
const gm_spec *const gm_tplaggdk85jgg2m_s[] = {NULL, NULL};
const gm_spec *const gm_tplahe50smef36f_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplaoj5r4tauejk_s[] = {NULL};
const gm_spec *const gm_tplb899rpghqa93_s[] = {NULL, NULL};
const gm_spec *const gm_tplbu8sthucqj1b_s[] = {NULL};
const gm_spec *const gm_tplc0d2186db2jt_s[] = {NULL};
const gm_spec *const gm_tplc1tlvqjnir84_s[] = {NULL};
const gm_spec *const gm_tplcn6c0fsph4j2_s[] = {&(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1}};
const gm_spec *const gm_tpldjbe17jhbkpg_s[] = {NULL, NULL};
const gm_spec *const gm_tpldlrsentitfg3_s[] = {NULL, NULL};
const gm_spec *const gm_tpldtv4ooqtkvgm_s[] = {&(gm_spec){32, 0, 0, 0, 0, 0, 0u, 2}};
const gm_spec *const gm_tplenpf051q0m4n_s[] = {&(gm_spec){32, 0, 0, 0, 0, 0, 0u, 1}, NULL};
const gm_spec *const gm_tpleoop2177apvc_s[] = {NULL, NULL};
const gm_spec *const gm_tplf3e41f3siie3_s[] = {&(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1}, NULL, NULL};
const gm_spec *const gm_tplf579fe8ol0qp_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplfcj32ttpsd6a_s[] = {NULL, NULL};
const gm_spec *const gm_tplfqk35ejq39la_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplfrm74l83ht8l_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplg4866gcgr837_s[] = {NULL};
const gm_spec *const gm_tplgcrr0tu9874k_s[] = {NULL};
const gm_spec *const gm_tplge1jgdcvfbq1_s[] = {NULL};
const gm_spec *const gm_tplgj9d3igk4i62_s[] = {NULL};
const gm_spec *const gm_tplhgdgs8lv4h7d_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplicuqpjvsak60_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplidcjkf79g8le_s[] = {NULL};
const gm_spec *const gm_tpliig5tr0gjn5a_s[] = {NULL, NULL};
const gm_spec *const gm_tplilne6f8eq90i_s[] = {NULL};
const gm_spec *const gm_tpljc7kqi31f4d2_s[] = {NULL};
const gm_spec *const gm_tpljf9iocqf0fr6_s[] = {NULL};
const gm_spec *const gm_tpljnlsd7076tqd_s[] = {NULL, NULL};
const gm_spec *const gm_tplkjc1g73er3ub_s[] = {NULL};
const gm_spec *const gm_tpll6udkhd3c609_s[] = {NULL};
const gm_spec *const gm_tpll733hu18149l_s[] = {NULL, NULL};
const gm_spec *const gm_tpllhk8pmjfso1j_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplljgqo309tg8i_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplm2m182mt5ljj_s[] = {NULL};
const gm_spec *const gm_tplm3f7baid20me_s[] = {NULL, NULL};
const gm_spec *const gm_tplm61d8mlvmepm_s[] = {NULL};
const gm_spec *const gm_tplmekan10d5buf_s[] = {NULL};
const gm_spec *const gm_tplmjku4vg3eupf_s[] = {NULL, NULL};
const gm_spec *const gm_tplnc4en2s3t0da_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplnko388ar3ptd_s[] = {NULL};
const gm_spec *const gm_tplnlqa25lecq48_s[] = {NULL, NULL};
const gm_spec *const gm_tplo6lbijqleti1_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplogq0vb9du4ii_s[] = {NULL, NULL};
const gm_spec *const gm_tplorj2ck0ovl7t_s[] = {NULL};
const gm_spec *const gm_tplovoda71kp04v_s[] = {NULL, NULL};
const gm_spec *const gm_tplqbtvqd3a4g25_s[] = {NULL};
const gm_spec *const gm_tplqd2g84l5ne39_s[] = {NULL, NULL};
const gm_spec *const gm_tplqhugo4i82obv_s[] = {NULL, NULL};
const gm_spec *const gm_tplqjo10upi8bs6_s[] = {NULL};
const gm_spec *const gm_tplre7l7cu4fpbv_s[] = {NULL, NULL};
const gm_spec *const gm_tplrivm1rlf1ps3_s[] = {NULL, NULL};
const gm_spec *const gm_tplrmoc4ag4eqrb_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpls80s4u4ujepk_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplsdamq31ts61q_s[] = {NULL};
const gm_spec *const gm_tplsrms2036pbmr_s[] = {NULL, NULL};
const gm_spec *const gm_tplsrr43aau9tgs_s[] = {NULL};
const gm_spec *const gm_tplt7tm5142l0mh_s[] = {NULL, NULL};
const gm_spec *const gm_tplts1d4nefj5gf_s[] = {NULL, NULL};
const gm_spec *const gm_tplu9669cm9mqja_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpluc9sbr66n218_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplucd2r10hh63v_s[] = {NULL, NULL};
const gm_spec *const gm_tpluhfd3hvppdji_s[] = {NULL, NULL};
const gm_spec *const gm_tpluoji9akrnj9c_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpluue9jh13g9df_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplvc30qsg5hfc6_s[] = {NULL};
const gm_spec *const gm_tplvcccam8nulr1_s[] = {NULL, NULL};
const gm_spec *const gm_tplvonunpuh0kn8_s[] = {NULL};
const gm_tmpl gm_tpl12e8u6hvkejb = {2u, gm_tpl12e8u6hvkejb_p, gm_tpl12e8u6hvkejb_i, gm_tpl12e8u6hvkejb_s};
const gm_tmpl gm_tpl13c887f2o944 = {1u, gm_tpl13c887f2o944_p, gm_tpl13c887f2o944_i, gm_tpl13c887f2o944_s};
const gm_tmpl gm_tpl149k5asebpko = {1u, gm_tpl149k5asebpko_p, gm_tpl149k5asebpko_i, gm_tpl149k5asebpko_s};
const gm_tmpl gm_tpl17l9mok2be4a = {1u, gm_tpl17l9mok2be4a_p, gm_tpl17l9mok2be4a_i, gm_tpl17l9mok2be4a_s};
const gm_tmpl gm_tpl18roma1gvhpk = {1u, gm_tpl18roma1gvhpk_p, gm_tpl18roma1gvhpk_i, gm_tpl18roma1gvhpk_s};
const gm_tmpl gm_tpl1f7c1o805fpf = {1u, gm_tpl1f7c1o805fpf_p, gm_tpl1f7c1o805fpf_i, gm_tpl1f7c1o805fpf_s};
const gm_tmpl gm_tpl1sdu3l536hkg = {5u, gm_tpl1sdu3l536hkg_p, gm_tpl1sdu3l536hkg_i, gm_tpl1sdu3l536hkg_s};
const gm_tmpl gm_tpl29pahnav2feq = {3u, gm_tpl29pahnav2feq_p, gm_tpl29pahnav2feq_i, gm_tpl29pahnav2feq_s};
const gm_tmpl gm_tpl2qrjfa8j0fl5 = {2u, gm_tpl2qrjfa8j0fl5_p, gm_tpl2qrjfa8j0fl5_i, gm_tpl2qrjfa8j0fl5_s};
const gm_tmpl gm_tpl324tc5bgmdkb = {2u, gm_tpl324tc5bgmdkb_p, gm_tpl324tc5bgmdkb_i, gm_tpl324tc5bgmdkb_s};
const gm_tmpl gm_tpl34g57qfcenfh = {1u, gm_tpl34g57qfcenfh_p, gm_tpl34g57qfcenfh_i, gm_tpl34g57qfcenfh_s};
const gm_tmpl gm_tpl3bnq2anocte9 = {1u, gm_tpl3bnq2anocte9_p, gm_tpl3bnq2anocte9_i, gm_tpl3bnq2anocte9_s};
const gm_tmpl gm_tpl3melp777ubs8 = {1u, gm_tpl3melp777ubs8_p, gm_tpl3melp777ubs8_i, gm_tpl3melp777ubs8_s};
const gm_tmpl gm_tpl4bj6lgqq92ev = {2u, gm_tpl4bj6lgqq92ev_p, gm_tpl4bj6lgqq92ev_i, gm_tpl4bj6lgqq92ev_s};
const gm_tmpl gm_tpl4esqdh06rpvt = {2u, gm_tpl4esqdh06rpvt_p, gm_tpl4esqdh06rpvt_i, gm_tpl4esqdh06rpvt_s};
const gm_tmpl gm_tpl570sljg0ivt5 = {3u, gm_tpl570sljg0ivt5_p, gm_tpl570sljg0ivt5_i, gm_tpl570sljg0ivt5_s};
const gm_tmpl gm_tpl58k6ai39p40h = {2u, gm_tpl58k6ai39p40h_p, gm_tpl58k6ai39p40h_i, gm_tpl58k6ai39p40h_s};
const gm_tmpl gm_tpl5g8f9c4f30p4 = {1u, gm_tpl5g8f9c4f30p4_p, gm_tpl5g8f9c4f30p4_i, gm_tpl5g8f9c4f30p4_s};
const gm_tmpl gm_tpl5gt902348obb = {1u, gm_tpl5gt902348obb_p, gm_tpl5gt902348obb_i, gm_tpl5gt902348obb_s};
const gm_tmpl gm_tpl5m0ku1mi16i0 = {2u, gm_tpl5m0ku1mi16i0_p, gm_tpl5m0ku1mi16i0_i, gm_tpl5m0ku1mi16i0_s};
const gm_tmpl gm_tpl5svfna5ij5ui = {3u, gm_tpl5svfna5ij5ui_p, gm_tpl5svfna5ij5ui_i, gm_tpl5svfna5ij5ui_s};
const gm_tmpl gm_tpl6bjoajp2fh2n = {8u, gm_tpl6bjoajp2fh2n_p, gm_tpl6bjoajp2fh2n_i, gm_tpl6bjoajp2fh2n_s};
const gm_tmpl gm_tpl6boglt2rkmh4 = {1u, gm_tpl6boglt2rkmh4_p, gm_tpl6boglt2rkmh4_i, gm_tpl6boglt2rkmh4_s};
const gm_tmpl gm_tpl6cs6ch4uvrua = {1u, gm_tpl6cs6ch4uvrua_p, gm_tpl6cs6ch4uvrua_i, gm_tpl6cs6ch4uvrua_s};
const gm_tmpl gm_tpl6jo4mmkdq2l1 = {1u, gm_tpl6jo4mmkdq2l1_p, gm_tpl6jo4mmkdq2l1_i, gm_tpl6jo4mmkdq2l1_s};
const gm_tmpl gm_tpl6qj65gvm8vp3 = {5u, gm_tpl6qj65gvm8vp3_p, gm_tpl6qj65gvm8vp3_i, gm_tpl6qj65gvm8vp3_s};
const gm_tmpl gm_tpl6r9reg6pnruq = {1u, gm_tpl6r9reg6pnruq_p, gm_tpl6r9reg6pnruq_i, gm_tpl6r9reg6pnruq_s};
const gm_tmpl gm_tpl74mr52kbqst5 = {1u, gm_tpl74mr52kbqst5_p, gm_tpl74mr52kbqst5_i, gm_tpl74mr52kbqst5_s};
const gm_tmpl gm_tpl77gpruiccsl2 = {1u, gm_tpl77gpruiccsl2_p, gm_tpl77gpruiccsl2_i, gm_tpl77gpruiccsl2_s};
const gm_tmpl gm_tpl8s8bnem30e1s = {2u, gm_tpl8s8bnem30e1s_p, gm_tpl8s8bnem30e1s_i, gm_tpl8s8bnem30e1s_s};
const gm_tmpl gm_tpl8tri2h8bpkpg = {1u, gm_tpl8tri2h8bpkpg_p, gm_tpl8tri2h8bpkpg_i, gm_tpl8tri2h8bpkpg_s};
const gm_tmpl gm_tpl955dep3t38r6 = {1u, gm_tpl955dep3t38r6_p, gm_tpl955dep3t38r6_i, gm_tpl955dep3t38r6_s};
const gm_tmpl gm_tpl9ne0ajfec16n = {6u, gm_tpl9ne0ajfec16n_p, gm_tpl9ne0ajfec16n_i, gm_tpl9ne0ajfec16n_s};
const gm_tmpl gm_tpl9sifsarv7t06 = {1u, gm_tpl9sifsarv7t06_p, gm_tpl9sifsarv7t06_i, gm_tpl9sifsarv7t06_s};
const gm_tmpl gm_tpla843kfhqtvit = {2u, gm_tpla843kfhqtvit_p, gm_tpla843kfhqtvit_i, gm_tpla843kfhqtvit_s};
const gm_tmpl gm_tplablvqji1bpu7 = {1u, gm_tplablvqji1bpu7_p, gm_tplablvqji1bpu7_i, gm_tplablvqji1bpu7_s};
const gm_tmpl gm_tplancpnb63qiq5 = {3u, gm_tplancpnb63qiq5_p, gm_tplancpnb63qiq5_i, gm_tplancpnb63qiq5_s};
const gm_tmpl gm_tplaos6tmpdtrie = {2u, gm_tplaos6tmpdtrie_p, gm_tplaos6tmpdtrie_i, gm_tplaos6tmpdtrie_s};
const gm_tmpl gm_tplaq7jlj8i0s87 = {1u, gm_tplaq7jlj8i0s87_p, gm_tplaq7jlj8i0s87_i, gm_tplaq7jlj8i0s87_s};
const gm_tmpl gm_tplb05kqqtmbu7v = {3u, gm_tplb05kqqtmbu7v_p, gm_tplb05kqqtmbu7v_i, gm_tplb05kqqtmbu7v_s};
const gm_tmpl gm_tplb52vln6fobrq = {1u, gm_tplb52vln6fobrq_p, gm_tplb52vln6fobrq_i, gm_tplb52vln6fobrq_s};
const gm_tmpl gm_tplbds86ojmcoik = {2u, gm_tplbds86ojmcoik_p, gm_tplbds86ojmcoik_i, gm_tplbds86ojmcoik_s};
const gm_tmpl gm_tplbk03g4ml40ad = {4u, gm_tplbk03g4ml40ad_p, gm_tplbk03g4ml40ad_i, gm_tplbk03g4ml40ad_s};
const gm_tmpl gm_tplbk1k026jl879 = {2u, gm_tplbk1k026jl879_p, gm_tplbk1k026jl879_i, gm_tplbk1k026jl879_s};
const gm_tmpl gm_tplc1emmpcosf7b = {2u, gm_tplc1emmpcosf7b_p, gm_tplc1emmpcosf7b_i, gm_tplc1emmpcosf7b_s};
const gm_tmpl gm_tpldftt3jdrahlt = {2u, gm_tpldftt3jdrahlt_p, gm_tpldftt3jdrahlt_i, gm_tpldftt3jdrahlt_s};
const gm_tmpl gm_tpldq0sm30o8mph = {2u, gm_tpldq0sm30o8mph_p, gm_tpldq0sm30o8mph_i, gm_tpldq0sm30o8mph_s};
const gm_tmpl gm_tpldqr3c69jgnqi = {1u, gm_tpldqr3c69jgnqi_p, gm_tpldqr3c69jgnqi_i, gm_tpldqr3c69jgnqi_s};
const gm_tmpl gm_tple377dkn6pj7u = {1u, gm_tple377dkn6pj7u_p, gm_tple377dkn6pj7u_i, gm_tple377dkn6pj7u_s};
const gm_tmpl gm_tple7qj0fr2tlub = {1u, gm_tple7qj0fr2tlub_p, gm_tple7qj0fr2tlub_i, gm_tple7qj0fr2tlub_s};
const gm_tmpl gm_tple87ca4est7gh = {1u, gm_tple87ca4est7gh_p, gm_tple87ca4est7gh_i, gm_tple87ca4est7gh_s};
const gm_tmpl gm_tpleonu21sc9fi7 = {2u, gm_tpleonu21sc9fi7_p, gm_tpleonu21sc9fi7_i, gm_tpleonu21sc9fi7_s};
const gm_tmpl gm_tplep2l66hmuvrc = {3u, gm_tplep2l66hmuvrc_p, gm_tplep2l66hmuvrc_i, gm_tplep2l66hmuvrc_s};
const gm_tmpl gm_tplfcgbc9s4hmn7 = {1u, gm_tplfcgbc9s4hmn7_p, gm_tplfcgbc9s4hmn7_i, gm_tplfcgbc9s4hmn7_s};
const gm_tmpl gm_tplfo19v5itil8t = {2u, gm_tplfo19v5itil8t_p, gm_tplfo19v5itil8t_i, gm_tplfo19v5itil8t_s};
const gm_tmpl gm_tplfoosl93e8k5t = {1u, gm_tplfoosl93e8k5t_p, gm_tplfoosl93e8k5t_i, gm_tplfoosl93e8k5t_s};
const gm_tmpl gm_tplfuin6hfnfo1c = {1u, gm_tplfuin6hfnfo1c_p, gm_tplfuin6hfnfo1c_i, gm_tplfuin6hfnfo1c_s};
const gm_tmpl gm_tplg21rd4as3ph6 = {3u, gm_tplg21rd4as3ph6_p, gm_tplg21rd4as3ph6_i, gm_tplg21rd4as3ph6_s};
const gm_tmpl gm_tplg4866gcgr837 = {1u, gm_tplg4866gcgr837_p, gm_tplg4866gcgr837_i, gm_tplg4866gcgr837_s};
const gm_tmpl gm_tplgclcdp539fpt = {2u, gm_tplgclcdp539fpt_p, gm_tplgclcdp539fpt_i, gm_tplgclcdp539fpt_s};
const gm_tmpl gm_tplgcrr0tu9874k = {1u, gm_tplgcrr0tu9874k_p, gm_tplgcrr0tu9874k_i, gm_tplgcrr0tu9874k_s};
const gm_tmpl gm_tplgnobkmf75b9i = {4u, gm_tplgnobkmf75b9i_p, gm_tplgnobkmf75b9i_i, gm_tplgnobkmf75b9i_s};
const gm_tmpl gm_tplgursfbit7uc3 = {2u, gm_tplgursfbit7uc3_p, gm_tplgursfbit7uc3_i, gm_tplgursfbit7uc3_s};
const gm_tmpl gm_tplgv9va17kt7ll = {1u, gm_tplgv9va17kt7ll_p, gm_tplgv9va17kt7ll_i, gm_tplgv9va17kt7ll_s};
const gm_tmpl gm_tplin004j5gtkru = {1u, gm_tplin004j5gtkru_p, gm_tplin004j5gtkru_i, gm_tplin004j5gtkru_s};
const gm_tmpl gm_tpliucrr7pqi5es = {1u, gm_tpliucrr7pqi5es_p, gm_tpliucrr7pqi5es_i, gm_tpliucrr7pqi5es_s};
const gm_tmpl gm_tplj78420ela1cg = {1u, gm_tplj78420ela1cg_p, gm_tplj78420ela1cg_i, gm_tplj78420ela1cg_s};
const gm_tmpl gm_tpljg9idve5dgca = {2u, gm_tpljg9idve5dgca_p, gm_tpljg9idve5dgca_i, gm_tpljg9idve5dgca_s};
const gm_tmpl gm_tpljucl1tv02je2 = {1u, gm_tpljucl1tv02je2_p, gm_tpljucl1tv02je2_i, gm_tpljucl1tv02je2_s};
const gm_tmpl gm_tplk5o80h990o81 = {1u, gm_tplk5o80h990o81_p, gm_tplk5o80h990o81_i, gm_tplk5o80h990o81_s};
const gm_tmpl gm_tplld08lp5kl07i = {1u, gm_tplld08lp5kl07i_p, gm_tplld08lp5kl07i_i, gm_tplld08lp5kl07i_s};
const gm_tmpl gm_tplld9eu0oenqb7 = {6u, gm_tplld9eu0oenqb7_p, gm_tplld9eu0oenqb7_i, gm_tplld9eu0oenqb7_s};
const gm_tmpl gm_tplljgqo309tg8i = {3u, gm_tplljgqo309tg8i_p, gm_tplljgqo309tg8i_i, gm_tplljgqo309tg8i_s};
const gm_tmpl gm_tplloue1uueuiig = {1u, gm_tplloue1uueuiig_p, gm_tplloue1uueuiig_i, gm_tplloue1uueuiig_s};
const gm_tmpl gm_tplm3f7baid20me = {2u, gm_tplm3f7baid20me_p, gm_tplm3f7baid20me_i, gm_tplm3f7baid20me_s};
const gm_tmpl gm_tplm5g4n5l4k6ck = {1u, gm_tplm5g4n5l4k6ck_p, gm_tplm5g4n5l4k6ck_i, gm_tplm5g4n5l4k6ck_s};
const gm_tmpl gm_tplmkgblgbo7imb = {1u, gm_tplmkgblgbo7imb_p, gm_tplmkgblgbo7imb_i, gm_tplmkgblgbo7imb_s};
const gm_tmpl gm_tplmpsv5qagthkk = {1u, gm_tplmpsv5qagthkk_p, gm_tplmpsv5qagthkk_i, gm_tplmpsv5qagthkk_s};
const gm_tmpl gm_tplngekt6vfvnsv = {2u, gm_tplngekt6vfvnsv_p, gm_tplngekt6vfvnsv_i, gm_tplngekt6vfvnsv_s};
const gm_tmpl gm_tplnitaca1dp5li = {2u, gm_tplnitaca1dp5li_p, gm_tplnitaca1dp5li_i, gm_tplnitaca1dp5li_s};
const gm_tmpl gm_tplnko388ar3ptd = {1u, gm_tplnko388ar3ptd_p, gm_tplnko388ar3ptd_i, gm_tplnko388ar3ptd_s};
const gm_tmpl gm_tplnrfprs01gfmj = {1u, gm_tplnrfprs01gfmj_p, gm_tplnrfprs01gfmj_i, gm_tplnrfprs01gfmj_s};
const gm_tmpl gm_tplntci9ovg2hdj = {1u, gm_tplntci9ovg2hdj_p, gm_tplntci9ovg2hdj_i, gm_tplntci9ovg2hdj_s};
const gm_tmpl gm_tplo203818ggfa8 = {1u, gm_tplo203818ggfa8_p, gm_tplo203818ggfa8_i, gm_tplo203818ggfa8_s};
const gm_tmpl gm_tplo4cs7gli8ts2 = {3u, gm_tplo4cs7gli8ts2_p, gm_tplo4cs7gli8ts2_i, gm_tplo4cs7gli8ts2_s};
const gm_tmpl gm_tplo5614u5649ka = {3u, gm_tplo5614u5649ka_p, gm_tplo5614u5649ka_i, gm_tplo5614u5649ka_s};
const gm_tmpl gm_tploa995k06npbj = {2u, gm_tploa995k06npbj_p, gm_tploa995k06npbj_i, gm_tploa995k06npbj_s};
const gm_tmpl gm_tplodcipjau6eqr = {2u, gm_tplodcipjau6eqr_p, gm_tplodcipjau6eqr_i, gm_tplodcipjau6eqr_s};
const gm_tmpl gm_tplogq0vb9du4ii = {2u, gm_tplogq0vb9du4ii_p, gm_tplogq0vb9du4ii_i, gm_tplogq0vb9du4ii_s};
const gm_tmpl gm_tploj4uaptg6qnk = {2u, gm_tploj4uaptg6qnk_p, gm_tploj4uaptg6qnk_i, gm_tploj4uaptg6qnk_s};
const gm_tmpl gm_tplomrgvu31uqof = {1u, gm_tplomrgvu31uqof_p, gm_tplomrgvu31uqof_i, gm_tplomrgvu31uqof_s};
const gm_tmpl gm_tplpehi2hdi7j7m = {3u, gm_tplpehi2hdi7j7m_p, gm_tplpehi2hdi7j7m_i, gm_tplpehi2hdi7j7m_s};
const gm_tmpl gm_tplpgeniqejbt43 = {3u, gm_tplpgeniqejbt43_p, gm_tplpgeniqejbt43_i, gm_tplpgeniqejbt43_s};
const gm_tmpl gm_tplplkqcb01ocrd = {1u, gm_tplplkqcb01ocrd_p, gm_tplplkqcb01ocrd_i, gm_tplplkqcb01ocrd_s};
const gm_tmpl gm_tplq2jm9n7vekeo = {1u, gm_tplq2jm9n7vekeo_p, gm_tplq2jm9n7vekeo_i, gm_tplq2jm9n7vekeo_s};
const gm_tmpl gm_tplqp3jlt332oq5 = {1u, gm_tplqp3jlt332oq5_p, gm_tplqp3jlt332oq5_i, gm_tplqp3jlt332oq5_s};
const gm_tmpl gm_tplqpg9790rv4ki = {1u, gm_tplqpg9790rv4ki_p, gm_tplqpg9790rv4ki_i, gm_tplqpg9790rv4ki_s};
const gm_tmpl gm_tplr1if0cs3oolt = {1u, gm_tplr1if0cs3oolt_p, gm_tplr1if0cs3oolt_i, gm_tplr1if0cs3oolt_s};
const gm_tmpl gm_tplr7vr7aubja28 = {1u, gm_tplr7vr7aubja28_p, gm_tplr7vr7aubja28_i, gm_tplr7vr7aubja28_s};
const gm_tmpl gm_tplrvl1hqu8hbkh = {2u, gm_tplrvl1hqu8hbkh_p, gm_tplrvl1hqu8hbkh_i, gm_tplrvl1hqu8hbkh_s};
const gm_tmpl gm_tpls5kpq7mskrde = {1u, gm_tpls5kpq7mskrde_p, gm_tpls5kpq7mskrde_i, gm_tpls5kpq7mskrde_s};
const gm_tmpl gm_tplsfrupsaoln3v = {3u, gm_tplsfrupsaoln3v_p, gm_tplsfrupsaoln3v_i, gm_tplsfrupsaoln3v_s};
const gm_tmpl gm_tplsil8jb9ng0ao = {1u, gm_tplsil8jb9ng0ao_p, gm_tplsil8jb9ng0ao_i, gm_tplsil8jb9ng0ao_s};
const gm_tmpl gm_tplsogg2sp0m9p9 = {2u, gm_tplsogg2sp0m9p9_p, gm_tplsogg2sp0m9p9_i, gm_tplsogg2sp0m9p9_s};
const gm_tmpl gm_tplt392n8hqfhn9 = {1u, gm_tplt392n8hqfhn9_p, gm_tplt392n8hqfhn9_i, gm_tplt392n8hqfhn9_s};
const gm_tmpl gm_tpltdh9l4qjtpek = {1u, gm_tpltdh9l4qjtpek_p, gm_tpltdh9l4qjtpek_i, gm_tpltdh9l4qjtpek_s};
const gm_tmpl gm_tpltng0q64u6p80 = {2u, gm_tpltng0q64u6p80_p, gm_tpltng0q64u6p80_i, gm_tpltng0q64u6p80_s};
const gm_tmpl gm_tpltnu092iqmfdd = {9u, gm_tpltnu092iqmfdd_p, gm_tpltnu092iqmfdd_i, gm_tpltnu092iqmfdd_s};
const gm_tmpl gm_tplu272587ugnvi = {2u, gm_tplu272587ugnvi_p, gm_tplu272587ugnvi_i, gm_tplu272587ugnvi_s};
const gm_tmpl gm_tplugm36655v1ns = {1u, gm_tplugm36655v1ns_p, gm_tplugm36655v1ns_i, gm_tplugm36655v1ns_s};
const gm_tmpl gm_tplvi7pmohm1ihu = {1u, gm_tplvi7pmohm1ihu_p, gm_tplvi7pmohm1ihu_i, gm_tplvi7pmohm1ihu_s};
const gm_tmpl_piece gm_tpl01fkj9ukhaa3_p[] = {{gm_s9uf4t24m7c3t, 30}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl07e4gjk0ub6d_p[] = {{gm_sj9co2vp4uudk, 12}, {gm_snenkhfev166s, 2}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl09kglfpefe8i_p[] = {{gm_s4v9747a9b85a, 10}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl10tenob4hk5k_p[] = {{gm_sd5giovmg2s7p, 16}, {gm_s9p1knha37i5h, 26}, {gm_s4hv5fsmnuq3r, 14}};
const gm_tmpl_piece gm_tpl16gsv91msflc_p[] = {{gm_sce79e7st00ad, 3}, {gm_sie82oc0ua5dm, 2}};
const gm_tmpl_piece gm_tpl1a3usdkusvtq_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s9g30d9lksra5, 2}, {gm_s8p032eorpf80, 2}};
const gm_tmpl_piece gm_tpl1q6o13b940dg_p[] = {{gm_smi31ifpstnmf, 8}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tpl1umecooe9oti_p[] = {{gm_sa5lo120id503, 25}, {gm_st2vlvkf674c2, 249}};
const gm_tmpl_piece gm_tpl2r84u6vnc58k_p[] = {{gm_sdhddtlu453l5, 12}, {gm_sjdk7e5fs4top, 5}, {gm_s2bnc7q68giqi, 16}, {gm_st8h6qvqu7ddb, 29}, {gm_sfurr787jvkcg, 5}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tpl33em4maossan_p[] = {{gm_s07uhsrd798kj, 8}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tpl34tavcs9635o_p[] = {{gm_sv1f0l9ks3lvu, 24}, {gm_spni6j7splqto, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl386j11hdkrib_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl3ergg6mce23h_p[] = {{gm_s1b80v2hb8d4l, 71}, {gm_s1ec711cn6c5f, 32}, {gm_spbhhmdbp335u, 3}};
const gm_tmpl_piece gm_tpl3pvc6fn0aqps_p[] = {{gm_s81f5igr577he, 13}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl43224j8old0s_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_snja4gioias3h, 18}};
const gm_tmpl_piece gm_tpl461nvut9qmgp_p[] = {{gm_snbque26o7lbi, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl46u099ch4aa2_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sdealia24icjj, 39}};
const gm_tmpl_piece gm_tpl499l2fj2qs8e_p[] = {{gm_su71itml70ikf, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl4clb07vpsm45_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s2c5ns5u9qs90, 34}, {gm_sfusuld9q5ekv, 43}};
const gm_tmpl_piece gm_tpl4ht4dp62es1n_p[] = {{gm_svv63hse0pejb, 17}, {gm_smaq5hrff6a5d, 55}};
const gm_tmpl_piece gm_tpl4iu2nfn1okgb_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s19d3jvdogm6m, 6}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tpl5g8f9c4f30p4_p[] = {{gm_sptnm8c6c52dr, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl5kl598jq5h12_p[] = {{gm_spm9ae4g2b2fd, 13}, {gm_stshdb5m6rl63, 4}, {gm_skfdo9qggtrbp, 5}, {gm_sqrh6i8djv6b4, 6}, {gm_s6s4ngfaj87vv, 4}, {gm_skfdo9qggtrbp, 5}, {gm_sqrh6i8djv6b4, 6}, {gm_snr5o5tak6405, 3}};
const gm_tmpl_piece gm_tpl64ltc7suc5qg_p[] = {{gm_smooaouqmm8dj, 1}, {gm_sr4qbrk6tlgaa, 0}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl6gp425a13qgr_p[] = {{gm_sk754mj3p1el6, 1}, {gm_s2053hhvf16fm, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl6salfcvi63n2_p[] = {{gm_ssud7c614kqjt, 4}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl737bm31hf71o_p[] = {{gm_s3plkb3in5251, 8}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpl7bbfeces83t7_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_smooaouqmm8dj, 1}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl7dh44jmr11le_p[] = {{gm_so9fi6r28770a, 2}, {gm_s1asl1e1tvb0k, 1}, {gm_srv4dcfccuset, 2}, {gm_sr4qbrk6tlgaa, 0}, {gm_sg5gdnrith2i6, 2}, {gm_shrelr880pj0m, 6}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl7kp6007qjdtb_p[] = {{gm_s4g35ls4ma80g, 12}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl7ut0r685hcaa_p[] = {{gm_smrc1u3r4i5hd, 6}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tpl8s8bnem30e1s_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sekdm5lqg7176, 42}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl941gb7vu8kgv_p[] = {{gm_sl935l3jslu0o, 27}, {gm_seg7e1ca8e64l, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl9kch76oh20pa_p[] = {{gm_scotq6054g01h, 5}, {gm_sr00p3fdg5qn3, 18}, {gm_sfrdbcc53tuv9, 17}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl9kqhb5r0uube_p[] = {{gm_snh0sl2cdagdn, 12}, {gm_s489g6hodd4uq, 43}};
const gm_tmpl_piece gm_tpl9red218f96cc_p[] = {{gm_sisa53plcfedi, 13}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl9v8qcr7jilen_p[] = {{gm_s2m4r2316r14d, 10}, {gm_se0oi28sapu01, 10}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplablvqji1bpu7_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s9indmpqvpug4, 57}};
const gm_tmpl_piece gm_tplb2gsdj8iioqj_p[] = {{gm_sa8k1i3b5phk8, 23}, {gm_stk9qd6rl9jdj, 20}, {gm_sljdair45oqft, 30}};
const gm_tmpl_piece gm_tplb52vln6fobrq_p[] = {{gm_sqin9fcebg38h, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplbfdkthp45ai4_p[] = {{gm_s2rbapbbig1pe, 18}, {gm_s0m0qaokk51g0, 3}, {gm_sr26j6lp0gfqo, 10}, {gm_smooaouqmm8dj, 1}, {gm_st9gkvtvi92qe, 16}, {gm_suftl0kath3th, 20}, {gm_s0m0qaokk51g0, 3}, {gm_spnr89edksdqp, 3}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tplbh7305mc9stu_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s33ft5ck7snhl, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplbj3gvifc61li_p[] = {{gm_svdi2pk0uienp, 44}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplbk128im3qlnl_p[] = {{gm_sc4t0u3ku1fbu, 22}, {gm_so514oj5nbhcu, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplbr0fi0201tmv_p[] = {{gm_sluvntf7gk99f, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplcn6c0fsph4j2_p[] = {{gm_sm7cboss659pv, 50}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpld1hhk0kef14l_p[] = {{gm_scp8gj1kjauec, 28}, {gm_slm25hfg2udo4, 7}};
const gm_tmpl_piece gm_tpldm0f92v61rgi_p[] = {{gm_scavc1uf2hs8l, 6}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpldn8tr7ddtik6_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}, {gm_s0ucactj6hj0a, 3}};
const gm_tmpl_piece gm_tpldqjt9kdk4ntt_p[] = {{gm_siamquc19uikr, 1}, {gm_sao4mumvadoct, 3}};
const gm_tmpl_piece gm_tpldt4nur6h705h_p[] = {{gm_s0m3gh47554ab, 5}, {gm_sfdbfrv46lihd, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tple2rp5rj93mkj_p[] = {{gm_sjvb17co9jlff, 14}, {gm_se7oqjo2sonp9, 8}};
const gm_tmpl_piece gm_tple4r1aumd134l_p[] = {{gm_siamquc19uikr, 1}, {gm_sveol5dvm5sd7, 19}};
const gm_tmpl_piece gm_tple75bobdc6q0g_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sl3q52ep6aa53, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tple87ca4est7gh_p[] = {{gm_su28d8kir9fan, 9}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplf3e41f3siie3_p[] = {{gm_su6oav9vpqcnu, 16}, {gm_sbdfikdtgvq9q, 27}, {gm_sprj2pveuq1p8, 12}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplf48rihumtlpr_p[] = {{gm_snku1611u1u7p, 8}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplf9434bnahqi1_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_ss558hlhv1kah, 30}};
const gm_tmpl_piece gm_tplfcj32ttpsd6a_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s4tgvb7s2dbuj, 19}, {gm_shp2btrg6e14g, 28}};
const gm_tmpl_piece gm_tplfjobpnodgc6e_p[] = {{gm_siamquc19uikr, 1}, {gm_sequ7jfvtub5t, 9}, {gm_s3p0rbs9hc3lg, 1}, {gm_snenkhfev166s, 2}, {gm_sr4qbrk6tlgaa, 0}, {gm_ssqifgr2icd6s, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplfjoq6lkhl94q_p[] = {{gm_sirn08c749sdd, 27}, {gm_svp314e9ficjs, 13}, {gm_s0m0qaokk51g0, 3}, {gm_sj62oio2kg58e, 19}};
const gm_tmpl_piece gm_tplfvv1qe6os0le_p[] = {{gm_s6khfdbr5aof9, 33}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplggnuionimjk9_p[] = {{gm_s6u49d04cqlg1, 16}, {gm_s33h2lce46bnm, 8}, {gm_s4acc6d5101lp, 44}};
const gm_tmpl_piece gm_tplgism5t1v67p6_p[] = {{gm_s60adpphft7s5, 4}, {gm_scs1evojsik8q, 3}, {gm_snukl6jqep6jf, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplh64dsjnr7d93_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_smh12ad93nju6, 44}, {gm_s23t712hkqlmp, 4}, {gm_smooaouqmm8dj, 1}, {gm_ssbqeok4607ju, 24}};
const gm_tmpl_piece gm_tplh7231rl9trm2_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s41047ga9d9tq, 25}, {gm_sg5gdnrith2i6, 2}, {gm_s87isqt5t284q, 8}};
const gm_tmpl_piece gm_tplhghj93ore8u2_p[] = {{gm_sd5g4r2t5gcbk, 24}, {gm_svo0v47tsnb8b, 13}};
const gm_tmpl_piece gm_tplht122d0ji4km_p[] = {{gm_seqji52qmi3ea, 3}, {gm_sao4mumvadoct, 3}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tpli2ivqo4qj5ok_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s32blj9c3tc60, 10}, {gm_sr4qbrk6tlgaa, 0}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpli4rfoi75onhj_p[] = {{gm_so91sclkgrupd, 32}, {gm_sueo2nge7h013, 13}, {gm_ss8vfi8kj31rc, 5}};
const gm_tmpl_piece gm_tpli7hldrrhg7sr_p[] = {{gm_s4dgh055v8im1, 42}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplicuqpjvsak60_p[] = {{gm_sdak0upmjiivc, 14}, {gm_s1gm12po6spua, 17}, {gm_sfknvqdnjah7l, 17}, {gm_si68mvbt2sjam, 18}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tplisurri6t3aun_p[] = {{gm_s2umangg4hn1g, 14}, {gm_scj5qlamj2bll, 38}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpljp2husef3b2l_p[] = {{gm_sekmurq2ua2sf, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpljpjg7sv41er0_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sdg7s5c89ve6u, 63}};
const gm_tmpl_piece gm_tplk1dctbs79mmk_p[] = {{gm_spffvcegd6os6, 5}, {gm_s5el4up5fp2qc, 44}, {gm_scq06n33arhgv, 4}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplk5o80h990o81_p[] = {{gm_sihb07k8fk2nc, 34}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplk5t5jhqqvnff_p[] = {{gm_se09jntm6upln, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpllp752obmvr55_p[] = {{gm_snf9ak4s55usv, 17}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplm7tetbq1snr9_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s1vcu5tk6vbma, 66}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplmm5mgnjalbfr_p[] = {{gm_sl628i1qguhss, 12}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpln4hlddpv31vi_p[] = {{gm_shpgcu8us1vm6, 10}, {gm_sao4mumvadoct, 3}, {gm_st5ngsmk07040, 7}, {gm_sfm8ps2e26dh1, 2}};
const gm_tmpl_piece gm_tplncm14tpa4cdg_p[] = {{gm_snb1pvju20aqn, 6}, {gm_s3p65h6c1goi8, 7}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplngsd5kcuu6fv_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_slgmqgkioa4u6, 2}};
const gm_tmpl_piece gm_tplnnqg0hvtcqac_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_srjkl5qvdutgq, 4}};
const gm_tmpl_piece gm_tplo2fstigc8bcf_p[] = {{gm_sns1hl83hdlcn, 4}, {gm_snqbh8eqcn135, 16}, {gm_spts1f8sg22km, 17}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplom6fga2t4kfv_p[] = {{gm_sio8t6i3tf974, 10}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplovlajhkvntql_p[] = {{gm_s6nqhdcqlaqpu, 43}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplp98fsht4k706_p[] = {{gm_snf877edekrff, 10}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplpgeniqejbt43_p[] = {{gm_som0q53jqvjh3, 4}, {gm_sr4qbrk6tlgaa, 0}, {gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplpodff9060oeo_p[] = {{gm_sjds8o3l7mmnl, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplpqtqehhhq7oj_p[] = {{gm_sbvo1o52epaqq, 3}, {gm_skfks8nrdfdgv, 4}, {gm_s3pmm119d0kjq, 15}};
const gm_tmpl_piece gm_tplpvmvg9ce66cu_p[] = {{gm_s1f440st4mnkp, 1}, {gm_sie82oc0ua5dm, 2}};
const gm_tmpl_piece gm_tplqg9a6t74ffg7_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s39mljh669qgl, 1}, {gm_s3p0rbs9hc3lg, 1}};
const gm_tmpl_piece gm_tplqnau1elkkogs_p[] = {{gm_s8vthjuk0el8j, 62}, {gm_s24s9g8r9gc0v, 6}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplqp3jlt332oq5_p[] = {{gm_s2okn0skhqnvu, 31}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplqpg9790rv4ki_p[] = {{gm_sdle4vcf3dhti, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplrcurrnquacuf_p[] = {{gm_sht1158338t3d, 10}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s2u7k18iev3v2, 3}, {gm_s2cuflhqkkuaq, 1}};
const gm_tmpl_piece gm_tplrkvt0j8tqlqg_p[] = {{gm_s4pk73sg6h8qk, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplrqgv2l0evbdg_p[] = {{gm_s6rbdi4l8b3ii, 17}, {gm_sqn4031c223rk, 12}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tplrvl1hqu8hbkh_p[] = {{gm_smooaouqmm8dj, 1}, {gm_st52k2850n5r5, 4}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplsa4s74k8e6ci_p[] = {{gm_sq9jk07jm2tgi, 9}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplsb492k540eai_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_ssmuvuejgvj6b, 26}};
const gm_tmpl_piece gm_tplska2t1f86rr8_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sl1f8ibfoc1i1, 12}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplsrr2cfplv9ff_p[] = {{gm_s6899ib4u9fpt, 23}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplt1555ki49he5_p[] = {{gm_ss9rpos7stctm, 33}, {gm_sli291jkgotin, 30}};
const gm_tmpl_piece gm_tpltnu092iqmfdd_p[] = {{gm_slmeqnp27emg7, 8}, {gm_slnqcu8l15gll, 11}, {gm_sa8gjfvck9d40, 29}, {gm_slmnp81e6ev88, 9}, {gm_salbpgh5m2h5m, 14}, {gm_s63011l92pvfr, 39}, {gm_sa16a135q4am6, 12}, {gm_sl7pe4skltkbu, 8}, {gm_s31g9ftlb6lgj, 10}, {gm_saempncqkf91r, 2}};
const gm_tmpl_piece gm_tplu0ijdfgp8vs3_p[] = {{gm_squr90ahaf9ht, 12}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplucd2r10hh63v_p[] = {{gm_sccadmt684uri, 22}, {gm_sgc7crh43q9tj, 5}, {gm_srkbv7g2spehb, 32}};
const gm_tmpl_piece gm_tpluf7u812t4p3d_p[] = {{gm_sbm0k1981kgpb, 39}, {gm_snr5o5tak6405, 3}};
const gm_tmpl_piece gm_tplvkf7arb107q2_p[] = {{gm_s78naq2blbmbt, 31}, {gm_sp1784q48t8s2, 3}};
const gm_tmpl_piece gm_tplvl1jqbb33ag2_p[] = {{gm_suiq4ou0s86ho, 13}, {gm_sr4qbrk6tlgaa, 0}};
const gm_type gm_ty467d5cm1d5 = {.size = 4, .align = 4, .kind = GM_K_INT, .bits = 32, .is_signed = false};
const gm_type gm_ty467d5cm1d5;
const gm_type gm_tyehhdtpeti1 = {.size = 1, .align = 1, .name = "Flavor", .kind = GM_K_UNION, .nvariants = 4, .variants = gm_tyehhdtpeti1_v, .tag_offset = 0, .payload_offset = 0, .tag_size = 1, .bits = 8, .tag_signed = false};
const gm_type gm_tyehhdtpeti1;
const gm_type gm_tyg63o53ar52 = {.size = 12, .align = 4, .name = "Builtin", .kind = GM_K_UNION, .nvariants = 82, .variants = gm_tyg63o53ar52_v, .tag_offset = 0, .payload_offset = 4, .tag_size = 1, .bits = 8, .tag_signed = false};
const gm_type gm_tyg63o53ar52;
const gm_type gm_tyqqajgod2bb = {.size = 8, .align = 8, .kind = GM_K_FLOAT, .bits = 64};
const gm_type gm_tyqqajgod2bb;
const gm_variant gm_tybqb4gb1d92_v[] = {{"Add", 0, &gm_tyehhdtpeti1}, {"Sub", 1, &gm_tyehhdtpeti1}, {"Mul", 2, &gm_tyehhdtpeti1}, {"Shl", 3, &gm_tyehhdtpeti1}, {"Div", 4, NULL}, {"Rem", 5, NULL}, {"Shr", 6, NULL}, {"BitAnd", 7, NULL}, {"BitOr", 8, NULL}, {"BitXor", 9, NULL}, {"Lt", 10, NULL}, {"Le", 11, NULL}, {"Gt", 12, NULL}, {"Ge", 13, NULL}};
const gm_variant gm_tysgeom7733j_v[] = {{"None", 0, NULL}, {"Some", 1, NULL}, {"Ok", 2, NULL}, {"Err", 3, NULL}};
const uint32_t gm_tpl0e4rd6ag9tqa_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl0elencma14kh_i[] = {0u};
const uint32_t gm_tpl0scna75sco5f_i[] = {0u, 1u};
const uint32_t gm_tpl0stjtt2gdn50_i[] = {0u};
const uint32_t gm_tpl10tenob4hk5k_i[] = {0u, 1u};
const uint32_t gm_tpl128vde47jcsu_i[] = {0u};
const uint32_t gm_tpl1blafg62e77e_i[] = {0u, 1u};
const uint32_t gm_tpl1buo4rn7srlb_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpl1hek9uhvdtmo_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpl1nqa0en23gvp_i[] = {0u};
const uint32_t gm_tpl29pahnav2feq_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl2a8tmpvk11vt_i[] = {0u, 1u};
const uint32_t gm_tpl2od99j0nh2ah_i[] = {0u};
const uint32_t gm_tpl324tc5bgmdkb_i[] = {0u, 1u};
const uint32_t gm_tpl32o7ct8vibbi_i[] = {0u};
const uint32_t gm_tpl34tavcs9635o_i[] = {0u, 1u};
const uint32_t gm_tpl3cldid2v21cr_i[] = {0u, 1u};
const uint32_t gm_tpl3jlrgt0cfgj3_i[] = {0u};
const uint32_t gm_tpl3qt6ss8l9cgf_i[] = {0u};
const uint32_t gm_tpl46u099ch4aa2_i[] = {0u};
const uint32_t gm_tpl4bj6lgqq92ev_i[] = {0u, 1u};
const uint32_t gm_tpl4j60geuv9mvj_i[] = {0u};
const uint32_t gm_tpl4sdckuvlf4dl_i[] = {0u};
const uint32_t gm_tpl500h9j75csqa_i[] = {0u};
const uint32_t gm_tpl5jb9rn2u95g6_i[] = {0u, 1u};
const uint32_t gm_tpl5jffirbo3a3v_i[] = {0u};
const uint32_t gm_tpl5m0ku1mi16i0_i[] = {0u, 1u};
const uint32_t gm_tpl5svfna5ij5ui_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl5vbp9fc0mpc1_i[] = {0u};
const uint32_t gm_tpl6186pbdul6ge_i[] = {0u, 1u};
const uint32_t gm_tpl6cs6ch4uvrua_i[] = {0u};
const uint32_t gm_tpl77hog51tnj6o_i[] = {0u};
const uint32_t gm_tpl7ll3166bkl17_i[] = {0u, 1u};
const uint32_t gm_tpl7ufg84gs1lc7_i[] = {0u, 1u};
const uint32_t gm_tpl82nt5tav1er7_i[] = {0u};
const uint32_t gm_tpl8qng452btr2a_i[] = {0u, 1u};
const uint32_t gm_tpl8tri2h8bpkpg_i[] = {0u};
const uint32_t gm_tpl9jrfnomsrfk1_i[] = {0u, 1u};
const uint32_t gm_tpl9kqhb5r0uube_i[] = {0u};
const uint32_t gm_tpl9kt3nn7p3a92_i[] = {0u};
const uint32_t gm_tpla67mmq1id2h3_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
const uint32_t gm_tplai4t67ppan77_i[] = {0u};
const uint32_t gm_tplalu8vhij0mma_i[] = {0u};
const uint32_t gm_tplau4cbc1piuvn_i[] = {0u, 1u, 2u};
const uint32_t gm_tplav4294nbj9c7_i[] = {0u};
const uint32_t gm_tplb043ibme104j_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
const uint32_t gm_tplb9i7jfun8rvg_i[] = {0u};
const uint32_t gm_tplbo3vrh9egnst_i[] = {0u, 1u};
const uint32_t gm_tplbu8sthucqj1b_i[] = {0u};
const uint32_t gm_tplc0jhs849b438_i[] = {0u};
const uint32_t gm_tplc1tlvqjnir84_i[] = {0u};
const uint32_t gm_tplceihv99elj2t_i[] = {0u, 1u};
const uint32_t gm_tplcouqld11oeil_i[] = {0u};
const uint32_t gm_tplcttkqubjcaqf_i[] = {0u};
const uint32_t gm_tpld9ts015fint3_i[] = {0u};
const uint32_t gm_tpldknbfev12jrb_i[] = {0u, 1u, 2u};
const uint32_t gm_tple7i0r0sg6sjs_i[] = {0u};
const uint32_t gm_tple87ca4est7gh_i[] = {0u};
const uint32_t gm_tplepdvp8ofjal7_i[] = {0u};
const uint32_t gm_tples1m1m9iv6b0_i[] = {0u, 1u, 2u, 3u, 4u, 5u};
const uint32_t gm_tplf0gekbh2mtku_i[] = {0u};
const uint32_t gm_tplf9jp9gfh0eik_i[] = {0u};
const uint32_t gm_tplft0ecs37svcn_i[] = {0u, 1u};
const uint32_t gm_tplh6c513o0038j_i[] = {0u};
const uint32_t gm_tplhciekfbsqng7_i[] = {0u};
const uint32_t gm_tplhkjcpttbsqt0_i[] = {0u};
const uint32_t gm_tplhos3sjrj7v9v_i[] = {0u};
const uint32_t gm_tpli2ivqo4qj5ok_i[] = {0u, 1u, 2u};
const uint32_t gm_tpli2qk88n6tll4_i[] = {0u};
const uint32_t gm_tplidcjkf79g8le_i[] = {0u};
const uint32_t gm_tplikisll89mgrm_i[] = {0u};
const uint32_t gm_tplim7pg0q4u4tg_i[] = {0u};
const uint32_t gm_tplivip1s1762gt_i[] = {0u};
const uint32_t gm_tpljt2av4h7c132_i[] = {0u};
const uint32_t gm_tplkji0sd9phcb9_i[] = {0u};
const uint32_t gm_tplkv0rjh54m5gn_i[] = {0u};
const uint32_t gm_tpll487d6t52m4g_i[] = {0u};
const uint32_t gm_tpllnpq25q3a5a4_i[] = {0u, 1u};
const uint32_t gm_tplm3a458dq976v_i[] = {0u};
const uint32_t gm_tplm3t9qdrf37sd_i[] = {0u};
const uint32_t gm_tplm61d8mlvmepm_i[] = {0u};
const uint32_t gm_tplm9hj31tk9ip4_i[] = {0u};
const uint32_t gm_tplnnqg0hvtcqac_i[] = {0u};
const uint32_t gm_tplntci9ovg2hdj_i[] = {0u};
const uint32_t gm_tplo2fstigc8bcf_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tplo867268s51oj_i[] = {0u};
const uint32_t gm_tplod1bbc3c9s5j_i[] = {0u, 1u, 2u};
const uint32_t gm_tploh8l9phm91d3_i[] = {0u};
const uint32_t gm_tplp4f8nloo6fdo_i[] = {0u};
const uint32_t gm_tplp93m8c6u3nrp_i[] = {0u, 1u};
const uint32_t gm_tplpe5hifub5ms7_i[] = {0u, 1u, 2u};
const uint32_t gm_tplpjjba8pqbhe2_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplpkvu14cg936d_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tplpntqck8i8f5b_i[] = {0u};
const uint32_t gm_tplq11qqgpg1tv1_i[] = {0u, 1u, 2u};
const uint32_t gm_tplq75gqr5tvlmd_i[] = {0u, 1u};
const uint32_t gm_tplqstpsk3jkk1c_i[] = {0u};
const uint32_t gm_tplr8vkqgn7raqo_i[] = {0u, 1u};
const uint32_t gm_tplrodjuj3fgmsa_i[] = {0u, 1u};
const uint32_t gm_tplrsdjn4cifhr9_i[] = {0u};
const uint32_t gm_tpls2ou5a11vcn7_i[] = {0u, 1u};
const uint32_t gm_tpls8octb03drbp_i[] = {0u};
const uint32_t gm_tplsa4s74k8e6ci_i[] = {0u};
const uint32_t gm_tplsc9pqkp31v7e_i[] = {0u};
const uint32_t gm_tplsnlr78cvfrao_i[] = {0u};
const uint32_t gm_tplt320ru9bfpdd_i[] = {0u};
const uint32_t gm_tplteu0ijrjjsvv_i[] = {0u};
const uint32_t gm_tpltnckk4rc73ud_i[] = {0u};
const uint32_t gm_tplu49icuspir05_i[] = {0u, 1u};
const uint32_t gm_tpludt9aee4n14r_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplv8s8fgl2lggu_i[] = {0u};
const uint32_t gm_tplvcuursopmobk_i[] = {0u};
const uint32_t gm_tplvqpq1chrhvno_i[] = {0u};
const uint8_t gm_s000up2l16pib[9] = "unary {}";
const uint8_t gm_s01ekgjjrfehv[13] = "gm_main_call";
const uint8_t gm_s01k0p56k10gk[29] = "cannot cast `{}` to `{}`: {}";
const uint8_t gm_s02aodrl2fpra[12] = "cannot use ";
const uint8_t gm_s0adus7j7p0g0[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000H\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_s0b795193rvs2[7] = "` has ";
const uint8_t gm_s0cf8090s33vc[30] = "`pub` cannot be applied to {}";
const uint8_t gm_s0dhnude2n5fe[8] = "{} tag;";
const uint8_t gm_s0eudsv4chfi5[44] = "` has a single payload; use one sub-pattern";
const uint8_t gm_s0hcm1974rbe7[34] = "the parameter `{}` is given twice";
const uint8_t gm_s0k04b3r99uqd[10] = "#vec [{}]";
const uint8_t gm_s0kbj4moi16fu[16] = "lex_field_index";
const uint8_t gm_s0kf3vtbitlnb[58] = "a math vector's elements are integers or floats, not `{}`";
const uint8_t gm_s0pi5sf493jqq[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000u\000\000\000\025\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_s0s3f0v5p7674[27] = "((void *)(uintptr_t){}ULL)";
const uint8_t gm_s0t5omnh472sf[21] = "` has no doc comment";
const uint8_t gm_s0uoh0jkm28kv[24] = "{} gm_r; {}; return {};";
const uint8_t gm_s12b39d0rgdtc[56] = "`: it converts to either kind of function type (\302\24716.2)";
const uint8_t gm_s13jru0todls1[38] = "{} needs a concrete {} type, not `{}`";
const uint8_t gm_s19069udlvhni[9] = "ProcKill";
const uint8_t gm_s199bfpbr16jo[44] = "no enclosing loop or `with` is labeled `{}`";
const uint8_t gm_s1af5e7vkvjv9[8] = "block @";
const uint8_t gm_s1bp6pdherlbe[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000\263\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s1eo6mkdbgh2b[38] = "the effect `{}` has no operation `{}`";
const uint8_t gm_s1jkh3mpunvd6[64] = "`#{}` cannot precede an array type; only `#simd` and `#vec` can";
const uint8_t gm_s1lh34nabv9g2[5] = "--> ";
const uint8_t gm_s1m38bo3naic1[8] = "binding";
const uint8_t gm_s1o778dp7d59d[43] = "`{}` is private to the module `{}` (\302\2473.3)";
const uint8_t gm_s1p0thm43h6d6[21] = "method not found: {}";
const uint8_t gm_s1smed2pig5ts[35] = "unexpected whitespace character U+";
const uint8_t gm_s1vlc7kpvelfb[25] = "memmove(&F.ret, {}, {});";
const uint8_t gm_s2470an4q44lj[25] = "std/collections/hash_map";
const uint8_t gm_s25qdppmkh7s6[7] = "`null`";
const uint8_t gm_s268n7l3tugli[6] = "{}@{}";
const uint8_t gm_s283bs8hsleb3[9] = "let-else";
const uint8_t gm_s29avse796bo5[18] = "`{}` is a keyword";
const uint8_t gm_s2dog0ijtvmd2[32] = "unsupported lockfile version {}";
const uint8_t gm_s2ef11ime8o01[42] = "internal error: `@fiber` outside a clause";
const uint8_t gm_s2kvk8bnjk52t[12] = "gm_node *cx";
const uint8_t gm_s2ogeh8fp9da3[12] = "GM_CONTINUE";
const uint8_t gm_s2rk6mdib4mtv[17] = "0123456789abcdef";
const uint8_t gm_s2sfjihvaabi3[9] = "{}.index";
const uint8_t gm_s2u7k18iev3v2[4] = "u, ";
const uint8_t gm_s32blj9c3tc60[11] = "((void *)&";
const uint8_t gm_s32rhj249n222[10] = "make_info";
const uint8_t gm_s38cjdjl7n99k[10] = "uint8_t p";
const uint8_t gm_s39jha51ulivf[27] = "__attribute__((noinline)) ";
const uint8_t gm_s3hb2q98sfsao[5] = "; }\012";
const uint8_t gm_s3jflb7gdk32k[32] = "unnecessary parentheses around ";
const uint8_t gm_s3qu37je1jc8f[46] = "` in [lint] (`gamma lint --rules` lists them)";
const uint8_t gm_s3ss0hcg1h7js[7] = " runs)";
const uint8_t gm_s3t2d4i4t34ct[74] = "the elements of a tuple iterated by `for` must have one type, but found `";
const uint8_t gm_s3tr540lff6s6[11] = "unit_value";
const uint8_t gm_s3udvgirbbq3p[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\235\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_s418vrbemh247[49] = "a string pattern needs a `string`, but this is `";
const uint8_t gm_s41uc8v0dkl4a[15] = "), tb = GM_LD(";
const uint8_t gm_s463po27vtmnn[15] = "-ffreestanding";
const uint8_t gm_s46ki8g7j9f6v[6] = "({}) ";
const uint8_t gm_s47db08p2n3lj[3] = "+[";
const uint8_t gm_s48hq6ubqo1rd[10] = "{} a[{}];";
const uint8_t gm_s48vlq6lddb8m[29] = " __attribute__((vector_size(";
const uint8_t gm_s4fhghepo9qb6[13] = "gm_atag_grow";
const uint8_t gm_s4g35ls4ma80g[13] = "gamma.toml: ";
const uint8_t gm_s4hf5j4eu72mv[66] = "invalid unicode escape: expected `}` after the hexadecimal digits";
const uint8_t gm_s4lmdtjb9rnah[70] = "`continue` finishes the clause with `()`, but the operation returns `";
const uint8_t gm_s4m4ttv1faeb1[175] = "`{}(\342\200\246)` is an iterator, not a value: it cannot be passed for a parameter of type `{}`; run it with `for x in {}(\342\200\246)`, or gather its values with `collect({}(\342\200\246))` (\302\24713.5)";
const uint8_t gm_s4nikb38gk9t0[22] = "gm_heap_alloc({}, {})";
const uint8_t gm_s4ok0uoiq1nu9[97] = "\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000";
const uint8_t gm_s4r4tmb9bg65q[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\306\000\000\000\036\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_s527dqlmmaier[6] = ":help";
const uint8_t gm_s52bclt1m0fbl[76] = "    {{ void *p = (uint8_t *){} + {}; memcpy(gm_g{} + {}, &p, sizeof p); }}\012";
const uint8_t gm_s55ccd7do7rbs[11] = "InOptional";
const uint8_t gm_s592b618tctb1[15] = "layout of `{}`";
const uint8_t gm_s5cbc2v9e254r[37] = "({})((({}){} & {}) | (({}){} & ~{}))";
const uint8_t gm_s5cfou479ljhn[29] = "compiler/check/array_ctor.gm";
const uint8_t gm_s5f9u0o3duhns[13] = "empty-branch";
const uint8_t gm_s5gkjub7hejc1[9] = "gm_cont ";
const uint8_t gm_s5hqjuhiq2fc2[41] = "\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\024\001\000\000\020\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_s5rqsv8c71bds[11] = "{\"edits\":[";
const uint8_t gm_s5vakuk6ui4kj[9] = "untyped ";
const uint8_t gm_s5vb1b1mnli2b[72] = "`try x else d` is ambiguous: write `try (x else d)` or `(try x) else d`";
const uint8_t gm_s5ve9r6iab8ga[26] = "`: unknown architecture `";
const uint8_t gm_s5veo86cvpdae[86] = "an array can be indexed only by an enum, and `{}` has a variant with a payload (`{}`)";
const uint8_t gm_s61i2o7ldibjh[35] = " is not exactly representable as `";
const uint8_t gm_s61jd5hctkcmt[38] = "static void gm_ginit{}(void) {{\012{}}}\012";
const uint8_t gm_s63e7t279sbjq[47] = "call through an invalid function pointer {:#x}";
const uint8_t gm_s64fvfprf3827[63] = "static int gm_xt{}(gm_node *cx) {{ (void)cx; {}; return 0; }}\012";
const uint8_t gm_s64skgbd4ii1k[10] = "Variables";
const uint8_t gm_s6cmbp3c9g09j[61] = "float literal patterns are not allowed; compare with a guard";
const uint8_t gm_s6eefft9fser8[9] = "gm_mulhi";
const uint8_t gm_s6g2mpli2mu72[11] = "gm_closure";
const uint8_t gm_s6i74fbmndo4a[15] = "gm_finish_main";
const uint8_t gm_s6mmuahmmssv6[17] = "` has no payload";
const uint8_t gm_s6n3pd0tmqbqc[10] = ") != 0) {";
const uint8_t gm_s6pgabt9io52p[132] = "the package contains the symbolic link `{}` -> `{}`, which points outside the package; links must stay inside the package directory";
const uint8_t gm_s6qf5dtv8rcdo[13] = "gm_cpu_count";
const uint8_t gm_s6rj805fm39qm[65] = "`{}` is an effect set, not a type; use it after `needs` (\302\24716.6)";
const uint8_t gm_s6vghnf0vdf2t[51] = "    gm_cover_init({}, argc > 2 \077 argv[2] : NULL);\012";
const uint8_t gm_s70iivik802q4[26] = "integer number overflowed";
const uint8_t gm_s70j71mp70pdq[14] = ")pn->clauses[";
const uint8_t gm_s76ekntop342o[8] = "MaskAny";
const uint8_t gm_s783a88c5t0ve[67] = "unknown type `{}`: integer types are at most 128 bits wide (\302\2475.2)";
const uint8_t gm_s7an5gtrdgen2[14] = "1 placeholder";
const uint8_t gm_s7b8bvi5kqaqt[7] = ": type";
const uint8_t gm_s7ed59qo09cfg[41] = "slicing a many-pointer needs both bounds";
const uint8_t gm_s7fdjhp9ga5oo[8] = "gm_xe{}";
const uint8_t gm_s7m3rbk4l87ah[15] = "NetSetTimeouts";
const uint8_t gm_s7mfvroqmll1f[3] = "{ ";
const uint8_t gm_s7qcnd652ae6d[25] = "static const gm_variant ";
const uint8_t gm_s816n04ro16nc[48] = "this `else` holds only an `if`: write `else if`";
const uint8_t gm_s81csu1l86dmv[52] = "a `#global` needs a type: `#global name: T = value`";
const uint8_t gm_s86kdga2ul6h4[9] = "net_send";
const uint8_t gm_s87p55h1isc6m[6] = "gm_xe";
const uint8_t gm_s89gtqa9dnss0[35] = "`: it would hide the declaration `";
const uint8_t gm_s8d2alh114kri[15] = "pat-const : {}";
const uint8_t gm_s8g92mbmiiva2[48] = "an `@asm` clobber must be a compile-time string";
const uint8_t gm_s8jclq8b6mi27[17] = " was called here";
const uint8_t gm_s8l22p9l388io[67] = "a fiber's argument must have a first-class type, not `{}` (\302\24714.6)";
const uint8_t gm_s8l7qb8s3942u[35] = "and a function type after the name";
const uint8_t gm_s8m12els4res5[26] = "the arguments of `.clamp`";
const uint8_t gm_s8o6lm1cn4eo5[3] = "fd";
const uint8_t gm_s8ps5ka0vnf6d[42] = "call through an invalid function pointer ";
const uint8_t gm_s8qkarceaerkr[22] = "` is not defined on `";
const uint8_t gm_s8rs2s65rnmku[18] = "-ffp-contract=off";
const uint8_t gm_s8u62e7lr2m4s[13] = "let-tuple {}";
const uint8_t gm_s903nhsp8hiug[14] = "resolve_write";
const uint8_t gm_s92n4v7htp6pi[11] = "->clauses[";
const uint8_t gm_s96j6o9smoqdu[8] = "FnToPtr";
const uint8_t gm_s9dh9d8gs6a16[9] = "block : ";
const uint8_t gm_s9dudft75oi1c[33] = "internal error: `{}` has no body";
const uint8_t gm_s9ehkuquejmat[45] = "for (size_t i = 0; i < {}; i++) {} += {}[i];";
const uint8_t gm_s9eqqrnq18n3l[31] = "`{}` in {} holds an empty path";
const uint8_t gm_s9gk1b0ve64mt[10] = "float pow";
const uint8_t gm_s9h8lp3gkedth[29] = "float builtin on a non-float";
const uint8_t gm_s9i2ld3a42lap[8] = "FsWrite";
const uint8_t gm_s9impgj2b5t1c[50] = "stack sizes through functions of the core library";
const uint8_t gm_s9k65bfq9n6l0[14] = "render_region";
const uint8_t gm_s9kou5ia9uri9[6] = "; } }";
const uint8_t gm_s9m6kd6gec368[14] = "    Removing ";
const uint8_t gm_s9p37ubghllhn[64] = "` may be misaligned; copy the field to a local first (\302\2475.10.4)";
const uint8_t gm_s9qjpsba9cv0i[10] = "thread_op";
const uint8_t gm_s9rk6ipadg2pq[111] = "    { const void *mp = msg; uintptr_t ml = len; memcpy((uint8_t *)&m, &mp, sizeof mp); memcpy((uint8_t *)&m + ";
const uint8_t gm_s9sjpa1f1m1tr[24] = "a constant of this type";
const uint8_t gm_sa3k87epai61e[25] = "to close the parenthesis";
const uint8_t gm_sa5m6gegug17p[40] = "stop after writing the C file (<exe>.c)";
const uint8_t gm_sa5s0jools6a2[9] = "RandFill";
const uint8_t gm_sa6hsburhm4pn[9] = "write {}";
const uint8_t gm_sa7q2bqmn8lgb[9] = "subtract";
const uint8_t gm_sa9e77lq9q04p[16] = "caller_location";
const uint8_t gm_sadg245agtuao[45] = "; an array can start filled: `[N]T(fill: v)`";
const uint8_t gm_sadj3uigeodiu[29] = "type `{}` has no method `{}`";
const uint8_t gm_sadphckf7ra7f[4707] = "\012#define GM_NOINLINE __attribute__((noinline))\012/* The step of the structural hash: the state and the word, multiplied by an odd constant\012   to 128 bits, whose halves are folded together, so that every bit of the result depends\012   on every bit of both. */\012static inline uint64_t gm_mix64(uint64_t h, uint64_t x) {\012    unsigned __int128 m = (unsigned __int128)(h ^ x) * 0x9E3779B97F4A7C15ULL;\012    return (uint64_t)m ^ (uint64_t)(m >> 64);\012}\012static inline uint64_t gm_mix128(uint64_t h, unsigned __int128 x) {\012    return gm_mix64(gm_mix64(h, (uint64_t)x), (uint64_t)(x >> 64));\012}\012static inline uint64_t gm_mix_float(uint64_t h, double x) {\012    uint64_t bits;\012    if (x == 0) x = 0;\012    memcpy(&bits, &x, 8);\012    return gm_mix64(h, bits);\012}\012/* Mixes n bytes as little-endian words of eight, the last one of the bytes that are left. */\012static uint64_t gm_mix_bytes(uint64_t h, const uint8_t *p, uint64_t n) {\012    for (; n >= 8; p += 8, n -= 8) {\012        uint64_t w;\012        memcpy(&w, p, 8);\012#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__\012        w = __builtin_bswap64(w);\012#endif\012        h = gm_mix64(h, w);\012    }\012    if (n > 0) {\012        uint64_t w = 0;\012        for (uint64_t i = 0; i < n; i++) w |= (uint64_t)p[i] << (8 * i);\012        h = gm_mix64(h, w);\012    }\012    return h;\012}\012static double gm_fmin(double x, double y) {\012    if (x != x) return y;\012    if (y != y) return x;\012    if (x == 0 && y == 0) return __builtin_signbit(x) \077 x : y;\012    return x < y \077 x : y;\012}\012static double gm_fmax(double x, double y) {\012    if (x != x) return y;\012    if (y != y) return x;\012    if (x == 0 && y == 0) return __builtin_signbit(x) \077 y : x;\012    return x > y \077 x : y;\012}\012/* base ^ e by repeated squaring, wrapping at the width of T; *o is set when a product that\012 * contributes to the result overflowed T (so the true result does not fit T). */\012#define GM_POW_FN(name, T)                                                      \\\012    static T name(T b, uint64_t e, bool *o) {                                   \\\012        T r = 1;                                                                \\\012        for (;;) {                                                              \\\012            if ((e & 1) && __builtin_mul_overflow(r, b, &r)) *o = true;        \\\012            e >>= 1;                                                            \\\012            if (!e) return r;                                                   \\\012            if (__builtin_mul_overflow(b, b, &b)) *o = true;                    \\\012        }                                                                       \\\012    }\012GM_POW_FN(gm_pow_u8, uint8_t)\012GM_POW_FN(gm_pow_i8, int8_t)\012GM_POW_FN(gm_pow_u16, uint16_t)\012GM_POW_FN(gm_pow_i16, int16_t)\012GM_POW_FN(gm_pow_u32, uint32_t)\012GM_POW_FN(gm_pow_i32, int32_t)\012GM_POW_FN(gm_pow_u64, uint64_t)\012GM_POW_FN(gm_pow_i64, int64_t)\012GM_POW_FN(gm_pow_u128, unsigned __int128)\012GM_POW_FN(gm_pow_i128, __int128)\012/* Bit counts of the low `bits` bits of x (the caller masks the rest). */\012static unsigned __int128 gm_clz(unsigned __int128 x, int bits) {\012    uint64_t hi = (uint64_t)(x >> 64), lo = (uint64_t)x;\012    int n = hi \077 __builtin_clzll(hi) : lo \077 64 + __builtin_clzll(lo) : 128;\012    return n - (128 - bits);\012}\012static unsigned __int128 gm_ctz(unsigned __int128 x, int bits) {\012    uint64_t hi = (uint64_t)(x >> 64), lo = (uint64_t)x;\012    int n = lo \077 __builtin_ctzll(lo) : hi \077 64 + __builtin_ctzll(hi) : 128;\012    return n < bits \077 n : bits;\012}\012static unsigned __int128 gm_popcount(unsigned __int128 x) {\012    return __builtin_popcountll((uint64_t)x) + __builtin_popcountll((uint64_t)(x >> 64));\012}\012static unsigned __int128 gm_bswap(unsigned __int128 x, int bits) {\012    unsigned __int128 r = ((unsigned __int128)__builtin_bswap64((uint64_t)x) << 64) | __builtin_bswap64((uint64_t)(x >> 64));\012    return r >> (128 - bits);\012}\012static unsigned __int128 gm_bitreverse(unsigned __int128 x, int bits) {\012    unsigned __int128 r = 0;\012    for (int i = 0; i < 4; i++) {\012        uint32_t w = (uint32_t)(x >> (32 * i));\012        w = ((w >> 1) & 0x55555555u) | ((w & 0x55555555u) << 1);\012        w = ((w >> 2) & 0x33333333u) | ((w & 0x33333333u) << 2);\012        w = ((w >> 4) & 0x0f0f0f0fu) | ((w & 0x0f0f0f0fu) << 4);\012        w = __builtin_bswap32(w);\012        r |= (unsigned __int128)w << (32 * (3 - i));\012    }\012    return r >> (128 - bits);\012}\012/* Rotates the low `bits` bits of x left by k (taken modulo bits). */\012static unsigned __int128 gm_rotl(unsigned __int128 x, uint64_t k, int bits) {\012    unsigned __int128 mask = bits == 128 \077 ~(unsigned __int128)0 : (((unsigned __int128)1 << bits) - 1);\012    x &= mask;\012    k %= (uint64_t)bits;\012    if (k == 0) return x;\012    return ((x << k) | (x >> ((uint64_t)bits - k))) & mask;\012}\012";
const uint8_t gm_sagd3osht35og[13] = "freed memory";
const uint8_t gm_sahd19pngn4vn[14] = "tag_offset = ";
const uint8_t gm_sahim4484f9l7[3] = ".c";
const uint8_t gm_sak3h3rlsvn4o[10] = "bool evok";
const uint8_t gm_salpeh0v5rqdf[8] = "shuffle";
const uint8_t gm_saovklm35s57t[184] = "{} needs the fields of `{}`, which a generic function does not know; a `#soa` of a type parameter has whole elements (`a[i]`), `len`, slicing, `copy_from`, `make` and `free` (\302\2475.6.4)";
const uint8_t gm_saq8ri2pmi7o5[19] = "parse_fn_type_only";
const uint8_t gm_sasea0sobfkrs[8] = "extern ";
const uint8_t gm_sat505mlqphna[26] = "invalid package version `";
const uint8_t gm_sauu2s1pbjqsh[11] = "the clause";
const uint8_t gm_sb21gep34rmm9[36] = "`{}` of dependency `{}` needs `git`";
const uint8_t gm_sb3sf0ef436fi[40] = "the body of this `with` never completes";
const uint8_t gm_sb5b70djeqhg8[16] = "` is a variable";
const uint8_t gm_sb5h01ig0b9oe[34] = "Asm {{ outputs: {}, inputs: {} }}";
const uint8_t gm_sb641udihu5hs[10] = "rand_fill";
const uint8_t gm_sb8b1gf0gtt49[43] = "(((unsigned __int128){}ULL << 64) | {}ULL)";
const uint8_t gm_sb8qu1qqed6pt[85] = "the rest parameter `{}` takes the remaining positional arguments; it cannot be named";
const uint8_t gm_sbbgtkch3q4n5[3] = " B";
const uint8_t gm_sbht6tj2u5vfl[8] = "runtime";
const uint8_t gm_sbjshone12eeu[158] = "checksum mismatch for package {} (expected {}, found {}); gamma.lock was edited or the cached checkout {} was modified (`gamma cache clean --all` removes it)";
const uint8_t gm_sbjuojbqo51av[36] = "`bits` to this type at compile time";
const uint8_t gm_sbkd9ldfr86ri[5] = "@\015\003\000";
const uint8_t gm_sbl7p8tv4j986[17] = " elements, not `";
const uint8_t gm_sblomia8fb3mu[20] = "<table>\012<thead><tr>";
const uint8_t gm_sbm19bhbgtvrj[129] = "\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sbmtphtelgust[29] = "__attribute__((export_name(\"";
const uint8_t gm_sbo8mn3eri83c[23] = "unwrap of error value ";
const uint8_t gm_sbqoiitqtfv75[11] = "reduce_min";
const uint8_t gm_sbssn254n4fs2[25] = "gm_asyncify_start_rewind";
const uint8_t gm_sbtq9nj91tefv[10] = ", sizeof ";
const uint8_t gm_sbvi33ihvufj1[20] = "((uint8_t *)gm_g{})";
const uint8_t gm_sc103i8mmkjaq[41] = "GM_ST({}, (uint8_t *)&{} + {} * {}, {});";
const uint8_t gm_sc1nadatuanhc[5] = "'\\0'";
const uint8_t gm_sc46nvblu52f3[18] = "remove the import";
const uint8_t gm_sc559v8etqgda[19] = ") __asm__(GM_SYM(\"";
const uint8_t gm_sc6pb1tb5tnnv[34] = "{}gm_poison(&F, 0xDD, sizeof F); ";
const uint8_t gm_scai7a7ojvpjh[19] = "[package]\012name = \"";
const uint8_t gm_scc5n7285iivu[13] = "reference = ";
const uint8_t gm_sccpc5t5dl8ni[51] = "    return GM_LD({}, a + {}) < GM_LD({}, b + {});\012";
const uint8_t gm_scd1bb5dijnis[15] = "(w)` (\302\2475.6.2)";
const uint8_t gm_scgm14ju6d0p8[15] = "cannot negate ";
const uint8_t gm_scgvr0isof71e[9] = "__wasi__";
const uint8_t gm_scllp0lpbljde[15] = "`pub(package)`";
const uint8_t gm_scobqq9qv6nnk[6] = "&#39;";
const uint8_t gm_scqqkvgm1j3cs[65] = "internal error: a lane operation on a value that is not a vector";
const uint8_t gm_scrnhm4ha22vj[18] = "` is indexed by `";
const uint8_t gm_scs3asa1395s7[6] = "{}e{}";
const uint8_t gm_scvsibgrrt3sc[13] = "check_module";
const uint8_t gm_sd2a8ckkfb013[26] = "` contains the character ";
const uint8_t gm_sd2iiq8cuaks3[5] = "rotr";
const uint8_t gm_sd5giovmg2s7p[17] = "static int gm_xt";
const uint8_t gm_sdak0upmjiivc[15] = "gm_unw.kind = ";
const uint8_t gm_sdak257j1qgl8[6] = "cstr ";
const uint8_t gm_sdbgs2fic3oib[11] = "gm_fs_open";
const uint8_t gm_sdbtbup89cn84[19] = " needs a concrete ";
const uint8_t gm_sdcf6v5v0oma5[6] = "dedup";
const uint8_t gm_sddniufplfkku[66] = "invalid memory access: write to read-only memory at address {:#x}";
const uint8_t gm_sdl70rjr9en4m[16] = "gamma: applied ";
const uint8_t gm_sdm98dp52j8ep[16] = "`{}` is a label";
const uint8_t gm_sdnh9sm5pppq1[40] = "expected a value, found the effect `{}`";
const uint8_t gm_sdprvvqv16trj[61] = "`; use `.trunc`, `.round`, `.checked` or `.sat` for integers";
const uint8_t gm_sdq66cp51pg1r[11] = "the local ";
const uint8_t gm_sdubr1na84odv[12] = ".align = {}";
const uint8_t gm_se09jntm6upln[4] = "ccx";
const uint8_t gm_se0oi28sapu01[11] = "`, found `";
const uint8_t gm_se5r6p8tqdo82[31] = "the literal is too large for `";
const uint8_t gm_seajfhgtfmhv7[12] = "fs_open_dir";
const uint8_t gm_seak1jbr9qmd1[9] = "({})({})";
const uint8_t gm_sebe0816umkgp[54] = "    if (n != GM_LD(uintptr_t, b + {})) return false;\012";
const uint8_t gm_sebk4v8jac58q[43] = "attribute `#{}` does not apply to a struct";
const uint8_t gm_seflbetisd1ui[3] = "kc";
const uint8_t gm_segm7or15hs5h[20] = "dynamicRegistration";
const uint8_t gm_sei6ib477d1ds[4] = "loc";
const uint8_t gm_sennl2fjkcbdb[4] = "big";
const uint8_t gm_senuk3dngdds0[17] = "prefer-immutable";
const uint8_t gm_sepq106jdsjea[11] = "slice-part";
const uint8_t gm_seqap6hh2h8p5[5] = "<h2>";
const uint8_t gm_serasf77ce9g3[52] = "a tuple must be either fully named or fully unnamed";
const uint8_t gm_serke084a3lbj[41] = "\000\000\000\000\000\000\000\000\034\000\000\000\000\000\000\000-\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sf51sbqtper4f[74] = "`{}` has no variant carrying `{}`, so the error cannot be lifted (\302\24710.6)";
const uint8_t gm_sf9b70aed3bu1[21] = "check bounds {} < {}";
const uint8_t gm_sfil4j1onqa90[14] = "gamma_repl.gm";
const uint8_t gm_sfinup9etdjip[4] = "MiB";
const uint8_t gm_sfiqishktrbg2[27] = "to begin the foreign block";
const uint8_t gm_sfjrpo4i6b0at[24] = "compiler/pkg/support.gm";
const uint8_t gm_sfnq22hc2kms9[14] = "binary_search";
const uint8_t gm_sfpdckbabsquc[18] = " *)(((uint8_t *)&";
const uint8_t gm_sfpnied4dp46h[15] = "gm_proc_launch";
const uint8_t gm_sfs5rrntfu3mi[25] = "a constant cannot hold `";
const uint8_t gm_sft51gvrcleut[16] = "gm_fs_canonical";
const uint8_t gm_sfu0in59pituf[62] = "a precision goes with floats, not with an integer base or `q`";
const uint8_t gm_sfurr787jvkcg[6] = ") <= ";
const uint8_t gm_sg0tidc1tpl0e[33] = "internal error: not an operation";
const uint8_t gm_sg8a1r64kvpgo[3] = "mk";
const uint8_t gm_sgd9tn7p5ihgk[10] = "Module {}";
const uint8_t gm_sge2cgjaklnf2[5] = "_$.@";
const uint8_t gm_sge6a56sqn6kj[21] = "    return na < nb;\012";
const uint8_t gm_sgfqei77bsia8[4] = " < ";
const uint8_t gm_sggm36eg9tkga[10] = "=` needs ";
const uint8_t gm_sgjkgsqdq34nu[37] = "the `with` that installs the handler";
const uint8_t gm_sgli4lbivdt1d[38] = "__attribute__((export_name(\"{}\"))) {}";
const uint8_t gm_sgls4l5hb5j8p[75] = "a spread needs a tuple, an array or a struct with visible fields, not `{}`";
const uint8_t gm_sgo00f0mafbnd[4] = "Max";
const uint8_t gm_sgovtffcl93pk[15] = "GM_PTR_TAG({})";
const uint8_t gm_sgp3f4mk5jmsl[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000L\001\000\000\031\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_sgq1v4nk059uv[10] = "this size";
const uint8_t gm_sgq6hvrcda4sp[37] = "`@{}` needs a pointer `^T`, not `{}`";
const uint8_t gm_sgs7af5k634cn[11] = "__mips64__";
const uint8_t gm_sgu3j1te831tl[24] = "`new` needs a directory";
const uint8_t gm_sguhoqhco0rfk[9] = "-pthread";
const uint8_t gm_sh1emun6fmppt[4] = "fix";
const uint8_t gm_sh1scn6dg8mpr[14] = ")((__int128)(";
const uint8_t gm_sh5lbh5j6oqpf[36] = "{}{} {}({}) __asm__(GM_SYM(\"{}\"));\012";
const uint8_t gm_sh75rh8dk51gf[12] = ",\"message\":";
const uint8_t gm_sh8houjk34evm[15] = "import cycle: ";
const uint8_t gm_sh8lvmk1ev2kd[16] = "Not a directory";
const uint8_t gm_shdnhcpvptc1q[5] = "str ";
const uint8_t gm_shf8e0v8f47ml[17] = "\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000";
const uint8_t gm_shg1pn1r3sjgi[13] = "gamma-bench-";
const uint8_t gm_shgjlb2nrn5fk[22] = "operation would block";
const uint8_t gm_shhrbleiv8o3j[14] = "__builtin_fma";
const uint8_t gm_shjpff93a0f25[7] = "CFLAGS";
const uint8_t gm_shmgu63rvoce3[36] = "a lane index is a `usize`, not `{}`";
const uint8_t gm_shq9t6lutffbf[4] = ".{}";
const uint8_t gm_shtrdthev5fjn[14] = ", (uint8_t *)";
const uint8_t gm_shttb5kqse295[12] = "end of file";
const uint8_t gm_shu6vhibjovbk[18] = "this field access";
const uint8_t gm_si698b9virvoh[7] = "{} {}\012";
const uint8_t gm_siaeuo6l48ehh[15] = "a `when` block";
const uint8_t gm_sic5vsema0r36[8] = "handler";
const uint8_t gm_sic6g350stk7h[21] = "cannot read CFLAGS: ";
const uint8_t gm_siddm9qkt382a[15] = " + (uintptr_t)";
const uint8_t gm_sifqqc9n3eem2[6] = "alloc";
const uint8_t gm_sihlhf2982kip[12] = "gm_wasi_ctx";
const uint8_t gm_sii9tamh3vah6[32] = "` (`:help` lists the commands)\012";
const uint8_t gm_sil41urphp1ol[12] = "piece|{}|{}";
const uint8_t gm_sipfpcphbcub1[34] = "__asm__ volatile (\"\" : \"+r\"({}));";
const uint8_t gm_sipi0b0c6ms62[6] = "char ";
const uint8_t gm_sirpcjgkmlehr[8] = "newText";
const uint8_t gm_sisd1n433forg[52] = "invalid memory access: address {:#x} points into {}";
const uint8_t gm_sismsc98l6u4e[4] = "Xor";
const uint8_t gm_sisn94tbdoeof[65] = "this changes `{}`, a copy of `{}`, which does not see the change";
const uint8_t gm_sium1s93t7o1a[19] = "the assigned value";
const uint8_t gm_sivsapeh482sm[8] = "Convert";
const uint8_t gm_sj0e9fo4a625j[80] = "for (size_t i{} = 0; i{} < {}; i{}++) GM_ST({}, (uint8_t *)&{} + i{} * {}, {});";
const uint8_t gm_sj0ur1o7a5584[16] = " elements, but ";
const uint8_t gm_sj3lfuqd4f1np[37] = "`#export` applies only to a function";
const uint8_t gm_sj718thiqudru[10] = "({} + {})";
const uint8_t gm_sj83d90uvga1k[8] = "{}.deps";
const uint8_t gm_sj97id02v9fh3[6] = "Chunk";
const uint8_t gm_sjcdr1cibbeq6[64] = "`\", file, line, col);\012    if (pn->kind == GM_NATIVE) {\012        ";
const uint8_t gm_sjcocjnsa7u7g[85] = "the value of this expression (of type `{}`) is unused; write `_ = \342\200\246` to discard it";
const uint8_t gm_sjf1r2hmheh87[29] = "` to bind the value as it is";
const uint8_t gm_sjkbtepn8bqeb[24] = "  called from {} in {}\012";
const uint8_t gm_sjmhvdjct2lq7[19] = "-D --deny-warnings";
const uint8_t gm_sjpml17j24nb4[64] = "invalid suffix `{}` for float literal; expected f16, f32 or f64";
const uint8_t gm_sjrtlfuv3os00[75] = "dependency `{}` needs a `path = \"\342\200\246\"`, a `git = \"\342\200\246\"` or a `url = \"\342\200\246\"`";
const uint8_t gm_sjsv6e8j9nikr[7] = "fn({})";
const uint8_t gm_sjt1hcgrgj8be[36] = "` does not apply to complex numbers";
const uint8_t gm_sk36cj5av4kuu[17] = "\000\000\000\000\000\000\000\0005\000\000\000\000\000\000\000";
const uint8_t gm_sk5vbnunk2sec[16] = "(const void *)(";
const uint8_t gm_sk7bri4eh56ps[17] = "if ({} == {}) {{";
const uint8_t gm_sk83con0mdsfe[6] = "tuple";
const uint8_t gm_sk8hc0njn930q[43] = "%{} is read where it is not written before";
const uint8_t gm_sk9at358cmauo[27] = ")-__builtin_convertvector(";
const uint8_t gm_ska00mcp5pgpq[53] = "`return` from a handler clause to a function literal";
const uint8_t gm_skfo9c0nr3nrd[50] = "`: its field `len` would hide the length, `a.len`";
const uint8_t gm_skgulapvvrhkp[36] = " kb; memcpy(&kb, &k, sizeof kb); ((";
const uint8_t gm_skjfb4faajq73[13] = ", (uint32_t)";
const uint8_t gm_skkog7g7r3edb[8] = "windows";
const uint8_t gm_skl1tisbdbp2m[13] = "gm_atag_slot";
const uint8_t gm_skme3gpq5egsp[40] = "invalid suffix `{}` for integer literal";
const uint8_t gm_skq9qlj43eei8[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\206\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_skr6p9lfc5ftp[11] = "type alias";
const uint8_t gm_skrdd0u45d86g[17] = "\000\000\000\000\000\000\000\000S\000\000\000\000\000\000\000";
const uint8_t gm_sksgae11pvk6p[2] = "\011";
const uint8_t gm_skt7g4v99o4j0[42] = "a local variable whose lifetime has ended";
const uint8_t gm_skusvrbuvvp37[13] = "after `else`";
const uint8_t gm_sl1kcdemlk6n0[10] = "method_is";
const uint8_t gm_sl4ll6nrpr5rj[33] = "(*({} *)(((uint8_t *)&{}) + {}))";
const uint8_t gm_sl80cge5ts0aj[14] = "pub(package) ";
const uint8_t gm_sl815v6oegv7n[14] = "; memcpy(gm_g";
const uint8_t gm_slc3rjglch387[50] = "missing argument for the compile-time parameter `";
const uint8_t gm_slfddg7c1u1oc[62] = "cannot assign to `{}`: it is immutable (declare it with `:=`)";
const uint8_t gm_slfinpjh0406b[47] = "vectors are not ordered; compare lanes with `.";
const uint8_t gm_slfk2il1skugc[13] = "gm_backtrace";
const uint8_t gm_slg6o1a142s3f[44] = "{} {{\012    gm_frame_overflow({}, {}ull);\012}}\012";
const uint8_t gm_slgiebdce3332[8] = "</h{}>\012";
const uint8_t gm_slgu49nv3o8e7[12] = "    memcpy(";
const uint8_t gm_slmnp81e6ev88[10] = ", .env = ";
const uint8_t gm_sln5qir3tgi7j[10] = "FutexWake";
const uint8_t gm_slpi6dabivg2e[29] = "field `{}` is supplied twice";
const uint8_t gm_sm0f8bs4gd2ma[18] = "__atomic_fetch_or";
const uint8_t gm_sm34bm8ke9n7b[22] = "` for integer literal";
const uint8_t gm_sm5ffrmin8340[10] = "subdir = ";
const uint8_t gm_sm6gmoq645v8o[70] = "`if c { true } else { false }` and the like, which are `c` or `not c`";
const uint8_t gm_sm732n1u78vvc[16] = "    if (ta == (";
const uint8_t gm_sm922ui8k0oca[9] = ",\"line\":";
const uint8_t gm_smbfsgie9fgg4[4] = "Env";
const uint8_t gm_smf8ll9cm8fgf[26] = "build run test bench repl";
const uint8_t gm_smhve5pq35drr[9] = "variants";
const uint8_t gm_smiaoqkobhu81[54] = "if ({} >= (uintptr_t)({})) gm_trap_index({}, {}, {});";
const uint8_t gm_smlcnjfj6t3mc[6] = "&amp;";
const uint8_t gm_smnkiec8dekcp[8] = "-target";
const uint8_t gm_smoi1nh7b1b6o[65] = "`@fiber` may be used only in the body of a `ctl` clause (\302\24714.6)";
const uint8_t gm_smp6ue9h98hbc[31] = ", argc > 2 \077 argv[2] : NULL);\012";
const uint8_t gm_smpb9qb8m3meo[7] = "check ";
const uint8_t gm_smsmo0f5ao530[10] = "fn-ref {}";
const uint8_t gm_sn2e6t65fstoj[19] = "__SIZEOF_POINTER__";
const uint8_t gm_sn6vmked7l7pf[7] = "-mf16c";
const uint8_t gm_sn8d077r4fgpq[26] = " __attribute__((packed)) ";
const uint8_t gm_snaq7hvenqsum[4] = "Rem";
const uint8_t gm_snfsrvfr6vnkt[8] = "elem = ";
const uint8_t gm_sngckqdbegtlc[26] = " is not an array or slice";
const uint8_t gm_snhamhc7ig6ce[25] = "<li><a href=\"#{}\"><code>";
const uint8_t gm_snhkhrbd8tjtf[45] = "range patterns match integers, but this is `";
const uint8_t gm_snku1611u1u7p[9] = "memmove(";
const uint8_t gm_snm19vfbec4q3[19] = "call perform {} {}";
const uint8_t gm_snmrruc88a739[10] = "inline {}";
const uint8_t gm_snpp5o7gj4ij7[10] = "vd{}[j{}]";
const uint8_t gm_snritti8butc6[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\356\000\000\000#\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_snrk30ug2qmu2[8] = "freebsd";
const uint8_t gm_snsqui8o89u28[15] = "` with `using`";
const uint8_t gm_snstimc3n04u0[12] = "FsRemoveDir";
const uint8_t gm_snuadcbi5e786[15] = " lanes, like `";
const uint8_t gm_snvodad5t43r7[11] = ")) return ";
const uint8_t gm_so1m4qmhi2fvm[93] = "`{}` takes one value per element, so {} is not converted to it; spread it: `{}(..x)` (\302\2476.5)";
const uint8_t gm_so3bl4q7kp6gl[21] = "unknown import kind ";
const uint8_t gm_so8g4o8hee3cb[7] = "{}|{}|";
const uint8_t gm_so94vinsu4uib[50] = "vector {} of {} lanes with a slice of {} elements";
const uint8_t gm_sob1n7uh7kpps[14] = "useless-defer";
const uint8_t gm_sodg3e7aummvt[8] = "release";
const uint8_t gm_soeo46rfuspd5[32] = "` must be 64 hexadecimal digits";
const uint8_t gm_soi6iv1hnej40[13] = "loop {} : {}";
const uint8_t gm_solguqm0afvc4[44] = "cannot infer the result type of `!e` here{}";
const uint8_t gm_sor8flejv9lfq[67] = "    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);\012";
const uint8_t gm_sorb6gi9d49ic[9] = "--filter";
const uint8_t gm_souep3pirutur[55] = "this `if` never completes: each of its branches leaves";
const uint8_t gm_sovlckpk2gctr[16] = "fold_const_args";
const uint8_t gm_sp2a19d61vbe0[4] = ", \"";
const uint8_t gm_sp4skfj2bn2ut[7] = "` is `";
const uint8_t gm_sp4uhnkunj2cg[3] = "Gt";
const uint8_t gm_sp8h6qkr4b6hg[12] = "        {}\012";
const uint8_t gm_spbv5h8c0ukao[5] = "call";
const uint8_t gm_spfleivdeduud[4] = "; `";
const uint8_t gm_sppcsr92ukfol[64] = "`#deprecated` takes an optional message: `#deprecated(\"use g\")`";
const uint8_t gm_sppeomacjvkpm[7] = "punct ";
const uint8_t gm_spqlqjii789ln[57] = "internal error: a select of a value that is not a vector";
const uint8_t gm_spsbmlrti644m[11] = "<dt><code>";
const uint8_t gm_sq05ai6ts8qlk[17] = "(\342\200\246))` (\302\24713.5)";
const uint8_t gm_sq1mn6f7h111r[3] = "_s";
const uint8_t gm_sq3r7vbcbs4ll[26] = "`package` must be a table";
const uint8_t gm_sq4jcg2uujqmj[16] = "` values, not `";
const uint8_t gm_sq7c24l9r5rti[118] = "a program for a freestanding target does not declare `main`; its entry points are its `#export`ed functions (\302\2473.7.1)";
const uint8_t gm_sq89uknu8mulu[8] = "a float";
const uint8_t gm_sq95m8isfa7vd[2] = "t";
const uint8_t gm_sq9org4qoda29[12] = "GM_PTR_ADD(";
const uint8_t gm_sqfu5kl4vat80[9] = "CpuCount";
const uint8_t gm_sqin9fcebg38h[7] = "unary ";
const uint8_t gm_sqjh1si5sbfvd[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\364\002\000\000\030\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_sqkcsn1qihjbl[41] = "    uintptr_t na = GM_LD(uintptr_t, a + ";
const uint8_t gm_sqsb0jek355o9[17] = "a struct literal";
const uint8_t gm_sqssndasqkc4l[17] = "__builtin_trunc(";
const uint8_t gm_sqvepl1893o5g[3] = "))";
const uint8_t gm_sqviis18fpvj7[5] = "-tzf";
const uint8_t gm_sr1lb1m0kmmrh[46] = "if ({} == 0) gm_trap(\"division by zero\", {});";
const uint8_t gm_sr6b2mblh2jam[223] = " {\012    gm_buf b = {0};\012    uint8_t sb[GM_VIEW_SCRATCH];\012    const void *d;\012    size_t n;\012    if (s) { gm_format_spec(&b, t, v, s); d = b.data; n = b.len; } else n = gm_format_view(&b, sb, t, v, &d);\012    bool u = n != 0 && ";
const uint8_t gm_sr8f1b5ta7294[9] = "\377\377\377\377\377\377\017\000";
const uint8_t gm_srd5daqbo6ilc[58] = "` is not a symbol name: use letters, digits and `_ $ . @`";
const uint8_t gm_sre51cnt7pim3[35] = "static const gm_type {} = {{{}}};\012";
const uint8_t gm_srehvgd1sod2m[18] = "missing `version`";
const uint8_t gm_srfobl8913gvl[3] = "` ";
const uint8_t gm_sri78umnjbra8[11] = "statements";
const uint8_t gm_srjb0lv60gms5[62] = "        gm_write_err(b.data, b.len);\012        return 1;\012    }\012";
const uint8_t gm_srkol0piugptj[14] = "template_text";
const uint8_t gm_srltdqer7h3t7[55] = "`{}.from_tag` needs an enum (a union without payloads)";
const uint8_t gm_srok3f5iht8q8[23] = "` (it does not define ";
const uint8_t gm_srqehfttj6ngk[4] = "Err";
const uint8_t gm_sruk98mc5gf6c[44] = "values of type `{}` are not ordered (\302\2477.5)";
const uint8_t gm_srv4cua6q51nq[5] = "free";
const uint8_t gm_ss3kdmf5g7qmv[82] = "` is a symbol of this implementation's runtime, so it cannot be exported (\302\24723.5)";
const uint8_t gm_ss67f0gmfen1m[3] = "::";
const uint8_t gm_ss9d6rvgfqq53[13] = "pat-variant ";
const uint8_t gm_ssb6m4k22290v[17] = "` was not closed";
const uint8_t gm_sscmtmpbttmhb[93] = "invalid suffix `{}` for a binary or octal literal; only decimal and hexadecimal floats exist";
const uint8_t gm_sseb1k7dlt7ge[67] = "give it a function pointer type first: `p: ^fn(\342\200\246) = fn(\342\200\246) \342\200\246`";
const uint8_t gm_ssffver18si54[25] = "compiler/sema/ty_expr.gm";
const uint8_t gm_ssift49n290vj[5] = "we{}";
const uint8_t gm_ssjk8k45afgjb[11] = "test bench";
const uint8_t gm_ssnbkpbck3vbo[22] = "` cannot convert to `";
const uint8_t gm_ssp208k4afn15[23] = "unknown import kind {}";
const uint8_t gm_st35tcurhvabs[31] = ", .kind = GM_DECL, .clauses = ";
const uint8_t gm_st4gqcsm35nph[90] = "invalid format spec `{{:{}}}`: expected `[[fill]align][+][#][0][width][.precision][kind]`";
const uint8_t gm_st9djk4oc14um[67] = "`else` needs an optional or a result on its left, but this is `{}`";
const uint8_t gm_staulpjprqbul[155] = "({} \077 (GM_LD(uintptr_t, (const uint8_t *){} + {}) != 0 && {}({}, GM_LD(void *, {}), GM_LD(uintptr_t, (const uint8_t *){} + {}), {})) : {}({}, {}, {}, {}))";
const uint8_t gm_stbno5snno6c5[8] = "if : {}";
const uint8_t gm_stbrououb87po[51] = "an index must have an unsigned integer type, not `";
const uint8_t gm_stfd80s46313k[38] = "internal error: not a process service";
const uint8_t gm_stgra1esdnhlk[12] = "GM_PTR_TAG(";
const uint8_t gm_stjq3hbrk4g4r[4] = "tir";
const uint8_t gm_stkm2m8f3okk8[18] = "internal error: `";
const uint8_t gm_stl4dqpj6esk9[14] = "archive = {}\012";
const uint8_t gm_stlamkovo4l42[4] = "-MD";
const uint8_t gm_stna0ubmss8p6[48] = "an assignment operator after the list of places";
const uint8_t gm_stocunr1upos5[9] = "({}->up)";
const uint8_t gm_sts2e9lggtpr2[47] = "a float literal cannot have the integer type `";
const uint8_t gm_stt50isac07pf[46] = "` for float literal; expected f16, f32 or f64";
const uint8_t gm_su0gvqh5vvvlj[37] = "the package version must be a string";
const uint8_t gm_su0l53i30raa8[6] = "fn{}\012";
const uint8_t gm_su4j8ek23cevi[51] = "; install Binaryen or set WASM_OPT to its wasm-opt";
const uint8_t gm_su6ipjjrnjs7p[10] = "sys_write";
const uint8_t gm_suafpipqeom8q[7] = "gm_rt-";
const uint8_t gm_suc2pvoh9ktdi[6] = "total";
const uint8_t gm_sud7vj6tst3lp[99] = "a private top-level declaration, or a local type, alias or nested function, that nothing refers to";
const uint8_t gm_sudpbkssneeek[11] = "gm_rt_args";
const uint8_t gm_suevj69t71tjc[35] = "` does not exist in the repository";
const uint8_t gm_sufoqrq4onvgh[16] = "gm_ctl_perform(";
const uint8_t gm_suhl0206dfbdm[5] = "Proc";
const uint8_t gm_suj1i29321vmu[10] = "gm_hb_cap";
const uint8_t gm_suod5ocif7o5v[5] = "save";
const uint8_t gm_suqchf5vh2hqp[5] = "inst";
const uint8_t gm_sv0fbf330lmcd[8] = "foreign";
const uint8_t gm_svbrm62b64ga9[11] = "closure {}";
const uint8_t gm_svgaurqm99100[48] = "(__builtin_isfinite({}) && {} >= {} && {} < {})";
const uint8_t gm_sviu03g0f5s30[43] = "empty identifier segment in build metadata";
const uint8_t gm_svk3u470blc4u[9] = "{}: {}{}";
const uint8_t gm_svldd4tfe8pf3[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\036\001\000\000\037\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_svshci6b9lbnq[9] = "gm_st_v_";
const uint8_t gm_svth7kg659g2i[13] = ", 0, sizeof ";
const uint8_t gm_svtkcim3vundp[18] = "an operation name";
uint8_t gm_c0n6vj9dmoi[8] __attribute__((aligned(8)));
uint8_t gm_c0nre4icjau[1] __attribute__((aligned(1)));
uint8_t gm_c0ocv2md26k[9] __attribute__((aligned(1)));
uint8_t gm_c1id0n0l1pf[8] __attribute__((aligned(8)));
uint8_t gm_c2a4s61n0q1[11] __attribute__((aligned(1)));
uint8_t gm_c2ftoibr66a[8] __attribute__((aligned(8)));
uint8_t gm_c2im5cngga6[7] __attribute__((aligned(1)));
uint8_t gm_c3oglifus73[18] __attribute__((aligned(1)));
uint8_t gm_c3tekh5hk29[12] __attribute__((aligned(1)));
uint8_t gm_c433keo1ggm[5] __attribute__((aligned(1)));
uint8_t gm_c4mn1ob3j1k[17] __attribute__((aligned(1)));
uint8_t gm_c51kem98eiq[6] __attribute__((aligned(1)));
uint8_t gm_c64sa63uljg[1] __attribute__((aligned(1)));
uint8_t gm_c66tv9kq8na[208] __attribute__((aligned(8)));
uint8_t gm_c6fqpnlmjmc[1] __attribute__((aligned(8)));
uint8_t gm_c7983l5vkhl[16] __attribute__((aligned(8)));
uint8_t gm_c79qbqmpu02[32] __attribute__((aligned(1)));
uint8_t gm_c8lsr2952en[14] __attribute__((aligned(1)));
uint8_t gm_c9b1o5ceti8[13] __attribute__((aligned(1)));
uint8_t gm_c9paf88rqhe[8] __attribute__((aligned(8)));
uint8_t gm_cbemal9c6bf[8] __attribute__((aligned(8)));
uint8_t gm_cbg3jjml4ui[5] __attribute__((aligned(1)));
uint8_t gm_cbvo0qov0pu[13] __attribute__((aligned(1)));
uint8_t gm_cc0f9pvdvgk[12] __attribute__((aligned(1)));
uint8_t gm_ccbbgs9ri4m[2] __attribute__((aligned(1)));
uint8_t gm_ccgj59b20un[15] __attribute__((aligned(1)));
uint8_t gm_cdeh13viquh[4] __attribute__((aligned(1)));
uint8_t gm_ce9kahcvfp7[1] __attribute__((aligned(8)));
uint8_t gm_cecnl9dg9cm[7] __attribute__((aligned(1)));
uint8_t gm_cel6p901dc1[1] __attribute__((aligned(1)));
uint8_t gm_cgsmfcq937e_2[16] __attribute__((aligned(8)));
uint8_t gm_cgt0a5n48ha[16] __attribute__((aligned(1)));
uint8_t gm_chkfraubo1r[4] __attribute__((aligned(4)));
uint8_t gm_cigjk56aans[16] __attribute__((aligned(1)));
uint8_t gm_cj5e0v8gk2i[20] __attribute__((aligned(1)));
uint8_t gm_cjajqd9smnj[12] __attribute__((aligned(1)));
uint8_t gm_cjg96tg3e0f[11] __attribute__((aligned(1)));
uint8_t gm_cjidvhvshci[19] __attribute__((aligned(1)));
uint8_t gm_cke4ts1najg[4] __attribute__((aligned(4)));
uint8_t gm_ckgo58jroh8[8] __attribute__((aligned(1)));
uint8_t gm_cl2be5u9pqk[16] __attribute__((aligned(8)));
uint8_t gm_clcchfce70r[32] __attribute__((aligned(1)));
uint8_t gm_cljjip91gu8[13] __attribute__((aligned(1)));
uint8_t gm_clkdg6jbli4[16] __attribute__((aligned(8)));
uint8_t gm_cm7k8v105sj[1] __attribute__((aligned(4)));
uint8_t gm_cmr3530gn23[13] __attribute__((aligned(1)));
uint8_t gm_cn8o84dqbf8[12] __attribute__((aligned(1)));
uint8_t gm_cn9bf8d1ktm[16] __attribute__((aligned(8)));
uint8_t gm_cnbt0kfagfe[2] __attribute__((aligned(1)));
uint8_t gm_cnkg4pj33ln[1] __attribute__((aligned(1)));
uint8_t gm_coeovpdckud[2] __attribute__((aligned(1)));
uint8_t gm_comi4sk42dt[8] __attribute__((aligned(1)));
uint8_t gm_cov7mr7mfr5[10] __attribute__((aligned(1)));
uint8_t gm_cp0utnbgndu[16] __attribute__((aligned(8)));
uint8_t gm_cphj36bitbq[688] __attribute__((aligned(8)));
uint8_t gm_cqacvlglfdh[7] __attribute__((aligned(1)));
uint8_t gm_cqq41hlun1j[5] __attribute__((aligned(1)));
uint8_t gm_cs5hilqh977[13] __attribute__((aligned(1)));
uint8_t gm_csmeedg7pnp[80] __attribute__((aligned(1)));
uint8_t gm_ct1fm60hrci[6] __attribute__((aligned(1)));
uint8_t gm_ctdm11uuaj6[16] __attribute__((aligned(8)));
uint8_t gm_ctpnchchr9h[8] __attribute__((aligned(1)));
uint8_t gm_k0ddgrnig17[40] __attribute__((aligned(8)));
uint8_t gm_k1ndla1vg9c[40] __attribute__((aligned(8)));
uint8_t gm_k2kjji37v2m[40] __attribute__((aligned(8)));
uint8_t gm_k6m9oif038q[40] __attribute__((aligned(8)));
uint8_t gm_k9pi0h2o5i2[40] __attribute__((aligned(8)));
uint8_t gm_kebji8epnem[40] __attribute__((aligned(8)));
uint8_t gm_khdoei7bkbm[40] __attribute__((aligned(8)));
uint8_t gm_khikhlemh0i[40] __attribute__((aligned(8)));
uint8_t gm_kjqpf95jhcm[40] __attribute__((aligned(8)));
uint8_t gm_kmlih2j5ae5[40] __attribute__((aligned(8)));
uint8_t gm_kmnip3ksasl[40] __attribute__((aligned(8)));
uint8_t gm_koniosmdv9m[40] __attribute__((aligned(8)));
uint8_t gm_kovthkfk7ta[40] __attribute__((aligned(8)));
uint8_t gm_kplh4t9vgh7[40] __attribute__((aligned(8)));
uint8_t gm_kq9hk6r9m7a[40] __attribute__((aligned(8)));
uint8_t gm_ktv0eonmebe[40] __attribute__((aligned(8)));
uint8_t gm_kv5f4tk0m2l[40] __attribute__((aligned(8)));
void *const gm_hl7iqllvekj4[] = {(void *)gm_f_for_body_cbj68cn};
void *const gm_hlib8i5oaffv[] = {(void *)gm_f_for_body_tdd1e60};
void *const gm_hlucu919n5n4[] = {(void *)gm_f_for_body_1avb45k};
void *const gm_hlvop6ag413a[] = {(void *)gm_f_for_body_n6686bi};
