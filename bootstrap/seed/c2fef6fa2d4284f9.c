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
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
struct gm_fr_args_6h3q549 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_exit_jfn9nl4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_var_otupsib {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
inline __attribute__((aligned(4))) gm_b16_8 gm_f_args_6h3q549(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_exit_jfn9nl4(void *env, gm_node *cx, gm_node *inst, uint8_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_8edr2np(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g88hj6k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_iqmk7ug(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_kocuepv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_l1ll8kh(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_var_otupsib(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern uint8_t gm_c08nj74isec[14] __attribute__((aligned(1)));
extern uint8_t gm_c0s28vl4pd8[17] __attribute__((aligned(1)));
extern uint8_t gm_c1a8m7m72ud[19] __attribute__((aligned(1)));
extern uint8_t gm_c1m49utf476[4] __attribute__((aligned(1)));
extern uint8_t gm_c1nbl7q1do7[23] __attribute__((aligned(1)));
extern uint8_t gm_c23qivcb2qk[16] __attribute__((aligned(8)));
extern uint8_t gm_c2hp1hl5ap9[6] __attribute__((aligned(1)));
extern uint8_t gm_c2pgnmr38k4[4] __attribute__((aligned(1)));
extern uint8_t gm_c3seud9n9be[1] __attribute__((aligned(1)));
extern uint8_t gm_c4i68jc4bv0[16] __attribute__((aligned(8)));
extern uint8_t gm_c5dlid9hf1q[12] __attribute__((aligned(1)));
extern uint8_t gm_c5llbmao9o5[3] __attribute__((aligned(1)));
extern uint8_t gm_c62sev3r509[1] __attribute__((aligned(1)));
extern uint8_t gm_c7167246mac[5] __attribute__((aligned(1)));
extern uint8_t gm_c72j1g8d455[1] __attribute__((aligned(1)));
extern uint8_t gm_c73ube8thpg[12] __attribute__((aligned(1)));
extern uint8_t gm_c78rm6v1bc8[2] __attribute__((aligned(1)));
extern uint8_t gm_c7bha71empo[20] __attribute__((aligned(1)));
extern uint8_t gm_c7k2o7e3uv7[3] __attribute__((aligned(1)));
extern uint8_t gm_c8lries3c2a[1442] __attribute__((aligned(1)));
extern uint8_t gm_c8qi7rt473p[96] __attribute__((aligned(1)));
extern uint8_t gm_c8tck9qi6pl[16] __attribute__((aligned(8)));
extern uint8_t gm_c90nlp6169j[1] __attribute__((aligned(1)));
extern uint8_t gm_c929husmmes[11] __attribute__((aligned(1)));
extern uint8_t gm_c9smt58mcql[5] __attribute__((aligned(1)));
extern uint8_t gm_cbkljfd0bjn[13] __attribute__((aligned(1)));
extern uint8_t gm_cbmpgied9p9[14] __attribute__((aligned(1)));
extern uint8_t gm_ccb2i0agl3a[10] __attribute__((aligned(1)));
extern uint8_t gm_cdgbrlsjvrb[16] __attribute__((aligned(8)));
extern uint8_t gm_cesm14gq4bu[7] __attribute__((aligned(1)));
extern uint8_t gm_cfhcj0skuhf[18] __attribute__((aligned(1)));
extern uint8_t gm_cfj2cvp0vvo[14] __attribute__((aligned(1)));
extern uint8_t gm_cg2gm9ndqso[4] __attribute__((aligned(1)));
extern uint8_t gm_cga37fdnf9v[11] __attribute__((aligned(1)));
extern uint8_t gm_ch037afqg9e[8] __attribute__((aligned(1)));
extern uint8_t gm_cio5gu1gbev[13] __attribute__((aligned(1)));
extern uint8_t gm_cj12hhe1r3t[6] __attribute__((aligned(1)));
extern uint8_t gm_cjvio2kihdk[3] __attribute__((aligned(1)));
extern uint8_t gm_ck98kig915p[7] __attribute__((aligned(1)));
extern uint8_t gm_cmkk46n1tqe[14] __attribute__((aligned(1)));
extern uint8_t gm_cms9a7p0moe[21] __attribute__((aligned(1)));
extern uint8_t gm_cmvun0d9m89[5] __attribute__((aligned(1)));
extern uint8_t gm_cn9tmpai0bm[9] __attribute__((aligned(1)));
extern uint8_t gm_cp9ta1t7ncn[10] __attribute__((aligned(1)));
extern uint8_t gm_crqb3tjd618[16] __attribute__((aligned(8)));
extern uint8_t gm_csb8vojob99[7] __attribute__((aligned(1)));
extern uint8_t gm_csvl88khklf[12] __attribute__((aligned(1)));
extern uint8_t gm_cta94eldhtr[24] __attribute__((aligned(1)));
extern uint8_t gm_cui00p62je5[7] __attribute__((aligned(1)));
extern uint8_t gm_cuk7s23am23[4] __attribute__((aligned(1)));
extern void *const gm_hl2p1t2nmcts[];
extern void *const gm_hlcpgr8cgvbp[];
extern void *const gm_hlfkkokngdk1[];
extern void *const gm_hlinnrcqopuj[];
extern void *const gm_hln6al4vijjv[];
extern void *const gm_hlnhfkgd9bs8[];
extern uint8_t gm_k0cjq15hqfg[40] __attribute__((aligned(8)));
extern uint8_t gm_k0gn7sac5pt[40] __attribute__((aligned(8)));
extern uint8_t gm_k12tcbqf2vk[40] __attribute__((aligned(8)));
extern uint8_t gm_k3ho3jie8mm[40] __attribute__((aligned(8)));
extern uint8_t gm_k3iblj1j3pp[40] __attribute__((aligned(8)));
extern uint8_t gm_k3pvi109nh9[40] __attribute__((aligned(8)));
extern uint8_t gm_k4hgfv8dkha[40] __attribute__((aligned(8)));
extern uint8_t gm_k6rrrg8di6i[40] __attribute__((aligned(8)));
extern uint8_t gm_k7peov41e38[40] __attribute__((aligned(8)));
extern uint8_t gm_k8fjm80oou5[40] __attribute__((aligned(8)));
extern uint8_t gm_ka7mhmnnlp9[40] __attribute__((aligned(8)));
extern uint8_t gm_kcdn8q7rsvc[40] __attribute__((aligned(8)));
extern uint8_t gm_kcu3lrib6j4[40] __attribute__((aligned(8)));
extern uint8_t gm_kdmasfkh2un[40] __attribute__((aligned(8)));
extern uint8_t gm_kdoa98a1tm0[40] __attribute__((aligned(8)));
extern uint8_t gm_kf24ji180ne[40] __attribute__((aligned(8)));
extern uint8_t gm_kft026ame64[40] __attribute__((aligned(8)));
extern uint8_t gm_kihs3be4rv6[40] __attribute__((aligned(8)));
extern uint8_t gm_kkpcsaqkj4k[40] __attribute__((aligned(8)));
extern uint8_t gm_kl5b02juaoa[40] __attribute__((aligned(8)));
extern uint8_t gm_knvhkf6cslq[40] __attribute__((aligned(8)));
extern uint8_t gm_kp550dhuma7[40] __attribute__((aligned(8)));
extern uint8_t gm_kr85ffi32os[24] __attribute__((aligned(8)));
extern uint8_t gm_krr4pbvd2tj[40] __attribute__((aligned(8)));
extern uint8_t gm_ksdcjqjfqsj[40] __attribute__((aligned(8)));
extern uint8_t gm_ksrlu8s6v53[40] __attribute__((aligned(8)));
extern uint8_t gm_ktbnd3fuoao[40] __attribute__((aligned(8)));
extern const uint8_t gm_s00odc15r3q8f[16];
extern const uint8_t gm_s02ktme6orfkh[7];
extern const uint8_t gm_s039kgqbmm0mv[10];
extern const uint8_t gm_s03c24qg7i06q[13];
extern const uint8_t gm_s041tbii048j3[8];
extern const uint8_t gm_s06mk2gjnlvf8[73];
extern const uint8_t gm_s06sbklusedgl[5];
extern const uint8_t gm_s08sdh8q6l3vj[49];
extern const uint8_t gm_s0eemp7of8buc[3];
extern const uint8_t gm_s0eqrtq5mqb3a[16];
extern const uint8_t gm_s0ffe093dar6e[19];
extern const uint8_t gm_s0fgdcd1ggor2[18];
extern const uint8_t gm_s0iid7nkkvf30[55];
extern const uint8_t gm_s0jho24omhfui[7];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0mpcnom7nq2p[47];
extern const uint8_t gm_s0mppcr6a4lgb[9];
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s0r9vb1qh90ue[116];
extern const uint8_t gm_s0sg56e5fp4di[12];
extern const uint8_t gm_s0stes97dk54i[147];
extern const uint8_t gm_s10qn2lc8ck56[78];
extern const uint8_t gm_s174sqhpcliev[43];
extern const uint8_t gm_s19m2tlro6kap[45];
extern const uint8_t gm_s1alpdaq78k1g[5];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1bf3a5i0lcmm[21];
extern const uint8_t gm_s1bpuqd44lur7[7];
extern const uint8_t gm_s1btmm2esmgsd[27];
extern const uint8_t gm_s1dmrsprfdp53[8];
extern const uint8_t gm_s1gm12po6spua[18];
extern const uint8_t gm_s1in79aaeui99[10];
extern const uint8_t gm_s1lomal6al3te[71];
extern const uint8_t gm_s1mh1dhprjqfo[19];
extern const uint8_t gm_s1qrqfh656g4q[6];
extern const uint8_t gm_s1sspofc2u3bb[37];
extern const uint8_t gm_s1tfd03l0gv6e[14];
extern const uint8_t gm_s1u0m6lm5u1su[7];
extern const uint8_t gm_s20tdu9mtc6fi[8];
extern const uint8_t gm_s289ga0mk9fi6[20];
extern const uint8_t gm_s2c267emk2nni[6];
extern const uint8_t gm_s2c5ns5u9qs90[35];
extern const uint8_t gm_s2cartb32o2of[10];
extern const uint8_t gm_s2gpk5q7mg4o8[5];
extern const uint8_t gm_s2h8cr50573go[8];
extern const uint8_t gm_s2hgdf6b9d53s[5];
extern const uint8_t gm_s2j5j1obh5p7l[6];
extern const uint8_t gm_s2micd2hub1nf[26];
extern const uint8_t gm_s2mvbldeg43ph[9];
extern const uint8_t gm_s2n0v309tod60[70];
extern const uint8_t gm_s2n3d0ogcjggg[14];
extern const uint8_t gm_s2p0dacnmvuhi[7];
extern const uint8_t gm_s2qp8mqi0c2le[48];
extern const uint8_t gm_s2r7keqsravo0[6];
extern const uint8_t gm_s2ttad1cg71bm[18];
extern const uint8_t gm_s2v8uomf8pkfv[13];
extern const uint8_t gm_s30cnsbbn0c4o[4];
extern const uint8_t gm_s31holt846m75[10];
extern const uint8_t gm_s32j5fa04tkqj[41];
extern const uint8_t gm_s352761194jbs[9];
extern const uint8_t gm_s35hbb2rhhdvv[33];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3adv1e9meuc1[15];
extern const uint8_t gm_s3dov523r8ujn[53];
extern const uint8_t gm_s3e0g1895m0j9[2];
extern const uint8_t gm_s3emon0a31046[81];
extern const uint8_t gm_s3ergh2il1tjf[43];
extern const uint8_t gm_s3hersi16nhbp[34];
extern const uint8_t gm_s3i3n5bvpvmmi[16];
extern const uint8_t gm_s3ijb9gbhdbib[27];
extern const uint8_t gm_s3j69epdb3egk[26];
extern const uint8_t gm_s3kb1co7e63ka[37];
extern const uint8_t gm_s3klpr9p78hh4[24];
extern const uint8_t gm_s3lq4s2q8ds4c[32];
extern const uint8_t gm_s3lv7dnoanjke[5];
extern const uint8_t gm_s3m769if3iik9[32];
extern const uint8_t gm_s3o37fp7c0sqt[15];
extern const uint8_t gm_s3o8dsbklmfsh[10];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3s4n6ehcrs2b[17];
extern const uint8_t gm_s3tacjdl2ciei[13];
extern const uint8_t gm_s41047ga9d9tq[26];
extern const uint8_t gm_s41bap0m04tt9[12];
extern const uint8_t gm_s41uc8v0dkl4a[15];
extern const uint8_t gm_s42aeamnkrchf[31];
extern const uint8_t gm_s43ls0s4m665j[57];
extern const uint8_t gm_s43sk7g92lovh[13];
extern const uint8_t gm_s458j1t80v8nr[15];
extern const uint8_t gm_s45i8231l5fma[10];
extern const uint8_t gm_s465vmg9c4jq6[14];
extern const uint8_t gm_s46khuklhbvqd[21];
extern const uint8_t gm_s495moo6tjeh0[20];
extern const uint8_t gm_s4a6o0t8isvlm[17];
extern const uint8_t gm_s4a980fahtblo[3];
extern const uint8_t gm_s4div03f0lvce[12];
extern const uint8_t gm_s4ft5s33591qg[7];
extern const uint8_t gm_s4ko774bb2ea9[56];
extern const uint8_t gm_s4l676io35g5u[12];
extern const uint8_t gm_s4oil3epcv041[3];
extern const uint8_t gm_s4oloc6hggc50[92];
extern const uint8_t gm_s4pucugl3svdm[11];
extern const uint8_t gm_s4rtd6brb45fq[41];
extern const uint8_t gm_s4t89div5i959[80];
extern const uint8_t gm_s4t8kk8uuclvr[27];
extern const uint8_t gm_s4vqnsfknimfg[28];
extern const uint8_t gm_s51qe9opse882[41];
extern const uint8_t gm_s52ul4dg63jhe[11];
extern const uint8_t gm_s56n05s0kn0p9[27];
extern const uint8_t gm_s585om8jb9mop[25];
extern const uint8_t gm_s58g91v8ecst5[13];
extern const uint8_t gm_s5974q1kmorlc[52];
extern const uint8_t gm_s59st1i1tm2m1[9];
extern const uint8_t gm_s5d58k7uql7sf[72];
extern const uint8_t gm_s5ecjs0hs1bpf[20];
extern const uint8_t gm_s5f9kvb1gm4j6[13];
extern const uint8_t gm_s5g1e6qrps9q6[19];
extern const uint8_t gm_s5irlrsnb30cc[24];
extern const uint8_t gm_s5j8t1cg2erp9[8];
extern const uint8_t gm_s5k9ll19r7m4g[4];
extern const uint8_t gm_s5l2i6ke0df90[21];
extern const uint8_t gm_s5m9qimbv7e8k[3];
extern const uint8_t gm_s5nt88lj0viqk[4];
extern const uint8_t gm_s5pm13tivcrur[15];
extern const uint8_t gm_s5rb1e1n8ivr7[48];
extern const uint8_t gm_s62t7qmf3ajh5[69];
extern const uint8_t gm_s63qpv8hba1m1[20];
extern const uint8_t gm_s65ftmvtj458k[11];
extern const uint8_t gm_s69o347hkiq9a[61];
extern const uint8_t gm_s6d57grm686e3[27];
extern const uint8_t gm_s6e8s18mgu3dj[19];
extern const uint8_t gm_s6f83og3gefc1[8];
extern const uint8_t gm_s6h2ge6vgkmhm[9];
extern const uint8_t gm_s6hgf366ivqrg[34];
extern const uint8_t gm_s6hp19vgdsi0o[24];
extern const uint8_t gm_s6ioufa56mnlh[5];
extern const uint8_t gm_s6kq1mi4s1g58[3];
extern const uint8_t gm_s6mvc5jp4o5mi[30];
extern const uint8_t gm_s6okk95q42e26[15];
extern const uint8_t gm_s6p5i58jsib94[8];
extern const uint8_t gm_s6p6u3ogotq4f[50];
extern const uint8_t gm_s6pceda0js0g8[6];
extern const uint8_t gm_s6peoato8pau5[13];
extern const uint8_t gm_s6pilvbjelm5p[4];
extern const uint8_t gm_s6t4r831mukdr[3];
extern const uint8_t gm_s6v6kgo7d00tn[41];
extern const uint8_t gm_s6vl5jv0tf2n4[8];
extern const uint8_t gm_s709tdhiijp4r[78];
extern const uint8_t gm_s76i8mdsri6em[70];
extern const uint8_t gm_s786i8duem00q[27];
extern const uint8_t gm_s78m6s8sukfpa[13];
extern const uint8_t gm_s79dh6evnpkuf[41];
extern const uint8_t gm_s7c2mvkr8kl49[35];
extern const uint8_t gm_s7cvig9qav6l5[3];
extern const uint8_t gm_s7hkl5mqa15gm[5];
extern const uint8_t gm_s7i2mni64durc[18];
extern const uint8_t gm_s7mosjq6fpdqc[15];
extern const uint8_t gm_s7mqv7hf4fso0[27];
extern const uint8_t gm_s7phrfovrc6ul[239];
extern const uint8_t gm_s7tfst2qj3lk8[24];
extern const uint8_t gm_s7vj61tk4g1rp[14];
extern const uint8_t gm_s7vusn2lig3dp[65];
extern const uint8_t gm_s802sbfa6mtsg[27];
extern const uint8_t gm_s84smn81idetm[11];
extern const uint8_t gm_s85hqf14lbkns[8];
extern const uint8_t gm_s85m9c4pt0ikp[6];
extern const uint8_t gm_s87e746eia9j4[6];
extern const uint8_t gm_s88lqrfvhc8i3[46];
extern const uint8_t gm_s8abg3vemofr6[4];
extern const uint8_t gm_s8aj3oag1l1u0[14];
extern const uint8_t gm_s8b49h6svih4r[82];
extern const uint8_t gm_s8ckgohq4msba[19];
extern const uint8_t gm_s8cr606ter6p9[39];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_s8gaig0cs2uce[19];
extern const uint8_t gm_s8i44sm1b77f2[121];
extern const uint8_t gm_s8ki76moa277n[23];
extern const uint8_t gm_s8m45dpj5ugr4[6];
extern const uint8_t gm_s8pjstf2pdmbk[14];
extern const uint8_t gm_s8ps5ka0vnf6d[42];
extern const uint8_t gm_s8qkarceaerkr[22];
extern const uint8_t gm_s8qvuobg6jcbo[13];
extern const uint8_t gm_s8rsp9val5ful[5];
extern const uint8_t gm_s8sv99c8q8gbc[71];
extern const uint8_t gm_s8ukphi4920cc[11];
extern const uint8_t gm_s92hgp9htn7k0[21];
extern const uint8_t gm_s930a5k4dd69o[10];
extern const uint8_t gm_s93ft79mfnlvp[10];
extern const uint8_t gm_s94v9o49bjk09[12];
extern const uint8_t gm_s959ni36b80a4[4];
extern const uint8_t gm_s99ra887gkc8u[39];
extern const uint8_t gm_s9a0n41a8i82u[4];
extern const uint8_t gm_s9eiod3sq367m[7];
extern const uint8_t gm_s9go9vvkpcjpd[48];
extern const uint8_t gm_s9grefqkso86a[3];
extern const uint8_t gm_s9kj8vgi6430a[6];
extern const uint8_t gm_s9loa14tklecu[3];
extern const uint8_t gm_s9oo6ammv0sch[17];
extern const uint8_t gm_s9p37ubghllhn[64];
extern const uint8_t gm_s9tlur6oo59qa[31];
extern const uint8_t gm_s9vag5co36u2r[18];
extern const uint8_t gm_sa1pcp92k3aj3[7];
extern const uint8_t gm_sa39b9bt4jpna[36];
extern const uint8_t gm_sa3hu8v6op70v[41];
extern const uint8_t gm_sa8mbp35lh2ts[84];
extern const uint8_t gm_sa9mckopi2o5a[86];
extern const uint8_t gm_sabq7k0io9ql3[66];
extern const uint8_t gm_saefgd8cv1aea[20];
extern const uint8_t gm_sahim4484f9l7[3];
extern const uint8_t gm_sajpbdgkjnse7[8];
extern const uint8_t gm_sal1kdi1rij5t[10];
extern const uint8_t gm_sao4mumvadoct[4];
extern const uint8_t gm_saoa4m7dh2vl3[55];
extern const uint8_t gm_sapu6l8enc7d7[7];
extern const uint8_t gm_sasea0sobfkrs[8];
extern const uint8_t gm_sauknalgur9no[11];
extern const uint8_t gm_sav0q47tkm7j2[20];
extern const uint8_t gm_savlppb39i8g6[6];
extern const uint8_t gm_savvoucn0tcn5[41];
extern const uint8_t gm_sb0d5eutumlsm[41];
extern const uint8_t gm_sb1nc8gh4as5h[15];
extern const uint8_t gm_sb2nfo787ropg[6];
extern const uint8_t gm_sb335j99bk4ch[41];
extern const uint8_t gm_sb398tg9q30m5[6];
extern const uint8_t gm_sb3c9ehk423nf[6];
extern const uint8_t gm_sb3o3ij0n2htb[5];
extern const uint8_t gm_sb455a3lakuom[60];
extern const uint8_t gm_sb54ui2amic32[2];
extern const uint8_t gm_sb8ag7vcv52jt[5];
extern const uint8_t gm_sb8c0irhu65ra[140];
extern const uint8_t gm_sbe7f0bkvca75[43];
extern const uint8_t gm_sbec11r2rv4ti[23];
extern const uint8_t gm_sbkmg1rlq4fa7[27];
extern const uint8_t gm_sbm4560lv1lt5[25];
extern const uint8_t gm_sbvdvkd7bikd7[25];
extern const uint8_t gm_sbvet41ukod2k[16];
extern const uint8_t gm_sc0brfbvonphe[64];
extern const uint8_t gm_sc0mj6111f7cc[10];
extern const uint8_t gm_sc19lloml9nb7[17];
extern const uint8_t gm_sc3mvoa74bm5n[41];
extern const uint8_t gm_sc46jkof3csqo[11];
extern const uint8_t gm_sc4daeflaltnv[3];
extern const uint8_t gm_sc4i6tq8tg5gu[13];
extern const uint8_t gm_sc4r4l9k6f577[3];
extern const uint8_t gm_sc5ekkhtqttfb[4];
extern const uint8_t gm_sc7jgo9oqhoja[21];
extern const uint8_t gm_sc7nhpcejp1mj[21];
extern const uint8_t gm_sc80lo6tf645h[31];
extern const uint8_t gm_sc9p2e0c29ni7[29];
extern const uint8_t gm_scak4selic6pl[21];
extern const uint8_t gm_scbrbhepkh2rj[25];
extern const uint8_t gm_scc6ms0hrfo8j[9];
extern const uint8_t gm_scd1bb5dijnis[15];
extern const uint8_t gm_scf54410fgl4j[39];
extern const uint8_t gm_scfdk5i1nqc6a[30];
extern const uint8_t gm_scifaj4dn1ok5[6];
extern const uint8_t gm_scm3afv9qr78v[5];
extern const uint8_t gm_scmcg8350emvu[19];
extern const uint8_t gm_scn0a8pnba0ho[41];
extern const uint8_t gm_scn0pl9eq3k2k[24];
extern const uint8_t gm_scne9acm49p4h[11];
extern const uint8_t gm_scommn379heqq[14];
extern const uint8_t gm_scq06n33arhgv[5];
extern const uint8_t gm_scq0i5n3f7it5[41];
extern const uint8_t gm_sct928cj9v78e[137];
extern const uint8_t gm_sd1gv97l675vb[26];
extern const uint8_t gm_sd323o3qr3mfd[64];
extern const uint8_t gm_sd4k4nkk0h9t7[14];
extern const uint8_t gm_sd4sd2h1n94bh[7];
extern const uint8_t gm_sd667aplubiqu[34];
extern const uint8_t gm_sd6vqnf9glo8m[51];
extern const uint8_t gm_sd8o4m7695hpq[38];
extern const uint8_t gm_sd96cluvcn3lj[33];
extern const uint8_t gm_sd99er7oafmtu[17];
extern const uint8_t gm_sd9o9rh5qe8e7[11];
extern const uint8_t gm_sdarhudj711nk[17];
extern const uint8_t gm_sdbih7ptecvdq[39];
extern const uint8_t gm_sdes3rh8ctb7v[21];
extern const uint8_t gm_sdfl2t7s13ucr[12];
extern const uint8_t gm_sdg1ovmjdibb5[6];
extern const uint8_t gm_sdh3pk0b3jsnu[67];
extern const uint8_t gm_sdhlphf1ucpbo[18];
extern const uint8_t gm_sdi58fds59v3t[30];
extern const uint8_t gm_sdinre9qvo2vq[28];
extern const uint8_t gm_sdjk9c005ec9t[13];
extern const uint8_t gm_sdo8qcbf74mp0[7];
extern const uint8_t gm_sdo9t6m49dsco[5];
extern const uint8_t gm_sdogehgrilpeb[3];
extern const uint8_t gm_sdpf7uf4ias00[24];
extern const uint8_t gm_sdqssko83l66b[28];
extern const uint8_t gm_sdrevll5io08q[10];
extern const uint8_t gm_sdvtt0l0jh9bp[10];
extern const uint8_t gm_se08n5d1gngu7[5];
extern const uint8_t gm_se1t3fpfm9j7m[12];
extern const uint8_t gm_se25mtccq2mtg[42];
extern const uint8_t gm_se4r520bkfair[18];
extern const uint8_t gm_se5tulnsn3o9s[10];
extern const uint8_t gm_se64as7gfqfsc[36];
extern const uint8_t gm_se8ggmq7h9bin[35];
extern const uint8_t gm_seak3hcpfjfp2[70];
extern const uint8_t gm_sebnh3ba276b8[17];
extern const uint8_t gm_secrjke11f3ph[13];
extern const uint8_t gm_seeo26ergtcha[5];
extern const uint8_t gm_seflbetisd1ui[3];
extern const uint8_t gm_seg7e1ca8e64l[5];
extern const uint8_t gm_segch6a5dme9u[5];
extern const uint8_t gm_segn0pv9sd905[41];
extern const uint8_t gm_segr993uh2crm[11];
extern const uint8_t gm_seh85cpcgte1u[57];
extern const uint8_t gm_sehgil1ui47md[7];
extern const uint8_t gm_seimuk2ck2a6g[38];
extern const uint8_t gm_sek5urp41gn7l[48];
extern const uint8_t gm_selnb2co583fc[6];
extern const uint8_t gm_senhrlorjabcl[3];
extern const uint8_t gm_seohfka1b0c2g[47];
extern const uint8_t gm_seojbs1tqr33k[46];
extern const uint8_t gm_sevf8q26aov70[6];
extern const uint8_t gm_sf2lr85hprd3e[40];
extern const uint8_t gm_sf3f9da3960bu[6];
extern const uint8_t gm_sf46mekb18qm8[14];
extern const uint8_t gm_sf9ilivr4c86m[4];
extern const uint8_t gm_sfau4tfjh7ajf[15];
extern const uint8_t gm_sfc071avthns7[21];
extern const uint8_t gm_sfcm41q17pr37[46];
extern const uint8_t gm_sffd375a90o1u[3];
extern const uint8_t gm_sffd7ppku35fr[10];
extern const uint8_t gm_sfg1v6bqcbbav[10];
extern const uint8_t gm_sfhbo1m6f9ql4[35];
extern const uint8_t gm_sfkqvfbp56vqq[11];
extern const uint8_t gm_sfl0m0vifcu3m[82];
extern const uint8_t gm_sfll40jgr4opl[4];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfq3o7cad07rv[14];
extern const uint8_t gm_sfvsgjcg86bbl[45];
extern const uint8_t gm_sg0ns322676oc[26];
extern const uint8_t gm_sg39dr0kaebm0[10];
extern const uint8_t gm_sg3dnv3iaqhkk[8];
extern const uint8_t gm_sg4faca6q9u0h[8];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg6b42o7q3v50[64];
extern const uint8_t gm_sg91clgj6l88m[6];
extern const uint8_t gm_sg92g0je3abps[14];
extern const uint8_t gm_sg93rj30beob9[29];
extern const uint8_t gm_sgb0oed9c3voo[5];
extern const uint8_t gm_sgc0h5so8ul3j[22];
extern const uint8_t gm_sgcku4bq4407l[19];
extern const uint8_t gm_sge7ihldgqi49[20];
extern const uint8_t gm_sge9mrih5euc5[13];
extern const uint8_t gm_sgekdmqbrecpp[9];
extern const uint8_t gm_sgf1euukkdve3[14];
extern const uint8_t gm_sgfqei77bsia8[4];
extern const uint8_t gm_sgh9nkjgl73of[27];
extern const uint8_t gm_sgiadc3jhhq91[23];
extern const uint8_t gm_sgmhc3klfmn46[5];
extern const uint8_t gm_sgmjl48sjjf69[22];
extern const uint8_t gm_sgmt0q35kl6hc[5];
extern const uint8_t gm_sgqifqduuc5vl[46];
extern const uint8_t gm_sgqs124m28gfg[7];
extern const uint8_t gm_sgrkf3bk77nul[17];
extern const uint8_t gm_sgtmc08t3k49t[42];
extern const uint8_t gm_sgukm1evmrjal[4];
extern const uint8_t gm_sh0adpq1lnkpj[40];
extern const uint8_t gm_sh0drhj4ffllf[11];
extern const uint8_t gm_sh0is7j09prt2[23];
extern const uint8_t gm_sh11qqm8nqu68[8];
extern const uint8_t gm_sh2bu6u8oapg5[73];
extern const uint8_t gm_sh43rpq1oqm5d[6];
extern const uint8_t gm_sh4ufq9bak51r[13];
extern const uint8_t gm_sh5cd76h4vacq[102];
extern const uint8_t gm_sh5f7b7njpj3l[6];
extern const uint8_t gm_sh60ig4u675k6[74];
extern const uint8_t gm_sh9sks72142ms[17];
extern const uint8_t gm_sha303gigke52[10];
extern const uint8_t gm_sharnvllqpvms[17];
extern const uint8_t gm_shb921rk4rkpo[5];
extern const uint8_t gm_shesnhdok5fgk[55];
extern const uint8_t gm_shf0g22rjm3o7[5];
extern const uint8_t gm_shfuf0tdpqgc0[320];
extern const uint8_t gm_shhn92c601tgo[29];
extern const uint8_t gm_shi7nidbc64qf[4];
extern const uint8_t gm_shl9ceh0q5tpe[6];
extern const uint8_t gm_shn8mjp9kc2n2[9];
extern const uint8_t gm_shncvaenjonsu[41];
extern const uint8_t gm_shntnl6m6g3am[27];
extern const uint8_t gm_shpgcu8us1vm6[11];
extern const uint8_t gm_shpjiqltl04un[33];
extern const uint8_t gm_shq77is85iktk[17];
extern const uint8_t gm_shroj9d6fajb6[4];
extern const uint8_t gm_shror0skne60i[45];
extern const uint8_t gm_shs874j97ior1[14];
extern const uint8_t gm_shsoptr61c2h5[41];
extern const uint8_t gm_shv3ttlq9l359[15];
extern const uint8_t gm_shv5ofgc65v68[3];
extern const uint8_t gm_si6sr5gqkcui7[6];
extern const uint8_t gm_si8jv2q8d22kl[10];
extern const uint8_t gm_sia6bdhdcj5co[13];
extern const uint8_t gm_siabpto2siqmj[7];
extern const uint8_t gm_sidiiakhbvqqi[9];
extern const uint8_t gm_sig8ahbvm8eb8[10];
extern const uint8_t gm_sigjilgl161c6[5];
extern const uint8_t gm_sigq4ahgd0l2a[64];
extern const uint8_t gm_sigup1gvr7a13[5];
extern const uint8_t gm_siihmb4jo3dsn[125];
extern const uint8_t gm_siji3oo7ud53t[10];
extern const uint8_t gm_sijp8lh2g2ld5[34];
extern const uint8_t gm_sipi37glknm5f[16];
extern const uint8_t gm_siuacbf0blrje[9];
extern const uint8_t gm_siv8n5ojlp0dd[18];
extern const uint8_t gm_sivm2ee5886a0[8];
extern const uint8_t gm_sj2cfogjb0pbr[41];
extern const uint8_t gm_sj49v74ko3ghf[15];
extern const uint8_t gm_sj56dkk1peu3m[54];
extern const uint8_t gm_sj5f575ekojrn[41];
extern const uint8_t gm_sj5j224m4ulf9[3];
extern const uint8_t gm_sj9krj49k3580[10];
extern const uint8_t gm_sj9kuut55po7f[46];
extern const uint8_t gm_sjbjet56e96co[17];
extern const uint8_t gm_sjcm18p35o9rq[4];
extern const uint8_t gm_sjdrosvte680q[12];
extern const uint8_t gm_sjfdnh735o7f8[5];
extern const uint8_t gm_sjg4ie5b5ufsm[8];
extern const uint8_t gm_sjgrq0dv175qo[30];
extern const uint8_t gm_sjiao13m1cclm[24];
extern const uint8_t gm_sjkj5g78lejrj[4];
extern const uint8_t gm_sjm54r6h90i7e[63];
extern const uint8_t gm_sjnkguo0atpm5[3];
extern const uint8_t gm_sjssolp3qfpqg[7];
extern const uint8_t gm_sjtp8ip8t28uh[7];
extern const uint8_t gm_sjtqic917nd2g[6];
extern const uint8_t gm_sk2dpusgkm375[22];
extern const uint8_t gm_sk34jm61h5m0d[3];
extern const uint8_t gm_sk3i3et1jqa7c[12];
extern const uint8_t gm_sk531tvbsm11t[44];
extern const uint8_t gm_sk721elh8ouv2[11];
extern const uint8_t gm_sk8hn2ar9njah[15];
extern const uint8_t gm_sk8r2qbdun3l0[35];
extern const uint8_t gm_sk9at358cmauo[27];
extern const uint8_t gm_skbd3tdpfkbk5[24];
extern const uint8_t gm_skedh3k15mgd3[18];
extern const uint8_t gm_skff6pclobvnu[40];
extern const uint8_t gm_skitebsmtce1i[34];
extern const uint8_t gm_skjg6jfphs2d0[10];
extern const uint8_t gm_skk0hh6be703h[20];
extern const uint8_t gm_skm5ng1ir37l8[8];
extern const uint8_t gm_skmai4uvseje8[24];
extern const uint8_t gm_skmfilbqoc8lo[87];
extern const uint8_t gm_skmueji31ds5r[49];
extern const uint8_t gm_skpk7m7mpsr06[6];
extern const uint8_t gm_skpueg68n121l[41];
extern const uint8_t gm_skq0s129a3p6p[41];
extern const uint8_t gm_skr00pujn28ln[29];
extern const uint8_t gm_skvko85ql2e6c[62];
extern const uint8_t gm_sl2gfi3ik4b1u[11];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl4gq9e3k2i6t[19];
extern const uint8_t gm_sl58ekc3js5bs[30];
extern const uint8_t gm_sl6br25hfjb5f[9];
extern const uint8_t gm_sl84tf4h2e7v7[29];
extern const uint8_t gm_sl93ka76m8q9k[41];
extern const uint8_t gm_sla366gd7nphd[58];
extern const uint8_t gm_sla3unrbmgc0v[6];
extern const uint8_t gm_slb6cj7ndgvdm[6];
extern const uint8_t gm_slcgg5l8i9236[50];
extern const uint8_t gm_slfinpjh0406b[47];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slhllpfip1vpg[3];
extern const uint8_t gm_sli291jkgotin[31];
extern const uint8_t gm_sljdfefkkeqgn[90];
extern const uint8_t gm_sljqt9ip2cpnb[41];
extern const uint8_t gm_sllcm1pqagh28[6];
extern const uint8_t gm_sln88h001q3mp[19];
extern const uint8_t gm_slof5tl7m8grj[4];
extern const uint8_t gm_slogre7vmagad[35];
extern const uint8_t gm_slolcr6ikvg33[7];
extern const uint8_t gm_slpqve1dqssq0[8];
extern const uint8_t gm_slq8b0dsnkd8e[14];
extern const uint8_t gm_slr4hrkmmmk3q[7];
extern const uint8_t gm_slsa15rinti36[12];
extern const uint8_t gm_slsbknfpio6sr[9];
extern const uint8_t gm_sm0105pgqu27v[17];
extern const uint8_t gm_sm55k23067j17[16];
extern const uint8_t gm_sm57fhlj6aanb[62];
extern const uint8_t gm_sm5t84rdabed6[41];
extern const uint8_t gm_sm78jth420g98[9];
extern const uint8_t gm_smbjf525bk9a0[20];
extern const uint8_t gm_smcv6j7b18cjr[17];
extern const uint8_t gm_smgvt83efirng[53];
extern const uint8_t gm_smh4q1ndijmcf[39];
extern const uint8_t gm_smiavg3q7ucd9[8];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smop6ueqrddak[77];
extern const uint8_t gm_smp6ue9h98hbc[31];
extern const uint8_t gm_smpb7l3te3i8n[52];
extern const uint8_t gm_smr5ddngcjuao[19];
extern const uint8_t gm_smrla0cd2esrp[14];
extern const uint8_t gm_sn1m0t4k3vpa5[5];
extern const uint8_t gm_sn2qlgie5sklu[45];
extern const uint8_t gm_sn4e8vibva5m7[45];
extern const uint8_t gm_sn5pv7k63ni4c[11];
extern const uint8_t gm_sn5rcg6kpf9gq[36];
extern const uint8_t gm_sn9fg2capllh2[79];
extern const uint8_t gm_snauhge6bheis[14];
extern const uint8_t gm_snbkfu70dq3fp[33];
extern const uint8_t gm_snggon8entbp9[32];
extern const uint8_t gm_sngn7trhgdnc2[43];
extern const uint8_t gm_snh2jpqenu534[17];
extern const uint8_t gm_snhr0nlm0epj5[60];
extern const uint8_t gm_snja7imrvdfb7[11];
extern const uint8_t gm_snjonc4tmu56e[21];
extern const uint8_t gm_snl0g0eaf7ift[8];
extern const uint8_t gm_snl2gc9fh9n9n[41];
extern const uint8_t gm_snldug1bl0l7h[6];
extern const uint8_t gm_snls9i3gr3n3f[41];
extern const uint8_t gm_sno069vrt5g9q[4];
extern const uint8_t gm_snq4uaiieftl8[6];
extern const uint8_t gm_snr5o5tak6405[4];
extern const uint8_t gm_sns1hl83hdlcn[5];
extern const uint8_t gm_sns2lv549pkqt[53];
extern const uint8_t gm_snsu3jrv1dbtj[32];
extern const uint8_t gm_snt446oeijaq5[13];
extern const uint8_t gm_snukl6jqep6jf[4];
extern const uint8_t gm_snurnbocs9786[49];
extern const uint8_t gm_snvncjka7ph6m[7];
extern const uint8_t gm_so4bvja70211d[63];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so6kuvn2nvuue[4];
extern const uint8_t gm_so77sfkiulkfr[11];
extern const uint8_t gm_so78814tnt5g5[2];
extern const uint8_t gm_so9fi6r28770a[3];
extern const uint8_t gm_so9fl7jecsmrb[27];
extern const uint8_t gm_soa9mnl4ahjud[8];
extern const uint8_t gm_soakgb2h3g40i[4];
extern const uint8_t gm_socribpa518de[13];
extern const uint8_t gm_sod98slk3vnra[20];
extern const uint8_t gm_sodpgd6927k7l[33];
extern const uint8_t gm_sof77pc3u6m60[25];
extern const uint8_t gm_sog4l3h5q9oa6[84];
extern const uint8_t gm_soj8art8cj0h6[25];
extern const uint8_t gm_solbcai20h8gs[7];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_somhu4vltiil1[4];
extern const uint8_t gm_soohe0i8iafj3[20];
extern const uint8_t gm_soq2d9k5a34bc[8];
extern const uint8_t gm_soqhrfmr1feii[7];
extern const uint8_t gm_sorvett7uvsl2[41];
extern const uint8_t gm_sotb03b2eqtp5[20];
extern const uint8_t gm_soulf4opjqcvv[10];
extern const uint8_t gm_sovcjofctijvj[47];
extern const uint8_t gm_sovg8lhrcqaa0[34];
extern const uint8_t gm_sovtt6julbcd8[12];
extern const uint8_t gm_sovv48653dnju[40];
extern const uint8_t gm_sp02qqgucmpt7[3];
extern const uint8_t gm_sp1cnbsshsv9k[18];
extern const uint8_t gm_sp2a19d61vbe0[4];
extern const uint8_t gm_sp39731r4i1pk[10];
extern const uint8_t gm_sp3k6emq42nh4[10];
extern const uint8_t gm_sp3rdossibc7f[4];
extern const uint8_t gm_sp7cdd0n91vli[8];
extern const uint8_t gm_sp8o48ld4n4jq[24];
extern const uint8_t gm_sp8ppesjprts0[67];
extern const uint8_t gm_sp929adkqo1cd[93];
extern const uint8_t gm_spak5smc8j24b[14];
extern const uint8_t gm_spbqvmg1179vc[24];
extern const uint8_t gm_spdkbvjffkg4b[11];
extern const uint8_t gm_spe8t3e79vnal[7];
extern const uint8_t gm_spevct0q3cjgb[5];
extern const uint8_t gm_spf3a0kmdpgm5[6];
extern const uint8_t gm_spffvcegd6os6[6];
extern const uint8_t gm_sphmte453j9c2[12];
extern const uint8_t gm_spmlh93537vbl[26];
extern const uint8_t gm_spmv5l6cklh5d[18];
extern const uint8_t gm_spn4jhv77b56t[23];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_spp1c7975jken[5];
extern const uint8_t gm_sppohbjp90mol[3];
extern const uint8_t gm_sprospqi1g16o[7];
extern const uint8_t gm_spun41oorb00o[30];
extern const uint8_t gm_spv9savsu9h0b[4];
extern const uint8_t gm_spvdlp3fsj44k[2];
extern const uint8_t gm_sq02q1bjdgrej[123];
extern const uint8_t gm_sq12h87ngj5nl[9];
extern const uint8_t gm_sq4o785gt8bsh[10];
extern const uint8_t gm_sq5khm5l62ens[37];
extern const uint8_t gm_sq68mfjf87n5h[16];
extern const uint8_t gm_sq94kuvk30n36[6];
extern const uint8_t gm_sqahda31evi4f[9];
extern const uint8_t gm_sqas1r3k72n65[51];
extern const uint8_t gm_sqci13ris3rb0[16];
extern const uint8_t gm_sqcpnesgf4t4h[41];
extern const uint8_t gm_sqcvjnsub5bfs[53];
extern const uint8_t gm_sqdfr4k3vea5p[4];
extern const uint8_t gm_sqhj00be09q5v[9];
extern const uint8_t gm_sqirjnd2segia[40];
extern const uint8_t gm_sqkcsn1qihjbl[41];
extern const uint8_t gm_sqldj8dr0j7rf[12];
extern const uint8_t gm_sqrh6i8djv6b4[7];
extern const uint8_t gm_sqslcbfraugjg[8];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sqtouugmg03b1[5];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sr1dn90ca9rnt[9];
extern const uint8_t gm_sr2kfei26cj64[18];
extern const uint8_t gm_sr2qa646easkn[56];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr8de1o4do8v1[11];
extern const uint8_t gm_srbq5qj276c59[30];
extern const char gm_src2955num0q9tm[];
extern const char gm_src2u46tincrqmv[];
extern const char gm_src6hq50r4rk7e6[];
extern const char gm_src94heo671ss5e[];
extern const char gm_srcfde9lsgge1ld[];
extern const char gm_srcgc0h5so8ul3j[];
extern const char gm_srcn13n69hd0cht[];
extern const char gm_srcns60k96jj1mt[];
extern const char gm_srco24pprak16p4[];
extern const char gm_srco4ljquuk5d87[];
extern const char gm_srcrchqfd7obmqe[];
extern const uint8_t gm_srcs6slmq8ak7[15];
extern const char gm_srcsk3h2du2ldnc[];
extern const uint8_t gm_srcsoo8lineh7[3];
extern const char gm_srcuiab49hlgv7g[];
extern const uint8_t gm_srefpimq1sas7[4];
extern const uint8_t gm_srg9h7ti0cg3a[25];
extern const uint8_t gm_srjc4stueafqk[43];
extern const uint8_t gm_srjddrdnspo96[29];
extern const uint8_t gm_srk8hgivpmv60[18];
extern const uint8_t gm_srkue5qoqvtpg[41];
extern const uint8_t gm_srni26bloa03c[12];
extern const uint8_t gm_srol0ktjpbtfv[58];
extern const uint8_t gm_srs4ljjaijhd2[24];
extern const uint8_t gm_srvm34bc3an1a[113];
extern const uint8_t gm_ss3lpq4se4ir5[5];
extern const uint8_t gm_ss4e1np28qvnl[11];
extern const uint8_t gm_ss65eeqhkg7mg[12];
extern const uint8_t gm_ss6cscjfm4jfc[11];
extern const uint8_t gm_ss77f3h3rt6l8[30];
extern const uint8_t gm_ss7pu45qp7oic[6];
extern const uint8_t gm_ss7qhfi2s00na[11];
extern const uint8_t gm_ss8fufid8t1o1[32];
extern const uint8_t gm_ss8q42tvppqf5[79];
extern const uint8_t gm_ss8v6rf5v54r7[9];
extern const uint8_t gm_ssbpo4qatmvlq[7];
extern const uint8_t gm_ssd6k1tnvttos[15];
extern const uint8_t gm_ssd7h3tuipgq5[55];
extern const uint8_t gm_sse9uo3ffn4rj[167];
extern const uint8_t gm_ssg9s8mvpv2la[45];
extern const uint8_t gm_sshj97ls6n4f3[18];
extern const uint8_t gm_ssk4a01soguqj[7];
extern const uint8_t gm_sskd2e48pqlgu[13];
extern const uint8_t gm_ssmuvuejgvj6b[27];
extern const uint8_t gm_ssor6j0m3frue[14];
extern const uint8_t gm_ssqifgr2icd6s[3];
extern const uint8_t gm_ssri9mbl4p6m3[68];
extern const uint8_t gm_st52k2850n5r5[5];
extern const uint8_t gm_st5nnur2j246q[36];
extern const uint8_t gm_st8ki5f85lc2j[13];
extern const uint8_t gm_st9n2vprlv3c1[22];
extern const uint8_t gm_sta33ql7gg6dk[46];
extern const uint8_t gm_staavgn0someg[56];
extern const uint8_t gm_stb45mimrpgat[22];
extern const uint8_t gm_stbt78k7uqc0m[24];
extern const uint8_t gm_stc3e0j0moc2h[5];
extern const uint8_t gm_stdh0kbfgj33k[9];
extern const uint8_t gm_stidgejoe9ul2[11];
extern const uint8_t gm_stl5olh2jfr0d[40];
extern const uint8_t gm_sto6ssj6r66e3[30];
extern const uint8_t gm_stq0db711o573[15];
extern const uint8_t gm_strsld72u30ds[9];
extern const uint8_t gm_su0agms0tqg5k[15];
extern const uint8_t gm_su13ngtdjura3[15];
extern const uint8_t gm_su1oqg46rlg2d[10];
extern const uint8_t gm_su2eceicte7s7[6];
extern const uint8_t gm_su4retifs0sre[9];
extern const uint8_t gm_su5mku12ctc3k[92];
extern const uint8_t gm_su7ofilkdis5t[12];
extern const uint8_t gm_su823o267lacu[103];
extern const uint8_t gm_su8dibgnmpcpa[42];
extern const uint8_t gm_su9t2v4p35eij[8];
extern const uint8_t gm_su9tevpu8h5ad[19];
extern const uint8_t gm_suaek0bfou9a6[14];
extern const uint8_t gm_suctpm7bivu2g[48];
extern const uint8_t gm_sudnegb3umvac[12];
extern const uint8_t gm_sue26fe85rntf[5];
extern const uint8_t gm_sufil5dud65av[62];
extern const uint8_t gm_suio2i7004ufh[8];
extern const uint8_t gm_suj5r301h9vv4[9];
extern const uint8_t gm_suj9anpi7p1mq[25];
extern const uint8_t gm_sujdc8gmukk24[16];
extern const uint8_t gm_sujipgkhb2fih[8];
extern const uint8_t gm_sulfrs2bmdlu2[6];
extern const uint8_t gm_suno9ilftpfht[12];
extern const uint8_t gm_suor7sr2pkoei[12];
extern const uint8_t gm_supf808555d3d[13];
extern const uint8_t gm_sus38q16nfojv[7];
extern const uint8_t gm_sut5kfu0n4de9[5];
extern const uint8_t gm_sv10m8g115ng3[59];
extern const uint8_t gm_sv1iq1n2a39tg[17];
extern const uint8_t gm_sv2hkl8uspfr0[41];
extern const uint8_t gm_sv3d9namfmaua[9];
extern const uint8_t gm_sv3o54ps0ppci[19];
extern const uint8_t gm_sv61f25m0b9gs[4];
extern const uint8_t gm_sv89ta651gq7u[20];
extern const uint8_t gm_svaet5ugb1fj3[22];
extern const uint8_t gm_svdi2pk0uienp[45];
extern const uint8_t gm_sve2u8a7eapjo[18];
extern const uint8_t gm_svegu6hi5aibp[40];
extern const uint8_t gm_svghvlr5om8a6[29];
extern const uint8_t gm_svhavo13qha5k[4];
extern const uint8_t gm_svkdb6kaja59r[7];
extern const uint8_t gm_svl5ddjf64ul7[18];
extern const uint8_t gm_svnpflkpvd513[5];
extern const uint8_t gm_svp314e9ficjs[14];
extern const uint8_t gm_svqq3k9j6h1lu[8];
extern const uint8_t gm_svsegvhrrmf7h[18];
extern const uint8_t gm_svthrqc59hf0v[21];
extern const uint32_t gm_tpl00b7kgp37ng7_i[];
extern const gm_tmpl gm_tpl00rv7vuig9ur;
extern const uint32_t gm_tpl00rv7vuig9ur_i[];
extern const gm_tmpl_piece gm_tpl00rv7vuig9ur_p[];
extern const gm_spec *const gm_tpl00rv7vuig9ur_s[];
extern const gm_tmpl gm_tpl05osb04qe3ad;
extern const uint32_t gm_tpl05osb04qe3ad_i[];
extern const gm_tmpl_piece gm_tpl05osb04qe3ad_p[];
extern const gm_spec *const gm_tpl05osb04qe3ad_s[];
extern const gm_tmpl gm_tpl086m1ajg247c;
extern const uint32_t gm_tpl086m1ajg247c_i[];
extern const gm_tmpl_piece gm_tpl086m1ajg247c_p[];
extern const gm_spec *const gm_tpl086m1ajg247c_s[];
extern const gm_tmpl_piece gm_tpl0c7sat2t7j6v_p[];
extern const gm_tmpl_piece gm_tpl0e2i60vm7gtr_p[];
extern const gm_spec *const gm_tpl0elencma14kh_s[];
extern const gm_tmpl_piece gm_tpl0emlhhlpmv0s_p[];
extern const gm_spec *const gm_tpl0emlhhlpmv0s_s[];
extern const uint32_t gm_tpl0epjr4745p10_i[];
extern const gm_spec *const gm_tpl0eumfheo3vtt_s[];
extern const gm_spec *const gm_tpl0fhs0t9qdf1o_s[];
extern const gm_tmpl gm_tpl0fpifsdum8e8;
extern const uint32_t gm_tpl0fpifsdum8e8_i[];
extern const gm_tmpl_piece gm_tpl0fpifsdum8e8_p[];
extern const gm_spec *const gm_tpl0fpifsdum8e8_s[];
extern const gm_tmpl gm_tpl0lggla435q5r;
extern const uint32_t gm_tpl0lggla435q5r_i[];
extern const gm_tmpl_piece gm_tpl0lggla435q5r_p[];
extern const gm_spec *const gm_tpl0lggla435q5r_s[];
extern const gm_tmpl gm_tpl0o80ilivc5vg;
extern const uint32_t gm_tpl0o80ilivc5vg_i[];
extern const gm_tmpl_piece gm_tpl0o80ilivc5vg_p[];
extern const gm_spec *const gm_tpl0o80ilivc5vg_s[];
extern const uint32_t gm_tpl0oli8trq10sp_i[];
extern const uint32_t gm_tpl0rhldcdavmcg_i[];
extern const uint32_t gm_tpl0rlavm9oqi24_i[];
extern const uint32_t gm_tpl0td8lr3f4ven_i[];
extern const gm_tmpl_piece gm_tpl0vock46lp3ec_p[];
extern const gm_tmpl gm_tpl15v8rvh46nek;
extern const uint32_t gm_tpl15v8rvh46nek_i[];
extern const gm_tmpl_piece gm_tpl15v8rvh46nek_p[];
extern const gm_spec *const gm_tpl15v8rvh46nek_s[];
extern const gm_spec *const gm_tpl1bno78mc58dl_s[];
extern const gm_tmpl_piece gm_tpl1buo4rn7srlb_p[];
extern const gm_spec *const gm_tpl1c419vqpem2n_s[];
extern const gm_tmpl_piece gm_tpl1dmvfuuebopd_p[];
extern const gm_spec *const gm_tpl1f7c1o805fpf_s[];
extern const uint32_t gm_tpl1o8uodh0pvjc_i[];
extern const uint32_t gm_tpl1q6o13b940dg_i[];
extern const gm_tmpl gm_tpl1qq6rf1tb5ds;
extern const uint32_t gm_tpl1qq6rf1tb5ds_i[];
extern const gm_tmpl_piece gm_tpl1qq6rf1tb5ds_p[];
extern const gm_spec *const gm_tpl1qq6rf1tb5ds_s[];
extern const gm_tmpl_piece gm_tpl1sdu3l536hkg_p[];
extern const gm_tmpl_piece gm_tpl1t0vlt7tq67r_p[];
extern const gm_spec *const gm_tpl1t0vlt7tq67r_s[];
extern const uint32_t gm_tpl1u8koiam5nmo_i[];
extern const gm_spec *const gm_tpl1u8koiam5nmo_s[];
extern const gm_spec *const gm_tpl1umecooe9oti_s[];
extern const gm_spec *const gm_tpl20a4acojo21c_s[];
extern const gm_tmpl gm_tpl23mh8eh9cef6;
extern const uint32_t gm_tpl23mh8eh9cef6_i[];
extern const gm_tmpl_piece gm_tpl23mh8eh9cef6_p[];
extern const gm_spec *const gm_tpl23mh8eh9cef6_s[];
extern const gm_tmpl_piece gm_tpl29spsi1atllj_p[];
extern const uint32_t gm_tpl2arnv1ofugei_i[];
extern const uint32_t gm_tpl2fb8qlpeqlet_i[];
extern const gm_tmpl gm_tpl2gebin91ghlu;
extern const uint32_t gm_tpl2gebin91ghlu_i[];
extern const gm_tmpl_piece gm_tpl2gebin91ghlu_p[];
extern const gm_spec *const gm_tpl2gebin91ghlu_s[];
extern const uint32_t gm_tpl2jsq89buriv9_i[];
extern const gm_tmpl gm_tpl2lqr5lnbpsvm;
extern const uint32_t gm_tpl2lqr5lnbpsvm_i[];
extern const gm_tmpl_piece gm_tpl2lqr5lnbpsvm_p[];
extern const gm_spec *const gm_tpl2lqr5lnbpsvm_s[];
extern const gm_tmpl gm_tpl2od99j0nh2ah;
extern const uint32_t gm_tpl2od99j0nh2ah_i[];
extern const gm_tmpl_piece gm_tpl2od99j0nh2ah_p[];
extern const gm_spec *const gm_tpl2od99j0nh2ah_s[];
extern const gm_tmpl gm_tpl2re1r0fre48m;
extern const uint32_t gm_tpl2re1r0fre48m_i[];
extern const gm_tmpl_piece gm_tpl2re1r0fre48m_p[];
extern const gm_spec *const gm_tpl2re1r0fre48m_s[];
extern const gm_spec *const gm_tpl344kemfor61g_s[];
extern const uint32_t gm_tpl34g57qfcenfh_i[];
extern const gm_tmpl_piece gm_tpl34lim186g0fm_p[];
extern const gm_spec *const gm_tpl352s0p85j412_s[];
extern const gm_tmpl gm_tpl386j11hdkrib;
extern const uint32_t gm_tpl386j11hdkrib_i[];
extern const gm_tmpl_piece gm_tpl386j11hdkrib_p[];
extern const gm_spec *const gm_tpl386j11hdkrib_s[];
extern const gm_spec *const gm_tpl3melp777ubs8_s[];
extern const gm_spec *const gm_tpl3ssq6hkk3qrj_s[];
extern const uint32_t gm_tpl3tcah2c39gjk_i[];
extern const gm_tmpl gm_tpl409di1gnsfit;
extern const uint32_t gm_tpl409di1gnsfit_i[];
extern const gm_tmpl_piece gm_tpl409di1gnsfit_p[];
extern const gm_spec *const gm_tpl409di1gnsfit_s[];
extern const gm_spec *const gm_tpl40m4ah8dc3ae_s[];
extern const gm_tmpl gm_tpl46u099ch4aa2;
extern const uint32_t gm_tpl46u099ch4aa2_i[];
extern const gm_tmpl_piece gm_tpl46u099ch4aa2_p[];
extern const gm_spec *const gm_tpl46u099ch4aa2_s[];
extern const gm_tmpl_piece gm_tpl4g6bpqlb10dt_p[];
extern const uint32_t gm_tpl4isjs1bg9fhg_i[];
extern const gm_tmpl_piece gm_tpl4kmab8oiki17_p[];
extern const gm_tmpl_piece gm_tpl4mhnbmvg0r80_p[];
extern const gm_spec *const gm_tpl4n3cnfr6vo96_s[];
extern const gm_tmpl_piece gm_tpl4upfck3qu2er_p[];
extern const gm_tmpl gm_tpl51u3ki3ilqou;
extern const uint32_t gm_tpl51u3ki3ilqou_i[];
extern const gm_tmpl_piece gm_tpl51u3ki3ilqou_p[];
extern const gm_spec *const gm_tpl51u3ki3ilqou_s[];
extern const gm_tmpl gm_tpl53ihvkdedd31;
extern const uint32_t gm_tpl53ihvkdedd31_i[];
extern const gm_tmpl_piece gm_tpl53ihvkdedd31_p[];
extern const gm_spec *const gm_tpl53ihvkdedd31_s[];
extern const uint32_t gm_tpl54tnh29jjfju_i[];
extern const gm_spec *const gm_tpl54tnh29jjfju_s[];
extern const uint32_t gm_tpl56mvlv70ecs8_i[];
extern const gm_tmpl_piece gm_tpl5a8og2hcltt3_p[];
extern const gm_tmpl_piece gm_tpl5cbugllfe764_p[];
extern const gm_spec *const gm_tpl5kgb1tfsut02_s[];
extern const uint32_t gm_tpl5ldtdub2029b_i[];
extern const uint32_t gm_tpl5oph1bqhjo1e_i[];
extern const uint32_t gm_tpl5prdhjdrg194_i[];
extern const uint32_t gm_tpl5r25bpo8hobt_i[];
extern const uint32_t gm_tpl5rdj46lif6eq_i[];
extern const uint32_t gm_tpl5s6q12qjusi2_i[];
extern const gm_tmpl_piece gm_tpl5svfna5ij5ui_p[];
extern const gm_tmpl_piece gm_tpl5vfcnh2qs08h_p[];
extern const gm_tmpl gm_tpl60bi48lvp2qn;
extern const uint32_t gm_tpl60bi48lvp2qn_i[];
extern const gm_tmpl_piece gm_tpl60bi48lvp2qn_p[];
extern const gm_spec *const gm_tpl60bi48lvp2qn_s[];
extern const gm_tmpl gm_tpl60na9lt7qpkj;
extern const uint32_t gm_tpl60na9lt7qpkj_i[];
extern const gm_tmpl_piece gm_tpl60na9lt7qpkj_p[];
extern const gm_spec *const gm_tpl60na9lt7qpkj_s[];
extern const gm_spec *const gm_tpl6186pbdul6ge_s[];
extern const gm_tmpl_piece gm_tpl63iuq3je4sbr_p[];
extern const gm_tmpl_piece gm_tpl642cf7k81lop_p[];
extern const uint32_t gm_tpl64ltc7suc5qg_i[];
extern const gm_tmpl_piece gm_tpl64qrh372lslh_p[];
extern const gm_spec *const gm_tpl65jqebqot15g_s[];
extern const gm_tmpl_piece gm_tpl69qlgb4nvkqp_p[];
extern const gm_tmpl_piece gm_tpl6boglt2rkmh4_p[];
extern const uint32_t gm_tpl6f612gert49l_i[];
extern const gm_tmpl gm_tpl6fm59pne5ng7;
extern const uint32_t gm_tpl6fm59pne5ng7_i[];
extern const gm_tmpl_piece gm_tpl6fm59pne5ng7_p[];
extern const gm_spec *const gm_tpl6fm59pne5ng7_s[];
extern const gm_tmpl gm_tpl6hqg4qptvadc;
extern const uint32_t gm_tpl6hqg4qptvadc_i[];
extern const gm_tmpl_piece gm_tpl6hqg4qptvadc_p[];
extern const gm_spec *const gm_tpl6hqg4qptvadc_s[];
extern const uint32_t gm_tpl6itong91gv3f_i[];
extern const gm_spec *const gm_tpl6itong91gv3f_s[];
extern const uint32_t gm_tpl6lgf07g31gim_i[];
extern const gm_tmpl_piece gm_tpl6lpvgmmqlalm_p[];
extern const gm_spec *const gm_tpl6q4b2p6tude6_s[];
extern const gm_spec *const gm_tpl6r9reg6pnruq_s[];
extern const uint32_t gm_tpl6rgl0akrj87c_i[];
extern const uint32_t gm_tpl6tnfk0d9dl8e_i[];
extern const gm_spec *const gm_tpl6tu8f1h1mg85_s[];
extern const gm_tmpl gm_tpl70tkged35ahl;
extern const uint32_t gm_tpl70tkged35ahl_i[];
extern const gm_tmpl_piece gm_tpl70tkged35ahl_p[];
extern const gm_spec *const gm_tpl70tkged35ahl_s[];
extern const gm_tmpl_piece gm_tpl75b43ohtuvq2_p[];
extern const uint32_t gm_tpl78lk9ugsmc0b_i[];
extern const gm_tmpl_piece gm_tpl78lk9ugsmc0b_p[];
extern const gm_tmpl gm_tpl7b1ibjg22p4i;
extern const uint32_t gm_tpl7b1ibjg22p4i_i[];
extern const gm_tmpl_piece gm_tpl7b1ibjg22p4i_p[];
extern const gm_spec *const gm_tpl7b1ibjg22p4i_s[];
extern const gm_tmpl gm_tpl7b2kb2ainpff;
extern const uint32_t gm_tpl7b2kb2ainpff_i[];
extern const gm_tmpl_piece gm_tpl7b2kb2ainpff_p[];
extern const gm_spec *const gm_tpl7b2kb2ainpff_s[];
extern const gm_tmpl_piece gm_tpl7cjap0b34jjm_p[];
extern const gm_tmpl gm_tpl7e5tr64a0u6e;
extern const uint32_t gm_tpl7e5tr64a0u6e_i[];
extern const gm_tmpl_piece gm_tpl7e5tr64a0u6e_p[];
extern const gm_spec *const gm_tpl7e5tr64a0u6e_s[];
extern const gm_tmpl_piece gm_tpl7eehu7c1eqka_p[];
extern const uint32_t gm_tpl7ilj5n9objtq_i[];
extern const uint32_t gm_tpl7kdhh1egc9gc_i[];
extern const uint32_t gm_tpl7meurf74nbc1_i[];
extern const gm_tmpl_piece gm_tpl7net9ifts13e_p[];
extern const uint32_t gm_tpl7oj2vfshts60_i[];
extern const uint32_t gm_tpl7pm1mtuteh6j_i[];
extern const gm_tmpl_piece gm_tpl7t589bv3t4us_p[];
extern const gm_tmpl gm_tpl80avf3gokjno;
extern const uint32_t gm_tpl80avf3gokjno_i[];
extern const gm_tmpl_piece gm_tpl80avf3gokjno_p[];
extern const gm_spec *const gm_tpl80avf3gokjno_s[];
extern const gm_tmpl gm_tpl83pfuovi474u;
extern const uint32_t gm_tpl83pfuovi474u_i[];
extern const gm_tmpl_piece gm_tpl83pfuovi474u_p[];
extern const gm_spec *const gm_tpl83pfuovi474u_s[];
extern const gm_tmpl gm_tpl8f1efhudc5d4;
extern const uint32_t gm_tpl8f1efhudc5d4_i[];
extern const gm_tmpl_piece gm_tpl8f1efhudc5d4_p[];
extern const gm_spec *const gm_tpl8f1efhudc5d4_s[];
extern const gm_spec *const gm_tpl8gj5o7rmo2fl_s[];
extern const gm_spec *const gm_tpl8qng452btr2a_s[];
extern const uint32_t gm_tpl8s825rf2tq3f_i[];
extern const gm_tmpl gm_tpl8ssnfo686ogd;
extern const uint32_t gm_tpl8ssnfo686ogd_i[];
extern const gm_tmpl_piece gm_tpl8ssnfo686ogd_p[];
extern const gm_spec *const gm_tpl8ssnfo686ogd_s[];
extern const gm_spec *const gm_tpl8un0qv11eeom_s[];
extern const gm_spec *const gm_tpl91b7sjgnmine_s[];
extern const gm_tmpl_piece gm_tpl94ei7rae9si2_p[];
extern const gm_tmpl_piece gm_tpl9cnobsjbh0r2_p[];
extern const gm_tmpl_piece gm_tpl9e18q8to9md2_p[];
extern const gm_tmpl gm_tpl9iaqf5bbsv8o;
extern const uint32_t gm_tpl9iaqf5bbsv8o_i[];
extern const gm_tmpl_piece gm_tpl9iaqf5bbsv8o_p[];
extern const gm_spec *const gm_tpl9iaqf5bbsv8o_s[];
extern const gm_tmpl gm_tpl9ibjmfjq0iar;
extern const uint32_t gm_tpl9ibjmfjq0iar_i[];
extern const gm_tmpl_piece gm_tpl9ibjmfjq0iar_p[];
extern const gm_spec *const gm_tpl9ibjmfjq0iar_s[];
extern const gm_tmpl gm_tpl9jrfnomsrfk1;
extern const uint32_t gm_tpl9jrfnomsrfk1_i[];
extern const gm_tmpl_piece gm_tpl9jrfnomsrfk1_p[];
extern const gm_spec *const gm_tpl9jrfnomsrfk1_s[];
extern const gm_tmpl gm_tpl9kqhb5r0uube;
extern const uint32_t gm_tpl9kqhb5r0uube_i[];
extern const gm_tmpl_piece gm_tpl9kqhb5r0uube_p[];
extern const gm_spec *const gm_tpl9kqhb5r0uube_s[];
extern const gm_tmpl gm_tpl9kt3nn7p3a92;
extern const uint32_t gm_tpl9kt3nn7p3a92_i[];
extern const gm_tmpl_piece gm_tpl9kt3nn7p3a92_p[];
extern const gm_spec *const gm_tpl9kt3nn7p3a92_s[];
extern const gm_spec *const gm_tpl9qc0voknsp7g_s[];
extern const gm_spec *const gm_tpl9qg0hlqqke7e_s[];
extern const gm_spec *const gm_tpl9ufa4j1b7kal_s[];
extern const gm_tmpl_piece gm_tpl9uslg4i3oo2s_p[];
extern const gm_tmpl_piece gm_tpl9v7lb7n4a602_p[];
extern const gm_spec *const gm_tpla0a5a633lqve_s[];
extern const gm_tmpl gm_tpla1fscog446dv;
extern const uint32_t gm_tpla1fscog446dv_i[];
extern const gm_tmpl_piece gm_tpla1fscog446dv_p[];
extern const gm_spec *const gm_tpla1fscog446dv_s[];
extern const uint32_t gm_tpla1i8rr6vfju7_i[];
extern const gm_tmpl gm_tpla4jdv6pnguh4;
extern const uint32_t gm_tpla4jdv6pnguh4_i[];
extern const gm_tmpl_piece gm_tpla4jdv6pnguh4_p[];
extern const gm_spec *const gm_tpla4jdv6pnguh4_s[];
extern const gm_spec *const gm_tpla6b7o385jo3o_s[];
extern const uint32_t gm_tplaa2bqbq660if_i[];
extern const gm_tmpl gm_tplaahjt316h1v3;
extern const uint32_t gm_tplaahjt316h1v3_i[];
extern const gm_tmpl_piece gm_tplaahjt316h1v3_p[];
extern const gm_spec *const gm_tplaahjt316h1v3_s[];
extern const gm_tmpl_piece gm_tplae28pp9b49u1_p[];
extern const gm_tmpl_piece gm_tplaka4u9f9dfvk_p[];
extern const gm_spec *const gm_tplanhmvcr622qo_s[];
extern const gm_tmpl_piece gm_tplaoelja80vfud_p[];
extern const gm_tmpl_piece gm_tplaotru3rka6ku_p[];
extern const gm_tmpl_piece gm_tplb2ihdc8dbskd_p[];
extern const gm_spec *const gm_tplb5ldvki5nntc_s[];
extern const uint32_t gm_tplb899rpghqa93_i[];
extern const gm_tmpl_piece gm_tplb899rpghqa93_p[];
extern const gm_tmpl gm_tplbbopfj3f21eb;
extern const uint32_t gm_tplbbopfj3f21eb_i[];
extern const gm_tmpl_piece gm_tplbbopfj3f21eb_p[];
extern const gm_spec *const gm_tplbbopfj3f21eb_s[];
extern const gm_spec *const gm_tplbfdkthp45ai4_s[];
extern const uint32_t gm_tplbh7305mc9stu_i[];
extern const gm_tmpl_piece gm_tplblrluopde8aq_p[];
extern const gm_tmpl gm_tplbmcpcu1c6osb;
extern const uint32_t gm_tplbmcpcu1c6osb_i[];
extern const gm_tmpl_piece gm_tplbmcpcu1c6osb_p[];
extern const gm_spec *const gm_tplbmcpcu1c6osb_s[];
extern const gm_tmpl gm_tplbo11j4rgqkb3;
extern const uint32_t gm_tplbo11j4rgqkb3_i[];
extern const gm_tmpl_piece gm_tplbo11j4rgqkb3_p[];
extern const gm_spec *const gm_tplbo11j4rgqkb3_s[];
extern const gm_tmpl_piece gm_tplc1tlvqjnir84_p[];
extern const gm_spec *const gm_tplc3kt858jk6jp_s[];
extern const uint32_t gm_tplc65rc46hqohp_i[];
extern const gm_spec *const gm_tplc70rik6173ge_s[];
extern const gm_spec *const gm_tplc8kkdoja48ii_s[];
extern const uint32_t gm_tplcb1495nujsb7_i[];
extern const gm_tmpl gm_tplceemmmm7iuq6;
extern const uint32_t gm_tplceemmmm7iuq6_i[];
extern const gm_tmpl_piece gm_tplceemmmm7iuq6_p[];
extern const gm_spec *const gm_tplceemmmm7iuq6_s[];
extern const gm_tmpl gm_tplceihv99elj2t;
extern const uint32_t gm_tplceihv99elj2t_i[];
extern const gm_tmpl_piece gm_tplceihv99elj2t_p[];
extern const gm_spec *const gm_tplceihv99elj2t_s[];
extern const uint32_t gm_tplcjeg5mjcrb8d_i[];
extern const gm_tmpl_piece gm_tplcpp8u5i709mj_p[];
extern const gm_tmpl_piece gm_tplcqfaq9vjn7hh_p[];
extern const gm_spec *const gm_tplcqfaq9vjn7hh_s[];
extern const gm_spec *const gm_tpld1hhk0kef14l_s[];
extern const gm_tmpl gm_tplde4760b0jcjn;
extern const uint32_t gm_tplde4760b0jcjn_i[];
extern const gm_tmpl_piece gm_tplde4760b0jcjn_p[];
extern const gm_spec *const gm_tplde4760b0jcjn_s[];
extern const uint32_t gm_tpldg1m204qb08c_i[];
extern const gm_spec *const gm_tpldg1m204qb08c_s[];
extern const gm_tmpl gm_tpldiv6dh0or1km;
extern const uint32_t gm_tpldiv6dh0or1km_i[];
extern const gm_tmpl_piece gm_tpldiv6dh0or1km_p[];
extern const gm_spec *const gm_tpldiv6dh0or1km_s[];
extern const gm_tmpl gm_tpldknbfev12jrb;
extern const uint32_t gm_tpldknbfev12jrb_i[];
extern const gm_tmpl_piece gm_tpldknbfev12jrb_p[];
extern const gm_spec *const gm_tpldknbfev12jrb_s[];
extern const gm_tmpl gm_tpldl4pn21ood35;
extern const uint32_t gm_tpldl4pn21ood35_i[];
extern const gm_tmpl_piece gm_tpldl4pn21ood35_p[];
extern const gm_spec *const gm_tpldl4pn21ood35_s[];
extern const gm_tmpl gm_tpldm28gt81dafq;
extern const uint32_t gm_tpldm28gt81dafq_i[];
extern const gm_tmpl_piece gm_tpldm28gt81dafq_p[];
extern const gm_spec *const gm_tpldm28gt81dafq_s[];
extern const gm_tmpl gm_tpldqjt9kdk4ntt;
extern const uint32_t gm_tpldqjt9kdk4ntt_i[];
extern const gm_tmpl_piece gm_tpldqjt9kdk4ntt_p[];
extern const gm_spec *const gm_tpldqjt9kdk4ntt_s[];
extern const gm_spec *const gm_tpldqr3c69jgnqi_s[];
extern const uint32_t gm_tpldqrk6ll2jb23_i[];
extern const gm_spec *const gm_tpldqrk6ll2jb23_s[];
extern const gm_tmpl_piece gm_tpldrfjfgoqe179_p[];
extern const uint32_t gm_tpldsg332pn89g6_i[];
extern const uint32_t gm_tpldtecn6n5hb86_i[];
extern const gm_tmpl gm_tpldu1003b7br77;
extern const uint32_t gm_tpldu1003b7br77_i[];
extern const gm_tmpl_piece gm_tpldu1003b7br77_p[];
extern const gm_spec *const gm_tpldu1003b7br77_s[];
extern const gm_tmpl_piece gm_tplduutdqd8m5r5_p[];
extern const gm_tmpl gm_tple6e96jcr39fk;
extern const uint32_t gm_tple6e96jcr39fk_i[];
extern const gm_tmpl_piece gm_tple6e96jcr39fk_p[];
extern const gm_spec *const gm_tple6e96jcr39fk_s[];
extern const gm_tmpl gm_tpleb7ph7fjnlbm;
extern const uint32_t gm_tpleb7ph7fjnlbm_i[];
extern const gm_tmpl_piece gm_tpleb7ph7fjnlbm_p[];
extern const gm_spec *const gm_tpleb7ph7fjnlbm_s[];
extern const gm_spec *const gm_tpled61een8fpfm_s[];
extern const uint32_t gm_tplehc5f52ftol1_i[];
extern const uint32_t gm_tplei9v853i04sp_i[];
extern const uint32_t gm_tpleiji4s5drk3o_i[];
extern const uint32_t gm_tplej21mtlj140h_i[];
extern const gm_spec *const gm_tplep4rfqi7to9g_s[];
extern const gm_tmpl_piece gm_tples1m1m9iv6b0_p[];
extern const gm_tmpl_piece gm_tpleth0e0pd50mr_p[];
extern const gm_spec *const gm_tplf0dvnr2mq4ft_s[];
extern const gm_tmpl_piece gm_tplf579fe8ol0qp_p[];
extern const gm_tmpl_piece gm_tplf6hbp7abojsd_p[];
extern const gm_tmpl_piece gm_tplfcgbc9s4hmn7_p[];
extern const gm_tmpl gm_tplfdsuiujvavc8;
extern const uint32_t gm_tplfdsuiujvavc8_i[];
extern const gm_tmpl_piece gm_tplfdsuiujvavc8_p[];
extern const gm_spec *const gm_tplfdsuiujvavc8_s[];
extern const gm_tmpl_piece gm_tplfpi7koctqa7b_p[];
extern const gm_spec *const gm_tplfqqsdgmc1ka9_s[];
extern const gm_tmpl gm_tplfr9bfp6t7vb7;
extern const uint32_t gm_tplfr9bfp6t7vb7_i[];
extern const gm_tmpl_piece gm_tplfr9bfp6t7vb7_p[];
extern const gm_spec *const gm_tplfr9bfp6t7vb7_s[];
extern const gm_tmpl_piece gm_tplfsrd1mgov7nd_p[];
extern const gm_spec *const gm_tplftg0h8omro11_s[];
extern const gm_tmpl gm_tplfv85d3oogq60;
extern const uint32_t gm_tplfv85d3oogq60_i[];
extern const gm_tmpl_piece gm_tplfv85d3oogq60_p[];
extern const gm_spec *const gm_tplfv85d3oogq60_s[];
extern const gm_tmpl gm_tplg0lh6vujmm6o;
extern const uint32_t gm_tplg0lh6vujmm6o_i[];
extern const gm_tmpl_piece gm_tplg0lh6vujmm6o_p[];
extern const gm_spec *const gm_tplg0lh6vujmm6o_s[];
extern const gm_tmpl_piece gm_tplg2qlrjk1sunc_p[];
extern const uint32_t gm_tplg4866gcgr837_i[];
extern const gm_tmpl gm_tplg575kk57c4i8;
extern const uint32_t gm_tplg575kk57c4i8_i[];
extern const gm_tmpl_piece gm_tplg575kk57c4i8_p[];
extern const gm_spec *const gm_tplg575kk57c4i8_s[];
extern const gm_tmpl_piece gm_tplg91jq41nbbkt_p[];
extern const uint32_t gm_tplggnuionimjk9_i[];
extern const gm_spec *const gm_tplgism5t1v67p6_s[];
extern const gm_tmpl gm_tplgm3gpate6ntg;
extern const uint32_t gm_tplgm3gpate6ntg_i[];
extern const gm_tmpl_piece gm_tplgm3gpate6ntg_p[];
extern const gm_spec *const gm_tplgm3gpate6ntg_s[];
extern const gm_spec *const gm_tplgniq1bprrgpo_s[];
extern const uint32_t gm_tplgq3upqu7s79r_i[];
extern const uint32_t gm_tplgvu725182gad_i[];
extern const gm_tmpl gm_tplh0kdp5708tgq;
extern const uint32_t gm_tplh0kdp5708tgq_i[];
extern const gm_tmpl_piece gm_tplh0kdp5708tgq_p[];
extern const gm_spec *const gm_tplh0kdp5708tgq_s[];
extern const uint32_t gm_tplh1blptpemhkl_i[];
extern const uint32_t gm_tplh78gsuiavvas_i[];
extern const gm_spec *const gm_tplha5n8a69m774_s[];
extern const gm_tmpl_piece gm_tplhatoh83mi4is_p[];
extern const uint32_t gm_tplhdrbi5vdvbe6_i[];
extern const gm_tmpl_piece gm_tplhiqpct20jd67_p[];
extern const gm_spec *const gm_tplhonv8g349dv9_s[];
extern const uint32_t gm_tplhr6emralh8q6_i[];
extern const gm_tmpl gm_tplht122d0ji4km;
extern const uint32_t gm_tplht122d0ji4km_i[];
extern const gm_tmpl_piece gm_tplht122d0ji4km_p[];
extern const gm_spec *const gm_tplht122d0ji4km_s[];
extern const gm_tmpl gm_tpli2vu74n9ss6f;
extern const uint32_t gm_tpli2vu74n9ss6f_i[];
extern const gm_tmpl_piece gm_tpli2vu74n9ss6f_p[];
extern const gm_spec *const gm_tpli2vu74n9ss6f_s[];
extern const gm_spec *const gm_tpli3i5m2srtl1a_s[];
extern const uint32_t gm_tpli7hldrrhg7sr_i[];
extern const gm_spec *const gm_tpli8aunvo88ks7_s[];
extern const uint32_t gm_tpli8blk453jgvg_i[];
extern const gm_tmpl_piece gm_tpli8blk453jgvg_p[];
extern const gm_spec *const gm_tpli8blk453jgvg_s[];
extern const gm_tmpl_piece gm_tpli9mr2dqh2vqo_p[];
extern const gm_tmpl_piece gm_tplibfhnj0illo7_p[];
extern const gm_tmpl_piece gm_tplicmhpq9e6sbc_p[];
extern const gm_tmpl_piece gm_tplidb7upjrao3q_p[];
extern const gm_tmpl_piece gm_tplidl0tsmrpak6_p[];
extern const gm_tmpl_piece gm_tplilne6f8eq90i_p[];
extern const gm_tmpl_piece gm_tpliq8p2spcvq06_p[];
extern const gm_spec *const gm_tpliq8p2spcvq06_s[];
extern const uint32_t gm_tpliqi7pna59a9o_i[];
extern const uint32_t gm_tplisurri6t3aun_i[];
extern const gm_tmpl gm_tplit8jkrg5bt3p;
extern const uint32_t gm_tplit8jkrg5bt3p_i[];
extern const gm_tmpl_piece gm_tplit8jkrg5bt3p_p[];
extern const gm_spec *const gm_tplit8jkrg5bt3p_s[];
extern const gm_spec *const gm_tpliufjdlri3svp_s[];
extern const gm_spec *const gm_tplj0vaqlfn7orc_s[];
extern const uint32_t gm_tplj3bqjm2hv786_i[];
extern const gm_tmpl_piece gm_tplj6imuu9dopvn_p[];
extern const gm_tmpl gm_tpljf9iocqf0fr6;
extern const uint32_t gm_tpljf9iocqf0fr6_i[];
extern const gm_tmpl_piece gm_tpljf9iocqf0fr6_p[];
extern const gm_spec *const gm_tpljf9iocqf0fr6_s[];
extern const gm_tmpl gm_tpljhar6jmbtsst;
extern const uint32_t gm_tpljhar6jmbtsst_i[];
extern const gm_tmpl_piece gm_tpljhar6jmbtsst_p[];
extern const gm_spec *const gm_tpljhar6jmbtsst_s[];
extern const gm_tmpl_piece gm_tpljkecvj49gp71_p[];
extern const uint32_t gm_tpljm6mn1j3nrs6_i[];
extern const gm_tmpl_piece gm_tpljnlsd7076tqd_p[];
extern const uint32_t gm_tpljp2husef3b2l_i[];
extern const uint32_t gm_tpljq1be78i0ptp_i[];
extern const uint32_t gm_tpljqhtbatb6393_i[];
extern const gm_spec *const gm_tpljt2av4h7c132_s[];
extern const gm_tmpl gm_tplk13976gpibvh;
extern const uint32_t gm_tplk13976gpibvh_i[];
extern const gm_tmpl_piece gm_tplk13976gpibvh_p[];
extern const gm_spec *const gm_tplk13976gpibvh_s[];
extern const gm_spec *const gm_tplk1a5101vtct2_s[];
extern const uint32_t gm_tplk1botso018is_i[];
extern const gm_spec *const gm_tplk1dctbs79mmk_s[];
extern const gm_tmpl_piece gm_tplk34ojqiaho84_p[];
extern const uint32_t gm_tplk66j4h09rih6_i[];
extern const uint32_t gm_tplk7f3hoqrengc_i[];
extern const gm_tmpl gm_tplkinjhvj7vqlj;
extern const uint32_t gm_tplkinjhvj7vqlj_i[];
extern const gm_tmpl_piece gm_tplkinjhvj7vqlj_p[];
extern const gm_spec *const gm_tplkinjhvj7vqlj_s[];
extern const gm_spec *const gm_tplkj8cf8psv8p6_s[];
extern const gm_spec *const gm_tplkjh7li83dd9g_s[];
extern const gm_tmpl_piece gm_tplkk98947i5b3i_p[];
extern const gm_spec *const gm_tplkkvn546crm4n_s[];
extern const gm_tmpl gm_tplkpmnsso79v4r;
extern const uint32_t gm_tplkpmnsso79v4r_i[];
extern const gm_tmpl_piece gm_tplkpmnsso79v4r_p[];
extern const gm_spec *const gm_tplkpmnsso79v4r_s[];
extern const gm_spec *const gm_tplkpqhnfan9vel_s[];
extern const gm_tmpl gm_tplktj7cco9lq40;
extern const uint32_t gm_tplktj7cco9lq40_i[];
extern const gm_tmpl_piece gm_tplktj7cco9lq40_p[];
extern const gm_spec *const gm_tplktj7cco9lq40_s[];
extern const gm_spec *const gm_tplkucr7969socr_s[];
extern const gm_tmpl gm_tpll408251sp58f;
extern const uint32_t gm_tpll408251sp58f_i[];
extern const gm_tmpl_piece gm_tpll408251sp58f_p[];
extern const gm_spec *const gm_tpll408251sp58f_s[];
extern const gm_spec *const gm_tpll738pfnpjq8c_s[];
extern const gm_spec *const gm_tpllaqikqjbb69k_s[];
extern const gm_spec *const gm_tpllcp48j1qim70_s[];
extern const uint32_t gm_tpllcp8fo2sqc1r_i[];
extern const gm_tmpl_piece gm_tplle7qvdq2jai6_p[];
extern const gm_spec *const gm_tpllfgb42aufjqd_s[];
extern const uint32_t gm_tpllhk8pmjfso1j_i[];
extern const gm_tmpl_piece gm_tplli6rlvq5vnvb_p[];
extern const gm_tmpl_piece gm_tpllihoeldkkkhv_p[];
extern const uint32_t gm_tpllir4kiseuk2b_i[];
extern const gm_tmpl_piece gm_tplljgqo309tg8i_p[];
extern const gm_tmpl gm_tpllktp07diubcq;
extern const uint32_t gm_tpllktp07diubcq_i[];
extern const gm_tmpl_piece gm_tpllktp07diubcq_p[];
extern const gm_spec *const gm_tpllktp07diubcq_s[];
extern const uint32_t gm_tplllolcurrktr6_i[];
extern const gm_tmpl gm_tpllovel7a2ips1;
extern const uint32_t gm_tpllovel7a2ips1_i[];
extern const gm_tmpl_piece gm_tpllovel7a2ips1_p[];
extern const gm_spec *const gm_tpllovel7a2ips1_s[];
extern const gm_spec *const gm_tpllp752obmvr55_s[];
extern const gm_spec *const gm_tpllpopiint29pt_s[];
extern const uint32_t gm_tpllqboe50ngs7g_i[];
extern const gm_spec *const gm_tplm01msh3jahhc_s[];
extern const gm_tmpl gm_tplm16s64mnihe4;
extern const uint32_t gm_tplm16s64mnihe4_i[];
extern const gm_tmpl_piece gm_tplm16s64mnihe4_p[];
extern const gm_spec *const gm_tplm16s64mnihe4_s[];
extern const uint32_t gm_tplm3r2aiholmt8_i[];
extern const gm_tmpl gm_tplm3t9qdrf37sd;
extern const uint32_t gm_tplm3t9qdrf37sd_i[];
extern const gm_tmpl_piece gm_tplm3t9qdrf37sd_p[];
extern const gm_spec *const gm_tplm3t9qdrf37sd_s[];
extern const gm_tmpl gm_tplm6g8a9odsasi;
extern const uint32_t gm_tplm6g8a9odsasi_i[];
extern const gm_tmpl_piece gm_tplm6g8a9odsasi_p[];
extern const gm_spec *const gm_tplm6g8a9odsasi_s[];
extern const gm_spec *const gm_tplmemqaoor78ph_s[];
extern const gm_tmpl_piece gm_tplmf8455dp92a3_p[];
extern const gm_tmpl gm_tplmkb4jrr2jqel;
extern const uint32_t gm_tplmkb4jrr2jqel_i[];
extern const gm_tmpl_piece gm_tplmkb4jrr2jqel_p[];
extern const gm_spec *const gm_tplmkb4jrr2jqel_s[];
extern const uint32_t gm_tplmkgblgbo7imb_i[];
extern const gm_tmpl_piece gm_tplmpc7ghuokash_p[];
extern const gm_spec *const gm_tplmpc7ghuokash_s[];
extern const uint32_t gm_tplmpsv5qagthkk_i[];
extern const gm_tmpl gm_tplmrpf5m9qkka0;
extern const uint32_t gm_tplmrpf5m9qkka0_i[];
extern const gm_tmpl_piece gm_tplmrpf5m9qkka0_p[];
extern const gm_spec *const gm_tplmrpf5m9qkka0_s[];
extern const gm_tmpl gm_tplmurfi7rvdamc;
extern const uint32_t gm_tplmurfi7rvdamc_i[];
extern const gm_tmpl_piece gm_tplmurfi7rvdamc_p[];
extern const gm_spec *const gm_tplmurfi7rvdamc_s[];
extern const gm_spec *const gm_tpln1ol2o7bp7d9_s[];
extern const gm_tmpl_piece gm_tpln2i759e19cgt_p[];
extern const uint32_t gm_tpln2j7s58m8uan_i[];
extern const gm_spec *const gm_tpln4hlddpv31vi_s[];
extern const gm_tmpl gm_tpln7ptjfs3pum5;
extern const uint32_t gm_tpln7ptjfs3pum5_i[];
extern const gm_tmpl_piece gm_tpln7ptjfs3pum5_p[];
extern const gm_spec *const gm_tpln7ptjfs3pum5_s[];
extern const gm_tmpl_piece gm_tplncscrjg4euhr_p[];
extern const gm_spec *const gm_tplncscrjg4euhr_s[];
extern const gm_tmpl gm_tplnd5opdv87prb;
extern const uint32_t gm_tplnd5opdv87prb_i[];
extern const gm_tmpl_piece gm_tplnd5opdv87prb_p[];
extern const gm_spec *const gm_tplnd5opdv87prb_s[];
extern const gm_tmpl_piece gm_tplneoolql7n1n7_p[];
extern const uint32_t gm_tplngsd5kcuu6fv_i[];
extern const gm_tmpl gm_tplnj647l00ku7g;
extern const uint32_t gm_tplnj647l00ku7g_i[];
extern const gm_tmpl_piece gm_tplnj647l00ku7g_p[];
extern const gm_spec *const gm_tplnj647l00ku7g_s[];
extern const gm_spec *const gm_tplnkb386hrc6va_s[];
extern const gm_tmpl gm_tplnl7d4k0qgogq;
extern const uint32_t gm_tplnl7d4k0qgogq_i[];
extern const gm_tmpl_piece gm_tplnl7d4k0qgogq_p[];
extern const gm_spec *const gm_tplnl7d4k0qgogq_s[];
extern const gm_spec *const gm_tplnodn319tb55r_s[];
extern const gm_tmpl gm_tplnt454lmhb7iv;
extern const uint32_t gm_tplnt454lmhb7iv_i[];
extern const gm_tmpl_piece gm_tplnt454lmhb7iv_p[];
extern const gm_spec *const gm_tplnt454lmhb7iv_s[];
extern const gm_tmpl_piece gm_tplntk567ck43t8_p[];
extern const gm_spec *const gm_tplnunrbopfpsiq_s[];
extern const uint32_t gm_tplnv6l3ecgreid_i[];
extern const uint32_t gm_tplo12k9ajl5nes_i[];
extern const gm_spec *const gm_tplo203818ggfa8_s[];
extern const gm_tmpl_piece gm_tploc41q7b5gurn_p[];
extern const gm_spec *const gm_tploc8iqi9f2el1_s[];
extern const gm_spec *const gm_tplokoueqaolfmt_s[];
extern const gm_spec *const gm_tplolsc5tfmth66_s[];
extern const gm_spec *const gm_tplopbg40ai4id4_s[];
extern const uint32_t gm_tplotas2madniqp_i[];
extern const gm_spec *const gm_tplotost4gd8bha_s[];
extern const gm_tmpl gm_tplp4f8nloo6fdo;
extern const uint32_t gm_tplp4f8nloo6fdo_i[];
extern const gm_tmpl_piece gm_tplp4f8nloo6fdo_p[];
extern const gm_spec *const gm_tplp4f8nloo6fdo_s[];
extern const gm_tmpl gm_tplp5mnidu5m01d;
extern const uint32_t gm_tplp5mnidu5m01d_i[];
extern const gm_tmpl_piece gm_tplp5mnidu5m01d_p[];
extern const gm_spec *const gm_tplp5mnidu5m01d_s[];
extern const gm_tmpl_piece gm_tplp7faum694g3u_p[];
extern const gm_tmpl gm_tplp81h2rbu9gd2;
extern const uint32_t gm_tplp81h2rbu9gd2_i[];
extern const gm_tmpl_piece gm_tplp81h2rbu9gd2_p[];
extern const gm_spec *const gm_tplp81h2rbu9gd2_s[];
extern const gm_tmpl gm_tplpaq33rl4cnb8;
extern const uint32_t gm_tplpaq33rl4cnb8_i[];
extern const gm_tmpl_piece gm_tplpaq33rl4cnb8_p[];
extern const gm_spec *const gm_tplpaq33rl4cnb8_s[];
extern const gm_tmpl gm_tplpcjaq35mrruk;
extern const uint32_t gm_tplpcjaq35mrruk_i[];
extern const gm_tmpl_piece gm_tplpcjaq35mrruk_p[];
extern const gm_spec *const gm_tplpcjaq35mrruk_s[];
extern const gm_tmpl_piece gm_tplpdnopu1p8235_p[];
extern const gm_tmpl_piece gm_tplpe5hifub5ms7_p[];
extern const gm_spec *const gm_tplpfv6kueslks8_s[];
extern const gm_tmpl gm_tplpntcuu1r3a58;
extern const uint32_t gm_tplpntcuu1r3a58_i[];
extern const gm_tmpl_piece gm_tplpntcuu1r3a58_p[];
extern const gm_spec *const gm_tplpntcuu1r3a58_s[];
extern const uint32_t gm_tplpog54do9oh2a_i[];
extern const uint32_t gm_tplpvmvg9ce66cu_i[];
extern const uint32_t gm_tplq12pt4e53uj3_i[];
extern const gm_tmpl gm_tplq5eond7ctc71;
extern const uint32_t gm_tplq5eond7ctc71_i[];
extern const gm_tmpl_piece gm_tplq5eond7ctc71_p[];
extern const gm_spec *const gm_tplq5eond7ctc71_s[];
extern const uint32_t gm_tplq73iq4gr17op_i[];
extern const uint32_t gm_tplq85e99v067d0_i[];
extern const uint32_t gm_tplq8t63lu2docj_i[];
extern const gm_tmpl_piece gm_tplqf2cda88og0i_p[];
extern const gm_tmpl gm_tplqfu63ajpo70a;
extern const uint32_t gm_tplqfu63ajpo70a_i[];
extern const gm_tmpl_piece gm_tplqfu63ajpo70a_p[];
extern const gm_spec *const gm_tplqfu63ajpo70a_s[];
extern const uint32_t gm_tplqipe054or34v_i[];
extern const gm_tmpl gm_tplqnml5avaeikk;
extern const uint32_t gm_tplqnml5avaeikk_i[];
extern const gm_tmpl_piece gm_tplqnml5avaeikk_p[];
extern const gm_spec *const gm_tplqnml5avaeikk_s[];
extern const uint32_t gm_tplqp3jlt332oq5_i[];
extern const uint32_t gm_tplr2tuff1mvdst_i[];
extern const gm_spec *const gm_tplr42r2lp0p4d1_s[];
extern const uint32_t gm_tplr7712ujh33fl_i[];
extern const gm_tmpl_piece gm_tplr9kq0pku58q3_p[];
extern const gm_tmpl_piece gm_tplr9mrmvfj4528_p[];
extern const uint32_t gm_tplrfab517u4o97_i[];
extern const gm_tmpl gm_tplrfnb607u5hvd;
extern const uint32_t gm_tplrfnb607u5hvd_i[];
extern const gm_tmpl_piece gm_tplrfnb607u5hvd_p[];
extern const gm_spec *const gm_tplrfnb607u5hvd_s[];
extern const uint32_t gm_tplrh70572i8ko0_i[];
extern const gm_tmpl gm_tplrkvt0j8tqlqg;
extern const uint32_t gm_tplrkvt0j8tqlqg_i[];
extern const gm_tmpl_piece gm_tplrkvt0j8tqlqg_p[];
extern const gm_spec *const gm_tplrkvt0j8tqlqg_s[];
extern const gm_spec *const gm_tplrodjuj3fgmsa_s[];
extern const gm_tmpl gm_tplruf3ms5lqgst;
extern const uint32_t gm_tplruf3ms5lqgst_i[];
extern const gm_tmpl_piece gm_tplruf3ms5lqgst_p[];
extern const gm_spec *const gm_tplruf3ms5lqgst_s[];
extern const gm_spec *const gm_tplrvl1hqu8hbkh_s[];
extern const gm_spec *const gm_tpls19nuda8st36_s[];
extern const gm_tmpl gm_tpls2ou5a11vcn7;
extern const uint32_t gm_tpls2ou5a11vcn7_i[];
extern const gm_tmpl_piece gm_tpls2ou5a11vcn7_p[];
extern const gm_spec *const gm_tpls2ou5a11vcn7_s[];
extern const gm_spec *const gm_tpls6cg1uub8gfo_s[];
extern const uint32_t gm_tpls6f1h1hgtqgb_i[];
extern const gm_spec *const gm_tpls6vb484t8cfc_s[];
extern const gm_tmpl gm_tpls80s4u4ujepk;
extern const uint32_t gm_tpls80s4u4ujepk_i[];
extern const gm_tmpl_piece gm_tpls80s4u4ujepk_p[];
extern const gm_spec *const gm_tpls80s4u4ujepk_s[];
extern const gm_spec *const gm_tpls8octb03drbp_s[];
extern const uint32_t gm_tpls92cg1g1s0vs_i[];
extern const gm_tmpl gm_tplsc9pqkp31v7e;
extern const uint32_t gm_tplsc9pqkp31v7e_i[];
extern const gm_tmpl_piece gm_tplsc9pqkp31v7e_p[];
extern const gm_spec *const gm_tplsc9pqkp31v7e_s[];
extern const uint32_t gm_tplsdamq31ts61q_i[];
extern const gm_tmpl gm_tplsdokr8v76bhc;
extern const uint32_t gm_tplsdokr8v76bhc_i[];
extern const gm_tmpl_piece gm_tplsdokr8v76bhc_p[];
extern const gm_spec *const gm_tplsdokr8v76bhc_s[];
extern const gm_spec *const gm_tplsgm949s33v8m_s[];
extern const gm_tmpl_piece gm_tplsh3mbm625h0g_p[];
extern const gm_tmpl gm_tplsjufktgf4o1v;
extern const uint32_t gm_tplsjufktgf4o1v_i[];
extern const gm_tmpl_piece gm_tplsjufktgf4o1v_p[];
extern const gm_spec *const gm_tplsjufktgf4o1v_s[];
extern const uint32_t gm_tplss5b4q9est2j_i[];
extern const gm_tmpl_piece gm_tplt0hjq4nb3jer_p[];
extern const uint32_t gm_tplt0qsjg06adbb_i[];
extern const gm_spec *const gm_tplt0qsjg06adbb_s[];
extern const gm_spec *const gm_tplt320ru9bfpdd_s[];
extern const uint32_t gm_tplt392n8hqfhn9_i[];
extern const gm_spec *const gm_tplt3iub1goev8r_s[];
extern const uint32_t gm_tplt524tdio8698_i[];
extern const uint32_t gm_tplt56hco5v1mvf_i[];
extern const gm_spec *const gm_tplt5hu85fc8epn_s[];
extern const uint32_t gm_tplt5ks894658kl_i[];
extern const gm_tmpl gm_tplt68r4kihke54;
extern const uint32_t gm_tplt68r4kihke54_i[];
extern const gm_tmpl_piece gm_tplt68r4kihke54_p[];
extern const gm_spec *const gm_tplt68r4kihke54_s[];
extern const gm_tmpl gm_tplt8hcvvk31bg7;
extern const uint32_t gm_tplt8hcvvk31bg7_i[];
extern const gm_tmpl_piece gm_tplt8hcvvk31bg7_p[];
extern const gm_spec *const gm_tplt8hcvvk31bg7_s[];
extern const uint32_t gm_tplt9o3qfo5dqct_i[];
extern const gm_tmpl_piece gm_tpltd7vnsp01mvt_p[];
extern const uint32_t gm_tpltdh9l4qjtpek_i[];
extern const gm_spec *const gm_tpltdoamgi2k9f9_s[];
extern const gm_tmpl_piece gm_tpltng0q64u6p80_p[];
extern const uint32_t gm_tpltnu092iqmfdd_i[];
extern const uint32_t gm_tplto3kksb9sot8_i[];
extern const uint32_t gm_tplttoavo0o7ugf_i[];
extern const gm_spec *const gm_tplttoavo0o7ugf_s[];
extern const gm_spec *const gm_tplu0h8mqm004jg_s[];
extern const gm_tmpl_piece gm_tplu58b6rd4mq28_p[];
extern const gm_spec *const gm_tplu65jn8nvmavg_s[];
extern const gm_tmpl_piece gm_tplu7gkm8brh8s9_p[];
extern const gm_spec *const gm_tplubod389iot6h_s[];
extern const gm_tmpl_piece gm_tpluc8r0n3peqad_p[];
extern const uint32_t gm_tplucd2r10hh63v_i[];
extern const gm_spec *const gm_tpludt9aee4n14r_s[];
extern const gm_tmpl_piece gm_tplue01enectvb0_p[];
extern const gm_spec *const gm_tplue2kdcikgcof_s[];
extern const gm_tmpl gm_tplukjghf5ng2te;
extern const uint32_t gm_tplukjghf5ng2te_i[];
extern const gm_tmpl_piece gm_tplukjghf5ng2te_p[];
extern const gm_spec *const gm_tplukjghf5ng2te_s[];
extern const gm_tmpl gm_tpluldhqdblqoav;
extern const uint32_t gm_tpluldhqdblqoav_i[];
extern const gm_tmpl_piece gm_tpluldhqdblqoav_p[];
extern const gm_spec *const gm_tpluldhqdblqoav_s[];
extern const gm_spec *const gm_tplurkkmrij8g4f_s[];
extern const gm_spec *const gm_tplurleasos6378_s[];
extern const gm_tmpl_piece gm_tplv1cj36rb6m0h_p[];
extern const uint32_t gm_tplv1i7e9gejv6a_i[];
extern const gm_spec *const gm_tplv23k7silrvnp_s[];
extern const gm_tmpl_piece gm_tplv8s8fgl2lggu_p[];
extern const uint32_t gm_tplvajlmotjd2qr_i[];
extern const gm_tmpl_piece gm_tplvc30qsg5hfc6_p[];
extern const uint32_t gm_tplvc9hnrt45n6d_i[];
extern const gm_tmpl gm_tplvf2bf6hftf4i;
extern const uint32_t gm_tplvf2bf6hftf4i_i[];
extern const gm_tmpl_piece gm_tplvf2bf6hftf4i_p[];
extern const gm_spec *const gm_tplvf2bf6hftf4i_s[];
extern const uint32_t gm_tplvj9n0kinsqlj_i[];
extern const uint32_t gm_tplvm3qupt9dved_i[];
extern const gm_tmpl gm_tplvm7e7lfhcheu;
extern const uint32_t gm_tplvm7e7lfhcheu_i[];
extern const gm_tmpl_piece gm_tplvm7e7lfhcheu_p[];
extern const gm_spec *const gm_tplvm7e7lfhcheu_s[];
extern const gm_tmpl gm_tplvonunpuh0kn8;
extern const uint32_t gm_tplvonunpuh0kn8_i[];
extern const gm_tmpl_piece gm_tplvonunpuh0kn8_p[];
extern const gm_spec *const gm_tplvonunpuh0kn8_s[];
extern const gm_spec *const gm_tplvpehcchgo971_s[];
extern const gm_spec *const gm_tplvpjd91cp2l7g_s[];
extern const uint32_t gm_tplvqa6aqf5q6ij_i[];
extern const gm_tmpl_piece gm_tplvqa6aqf5q6ij_p[];
extern const gm_spec *const gm_tplvv610d042tt6_s[];
extern const gm_type gm_ty9oqmmiu8qc;
extern const gm_variant gm_tylv5i3f25jk_v[];
inline __attribute__((aligned(4))) gm_b16_8 gm_f_args_6h3q549(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_args_6h3q549 F;
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    size_t argn2; const void *argp2 = gm_args(&argn2);
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(argp2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(argn2));
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_exit_jfn9nl4(void *env, gm_node *cx, gm_node *inst, uint8_t a0) {
    struct gm_fr_exit_jfn9nl4 F;
    uint8_t gm_l0; /* code */
    GM_STACK_CHECK_SHALLOW(&F, 4u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = gm_l0;
    gm_tqs5p4n2tq19v t3;
    GM_ST(uint8_t, ((uint8_t *)&t3), t2);
    ((uint8_t *)&t3)[1] = 1;
    GM_ST(gm_tqs5p4n2tq19v, inst->result, t3);
    gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
    goto gm_lad;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_var_otupsib(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_var_otupsib F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    const uint8_t *ev3; size_t evn3;
    bool evok3 = gm_env_var((const uint8_t *)GM_LD(void *, ((uint8_t *)&t2)), GM_LD(uintptr_t, ((uint8_t *)&t2) + 8), &ev3, &evn3);
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(ev3));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(evn3));
    gm_th2lqbo77pvsc t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t5), t4);
    ((uint8_t *)&t5)[16] = 1;
    gm_th2lqbo77pvsc t6;
    memset(&t6, 0, sizeof t6);
    gm_th2lqbo77pvsc t7 = evok3 ? t5 : t6;
    memmove(gm_out, ((uint8_t *)&t7), 24);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
const char gm_src2955num0q9tm[] = "compiler/check/optionals.gm";
const char gm_src2u46tincrqmv[] = "compiler/lsp/analysis.gm";
const char gm_src6hq50r4rk7e6[] = "compiler/interp/frames.gm";
const char gm_src94heo671ss5e[] = "compiler/ide/items.gm";
const char gm_srcfde9lsgge1ld[] = "compiler/interp/format.gm";
const char gm_srcgc0h5so8ul3j[] = "compiler/interp/vm.gm";
const char gm_srcn13n69hd0cht[] = "compiler/lsp/refs.gm";
const char gm_srcns60k96jj1mt[] = "compiler/interp/vector.gm";
const char gm_srco24pprak16p4[] = "compiler/driver/options.gm";
const char gm_srco4ljquuk5d87[] = "compiler/check/dump.gm";
const char gm_srcrchqfd7obmqe[] = "compiler/fmt/chars.gm";
const char gm_srcsk3h2du2ldnc[] = "compiler/check/pattern.gm";
const char gm_srcuiab49hlgv7g[] = "compiler/parse/parser.gm";
const gm_spec *const gm_tpl0elencma14kh_s[] = {NULL};
const gm_spec *const gm_tpl0emlhhlpmv0s_s[] = {NULL, NULL};
const gm_spec *const gm_tpl0eumfheo3vtt_s[] = {NULL};
const gm_spec *const gm_tpl0fhs0t9qdf1o_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl1bno78mc58dl_s[] = {NULL};
const gm_spec *const gm_tpl1c419vqpem2n_s[] = {NULL, NULL};
const gm_spec *const gm_tpl1f7c1o805fpf_s[] = {NULL};
const gm_spec *const gm_tpl1t0vlt7tq67r_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl1u8koiam5nmo_s[] = {NULL};
const gm_spec *const gm_tpl1umecooe9oti_s[] = {NULL};
const gm_spec *const gm_tpl20a4acojo21c_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl344kemfor61g_s[] = {NULL};
const gm_spec *const gm_tpl352s0p85j412_s[] = {NULL};
const gm_spec *const gm_tpl3melp777ubs8_s[] = {NULL};
const gm_spec *const gm_tpl3ssq6hkk3qrj_s[] = {NULL};
const gm_spec *const gm_tpl40m4ah8dc3ae_s[] = {NULL};
const gm_spec *const gm_tpl4n3cnfr6vo96_s[] = {NULL};
const gm_spec *const gm_tpl54tnh29jjfju_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl5kgb1tfsut02_s[] = {NULL};
const gm_spec *const gm_tpl60bi48lvp2qn_s[] = {NULL};
const gm_spec *const gm_tpl6186pbdul6ge_s[] = {NULL, NULL};
const gm_spec *const gm_tpl65jqebqot15g_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl6itong91gv3f_s[] = {NULL, NULL};
const gm_spec *const gm_tpl6q4b2p6tude6_s[] = {NULL};
const gm_spec *const gm_tpl6r9reg6pnruq_s[] = {NULL};
const gm_spec *const gm_tpl6tu8f1h1mg85_s[] = {NULL};
const gm_spec *const gm_tpl8gj5o7rmo2fl_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl8qng452btr2a_s[] = {NULL, NULL};
const gm_spec *const gm_tpl8un0qv11eeom_s[] = {NULL};
const gm_spec *const gm_tpl91b7sjgnmine_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl9kqhb5r0uube_s[] = {NULL};
const gm_spec *const gm_tpl9qc0voknsp7g_s[] = {NULL};
const gm_spec *const gm_tpl9qg0hlqqke7e_s[] = {NULL};
const gm_spec *const gm_tpl9ufa4j1b7kal_s[] = {NULL, NULL};
const gm_spec *const gm_tpla0a5a633lqve_s[] = {NULL};
const gm_spec *const gm_tpla1fscog446dv_s[] = {NULL};
const gm_spec *const gm_tpla6b7o385jo3o_s[] = {NULL};
const gm_spec *const gm_tplanhmvcr622qo_s[] = {NULL};
const gm_spec *const gm_tplb5ldvki5nntc_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplbfdkthp45ai4_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplc3kt858jk6jp_s[] = {NULL, NULL};
const gm_spec *const gm_tplc70rik6173ge_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplc8kkdoja48ii_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplcqfaq9vjn7hh_s[] = {NULL, NULL};
const gm_spec *const gm_tpld1hhk0kef14l_s[] = {NULL};
const gm_spec *const gm_tpldg1m204qb08c_s[] = {NULL, NULL};
const gm_spec *const gm_tpldqr3c69jgnqi_s[] = {NULL};
const gm_spec *const gm_tpldqrk6ll2jb23_s[] = {NULL};
const gm_spec *const gm_tpled61een8fpfm_s[] = {NULL};
const gm_spec *const gm_tplep4rfqi7to9g_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplf0dvnr2mq4ft_s[] = {NULL, NULL};
const gm_spec *const gm_tplfqqsdgmc1ka9_s[] = {NULL, NULL};
const gm_spec *const gm_tplfr9bfp6t7vb7_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplftg0h8omro11_s[] = {NULL};
const gm_spec *const gm_tplgism5t1v67p6_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplgm3gpate6ntg_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplgniq1bprrgpo_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplha5n8a69m774_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplhonv8g349dv9_s[] = {NULL};
const gm_spec *const gm_tpli3i5m2srtl1a_s[] = {NULL};
const gm_spec *const gm_tpli8aunvo88ks7_s[] = {NULL};
const gm_spec *const gm_tpli8blk453jgvg_s[] = {NULL};
const gm_spec *const gm_tpliq8p2spcvq06_s[] = {NULL, NULL};
const gm_spec *const gm_tpliufjdlri3svp_s[] = {NULL, NULL};
const gm_spec *const gm_tplj0vaqlfn7orc_s[] = {NULL, NULL};
const gm_spec *const gm_tpljt2av4h7c132_s[] = {NULL};
const gm_spec *const gm_tplk1a5101vtct2_s[] = {NULL, NULL};
const gm_spec *const gm_tplk1dctbs79mmk_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplkj8cf8psv8p6_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplkjh7li83dd9g_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplkkvn546crm4n_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplkpqhnfan9vel_s[] = {NULL};
const gm_spec *const gm_tplkucr7969socr_s[] = {NULL};
const gm_spec *const gm_tpll738pfnpjq8c_s[] = {NULL};
const gm_spec *const gm_tpllaqikqjbb69k_s[] = {NULL};
const gm_spec *const gm_tpllcp48j1qim70_s[] = {NULL};
const gm_spec *const gm_tpllfgb42aufjqd_s[] = {NULL};
const gm_spec *const gm_tpllp752obmvr55_s[] = {NULL};
const gm_spec *const gm_tpllpopiint29pt_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplm01msh3jahhc_s[] = {NULL};
const gm_spec *const gm_tplmemqaoor78ph_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplmpc7ghuokash_s[] = {NULL, NULL};
const gm_spec *const gm_tpln1ol2o7bp7d9_s[] = {NULL, NULL};
const gm_spec *const gm_tpln4hlddpv31vi_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplncscrjg4euhr_s[] = {NULL, NULL};
const gm_spec *const gm_tplnkb386hrc6va_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplnodn319tb55r_s[] = {NULL};
const gm_spec *const gm_tplnt454lmhb7iv_s[] = {NULL};
const gm_spec *const gm_tplnunrbopfpsiq_s[] = {NULL, NULL};
const gm_spec *const gm_tplo203818ggfa8_s[] = {NULL};
const gm_spec *const gm_tploc8iqi9f2el1_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplokoueqaolfmt_s[] = {NULL};
const gm_spec *const gm_tplolsc5tfmth66_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplopbg40ai4id4_s[] = {NULL};
const gm_spec *const gm_tplotost4gd8bha_s[] = {NULL, NULL};
const gm_spec *const gm_tplpfv6kueslks8_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplr42r2lp0p4d1_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplrfnb607u5hvd_s[] = {NULL, NULL};
const gm_spec *const gm_tplrodjuj3fgmsa_s[] = {NULL, NULL};
const gm_spec *const gm_tplrvl1hqu8hbkh_s[] = {NULL, NULL};
const gm_spec *const gm_tpls19nuda8st36_s[] = {NULL};
const gm_spec *const gm_tpls6cg1uub8gfo_s[] = {NULL};
const gm_spec *const gm_tpls6vb484t8cfc_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpls8octb03drbp_s[] = {NULL};
const gm_spec *const gm_tplsgm949s33v8m_s[] = {NULL};
const gm_spec *const gm_tplt0qsjg06adbb_s[] = {NULL, NULL};
const gm_spec *const gm_tplt320ru9bfpdd_s[] = {NULL};
const gm_spec *const gm_tplt3iub1goev8r_s[] = {NULL, NULL};
const gm_spec *const gm_tplt5hu85fc8epn_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpltdoamgi2k9f9_s[] = {NULL, NULL};
const gm_spec *const gm_tplttoavo0o7ugf_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplu0h8mqm004jg_s[] = {NULL};
const gm_spec *const gm_tplu65jn8nvmavg_s[] = {NULL};
const gm_spec *const gm_tplubod389iot6h_s[] = {NULL};
const gm_spec *const gm_tpludt9aee4n14r_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplue2kdcikgcof_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplurkkmrij8g4f_s[] = {NULL, NULL};
const gm_spec *const gm_tplurleasos6378_s[] = {NULL};
const gm_spec *const gm_tplv23k7silrvnp_s[] = {NULL};
const gm_spec *const gm_tplvpehcchgo971_s[] = {NULL};
const gm_spec *const gm_tplvpjd91cp2l7g_s[] = {NULL, NULL};
const gm_spec *const gm_tplvv610d042tt6_s[] = {NULL, NULL, NULL};
const gm_tmpl gm_tpl00rv7vuig9ur = {1u, gm_tpl00rv7vuig9ur_p, gm_tpl00rv7vuig9ur_i, gm_tpl00rv7vuig9ur_s};
const gm_tmpl gm_tpl05osb04qe3ad = {1u, gm_tpl05osb04qe3ad_p, gm_tpl05osb04qe3ad_i, gm_tpl05osb04qe3ad_s};
const gm_tmpl gm_tpl086m1ajg247c = {3u, gm_tpl086m1ajg247c_p, gm_tpl086m1ajg247c_i, gm_tpl086m1ajg247c_s};
const gm_tmpl gm_tpl0fpifsdum8e8 = {2u, gm_tpl0fpifsdum8e8_p, gm_tpl0fpifsdum8e8_i, gm_tpl0fpifsdum8e8_s};
const gm_tmpl gm_tpl0lggla435q5r = {1u, gm_tpl0lggla435q5r_p, gm_tpl0lggla435q5r_i, gm_tpl0lggla435q5r_s};
const gm_tmpl gm_tpl0o80ilivc5vg = {2u, gm_tpl0o80ilivc5vg_p, gm_tpl0o80ilivc5vg_i, gm_tpl0o80ilivc5vg_s};
const gm_tmpl gm_tpl15v8rvh46nek = {1u, gm_tpl15v8rvh46nek_p, gm_tpl15v8rvh46nek_i, gm_tpl15v8rvh46nek_s};
const gm_tmpl gm_tpl1qq6rf1tb5ds = {1u, gm_tpl1qq6rf1tb5ds_p, gm_tpl1qq6rf1tb5ds_i, gm_tpl1qq6rf1tb5ds_s};
const gm_tmpl gm_tpl23mh8eh9cef6 = {3u, gm_tpl23mh8eh9cef6_p, gm_tpl23mh8eh9cef6_i, gm_tpl23mh8eh9cef6_s};
const gm_tmpl gm_tpl2gebin91ghlu = {2u, gm_tpl2gebin91ghlu_p, gm_tpl2gebin91ghlu_i, gm_tpl2gebin91ghlu_s};
const gm_tmpl gm_tpl2lqr5lnbpsvm = {3u, gm_tpl2lqr5lnbpsvm_p, gm_tpl2lqr5lnbpsvm_i, gm_tpl2lqr5lnbpsvm_s};
const gm_tmpl gm_tpl2od99j0nh2ah = {1u, gm_tpl2od99j0nh2ah_p, gm_tpl2od99j0nh2ah_i, gm_tpl2od99j0nh2ah_s};
const gm_tmpl gm_tpl2re1r0fre48m = {1u, gm_tpl2re1r0fre48m_p, gm_tpl2re1r0fre48m_i, gm_tpl2re1r0fre48m_s};
const gm_tmpl gm_tpl386j11hdkrib = {3u, gm_tpl386j11hdkrib_p, gm_tpl386j11hdkrib_i, gm_tpl386j11hdkrib_s};
const gm_tmpl gm_tpl409di1gnsfit = {2u, gm_tpl409di1gnsfit_p, gm_tpl409di1gnsfit_i, gm_tpl409di1gnsfit_s};
const gm_tmpl gm_tpl46u099ch4aa2 = {1u, gm_tpl46u099ch4aa2_p, gm_tpl46u099ch4aa2_i, gm_tpl46u099ch4aa2_s};
const gm_tmpl gm_tpl51u3ki3ilqou = {5u, gm_tpl51u3ki3ilqou_p, gm_tpl51u3ki3ilqou_i, gm_tpl51u3ki3ilqou_s};
const gm_tmpl gm_tpl53ihvkdedd31 = {1u, gm_tpl53ihvkdedd31_p, gm_tpl53ihvkdedd31_i, gm_tpl53ihvkdedd31_s};
const gm_tmpl gm_tpl60bi48lvp2qn = {1u, gm_tpl60bi48lvp2qn_p, gm_tpl60bi48lvp2qn_i, gm_tpl60bi48lvp2qn_s};
const gm_tmpl gm_tpl60na9lt7qpkj = {2u, gm_tpl60na9lt7qpkj_p, gm_tpl60na9lt7qpkj_i, gm_tpl60na9lt7qpkj_s};
const gm_tmpl gm_tpl6fm59pne5ng7 = {1u, gm_tpl6fm59pne5ng7_p, gm_tpl6fm59pne5ng7_i, gm_tpl6fm59pne5ng7_s};
const gm_tmpl gm_tpl6hqg4qptvadc = {1u, gm_tpl6hqg4qptvadc_p, gm_tpl6hqg4qptvadc_i, gm_tpl6hqg4qptvadc_s};
const gm_tmpl gm_tpl70tkged35ahl = {2u, gm_tpl70tkged35ahl_p, gm_tpl70tkged35ahl_i, gm_tpl70tkged35ahl_s};
const gm_tmpl gm_tpl7b1ibjg22p4i = {1u, gm_tpl7b1ibjg22p4i_p, gm_tpl7b1ibjg22p4i_i, gm_tpl7b1ibjg22p4i_s};
const gm_tmpl gm_tpl7b2kb2ainpff = {6u, gm_tpl7b2kb2ainpff_p, gm_tpl7b2kb2ainpff_i, gm_tpl7b2kb2ainpff_s};
const gm_tmpl gm_tpl7e5tr64a0u6e = {1u, gm_tpl7e5tr64a0u6e_p, gm_tpl7e5tr64a0u6e_i, gm_tpl7e5tr64a0u6e_s};
const gm_tmpl gm_tpl80avf3gokjno = {3u, gm_tpl80avf3gokjno_p, gm_tpl80avf3gokjno_i, gm_tpl80avf3gokjno_s};
const gm_tmpl gm_tpl83pfuovi474u = {1u, gm_tpl83pfuovi474u_p, gm_tpl83pfuovi474u_i, gm_tpl83pfuovi474u_s};
const gm_tmpl gm_tpl8f1efhudc5d4 = {1u, gm_tpl8f1efhudc5d4_p, gm_tpl8f1efhudc5d4_i, gm_tpl8f1efhudc5d4_s};
const gm_tmpl gm_tpl8ssnfo686ogd = {3u, gm_tpl8ssnfo686ogd_p, gm_tpl8ssnfo686ogd_i, gm_tpl8ssnfo686ogd_s};
const gm_tmpl gm_tpl9iaqf5bbsv8o = {1u, gm_tpl9iaqf5bbsv8o_p, gm_tpl9iaqf5bbsv8o_i, gm_tpl9iaqf5bbsv8o_s};
const gm_tmpl gm_tpl9ibjmfjq0iar = {1u, gm_tpl9ibjmfjq0iar_p, gm_tpl9ibjmfjq0iar_i, gm_tpl9ibjmfjq0iar_s};
const gm_tmpl gm_tpl9jrfnomsrfk1 = {2u, gm_tpl9jrfnomsrfk1_p, gm_tpl9jrfnomsrfk1_i, gm_tpl9jrfnomsrfk1_s};
const gm_tmpl gm_tpl9kqhb5r0uube = {1u, gm_tpl9kqhb5r0uube_p, gm_tpl9kqhb5r0uube_i, gm_tpl9kqhb5r0uube_s};
const gm_tmpl gm_tpl9kt3nn7p3a92 = {1u, gm_tpl9kt3nn7p3a92_p, gm_tpl9kt3nn7p3a92_i, gm_tpl9kt3nn7p3a92_s};
const gm_tmpl gm_tpla1fscog446dv = {1u, gm_tpla1fscog446dv_p, gm_tpla1fscog446dv_i, gm_tpla1fscog446dv_s};
const gm_tmpl gm_tpla4jdv6pnguh4 = {1u, gm_tpla4jdv6pnguh4_p, gm_tpla4jdv6pnguh4_i, gm_tpla4jdv6pnguh4_s};
const gm_tmpl gm_tplaahjt316h1v3 = {1u, gm_tplaahjt316h1v3_p, gm_tplaahjt316h1v3_i, gm_tplaahjt316h1v3_s};
const gm_tmpl gm_tplbbopfj3f21eb = {1u, gm_tplbbopfj3f21eb_p, gm_tplbbopfj3f21eb_i, gm_tplbbopfj3f21eb_s};
const gm_tmpl gm_tplbmcpcu1c6osb = {6u, gm_tplbmcpcu1c6osb_p, gm_tplbmcpcu1c6osb_i, gm_tplbmcpcu1c6osb_s};
const gm_tmpl gm_tplbo11j4rgqkb3 = {1u, gm_tplbo11j4rgqkb3_p, gm_tplbo11j4rgqkb3_i, gm_tplbo11j4rgqkb3_s};
const gm_tmpl gm_tplceemmmm7iuq6 = {2u, gm_tplceemmmm7iuq6_p, gm_tplceemmmm7iuq6_i, gm_tplceemmmm7iuq6_s};
const gm_tmpl gm_tplceihv99elj2t = {2u, gm_tplceihv99elj2t_p, gm_tplceihv99elj2t_i, gm_tplceihv99elj2t_s};
const gm_tmpl gm_tplde4760b0jcjn = {2u, gm_tplde4760b0jcjn_p, gm_tplde4760b0jcjn_i, gm_tplde4760b0jcjn_s};
const gm_tmpl gm_tpldiv6dh0or1km = {4u, gm_tpldiv6dh0or1km_p, gm_tpldiv6dh0or1km_i, gm_tpldiv6dh0or1km_s};
const gm_tmpl gm_tpldknbfev12jrb = {3u, gm_tpldknbfev12jrb_p, gm_tpldknbfev12jrb_i, gm_tpldknbfev12jrb_s};
const gm_tmpl gm_tpldl4pn21ood35 = {1u, gm_tpldl4pn21ood35_p, gm_tpldl4pn21ood35_i, gm_tpldl4pn21ood35_s};
const gm_tmpl gm_tpldm28gt81dafq = {1u, gm_tpldm28gt81dafq_p, gm_tpldm28gt81dafq_i, gm_tpldm28gt81dafq_s};
const gm_tmpl gm_tpldqjt9kdk4ntt = {1u, gm_tpldqjt9kdk4ntt_p, gm_tpldqjt9kdk4ntt_i, gm_tpldqjt9kdk4ntt_s};
const gm_tmpl gm_tpldu1003b7br77 = {3u, gm_tpldu1003b7br77_p, gm_tpldu1003b7br77_i, gm_tpldu1003b7br77_s};
const gm_tmpl gm_tple6e96jcr39fk = {2u, gm_tple6e96jcr39fk_p, gm_tple6e96jcr39fk_i, gm_tple6e96jcr39fk_s};
const gm_tmpl gm_tpleb7ph7fjnlbm = {2u, gm_tpleb7ph7fjnlbm_p, gm_tpleb7ph7fjnlbm_i, gm_tpleb7ph7fjnlbm_s};
const gm_tmpl gm_tplfdsuiujvavc8 = {3u, gm_tplfdsuiujvavc8_p, gm_tplfdsuiujvavc8_i, gm_tplfdsuiujvavc8_s};
const gm_tmpl gm_tplfr9bfp6t7vb7 = {14u, gm_tplfr9bfp6t7vb7_p, gm_tplfr9bfp6t7vb7_i, gm_tplfr9bfp6t7vb7_s};
const gm_tmpl gm_tplfv85d3oogq60 = {1u, gm_tplfv85d3oogq60_p, gm_tplfv85d3oogq60_i, gm_tplfv85d3oogq60_s};
const gm_tmpl gm_tplg0lh6vujmm6o = {1u, gm_tplg0lh6vujmm6o_p, gm_tplg0lh6vujmm6o_i, gm_tplg0lh6vujmm6o_s};
const gm_tmpl gm_tplg575kk57c4i8 = {2u, gm_tplg575kk57c4i8_p, gm_tplg575kk57c4i8_i, gm_tplg575kk57c4i8_s};
const gm_tmpl gm_tplgm3gpate6ntg = {3u, gm_tplgm3gpate6ntg_p, gm_tplgm3gpate6ntg_i, gm_tplgm3gpate6ntg_s};
const gm_tmpl gm_tplh0kdp5708tgq = {1u, gm_tplh0kdp5708tgq_p, gm_tplh0kdp5708tgq_i, gm_tplh0kdp5708tgq_s};
const gm_tmpl gm_tplht122d0ji4km = {2u, gm_tplht122d0ji4km_p, gm_tplht122d0ji4km_i, gm_tplht122d0ji4km_s};
const gm_tmpl gm_tpli2vu74n9ss6f = {1u, gm_tpli2vu74n9ss6f_p, gm_tpli2vu74n9ss6f_i, gm_tpli2vu74n9ss6f_s};
const gm_tmpl gm_tplit8jkrg5bt3p = {1u, gm_tplit8jkrg5bt3p_p, gm_tplit8jkrg5bt3p_i, gm_tplit8jkrg5bt3p_s};
const gm_tmpl gm_tpljf9iocqf0fr6 = {1u, gm_tpljf9iocqf0fr6_p, gm_tpljf9iocqf0fr6_i, gm_tpljf9iocqf0fr6_s};
const gm_tmpl gm_tpljhar6jmbtsst = {1u, gm_tpljhar6jmbtsst_p, gm_tpljhar6jmbtsst_i, gm_tpljhar6jmbtsst_s};
const gm_tmpl gm_tplk13976gpibvh = {1u, gm_tplk13976gpibvh_p, gm_tplk13976gpibvh_i, gm_tplk13976gpibvh_s};
const gm_tmpl gm_tplkinjhvj7vqlj = {1u, gm_tplkinjhvj7vqlj_p, gm_tplkinjhvj7vqlj_i, gm_tplkinjhvj7vqlj_s};
const gm_tmpl gm_tplkpmnsso79v4r = {1u, gm_tplkpmnsso79v4r_p, gm_tplkpmnsso79v4r_i, gm_tplkpmnsso79v4r_s};
const gm_tmpl gm_tplktj7cco9lq40 = {2u, gm_tplktj7cco9lq40_p, gm_tplktj7cco9lq40_i, gm_tplktj7cco9lq40_s};
const gm_tmpl gm_tpll408251sp58f = {1u, gm_tpll408251sp58f_p, gm_tpll408251sp58f_i, gm_tpll408251sp58f_s};
const gm_tmpl gm_tpllktp07diubcq = {2u, gm_tpllktp07diubcq_p, gm_tpllktp07diubcq_i, gm_tpllktp07diubcq_s};
const gm_tmpl gm_tpllovel7a2ips1 = {2u, gm_tpllovel7a2ips1_p, gm_tpllovel7a2ips1_i, gm_tpllovel7a2ips1_s};
const gm_tmpl gm_tplm16s64mnihe4 = {1u, gm_tplm16s64mnihe4_p, gm_tplm16s64mnihe4_i, gm_tplm16s64mnihe4_s};
const gm_tmpl gm_tplm3t9qdrf37sd = {1u, gm_tplm3t9qdrf37sd_p, gm_tplm3t9qdrf37sd_i, gm_tplm3t9qdrf37sd_s};
const gm_tmpl gm_tplm6g8a9odsasi = {1u, gm_tplm6g8a9odsasi_p, gm_tplm6g8a9odsasi_i, gm_tplm6g8a9odsasi_s};
const gm_tmpl gm_tplmkb4jrr2jqel = {1u, gm_tplmkb4jrr2jqel_p, gm_tplmkb4jrr2jqel_i, gm_tplmkb4jrr2jqel_s};
const gm_tmpl gm_tplmrpf5m9qkka0 = {2u, gm_tplmrpf5m9qkka0_p, gm_tplmrpf5m9qkka0_i, gm_tplmrpf5m9qkka0_s};
const gm_tmpl gm_tplmurfi7rvdamc = {1u, gm_tplmurfi7rvdamc_p, gm_tplmurfi7rvdamc_i, gm_tplmurfi7rvdamc_s};
const gm_tmpl gm_tpln7ptjfs3pum5 = {1u, gm_tpln7ptjfs3pum5_p, gm_tpln7ptjfs3pum5_i, gm_tpln7ptjfs3pum5_s};
const gm_tmpl gm_tplnd5opdv87prb = {1u, gm_tplnd5opdv87prb_p, gm_tplnd5opdv87prb_i, gm_tplnd5opdv87prb_s};
const gm_tmpl gm_tplnj647l00ku7g = {3u, gm_tplnj647l00ku7g_p, gm_tplnj647l00ku7g_i, gm_tplnj647l00ku7g_s};
const gm_tmpl gm_tplnl7d4k0qgogq = {1u, gm_tplnl7d4k0qgogq_p, gm_tplnl7d4k0qgogq_i, gm_tplnl7d4k0qgogq_s};
const gm_tmpl gm_tplnt454lmhb7iv = {1u, gm_tplnt454lmhb7iv_p, gm_tplnt454lmhb7iv_i, gm_tplnt454lmhb7iv_s};
const gm_tmpl gm_tplp4f8nloo6fdo = {1u, gm_tplp4f8nloo6fdo_p, gm_tplp4f8nloo6fdo_i, gm_tplp4f8nloo6fdo_s};
const gm_tmpl gm_tplp5mnidu5m01d = {1u, gm_tplp5mnidu5m01d_p, gm_tplp5mnidu5m01d_i, gm_tplp5mnidu5m01d_s};
const gm_tmpl gm_tplp81h2rbu9gd2 = {1u, gm_tplp81h2rbu9gd2_p, gm_tplp81h2rbu9gd2_i, gm_tplp81h2rbu9gd2_s};
const gm_tmpl gm_tplpaq33rl4cnb8 = {2u, gm_tplpaq33rl4cnb8_p, gm_tplpaq33rl4cnb8_i, gm_tplpaq33rl4cnb8_s};
const gm_tmpl gm_tplpcjaq35mrruk = {3u, gm_tplpcjaq35mrruk_p, gm_tplpcjaq35mrruk_i, gm_tplpcjaq35mrruk_s};
const gm_tmpl gm_tplpntcuu1r3a58 = {8u, gm_tplpntcuu1r3a58_p, gm_tplpntcuu1r3a58_i, gm_tplpntcuu1r3a58_s};
const gm_tmpl gm_tplq5eond7ctc71 = {1u, gm_tplq5eond7ctc71_p, gm_tplq5eond7ctc71_i, gm_tplq5eond7ctc71_s};
const gm_tmpl gm_tplqfu63ajpo70a = {1u, gm_tplqfu63ajpo70a_p, gm_tplqfu63ajpo70a_i, gm_tplqfu63ajpo70a_s};
const gm_tmpl gm_tplqnml5avaeikk = {1u, gm_tplqnml5avaeikk_p, gm_tplqnml5avaeikk_i, gm_tplqnml5avaeikk_s};
const gm_tmpl gm_tplrfnb607u5hvd = {2u, gm_tplrfnb607u5hvd_p, gm_tplrfnb607u5hvd_i, gm_tplrfnb607u5hvd_s};
const gm_tmpl gm_tplrkvt0j8tqlqg = {1u, gm_tplrkvt0j8tqlqg_p, gm_tplrkvt0j8tqlqg_i, gm_tplrkvt0j8tqlqg_s};
const gm_tmpl gm_tplruf3ms5lqgst = {1u, gm_tplruf3ms5lqgst_p, gm_tplruf3ms5lqgst_i, gm_tplruf3ms5lqgst_s};
const gm_tmpl gm_tpls2ou5a11vcn7 = {2u, gm_tpls2ou5a11vcn7_p, gm_tpls2ou5a11vcn7_i, gm_tpls2ou5a11vcn7_s};
const gm_tmpl gm_tpls80s4u4ujepk = {7u, gm_tpls80s4u4ujepk_p, gm_tpls80s4u4ujepk_i, gm_tpls80s4u4ujepk_s};
const gm_tmpl gm_tplsc9pqkp31v7e = {1u, gm_tplsc9pqkp31v7e_p, gm_tplsc9pqkp31v7e_i, gm_tplsc9pqkp31v7e_s};
const gm_tmpl gm_tplsdokr8v76bhc = {1u, gm_tplsdokr8v76bhc_p, gm_tplsdokr8v76bhc_i, gm_tplsdokr8v76bhc_s};
const gm_tmpl gm_tplsjufktgf4o1v = {1u, gm_tplsjufktgf4o1v_p, gm_tplsjufktgf4o1v_i, gm_tplsjufktgf4o1v_s};
const gm_tmpl gm_tplt68r4kihke54 = {3u, gm_tplt68r4kihke54_p, gm_tplt68r4kihke54_i, gm_tplt68r4kihke54_s};
const gm_tmpl gm_tplt8hcvvk31bg7 = {1u, gm_tplt8hcvvk31bg7_p, gm_tplt8hcvvk31bg7_i, gm_tplt8hcvvk31bg7_s};
const gm_tmpl gm_tplukjghf5ng2te = {2u, gm_tplukjghf5ng2te_p, gm_tplukjghf5ng2te_i, gm_tplukjghf5ng2te_s};
const gm_tmpl gm_tpluldhqdblqoav = {1u, gm_tpluldhqdblqoav_p, gm_tpluldhqdblqoav_i, gm_tpluldhqdblqoav_s};
const gm_tmpl gm_tplvf2bf6hftf4i = {2u, gm_tplvf2bf6hftf4i_p, gm_tplvf2bf6hftf4i_i, gm_tplvf2bf6hftf4i_s};
const gm_tmpl gm_tplvm7e7lfhcheu = {2u, gm_tplvm7e7lfhcheu_p, gm_tplvm7e7lfhcheu_i, gm_tplvm7e7lfhcheu_s};
const gm_tmpl gm_tplvonunpuh0kn8 = {1u, gm_tplvonunpuh0kn8_p, gm_tplvonunpuh0kn8_i, gm_tplvonunpuh0kn8_s};
const gm_tmpl_piece gm_tpl0c7sat2t7j6v_p[] = {{gm_s1mh1dhprjqfo, 18}, {gm_smp6ue9h98hbc, 30}};
const gm_tmpl_piece gm_tpl0e2i60vm7gtr_p[] = {{gm_sao4mumvadoct, 3}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tpl0emlhhlpmv0s_p[] = {{gm_snls9i3gr3n3f, 40}, {gm_s8abg3vemofr6, 3}, {gm_sp8ppesjprts0, 66}};
const gm_tmpl_piece gm_tpl0vock46lp3ec_p[] = {{gm_s0mpcnom7nq2p, 46}, {gm_skitebsmtce1i, 33}, {gm_sjnkguo0atpm5, 2}};
const gm_tmpl_piece gm_tpl1buo4rn7srlb_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_smooaouqmm8dj, 1}, {gm_ssk4a01soguqj, 6}, {gm_s458j1t80v8nr, 14}, {gm_sg5gdnrith2i6, 2}, {gm_s959ni36b80a4, 3}};
const gm_tmpl_piece gm_tpl1dmvfuuebopd_p[] = {{gm_sc19lloml9nb7, 16}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl1sdu3l536hkg_p[] = {{gm_sns1hl83hdlcn, 4}, {gm_st52k2850n5r5, 4}, {gm_so9fl7jecsmrb, 26}, {gm_sp2a19d61vbe0, 3}, {gm_sgukm1evmrjal, 3}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpl1t0vlt7tq67r_p[] = {{gm_sd1gv97l675vb, 25}, {gm_sh4ufq9bak51r, 12}, {gm_sv3o54ps0ppci, 18}, {gm_s9kj8vgi6430a, 5}, {gm_s6t4r831mukdr, 2}};
const gm_tmpl_piece gm_tpl29spsi1atllj_p[] = {{gm_sevf8q26aov70, 5}, {gm_s39mljh669qgl, 1}, {gm_s39mljh669qgl, 1}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tpl34lim186g0fm_p[] = {{gm_s2mvbldeg43ph, 8}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl4g6bpqlb10dt_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s85m9c4pt0ikp, 5}};
const gm_tmpl_piece gm_tpl4kmab8oiki17_p[] = {{gm_s4t8kk8uuclvr, 26}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tpl4mhnbmvg0r80_p[] = {{gm_sk2dpusgkm375, 21}, {gm_smbjf525bk9a0, 19}};
const gm_tmpl_piece gm_tpl4upfck3qu2er_p[] = {{gm_sr2kfei26cj64, 17}, {gm_s0m0qaokk51g0, 3}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl5a8og2hcltt3_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sf3f9da3960bu, 5}, {gm_sp02qqgucmpt7, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl5cbugllfe764_p[] = {{gm_smooaouqmm8dj, 1}, {gm_s3p0rbs9hc3lg, 1}, {gm_s3p0rbs9hc3lg, 1}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl5svfna5ij5ui_p[] = {{gm_shv3ttlq9l359, 14}, {gm_s3p0rbs9hc3lg, 1}, {gm_smooaouqmm8dj, 1}, {gm_scm3afv9qr78v, 4}};
const gm_tmpl_piece gm_tpl5vfcnh2qs08h_p[] = {{gm_strsld72u30ds, 8}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl63iuq3je4sbr_p[] = {{gm_se4r520bkfair, 17}, {gm_sd6vqnf9glo8m, 50}};
const gm_tmpl_piece gm_tpl642cf7k81lop_p[] = {{gm_seflbetisd1ui, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl64qrh372lslh_p[] = {{gm_sudnegb3umvac, 11}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sq12h87ngj5nl, 8}};
const gm_tmpl_piece gm_tpl69qlgb4nvkqp_p[] = {{gm_sj9krj49k3580, 9}, {gm_sqdfr4k3vea5p, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl6boglt2rkmh4_p[] = {{gm_slfinpjh0406b, 46}, {gm_scd1bb5dijnis, 14}};
const gm_tmpl_piece gm_tpl6lpvgmmqlalm_p[] = {{gm_s5pm13tivcrur, 14}, {gm_sh60ig4u675k6, 73}};
const gm_tmpl_piece gm_tpl75b43ohtuvq2_p[] = {{gm_sasea0sobfkrs, 7}, {gm_s6t4r831mukdr, 2}};
const gm_tmpl_piece gm_tpl78lk9ugsmc0b_p[] = {{gm_secrjke11f3ph, 12}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl7cjap0b34jjm_p[] = {{gm_s19m2tlro6kap, 44}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl7eehu7c1eqka_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sd323o3qr3mfd, 63}};
const gm_tmpl_piece gm_tpl7net9ifts13e_p[] = {{gm_sc0mj6111f7cc, 9}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl7t589bv3t4us_p[] = {{gm_so78814tnt5g5, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl83pfuovi474u_p[] = {{gm_s6ioufa56mnlh, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl94ei7rae9si2_p[] = {{gm_sb1nc8gh4as5h, 14}, {gm_s585om8jb9mop, 24}};
const gm_tmpl_piece gm_tpl9cnobsjbh0r2_p[] = {{gm_s3p0rbs9hc3lg, 1}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tpl9e18q8to9md2_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_sjfdnh735o7f8, 4}, {gm_sk9at358cmauo, 26}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpl9uslg4i3oo2s_p[] = {{gm_s8ps5ka0vnf6d, 41}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl9v7lb7n4a602_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s7hkl5mqa15gm, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplaahjt316h1v3_p[] = {{gm_s9oo6ammv0sch, 16}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplae28pp9b49u1_p[] = {{gm_sjgrq0dv175qo, 29}, {gm_sjnkguo0atpm5, 2}};
const gm_tmpl_piece gm_tplaka4u9f9dfvk_p[] = {{gm_sla366gd7nphd, 57}, {gm_s9p37ubghllhn, 63}};
const gm_tmpl_piece gm_tplaoelja80vfud_p[] = {{gm_sns1hl83hdlcn, 4}, {gm_svnpflkpvd513, 4}, {gm_sigup1gvr7a13, 4}, {gm_skpk7m7mpsr06, 5}, {gm_se64as7gfqfsc, 35}, {gm_scq06n33arhgv, 4}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplaotru3rka6ku_p[] = {{gm_sdhlphf1ucpbo, 17}, {gm_sehgil1ui47md, 6}, {gm_spdkbvjffkg4b, 10}, {gm_sqrh6i8djv6b4, 6}, {gm_snr5o5tak6405, 3}};
const gm_tmpl_piece gm_tplb2ihdc8dbskd_p[] = {{gm_s5d58k7uql7sf, 71}, {gm_sp7cdd0n91vli, 7}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplb899rpghqa93_p[] = {{gm_sdes3rh8ctb7v, 20}, {gm_so514oj5nbhcu, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplblrluopde8aq_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_somhu4vltiil1, 3}, {gm_shi7nidbc64qf, 3}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplc1tlvqjnir84_p[] = {{gm_s174sqhpcliev, 42}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplcpp8u5i709mj_p[] = {{gm_s00odc15r3q8f, 15}, {gm_s2p0dacnmvuhi, 6}, {gm_s41bap0m04tt9, 11}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplcqfaq9vjn7hh_p[] = {{gm_sqkcsn1qihjbl, 40}, {gm_s6mvc5jp4o5mi, 29}, {gm_snr5o5tak6405, 3}};
const gm_tmpl_piece gm_tpldrfjfgoqe179_p[] = {{gm_s495moo6tjeh0, 19}, {gm_slogre7vmagad, 34}};
const gm_tmpl_piece gm_tplduutdqd8m5r5_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s4oloc6hggc50, 91}};
const gm_tmpl_piece gm_tples1m1m9iv6b0_p[] = {{gm_soa9mnl4ahjud, 7}, {gm_s3p0rbs9hc3lg, 1}, {gm_smooaouqmm8dj, 1}, {gm_sdo9t6m49dsco, 4}, {gm_sr4qbrk6tlgaa, 0}, {gm_smooaouqmm8dj, 1}, {gm_si6sr5gqkcui7, 5}};
const gm_tmpl_piece gm_tpleth0e0pd50mr_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s6t4r831mukdr, 2}};
const gm_tmpl_piece gm_tplf579fe8ol0qp_p[] = {{gm_s7vj61tk4g1rp, 13}, {gm_srcs6slmq8ak7, 14}, {gm_s8sv99c8q8gbc, 70}, {gm_srg9h7ti0cg3a, 24}};
const gm_tmpl_piece gm_tplf6hbp7abojsd_p[] = {{gm_s4oil3epcv041, 2}, {gm_s8qkarceaerkr, 21}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplfcgbc9s4hmn7_p[] = {{gm_sabq7k0io9ql3, 65}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplfdsuiujvavc8_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3emon0a31046, 80}, {gm_slgmqgkioa4u6, 2}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplfpi7koctqa7b_p[] = {{gm_stbt78k7uqc0m, 23}, {gm_s6t4r831mukdr, 2}};
const gm_tmpl_piece gm_tplfsrd1mgov7nd_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sha303gigke52, 9}, {gm_ss8v6rf5v54r7, 8}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplg2qlrjk1sunc_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sovv48653dnju, 39}};
const gm_tmpl_piece gm_tplg91jq41nbbkt_p[] = {{gm_spmv5l6cklh5d, 17}, {gm_snvncjka7ph6m, 6}, {gm_sdo8qcbf74mp0, 6}};
const gm_tmpl_piece gm_tplhatoh83mi4is_p[] = {{gm_sta33ql7gg6dk, 45}, {gm_skvko85ql2e6c, 61}};
const gm_tmpl_piece gm_tplhiqpct20jd67_p[] = {{gm_sb54ui2amic32, 1}, {gm_sb54ui2amic32, 1}};
const gm_tmpl_piece gm_tpli8blk453jgvg_p[] = {{gm_seohfka1b0c2g, 46}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpli9mr2dqh2vqo_p[] = {{gm_spffvcegd6os6, 5}, {gm_shesnhdok5fgk, 54}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplibfhnj0illo7_p[] = {{gm_sto6ssj6r66e3, 29}, {gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplicmhpq9e6sbc_p[] = {{gm_so9fi6r28770a, 2}, {gm_s1asl1e1tvb0k, 1}, {gm_sgrkf3bk77nul, 16}};
const gm_tmpl_piece gm_tplidb7upjrao3q_p[] = {{gm_s6e8s18mgu3dj, 18}, {gm_sdi58fds59v3t, 29}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplidl0tsmrpak6_p[] = {{gm_suio2i7004ufh, 7}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplilne6f8eq90i_p[] = {{gm_sdpf7uf4ias00, 23}, {gm_s3m769if3iik9, 31}};
const gm_tmpl_piece gm_tpliq8p2spcvq06_p[] = {{gm_spp1c7975jken, 4}, {gm_sfll40jgr4opl, 3}, {gm_sivm2ee5886a0, 7}};
const gm_tmpl_piece gm_tplj6imuu9dopvn_p[] = {{gm_sc7nhpcejp1mj, 20}, {gm_selnb2co583fc, 5}, {gm_st52k2850n5r5, 4}, {gm_sigup1gvr7a13, 4}, {gm_sgfqei77bsia8, 3}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpljkecvj49gp71_p[] = {{gm_supf808555d3d, 12}, {gm_s39mljh669qgl, 1}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tpljnlsd7076tqd_p[] = {{gm_s2j5j1obh5p7l, 5}, {gm_snukl6jqep6jf, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplk34ojqiaho84_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplkk98947i5b3i_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_srefpimq1sas7, 3}, {gm_s9a0n41a8i82u, 3}};
const gm_tmpl_piece gm_tplle7qvdq2jai6_p[] = {{gm_som0q53jqvjh3, 4}, {gm_s2v8uomf8pkfv, 12}, {gm_sehgil1ui47md, 6}, {gm_s41uc8v0dkl4a, 14}, {gm_sqrh6i8djv6b4, 6}, {gm_snr5o5tak6405, 3}};
const gm_tmpl_piece gm_tplli6rlvq5vnvb_p[] = {{gm_soa9mnl4ahjud, 7}, {gm_sgmhc3klfmn46, 4}, {gm_smooaouqmm8dj, 1}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpllihoeldkkkhv_p[] = {{gm_scmcg8350emvu, 18}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplljgqo309tg8i_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_svkdb6kaja59r, 6}, {gm_so514oj5nbhcu, 1}, {gm_s8dbdqj1sqibj, 2}};
const gm_tmpl_piece gm_tpllktp07diubcq_p[] = {{gm_spv9savsu9h0b, 3}, {gm_s039kgqbmm0mv, 9}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplmf8455dp92a3_p[] = {{gm_seak3hcpfjfp2, 69}, {gm_sh9sks72142ms, 16}};
const gm_tmpl_piece gm_tplmpc7ghuokash_p[] = {{gm_sg5gdnrith2i6, 2}, {gm_sao4mumvadoct, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplmurfi7rvdamc_p[] = {{gm_som0q53jqvjh3, 4}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tpln2i759e19cgt_p[] = {{gm_s5l2i6ke0df90, 20}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplncscrjg4euhr_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sl3q52ep6aa53, 1}, {gm_sahim4484f9l7, 2}};
const gm_tmpl_piece gm_tplneoolql7n1n7_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s4ft5s33591qg, 6}, {gm_stc3e0j0moc2h, 4}, {gm_sb398tg9q30m5, 5}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tplnt454lmhb7iv_p[] = {{gm_shpjiqltl04un, 32}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplntk567ck43t8_p[] = {{gm_sm55k23067j17, 15}, {gm_sr2qa646easkn, 55}};
const gm_tmpl_piece gm_tploc41q7b5gurn_p[] = {{gm_som0q53jqvjh3, 4}, {gm_slhllpfip1vpg, 2}, {gm_s8m45dpj5ugr4, 5}, {gm_sqtouugmg03b1, 4}};
const gm_tmpl_piece gm_tplp7faum694g3u_p[] = {{gm_s1bpuqd44lur7, 6}, {gm_sjcm18p35o9rq, 3}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplpdnopu1p8235_p[] = {{gm_sb2nfo787ropg, 5}, {gm_sq68mfjf87n5h, 15}, {gm_sbvet41ukod2k, 15}, {gm_sqslcbfraugjg, 7}, {gm_sfg1v6bqcbbav, 9}};
const gm_tmpl_piece gm_tplpe5hifub5ms7_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_soqhrfmr1feii, 6}, {gm_s93ft79mfnlvp, 9}, {gm_s1tfd03l0gv6e, 13}};
const gm_tmpl_piece gm_tplqf2cda88og0i_p[] = {{gm_sdjk9c005ec9t, 12}, {gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplr9kq0pku58q3_p[] = {{gm_s03c24qg7i06q, 12}, {gm_s78m6s8sukfpa, 12}, {gm_sp7cdd0n91vli, 7}, {gm_sgcku4bq4407l, 18}};
const gm_tmpl_piece gm_tplr9mrmvfj4528_p[] = {{gm_sfq3o7cad07rv, 13}, {gm_s84smn81idetm, 10}};
const gm_tmpl_piece gm_tplsh3mbm625h0g_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s7c2mvkr8kl49, 34}, {gm_s0r9vb1qh90ue, 115}};
const gm_tmpl_piece gm_tplt0hjq4nb3jer_p[] = {{gm_s7tfst2qj3lk8, 23}, {gm_slogre7vmagad, 34}};
const gm_tmpl_piece gm_tpltd7vnsp01mvt_p[] = {{gm_sve2u8a7eapjo, 17}, {gm_sno069vrt5g9q, 3}, {gm_s6t4r831mukdr, 2}};
const gm_tmpl_piece gm_tpltng0q64u6p80_p[] = {{gm_slq8b0dsnkd8e, 13}, {gm_snjonc4tmu56e, 20}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplu58b6rd4mq28_p[] = {{gm_smooaouqmm8dj, 1}, {gm_svhavo13qha5k, 3}, {gm_ssbpo4qatmvlq, 6}, {gm_s1asl1e1tvb0k, 1}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplu7gkm8brh8s9_p[] = {{gm_srni26bloa03c, 11}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpluc8r0n3peqad_p[] = {{gm_sdbih7ptecvdq, 38}, {gm_sg5gdnrith2i6, 2}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tplue01enectvb0_p[] = {{gm_senhrlorjabcl, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplv1cj36rb6m0h_p[] = {{gm_s4t89div5i959, 79}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplv8s8fgl2lggu_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_ssd7h3tuipgq5, 54}};
const gm_tmpl_piece gm_tplvc30qsg5hfc6_p[] = {{gm_sfhbo1m6f9ql4, 34}, {gm_sg92g0je3abps, 13}};
const gm_tmpl_piece gm_tplvqa6aqf5q6ij_p[] = {{gm_s43sk7g92lovh, 12}, {gm_sfm9elkq6bn2e, 1}};
const gm_type gm_ty9oqmmiu8qc = {.size = 2, .align = 2, .kind = GM_K_INT, .bits = 16, .is_signed = false};
const gm_type gm_ty9oqmmiu8qc;
const gm_variant gm_tylv5i3f25jk_v[] = {{"Neg", 0, NULL}, {"BitNot", 1, NULL}, {"Not", 2, NULL}};
const uint32_t gm_tpl00b7kgp37ng7_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
const uint32_t gm_tpl0epjr4745p10_i[] = {0u, 1u, 2u, 3u, 4u, 5u};
const uint32_t gm_tpl0oli8trq10sp_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl0rhldcdavmcg_i[] = {0u, 1u};
const uint32_t gm_tpl0rlavm9oqi24_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl0td8lr3f4ven_i[] = {0u, 1u};
const uint32_t gm_tpl1o8uodh0pvjc_i[] = {0u, 1u};
const uint32_t gm_tpl1q6o13b940dg_i[] = {0u};
const uint32_t gm_tpl1u8koiam5nmo_i[] = {0u};
const uint32_t gm_tpl2arnv1ofugei_i[] = {0u};
const uint32_t gm_tpl2fb8qlpeqlet_i[] = {0u, 1u};
const uint32_t gm_tpl2jsq89buriv9_i[] = {0u};
const uint32_t gm_tpl34g57qfcenfh_i[] = {0u};
const uint32_t gm_tpl3tcah2c39gjk_i[] = {0u};
const uint32_t gm_tpl4isjs1bg9fhg_i[] = {0u};
const uint32_t gm_tpl54tnh29jjfju_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl56mvlv70ecs8_i[] = {0u};
const uint32_t gm_tpl5ldtdub2029b_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl5oph1bqhjo1e_i[] = {0u};
const uint32_t gm_tpl5prdhjdrg194_i[] = {0u, 1u};
const uint32_t gm_tpl5r25bpo8hobt_i[] = {0u, 1u};
const uint32_t gm_tpl5rdj46lif6eq_i[] = {0u, 1u};
const uint32_t gm_tpl5s6q12qjusi2_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpl64ltc7suc5qg_i[] = {0u, 1u};
const uint32_t gm_tpl6f612gert49l_i[] = {0u};
const uint32_t gm_tpl6itong91gv3f_i[] = {0u, 1u};
const uint32_t gm_tpl6lgf07g31gim_i[] = {0u, 1u};
const uint32_t gm_tpl6rgl0akrj87c_i[] = {0u};
const uint32_t gm_tpl6tnfk0d9dl8e_i[] = {0u};
const uint32_t gm_tpl78lk9ugsmc0b_i[] = {0u};
const uint32_t gm_tpl7ilj5n9objtq_i[] = {0u};
const uint32_t gm_tpl7kdhh1egc9gc_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl7meurf74nbc1_i[] = {0u};
const uint32_t gm_tpl7oj2vfshts60_i[] = {0u};
const uint32_t gm_tpl7pm1mtuteh6j_i[] = {0u};
const uint32_t gm_tpl83pfuovi474u_i[] = {0u};
const uint32_t gm_tpl8s825rf2tq3f_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl8ssnfo686ogd_i[] = {0u, 1u, 2u};
const uint32_t gm_tpla1fscog446dv_i[] = {0u};
const uint32_t gm_tpla1i8rr6vfju7_i[] = {0u};
const uint32_t gm_tplaa2bqbq660if_i[] = {0u, 1u, 2u};
const uint32_t gm_tplaahjt316h1v3_i[] = {0u};
const uint32_t gm_tplb899rpghqa93_i[] = {0u, 1u};
const uint32_t gm_tplbh7305mc9stu_i[] = {0u, 1u};
const uint32_t gm_tplc65rc46hqohp_i[] = {0u};
const uint32_t gm_tplcb1495nujsb7_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
const uint32_t gm_tplcjeg5mjcrb8d_i[] = {0u};
const uint32_t gm_tpldg1m204qb08c_i[] = {0u, 1u};
const uint32_t gm_tpldqrk6ll2jb23_i[] = {0u};
const uint32_t gm_tpldsg332pn89g6_i[] = {0u};
const uint32_t gm_tpldtecn6n5hb86_i[] = {0u, 1u};
const uint32_t gm_tplehc5f52ftol1_i[] = {0u, 1u};
const uint32_t gm_tplei9v853i04sp_i[] = {0u, 1u};
const uint32_t gm_tpleiji4s5drk3o_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplej21mtlj140h_i[] = {0u, 1u};
const uint32_t gm_tplg4866gcgr837_i[] = {0u};
const uint32_t gm_tplggnuionimjk9_i[] = {0u, 1u};
const uint32_t gm_tplgq3upqu7s79r_i[] = {0u, 1u};
const uint32_t gm_tplgvu725182gad_i[] = {0u, 1u, 2u};
const uint32_t gm_tplh1blptpemhkl_i[] = {0u};
const uint32_t gm_tplh78gsuiavvas_i[] = {0u};
const uint32_t gm_tplhdrbi5vdvbe6_i[] = {0u, 1u, 2u};
const uint32_t gm_tplhr6emralh8q6_i[] = {0u};
const uint32_t gm_tpli7hldrrhg7sr_i[] = {0u};
const uint32_t gm_tpli8blk453jgvg_i[] = {0u};
const uint32_t gm_tpliqi7pna59a9o_i[] = {0u};
const uint32_t gm_tplisurri6t3aun_i[] = {0u, 1u};
const uint32_t gm_tplit8jkrg5bt3p_i[] = {0u};
const uint32_t gm_tplj3bqjm2hv786_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpljm6mn1j3nrs6_i[] = {0u, 1u};
const uint32_t gm_tpljp2husef3b2l_i[] = {0u};
const uint32_t gm_tpljq1be78i0ptp_i[] = {0u};
const uint32_t gm_tpljqhtbatb6393_i[] = {0u, 1u};
const uint32_t gm_tplk1botso018is_i[] = {0u};
const uint32_t gm_tplk66j4h09rih6_i[] = {0u};
const uint32_t gm_tplk7f3hoqrengc_i[] = {0u};
const uint32_t gm_tpllcp8fo2sqc1r_i[] = {0u, 1u};
const uint32_t gm_tpllhk8pmjfso1j_i[] = {0u, 1u, 2u};
const uint32_t gm_tpllir4kiseuk2b_i[] = {0u, 1u, 2u};
const uint32_t gm_tplllolcurrktr6_i[] = {0u};
const uint32_t gm_tpllqboe50ngs7g_i[] = {0u};
const uint32_t gm_tplm3r2aiholmt8_i[] = {0u, 1u};
const uint32_t gm_tplmkgblgbo7imb_i[] = {0u};
const uint32_t gm_tplmpsv5qagthkk_i[] = {0u};
const uint32_t gm_tpln2j7s58m8uan_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpln7ptjfs3pum5_i[] = {0u};
const uint32_t gm_tplngsd5kcuu6fv_i[] = {0u};
const uint32_t gm_tplnv6l3ecgreid_i[] = {0u, 1u};
const uint32_t gm_tplo12k9ajl5nes_i[] = {0u, 1u};
const uint32_t gm_tplotas2madniqp_i[] = {0u};
const uint32_t gm_tplpntcuu1r3a58_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
const uint32_t gm_tplpog54do9oh2a_i[] = {0u, 1u};
const uint32_t gm_tplpvmvg9ce66cu_i[] = {0u};
const uint32_t gm_tplq12pt4e53uj3_i[] = {0u, 1u};
const uint32_t gm_tplq73iq4gr17op_i[] = {0u};
const uint32_t gm_tplq85e99v067d0_i[] = {0u};
const uint32_t gm_tplq8t63lu2docj_i[] = {0u};
const uint32_t gm_tplqipe054or34v_i[] = {0u};
const uint32_t gm_tplqp3jlt332oq5_i[] = {0u};
const uint32_t gm_tplr2tuff1mvdst_i[] = {0u};
const uint32_t gm_tplr7712ujh33fl_i[] = {0u, 1u};
const uint32_t gm_tplrfab517u4o97_i[] = {0u};
const uint32_t gm_tplrh70572i8ko0_i[] = {0u};
const uint32_t gm_tpls6f1h1hgtqgb_i[] = {0u};
const uint32_t gm_tpls92cg1g1s0vs_i[] = {0u, 1u, 2u, 3u, 4u, 5u};
const uint32_t gm_tplsdamq31ts61q_i[] = {0u};
const uint32_t gm_tplss5b4q9est2j_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
const uint32_t gm_tplt0qsjg06adbb_i[] = {0u, 1u};
const uint32_t gm_tplt392n8hqfhn9_i[] = {0u};
const uint32_t gm_tplt524tdio8698_i[] = {0u};
const uint32_t gm_tplt56hco5v1mvf_i[] = {0u};
const uint32_t gm_tplt5ks894658kl_i[] = {0u};
const uint32_t gm_tplt8hcvvk31bg7_i[] = {0u};
const uint32_t gm_tplt9o3qfo5dqct_i[] = {0u};
const uint32_t gm_tpltdh9l4qjtpek_i[] = {0u};
const uint32_t gm_tpltnu092iqmfdd_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
const uint32_t gm_tplto3kksb9sot8_i[] = {0u};
const uint32_t gm_tplttoavo0o7ugf_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplucd2r10hh63v_i[] = {0u, 1u};
const uint32_t gm_tpluldhqdblqoav_i[] = {0u};
const uint32_t gm_tplv1i7e9gejv6a_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplvajlmotjd2qr_i[] = {0u, 1u};
const uint32_t gm_tplvc9hnrt45n6d_i[] = {0u, 1u};
const uint32_t gm_tplvj9n0kinsqlj_i[] = {0u};
const uint32_t gm_tplvm3qupt9dved_i[] = {0u};
const uint32_t gm_tplvqa6aqf5q6ij_i[] = {0u};
const uint8_t gm_s02ktme6orfkh[7] = "gm_dir";
const uint8_t gm_s041tbii048j3[8] = "newName";
const uint8_t gm_s06mk2gjnlvf8[73] = "the elements of a `#soa` array are written positionally, or as `fill: s`";
const uint8_t gm_s06sbklusedgl[5] = " \077 (";
const uint8_t gm_s08sdh8q6l3vj[49] = "` is an effect set, not a single effect (\302\24716.6)";
const uint8_t gm_s0eemp7of8buc[3] = "->";
const uint8_t gm_s0eqrtq5mqb3a[16] = "ArrayPtrToSlice";
const uint8_t gm_s0ffe093dar6e[19] = "cannot complement ";
const uint8_t gm_s0fgdcd1ggor2[18] = "    {} r = {}();\012";
const uint8_t gm_s0iid7nkkvf30[55] = "error: input is not valid UTF-8; the line was ignored\012";
const uint8_t gm_s0jho24omhfui[7] = "branch";
const uint8_t gm_s0m0qaokk51g0[4] = " + ";
const uint8_t gm_s0mppcr6a4lgb[9] = "{:.1} {}";
const uint8_t gm_s0nf1rg04f3m5[4] = "all";
const uint8_t gm_s0sg56e5fp4di[12] = "(*({} *){})";
const uint8_t gm_s0stes97dk54i[147] = "run those of the dependency <name> of the input's\012package (or of one of its dependencies) instead, with the\012dependency versions this package locks";
const uint8_t gm_s10qn2lc8ck56[78] = "` refers to itself, so it needs a declared return type: write `-> R` (\302\24711.1)";
const uint8_t gm_s1alpdaq78k1g[5] = "seen";
const uint8_t gm_s1asl1e1tvb0k[2] = ")";
const uint8_t gm_s1bf3a5i0lcmm[21] = "Library/Caches/gamma";
const uint8_t gm_s1btmm2esmgsd[27] = "`{}` has no variant `{}`{}";
const uint8_t gm_s1dmrsprfdp53[8] = "{} = {}";
const uint8_t gm_s1gm12po6spua[18] = "; gm_unw.frame = ";
const uint8_t gm_s1in79aaeui99[10] = "gm_socket";
const uint8_t gm_s1lomal6al3te[71] = "if ({} == {} && {} == ({})-1) gm_trap(\"integer overflow in `{}`\", {});";
const uint8_t gm_s1qrqfh656g4q[6] = "Alloc";
const uint8_t gm_s1sspofc2u3bb[37] = "`.cross` needs three elements, and `";
const uint8_t gm_s1u0m6lm5u1su[7] = ".ptr, ";
const uint8_t gm_s20tdu9mtc6fi[8] = " = {0};";
const uint8_t gm_s289ga0mk9fi6[20] = "install decls {} {}";
const uint8_t gm_s2c267emk2nni[6] = ".data";
const uint8_t gm_s2c5ns5u9qs90[35] = "` contains the second-class type `";
const uint8_t gm_s2cartb32o2of[10] = ") { gm_gi";
const uint8_t gm_s2gpk5q7mg4o8[5] = "swap";
const uint8_t gm_s2h8cr50573go[8] = "PtrDiff";
const uint8_t gm_s2hgdf6b9d53s[5] = "Some";
const uint8_t gm_s2micd2hub1nf[26] = "F.h.up = (gm_frame *)env;";
const uint8_t gm_s2n0v309tod60[70] = "`--tag`, `--branch`, `--rev`, `--version` and `--subdir` need `--git`";
const uint8_t gm_s2n3d0ogcjggg[14] = "     Created ";
const uint8_t gm_s2qp8mqi0c2le[48] = "internal error: a vector access of a non-vector";
const uint8_t gm_s2r7keqsravo0[6] = "print";
const uint8_t gm_s2ttad1cg71bm[18] = "`}` to close the ";
const uint8_t gm_s30cnsbbn0c4o[4] = "vcs";
const uint8_t gm_s31holt846m75[10] = "gm_resume";
const uint8_t gm_s32j5fa04tkqj[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\325\000\000\000'\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_s352761194jbs[9] = "--worker";
const uint8_t gm_s35hbb2rhhdvv[33] = "to close the attribute arguments";
const uint8_t gm_s3adv1e9meuc1[15] = "gm_thread_join";
const uint8_t gm_s3dov523r8ujn[53] = "static const gm_tmpl {} = {{0u, {}_p, NULL, NULL}};\012";
const uint8_t gm_s3e0g1895m0j9[2] = "B";
const uint8_t gm_s3ergh2il1tjf[43] = "from_tag of a value that is not an integer";
const uint8_t gm_s3hersi16nhbp[34] = "uninitialized values of this type";
const uint8_t gm_s3i3n5bvpvmmi[16] = "the initializer";
const uint8_t gm_s3ijb9gbhdbib[27] = "`in` is not defined for {}";
const uint8_t gm_s3j69epdb3egk[26] = ", cx->env, cx->hctx, cx{}";
const uint8_t gm_s3kb1co7e63ka[37] = "expected a value, found the effect `";
const uint8_t gm_s3klpr9p78hh4[24] = "a place of unknown type";
const uint8_t gm_s3lq4s2q8ds4c[32] = "{ gm_buf mb = {0}; size_t ml = ";
const uint8_t gm_s3lv7dnoanjke[5] = "long";
const uint8_t gm_s3o37fp7c0sqt[15] = "GM_INT_TO_PTR(";
const uint8_t gm_s3o8dsbklmfsh[10] = "workspace";
const uint8_t gm_s3p0rbs9hc3lg[2] = " ";
const uint8_t gm_s3s4n6ehcrs2b[17] = "builtin function";
const uint8_t gm_s3tacjdl2ciei[13] = "((uintptr_t)";
const uint8_t gm_s41047ga9d9tq[26] = " {\012    gm_frame_overflow(";
const uint8_t gm_s42aeamnkrchf[31] = "the calling conventions differ";
const uint8_t gm_s43ls0s4m665j[57] = "attribute `#callconv` does not apply to a foreign member";
const uint8_t gm_s45i8231l5fma[10] = "fields = ";
const uint8_t gm_s465vmg9c4jq6[14] = " not applied\012";
const uint8_t gm_s46khuklhbvqd[21] = "cannot create {}: {}";
const uint8_t gm_s4a6o0t8isvlm[17] = "((uint8_t *)gm_g";
const uint8_t gm_s4div03f0lvce[12] = "F.w{} = {};";
const uint8_t gm_s4ft5s33591qg[7] = " |= ((";
const uint8_t gm_s4ko774bb2ea9[56] = "expected a struct or union literal after the attributes";
const uint8_t gm_s4l676io35g5u[12] = "((void (*)(";
const uint8_t gm_s4pucugl3svdm[11] = "extern {}\012";
const uint8_t gm_s4rtd6brb45fq[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000e\004\000\000\031\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_s4vqnsfknimfg[28] = "`pub` cannot be applied to ";
const uint8_t gm_s51qe9opse882[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\341\006\000\000\027\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s52ul4dg63jhe[11] = " < 0) == (";
const uint8_t gm_s56n05s0kn0p9[27] = "cannot read GAMMA_RUNNER: ";
const uint8_t gm_s58g91v8ecst5[13] = "gm_proc_kill";
const uint8_t gm_s5974q1kmorlc[52] = "`.{}` is not supported on this target (x86-64 only)";
const uint8_t gm_s59st1i1tm2m1[9] = "`~` on `";
const uint8_t gm_s5ecjs0hs1bpf[20] = "[All modules]({})\012\012";
const uint8_t gm_s5f9kvb1gm4j6[13] = "gm_net_error";
const uint8_t gm_s5g1e6qrps9q6[19] = "definitionProvider";
const uint8_t gm_s5irlrsnb30cc[24] = "no gamma.toml found in ";
const uint8_t gm_s5j8t1cg2erp9[8] = " needs ";
const uint8_t gm_s5k9ll19r7m4g[4] = "j64";
const uint8_t gm_s5m9qimbv7e8k[3] = " }";
const uint8_t gm_s5nt88lj0viqk[4] = "b{}";
const uint8_t gm_s5pm13tivcrur[15] = "this performs ";
const uint8_t gm_s5rb1e1n8ivr7[48] = "the import path `{}` leaves the package (\302\2473.2)";
const uint8_t gm_s62t7qmf3ajh5[69] = "package `{}`: `subdir` must be a relative path inside the repository";
const uint8_t gm_s63qpv8hba1m1[20] = "memcpy({}, {}, {});";
const uint8_t gm_s65ftmvtj458k[11] = "{}{}\012{}\012{}";
const uint8_t gm_s69o347hkiq9a[61] = "an `if` with an empty body and no `else`, or an empty `else`";
const uint8_t gm_s6d57grm686e3[27] = "compiler/check/template.gm";
const uint8_t gm_s6f83og3gefc1[8] = "->fmt[k";
const uint8_t gm_s6h2ge6vgkmhm[9] = "noreturn";
const uint8_t gm_s6hgf366ivqrg[34] = "duplicate tuple element name `{}`";
const uint8_t gm_s6hp19vgdsi0o[24] = "No space left on device";
const uint8_t gm_s6kq1mi4s1g58[3] = ":\077";
const uint8_t gm_s6okk95q42e26[15] = "run test bench";
const uint8_t gm_s6p5i58jsib94[8] = "{:016x}";
const uint8_t gm_s6p6u3ogotq4f[50] = "` in the other; only pointers convert to pointers";
const uint8_t gm_s6pceda0js0g8[6] = "</p>\012";
const uint8_t gm_s6peoato8pau5[13] = "((uint8_t *)";
const uint8_t gm_s6pilvbjelm5p[4] = "str";
const uint8_t gm_s6v6kgo7d00tn[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000T\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_s6vl5jv0tf2n4[8] = "\012note: ";
const uint8_t gm_s709tdhiijp4r[78] = "`&` needs a place (a variable, `p^`, field or element), not a temporary value";
const uint8_t gm_s76i8mdsri6em[70] = "invalid free of {:#x}: not the start of a live heap block of {} bytes";
const uint8_t gm_s786i8duem00q[27] = "compiler/check/matching.gm";
const uint8_t gm_s79dh6evnpkuf[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000o\001\000\000\031\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_s7c2mvkr8kl49[35] = "` is not available on vectors of `";
const uint8_t gm_s7cvig9qav6l5[3] = "0b";
const uint8_t gm_s7i2mni64durc[18] = "the target of `&`";
const uint8_t gm_s7mosjq6fpdqc[15] = "gm_format_spec";
const uint8_t gm_s7mqv7hf4fso0[27] = "     Nothing to create in ";
const uint8_t gm_s7phrfovrc6ul[239] = "pub Os :: union { Linux, MacOS, Windows, FreeBSD, Wasi, Freestanding }\012pub Arch :: union { X86_64, Aarch64, Riscv64, S390x, Wasm32, Other }\012pub Endian :: union { Little, Big }\012pub Mode :: union { Debug, Checked, Safe, Fast }\012pub os :: Os.";
const uint8_t gm_s7vusn2lig3dp[65] = "`{}` is an operation of `{}`; call it as `{}{}.{}(\342\200\246)` (\302\24712.1)";
const uint8_t gm_s802sbfa6mtsg[27] = "missing element {} of `{}`";
const uint8_t gm_s85hqf14lbkns[8] = "AbsDiff";
const uint8_t gm_s87e746eia9j4[6] = " > 0)";
const uint8_t gm_s88lqrfvhc8i3[46] = "internal error in the Gamma language server: ";
const uint8_t gm_s8aj3oag1l1u0[14] = "optional-type";
const uint8_t gm_s8b49h6svih4r[82] = "(\342\200\246)` is an iterator, not a value: it cannot be passed for a parameter of type `";
const uint8_t gm_s8ckgohq4msba[19] = "{} was called here";
const uint8_t gm_s8cr606ter6p9[39] = "this `{}` does not match the open `{}`";
const uint8_t gm_s8gaig0cs2uce[19] = "dependency cycle: ";
const uint8_t gm_s8i44sm1b77f2[121] = "this program switches fibers, which on WASI needs Binaryen's `wasm-opt` (https://github.com/WebAssembly/binaryen), but `";
const uint8_t gm_s8ki76moa277n[23] = "write `(try x) else d`";
const uint8_t gm_s8m45dpj5ugr4[6] = "; /* ";
const uint8_t gm_s8pjstf2pdmbk[14] = ", a) < GM_LD(";
const uint8_t gm_s8qvuobg6jcbo[13] = "((void *){})";
const uint8_t gm_s8rsp9val5ful[5] = "gm_s";
const uint8_t gm_s8ukphi4920cc[11] = "archive = ";
const uint8_t gm_s92hgp9htn7k0[21] = "gm_fret:\012    return ";
const uint8_t gm_s930a5k4dd69o[10] = "truncated";
const uint8_t gm_s94v9o49bjk09[12] = "gm_fs_lstat";
const uint8_t gm_s99ra887gkc8u[39] = "parameter `{}` is given more than once";
const uint8_t gm_s9eiod3sq367m[7] = "utf-16";
const uint8_t gm_s9go9vvkpcjpd[48] = "size_t argn; const void *argp = gm_args(&argn);";
const uint8_t gm_s9grefqkso86a[3] = "pn";
const uint8_t gm_s9loa14tklecu[3] = "md";
const uint8_t gm_s9tlur6oo59qa[31] = "out of memory in the scheduler";
const uint8_t gm_s9vag5co36u2r[18] = "`{}` clause in {}";
const uint8_t gm_sa1pcp92k3aj3[7] = "GetPtr";
const uint8_t gm_sa39b9bt4jpna[36] = "the `Yield` effect is not available";
const uint8_t gm_sa3hu8v6op70v[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000k\000\000\000\035\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_sa8mbp35lh2ts[84] = "gm_bt B = {{ gm_bt_top \077 gm_bt_top->prev : NULL, {}, NULL, 0, 0 }}; gm_bt_top = &B;";
const uint8_t gm_sa9mckopi2o5a[86] = "a `ctl` clause cannot `continue`: end it with `resume`, `break` or an exit (\302\24712.7.1)";
const uint8_t gm_saefgd8cv1aea[20] = " && !gm_unw.kind) {";
const uint8_t gm_sajpbdgkjnse7[8] = "main.gm";
const uint8_t gm_sal1kdi1rij5t[10] = "fs_rename";
const uint8_t gm_saoa4m7dh2vl3[55] = "a character literal must contain exactly one character";
const uint8_t gm_sapu6l8enc7d7[7] = "</dd>\012";
const uint8_t gm_sauknalgur9no[11] = "({} == {})";
const uint8_t gm_sav0q47tkm7j2[20] = "` (known profiles: ";
const uint8_t gm_savlppb39i8g6[6] = "</th>";
const uint8_t gm_savvoucn0tcn5[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\307\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_sb0d5eutumlsm[41] = "\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\077\002\000\000P\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_sb1nc8gh4as5h[15] = "the directory ";
const uint8_t gm_sb335j99bk4ch[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\030\001\000\000N\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000";
const uint8_t gm_sb3c9ehk423nf[6] = "Debug";
const uint8_t gm_sb3o3ij0n2htb[5] = "F.{}";
const uint8_t gm_sb455a3lakuom[60] = "`not` cannot be an operand here; parenthesize it: `(not x)`";
const uint8_t gm_sb8ag7vcv52jt[5] = "gm_k";
const uint8_t gm_sb8c0irhu65ra[140] = "a block body's last expression is not its value: write `return \342\200\246`, or `=> \342\200\246` for a body that is one expression (this function returns `";
const uint8_t gm_sbe7f0bkvca75[43] = "a compile-time function value of this type";
const uint8_t gm_sbec11r2rv4ti[23] = "flag_offset = GM_NICHE";
const uint8_t gm_sbkmg1rlq4fa7[27] = "; choose one with `--name`";
const uint8_t gm_sbm4560lv1lt5[25] = "expected `bool`, found `";
const uint8_t gm_sbvdvkd7bikd7[25] = ", (void *){}, {}, NULL{}";
const uint8_t gm_sc0brfbvonphe[64] = "a function pointer performs only the effects in its type, and `";
const uint8_t gm_sc3mvoa74bm5n[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\204\001\000\000E\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sc46jkof3csqo[11] = "-flto=thin";
const uint8_t gm_sc4daeflaltnv[3] = "- ";
const uint8_t gm_sc4i6tq8tg5gu[13] = "gm_fmt_float";
const uint8_t gm_sc4r4l9k6f577[3] = "ev";
const uint8_t gm_sc5ekkhtqttfb[4] = "_ =";
const uint8_t gm_sc7jgo9oqhoja[21] = "memmove({}, {}, {});";
const uint8_t gm_sc80lo6tf645h[31] = "imaginary literals are decimal";
const uint8_t gm_sc9p2e0c29ni7[29] = "__asm__ volatile (\"\" : \"+r\"(";
const uint8_t gm_scak4selic6pl[21] = "invalid template: {}";
const uint8_t gm_scbrbhepkh2rj[25] = "        gm_buf_push(&b, ";
const uint8_t gm_scc6ms0hrfo8j[9] = "complex ";
const uint8_t gm_scf54410fgl4j[39] = "cannot run `curl` (is it installed\077): ";
const uint8_t gm_scfdk5i1nqc6a[30] = "unknown escape sequence `\\{}`";
const uint8_t gm_scifaj4dn1ok5[6] = "title";
const uint8_t gm_scn0a8pnba0ho[41] = "`{}` cannot take a receiver of type `{}`";
const uint8_t gm_scn0pl9eq3k2k[24] = "ratio: division by zero";
const uint8_t gm_scne9acm49p4h[11] = "1 value is";
const uint8_t gm_scommn379heqq[14] = "{}^{{commit}}";
const uint8_t gm_scq0i5n3f7it5[41] = "{} has no branch `{}` (required by `{}`)";
const uint8_t gm_sct928cj9v78e[137] = "` cannot be passed to a variadic parameter: use a C-compatible scalar of at least C `int` width or `f64`, converting explicitly (\302\24719.2)";
const uint8_t gm_sd1gv97l675vb[26] = "typedef union { _Alignas(";
const uint8_t gm_sd4k4nkk0h9t7[14] = "gm_spec_radix";
const uint8_t gm_sd4sd2h1n94bh[7] = ".index";
const uint8_t gm_sd667aplubiqu[34] = "    h = gm_mix_bytes(h, {}, {});\012";
const uint8_t gm_sd8o4m7695hpq[38] = "internal error: not a network service";
const uint8_t gm_sd96cluvcn3lj[33] = "    if (((uint8_t *)&r)[{}]) {{\012";
const uint8_t gm_sd99er7oafmtu[17] = " is out of range";
const uint8_t gm_sd9o9rh5qe8e7[11] = "fs_symlink";
const uint8_t gm_sdarhudj711nk[17] = "this dereference";
const uint8_t gm_sdfl2t7s13ucr[12] = "continue {}";
const uint8_t gm_sdg1ovmjdibb5[6] = ":quit";
const uint8_t gm_sdh3pk0b3jsnu[67] = "`, which might be the same effect for some type arguments (\302\24716.6)";
const uint8_t gm_sdinre9qvo2vq[28] = "cannot convert `{}` to `{}`";
const uint8_t gm_sdogehgrilpeb[3] = "ul";
const uint8_t gm_sdqssko83l66b[28] = "GM_API int gm_checks = {};\012";
const uint8_t gm_sdrevll5io08q[10] = "gm_hblock";
const uint8_t gm_sdvtt0l0jh9bp[10] = "timed out";
const uint8_t gm_se08n5d1gngu7[5] = "` = ";
const uint8_t gm_se1t3fpfm9j7m[12] = "extern {};\012";
const uint8_t gm_se25mtccq2mtg[42] = "({} & (((unsigned __int128)1 << {}) - 1))";
const uint8_t gm_se5tulnsn3o9s[10] = "handlers ";
const uint8_t gm_se8ggmq7h9bin[35] = "` lives until the end of its scope";
const uint8_t gm_sebnh3ba276b8[17] = "` is not (\302\2474.4)";
const uint8_t gm_seeo26ergtcha[5] = "Ceil";
const uint8_t gm_seg7e1ca8e64l[5] = "}`: ";
const uint8_t gm_segch6a5dme9u[5] = "-inf";
const uint8_t gm_segn0pv9sd905[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000(\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_segr993uh2crm[11] = "FsCloseDir";
const uint8_t gm_seh85cpcgte1u[57] = "a shift amount must have an unsigned integer type, not `";
const uint8_t gm_seimuk2ck2a6g[38] = " = gm_fiber_new(inst->fw, (uint8_t *)";
const uint8_t gm_sek5urp41gn7l[48] = "a `with \342\200\246 on` body reading an outer temporary";
const uint8_t gm_seojbs1tqr33k[46] = "radix prefixes must be lowercase; write `0{}`";
const uint8_t gm_sf2lr85hprd3e[40] = "the value of this expression (of type `";
const uint8_t gm_sf46mekb18qm8[14] = "gm_fret:\012    ";
const uint8_t gm_sf9ilivr4c86m[4] = "rev";
const uint8_t gm_sfau4tfjh7ajf[15] = "gm_with_clause";
const uint8_t gm_sfc071avthns7[21] = "`{}` is deprecated{}";
const uint8_t gm_sfcm41q17pr37[46] = "error: the input ends where more is expected\012";
const uint8_t gm_sffd375a90o1u[3] = "-f";
const uint8_t gm_sffd7ppku35fr[10] = "--missing";
const uint8_t gm_sfkqvfbp56vqq[11] = "gm_env_cmp";
const uint8_t gm_sfl0m0vifcu3m[82] = "the template numbers its values, so this spread must be a tuple of them, not `{}`";
const uint8_t gm_sfvsgjcg86bbl[45] = "a function type cannot have a rest parameter";
const uint8_t gm_sg0ns322676oc[26] = "compiler/load/manifest.gm";
const uint8_t gm_sg39dr0kaebm0[10] = "branch={}";
const uint8_t gm_sg3dnv3iaqhkk[8] = " pieces";
const uint8_t gm_sg4faca6q9u0h[8] = "gm_wide";
const uint8_t gm_sg6b42o7q3v50[64] = "the result of this constant expression is too large (more than ";
const uint8_t gm_sg91clgj6l88m[6] = "curl:";
const uint8_t gm_sg92g0je3abps[14] = "` is an array";
const uint8_t gm_sg93rj30beob9[29] = "a handler is declared for `^";
const uint8_t gm_sgb0oed9c3voo[5] = "make";
const uint8_t gm_sgc0h5so8ul3j[22] = "compiler/interp/vm.gm";
const uint8_t gm_sge7ihldgqi49[20] = "min/max/clamp on {}";
const uint8_t gm_sge9mrih5euc5[13] = "the import `";
const uint8_t gm_sgekdmqbrecpp[9] = "{}({}){}";
const uint8_t gm_sgf1euukkdve3[14] = "many-ptr-type";
const uint8_t gm_sgh9nkjgl73of[27] = "textDocument/prepareRename";
const uint8_t gm_sgiadc3jhhq91[23] = ",\"line\":{},\"offset\":{}";
const uint8_t gm_sgmjl48sjjf69[22] = "((uint8_t *)&{} + {})";
const uint8_t gm_sgmt0q35kl6hc[5] = "ret ";
const uint8_t gm_sgqifqduuc5vl[46] = "report every rule that would warn as an error";
const uint8_t gm_sgqs124m28gfg[7] = "Socket";
const uint8_t gm_sgtmc08t3k49t[42] = "\012\012| Module | Description |\012| --- | --- |\012";
const uint8_t gm_sh0adpq1lnkpj[40] = "`{}({})` is not a lossless conversion{}";
const uint8_t gm_sh0drhj4ffllf[11] = "gm_fmt_int";
const uint8_t gm_sh0is7j09prt2[23] = "to close the subscript";
const uint8_t gm_sh11qqm8nqu68[8] = "CstrLen";
const uint8_t gm_sh2bu6u8oapg5[73] = "`{}` is not a first-class type, so it cannot be a type argument (\302\24716.1)";
const uint8_t gm_sh43rpq1oqm5d[6] = "gm_xa";
const uint8_t gm_sh5cd76h4vacq[102] = "a `pub(package)` function or constant, or a `pub` one of a program package, that no other module uses";
const uint8_t gm_sh5f7b7njpj3l[6] = "apple";
const uint8_t gm_sharnvllqpvms[17] = "Invalid argument";
const uint8_t gm_shb921rk4rkpo[5] = "<td>";
const uint8_t gm_shf0g22rjm3o7[5] = "line";
const uint8_t gm_shfuf0tdpqgc0[320] = "GM_NOINLINE {} {{\012    gm_node *pn = gm_find(cx, {});\012    if (!pn) gm_trap(\"no handler installed for effect `{}`\", file, line, col);\012    if (pn->kind == GM_NATIVE) {{\012        {}(GM_LD(void *, &a), GM_LD(uintptr_t, (uint8_t *)&a + {}));\012    }} else {{\012        ((({})pn->clauses[{}]))(pn->env, pn->hctx, pn, a);\012    }}\012}}\012";
const uint8_t gm_shhn92c601tgo[29] = "((((unsigned __int128)GM_LD(";
const uint8_t gm_shl9ceh0q5tpe[6] = "debug";
const uint8_t gm_shn8mjp9kc2n2[9] = "uint64_t";
const uint8_t gm_shncvaenjonsu[41] = "an alignment must be an integer constant";
const uint8_t gm_shntnl6m6g3am[27] = "compiler/interp/threads.gm";
const uint8_t gm_shpgcu8us1vm6[11] = "uint32_t j";
const uint8_t gm_shq77is85iktk[17] = "soa_copy_columns";
const uint8_t gm_shroj9d6fajb6[4] = "+{}";
const uint8_t gm_shror0skne60i[45] = "`{}` is declared twice in this foreign block";
const uint8_t gm_shs874j97ior1[14] = "the statement";
const uint8_t gm_shsoptr61c2h5[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000T\000\000\000\023\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_shv5ofgc65v68[3] = "[[";
const uint8_t gm_si8jv2q8d22kl[10] = "proc_kill";
const uint8_t gm_sia6bdhdcj5co[13] = "gm_wasi_next";
const uint8_t gm_siabpto2siqmj[7] = "err = ";
const uint8_t gm_sidiiakhbvqqi[9] = "`; not `";
const uint8_t gm_sig8ahbvm8eb8[10] = "{} {} yet";
const uint8_t gm_sigjilgl161c6[5] = "cn{}";
const uint8_t gm_sigq4ahgd0l2a[64] = "this call is not an iterator: its effects include no `Yield(T)`";
const uint8_t gm_siihmb4jo3dsn[125] = "    gm_buf b = {0};\012    uint8_t sb[GM_VIEW_SCRATCH];\012    const void *fd;\012    size_t fn = gm_format_view(&b, sb, d, v, &fd);\012";
const uint8_t gm_siji3oo7ud53t[10] = "void *env";
const uint8_t gm_sijp8lh2g2ld5[34] = "calls to foreign functions (`{}`)";
const uint8_t gm_sipi37glknm5f[16] = "gm_thread_stack";
const uint8_t gm_siuacbf0blrje[9] = "label {}";
const uint8_t gm_siv8n5ojlp0dd[18] = "triggerCharacters";
const uint8_t gm_sj2cfogjb0pbr[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\330\000\000\000\026\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_sj49v74ko3ghf[15] = "net_local_port";
const uint8_t gm_sj56dkk1peu3m[54] = "unknown `cache` subcommand `{}`: use `dir` or `clean`";
const uint8_t gm_sj5f575ekojrn[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\0006\001\000\000\032\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sj5j224m4ulf9[3] = "%=";
const uint8_t gm_sj9kuut55po7f[46] = "expected a continuation `cont(R)`, found `{}`";
const uint8_t gm_sjbjet56e96co[17] = "note: `main` in ";
const uint8_t gm_sjdrosvte680q[12] = "ProcCloseIn";
const uint8_t gm_sjg4ie5b5ufsm[8] = "{}..={}";
const uint8_t gm_sjiao13m1cclm[24] = "the second-class type `";
const uint8_t gm_sjkj5g78lejrj[4] = "int";
const uint8_t gm_sjm54r6h90i7e[63] = "`; the constraints are Eq, Ord, Num, Int, Neg, Float and Plain";
const uint8_t gm_sjssolp3qfpqg[7] = "stored";
const uint8_t gm_sjtp8ip8t28uh[7] = "({}){}";
const uint8_t gm_sjtqic917nd2g[6] = "since";
const uint8_t gm_sk34jm61h5m0d[3] = "-W";
const uint8_t gm_sk3i3et1jqa7c[12] = "gm_with_end";
const uint8_t gm_sk531tvbsm11t[44] = "`is` compares a type only with another type";
const uint8_t gm_sk721elh8ouv2[11] = "FsTruncate";
const uint8_t gm_sk8hn2ar9njah[15] = "_Bool present;";
const uint8_t gm_sk8r2qbdun3l0[35] = "the {} has no parameter named `{}`";
const uint8_t gm_skbd3tdpfkbk5[24] = "invalid format spec `{:";
const uint8_t gm_skedh3k15mgd3[18] = "` this performs; ";
const uint8_t gm_skff6pclobvnu[40] = "a compile-time value of this float type";
const uint8_t gm_skjg6jfphs2d0[10] = "gm_fs_cwd";
const uint8_t gm_skk0hh6be703h[20] = "    uintptr_t na = ";
const uint8_t gm_skm5ng1ir37l8[8] = "Removed";
const uint8_t gm_skmai4uvseje8[24] = "empty character literal";
const uint8_t gm_skmfilbqoc8lo[87] = "`{}` uses local variables of the enclosing function, so it cannot call itself (\302\24711.8)";
const uint8_t gm_skmueji31ds5r[49] = "expected a closing run of exactly {} backtick(s)";
const uint8_t gm_skpueg68n121l[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000U\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_skq0s129a3p6p[41] = "\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\"\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_skr00pujn28ln[29] = "typedef struct {{ {} }} {};\012";
const uint8_t gm_sl2gfi3ik4b1u[11] = "\")\012#undef ";
const uint8_t gm_sl4gq9e3k2i6t[19] = "window/showMessage";
const uint8_t gm_sl58ekc3js5bs[30] = "invalid version requirement `";
const uint8_t gm_sl6br25hfjb5f[9] = "\377\377\377\377\377\377\377\377";
const uint8_t gm_sl84tf4h2e7v7[29] = "(({})code)(env, cx, NULL, a)";
const uint8_t gm_sl93ka76m8q9k[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\003\001\000\0001\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000";
const uint8_t gm_sla3unrbmgc0v[6] = "evn{}";
const uint8_t gm_slb6cj7ndgvdm[6] = "allow";
const uint8_t gm_slcgg5l8i9236[50] = "((((unsigned __int128)GM_LD({}, {})) >> {}) & {})";
const uint8_t gm_sli291jkgotin[31] = " entries, which is not allowed";
const uint8_t gm_sljdfefkkeqgn[90] = "`@copy_elems` takes a slice and a slice of the same element type: `@copy_elems(dst, src)`";
const uint8_t gm_sljqt9ip2cpnb[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\347\000\000\000,\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000";
const uint8_t gm_sllcm1pqagh28[6] = "defer";
const uint8_t gm_sln88h001q3mp[19] = "Input/output error";
const uint8_t gm_slof5tl7m8grj[4] = "c{}";
const uint8_t gm_slolcr6ikvg33[7] = "rawptr";
const uint8_t gm_slpqve1dqssq0[8] = "GM_TLS ";
const uint8_t gm_slr4hrkmmmk3q[7] = "no-arm";
const uint8_t gm_slsa15rinti36[12] = "alloc_frame";
const uint8_t gm_slsbknfpio6sr[9] = "Some({})";
const uint8_t gm_sm0105pgqu27v[17] = "\000\000\000\000\000\000\000\000V\000\000\000\000\000\000\000";
const uint8_t gm_sm57fhlj6aanb[62] = "__repl_type :: fn(T: type, v: T) -> string => @type_name(T)\012\012";
const uint8_t gm_sm5t84rdabed6[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000>\001\000\000$\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sm78jth420g98[9] = "\240\206\001\000\000\000\000\000";
const uint8_t gm_smcv6j7b18cjr[17] = "gamma: <stdin>: ";
const uint8_t gm_smgvt83efirng[53] = "if ({} >= {}) gm_trap_shift((uint64_t){}, \"{}\", {});";
const uint8_t gm_smh4q1ndijmcf[39] = "nothing after it in its block names it";
const uint8_t gm_smiavg3q7ucd9[8] = "gm_hval";
const uint8_t gm_smop6ueqrddak[77] = "memset(&{}, 0, sizeof {}); memcpy(&{}, {}, sizeof {} < {} \077 sizeof {} : {});";
const uint8_t gm_smpb7l3te3i8n[52] = "`@stack_size` needs the name of a function (\302\24714.8)";
const uint8_t gm_smr5ddngcjuao[19] = "if (gm_unw.kind) {";
const uint8_t gm_smrla0cd2esrp[14] = "write `{}.{}`";
const uint8_t gm_sn1m0t4k3vpa5[5] = " fix";
const uint8_t gm_sn2qlgie5sklu[45] = "if (gm_unw.frame == &F.h && gm_unw.label == ";
const uint8_t gm_sn4e8vibva5m7[45] = "`break` and `continue` must be inside a loop";
const uint8_t gm_sn5pv7k63ni4c[11] = "` is empty";
const uint8_t gm_sn5rcg6kpf9gq[36] = "`@{}` takes {} positional arguments";
const uint8_t gm_sn9fg2capllh2[79] = "static bool {}(void *code, void *env, gm_node *cx, const void *d, uintptr_t n)";
const uint8_t gm_snauhge6bheis[14] = "--speed-limit";
const uint8_t gm_snbkfu70dq3fp[33] = "Resource temporarily unavailable";
const uint8_t gm_snggon8entbp9[32] = "    return !a[{}] || {}(a, b);\012";
const uint8_t gm_sngn7trhgdnc2[43] = "`{}` is indexed by `{}` values, not `{}`{}";
const uint8_t gm_snh2jpqenu534[17] = "memmove(&F.ret, ";
const uint8_t gm_snhr0nlm0epj5[60] = "`{}` is permitted only in the body of a `ctl` clause (\302\24714)";
const uint8_t gm_snja7imrvdfb7[11] = "\\u{{{:x}}}";
const uint8_t gm_snl0g0eaf7ift[8] = "sparc64";
const uint8_t gm_snl2gc9fh9n9n[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\304\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_snldug1bl0l7h[6] = ")) : ";
const uint8_t gm_sno069vrt5g9q[4] = " } ";
const uint8_t gm_snq4uaiieftl8[6] = "BitOr";
const uint8_t gm_sns2lv549pkqt[53] = "--format=%(refname)%09%(objectname)%09%(*objectname)";
const uint8_t gm_snsu3jrv1dbtj[32] = "` is the name of a builtin type";
const uint8_t gm_snt446oeijaq5[13] = "is_sorted_by";
const uint8_t gm_snurnbocs9786[49] = "static __attribute__((aligned(4))) {}{}{} {}({})";
const uint8_t gm_so4bvja70211d[63] = "`in` compares with `is`, which is not defined for `{}` (\302\2477.6)";
const uint8_t gm_so6kuvn2nvuue[4] = "lex";
const uint8_t gm_so77sfkiulkfr[11] = "    if (a[";
const uint8_t gm_soakgb2h3g40i[4] = "'''";
const uint8_t gm_socribpa518de[13] = "gm_net_close";
const uint8_t gm_sod98slk3vnra[20] = "compiler/tir/tir.gm";
const uint8_t gm_sodpgd6927k7l[33] = "Error: failed to run main module";
const uint8_t gm_sof77pc3u6m60[25] = " __attribute__((aligned(";
const uint8_t gm_sog4l3h5q9oa6[84] = "the left side of an assignment must be a place (a variable, `p^`, field or element)";
const uint8_t gm_soj8art8cj0h6[25] = "*(volatile {} *){} = {};";
const uint8_t gm_solbcai20h8gs[7] = "table ";
const uint8_t gm_soohe0i8iafj3[20] = "unknown option `{}`";
const uint8_t gm_soq2d9k5a34bc[8] = "gm_segv";
const uint8_t gm_sorvett7uvsl2[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\233\003\000\000\025\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sotb03b2eqtp5[20] = "collapsible-else-if";
const uint8_t gm_soulf4opjqcvv[10] = "tag of {}";
const uint8_t gm_sovcjofctijvj[47] = "lane {} does not exist: `{}` has lanes 0 to {}";
const uint8_t gm_sovg8lhrcqaa0[34] = "`--jobs` needs a number, not `{}`";
const uint8_t gm_sovtt6julbcd8[12] = "`--profile=";
const uint8_t gm_sp1cnbsshsv9k[18] = "` has no method `";
const uint8_t gm_sp39731r4i1pk[10] = "rev-parse";
const uint8_t gm_sp3k6emq42nh4[10] = "` (\302\2477.6)";
const uint8_t gm_sp3rdossibc7f[4] = "<<\077";
const uint8_t gm_sp8o48ld4n4jq[24] = "compiler/parse/types.gm";
const uint8_t gm_sp929adkqo1cd[93] = "effect parameters are types or effect sets: `effect(T: type)`, `effect(E: effects)` (\302\24712.1)";
const uint8_t gm_spak5smc8j24b[14] = "{}(*({} *){})";
const uint8_t gm_spbqvmg1179vc[24] = "cannot {} `{}` and `{}`";
const uint8_t gm_spe8t3e79vnal[7] = "export";
const uint8_t gm_spevct0q3cjgb[5] = "fill";
const uint8_t gm_spf3a0kmdpgm5[6] = "{}#{}";
const uint8_t gm_sphmte453j9c2[12] = " = handler ";
const uint8_t gm_spmlh93537vbl[26] = "compiler/parse/pattern.gm";
const uint8_t gm_spmv5l6cklh5d[18] = "the template has ";
const uint8_t gm_spn4jhv77b56t[23] = "\012pub endian :: Endian.";
const uint8_t gm_sppohbjp90mol[3] = "-C";
const uint8_t gm_sprospqi1g16o[7] = "op_neg";
const uint8_t gm_spun41oorb00o[30] = "\012    print(\"{}\\n\", __repl_v)\012";
const uint8_t gm_spv9savsu9h0b[4] = "fn ";
const uint8_t gm_spvdlp3fsj44k[2] = "\001";
const uint8_t gm_sq02q1bjdgrej[123] = ". The server restarted; please report this, with the file that caused it, at https://github.com/chroma04/gamma-lang/issues";
const uint8_t gm_sq4o785gt8bsh[10] = "--package";
const uint8_t gm_sq5khm5l62ens[37] = "call through a null function pointer";
const uint8_t gm_sq94kuvk30n36[6] = "gm_ga";
const uint8_t gm_sqahda31evi4f[9] = " (int)((";
const uint8_t gm_sqas1r3k72n65[51] = "gm_fret:\012    {}return;\012gm_fprop:\012    {}return;\012}}\012";
const uint8_t gm_sqci13ris3rb0[16] = "prepare_codegen";
const uint8_t gm_sqcpnesgf4t4h[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\306\000\000\000\036\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000";
const uint8_t gm_sqcvjnsub5bfs[53] = "invalid template (in an instantiation used here): {}";
const uint8_t gm_sqhj00be09q5v[9] = " #packed";
const uint8_t gm_sqirjnd2segia[40] = "array elements are written positionally";
const uint8_t gm_sqldj8dr0j7rf[12] = "#line {} {}";
const uint8_t gm_sqtc9fffut4s2[13] = "const void *";
const uint8_t gm_sr1dn90ca9rnt[9] = "\000 \000\000\000\000\000\000";
const uint8_t gm_sr2kfei26cj64[18] = "GM_LD(uintptr_t, ";
const uint8_t gm_sr8de1o4do8v1[11] = "atomic_xor";
const uint8_t gm_srbq5qj276c59[30] = "invalid `url` of dependency `";
const uint8_t gm_srcsoo8lineh7[3] = "&=";
const uint8_t gm_srjc4stueafqk[43] = "main :: fn() -> AllocError!() needs {} {{\012";
const uint8_t gm_srjddrdnspo96[29] = "unclosed array, expected `]`";
const uint8_t gm_srk8hgivpmv60[18] = "__atomic_store_n(";
const uint8_t gm_srkue5qoqvtpg[41] = "the name of the test as a string literal";
const uint8_t gm_srol0ktjpbtfv[58] = "cannot be checked against `{}`: unknown architecture `{}`";
const uint8_t gm_srs4ljjaijhd2[24] = " lanes with a slice of ";
const uint8_t gm_srvm34bc3an1a[113] = "\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000";
const uint8_t gm_ss3lpq4se4ir5[5] = "{} *";
const uint8_t gm_ss4e1np28qvnl[11] = " addressed";
const uint8_t gm_ss65eeqhkg7mg[12] = "starts_with";
const uint8_t gm_ss6cscjfm4jfc[11] = "--location";
const uint8_t gm_ss77f3h3rt6l8[30] = ": return GM_RUN_MAIN(gm_entry";
const uint8_t gm_ss7pu45qp7oic[6] = "<iter";
const uint8_t gm_ss7qhfi2s00na[11] = "<for body>";
const uint8_t gm_ss8fufid8t1o1[32] = "array length {} is out of range";
const uint8_t gm_ss8q42tvppqf5[79] = "invalid integer width in suffix `{}`: widths are 1 to 128 with no leading zero";
const uint8_t gm_ssbpo4qatmvlq[7] = ")0 - (";
const uint8_t gm_ssd6k1tnvttos[15] = "intrinsic @{}\012";
const uint8_t gm_sse9uo3ffn4rj[167] = "static GM_TLS uint8_t gm_gi{};\012static void gm_ginit{}(void);\012static inline uint8_t *gm_ga{}(void) {{ if (!gm_gi{}) {{ gm_gi{} = 1; gm_ginit{}(); }} return gm_g{}; }}\012";
const uint8_t gm_ssg9s8mvpv2la[45] = "expected a newline or `;` after {}, found {}";
const uint8_t gm_sshj97ls6n4f3[18] = "unmatched closing";
const uint8_t gm_sskd2e48pqlgu[13] = "gm_fiber_new";
const uint8_t gm_ssmuvuejgvj6b[27] = "` needs a mutable receiver";
const uint8_t gm_ssor6j0m3frue[14] = "(uintptr_t){}";
const uint8_t gm_ssqifgr2icd6s[3] = " *";
const uint8_t gm_ssri9mbl4p6m3[68] = "an `if` or `while` whose condition is the literal `true` or `false`";
const uint8_t gm_st5nnur2j246q[36] = ", line);\012    GM_ST(uint32_t, loc + ";
const uint8_t gm_st8ki5f85lc2j[13] = "StaticAssert";
const uint8_t gm_st9n2vprlv3c1[22] = "`is` on `#soa` slices";
const uint8_t gm_staavgn0someg[56] = "expected a handler, but no handler is declared for `{}`";
const uint8_t gm_stb45mimrpgat[22] = "install handler {} {}";
const uint8_t gm_stdh0kbfgj33k[9] = "` with `";
const uint8_t gm_stidgejoe9ul2[11] = "FsReadLink";
const uint8_t gm_stl5olh2jfr0d[40] = "; an array is not a slice: pass `a[..]`";
const uint8_t gm_stq0db711o573[15] = "</blockquote>\012";
const uint8_t gm_su0agms0tqg5k[15] = "{}call {} ({})";
const uint8_t gm_su13ngtdjura3[15] = "(void *)&F.w{}";
const uint8_t gm_su1oqg46rlg2d[10] = "this copy";
const uint8_t gm_su2eceicte7s7[6] = " gm_l";
const uint8_t gm_su4retifs0sre[9] = "__FILC__";
const uint8_t gm_su5mku12ctc3k[92] = "this compile-time evaluation calls `{}`, which depends on it; the cycle cannot be evaluated";
const uint8_t gm_su7ofilkdis5t[12] = "{} = F.w{};";
const uint8_t gm_su823o267lacu[103] = ") is no longer available in the repository (a tag moved or history was rewritten\077); run `gamma update ";
const uint8_t gm_su8dibgnmpcpa[42] = "effects without a handler at compile time";
const uint8_t gm_su9t2v4p35eij[8] = "gm_trap";
const uint8_t gm_su9tevpu8h5ad[19] = "stable_sort_by_key";
const uint8_t gm_suaek0bfou9a6[14] = "GM_ROUNDEVEN(";
const uint8_t gm_suctpm7bivu2g[48] = "`@bswap` needs an integer of whole bytes, not `";
const uint8_t gm_sue26fe85rntf[5] = "note";
const uint8_t gm_sufil5dud65av[62] = "if (!{}) gm_trap(\"no handler installed for effect `{}`\", {});";
const uint8_t gm_suj5r301h9vv4[9] = "indexing";
const uint8_t gm_suj9anpi7p1mq[25] = "compiler/cgen/program.gm";
const uint8_t gm_sujdc8gmukk24[16] = "gm_note_failure";
const uint8_t gm_sujipgkhb2fih[8] = "`, but ";
const uint8_t gm_sulfrs2bmdlu2[6] = "Child";
const uint8_t gm_suno9ilftpfht[12] = "net_connect";
const uint8_t gm_suor7sr2pkoei[12] = "__typeof__(";
const uint8_t gm_sus38q16nfojv[7] = "soa {}";
const uint8_t gm_sut5kfu0n4de9[5] = " {} ";
const uint8_t gm_sv10m8g115ng3[59] = "` is a variable, so its value is not known at compile time";
const uint8_t gm_sv1iq1n2a39tg[17] = "\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000";
const uint8_t gm_sv2hkl8uspfr0[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\0001\001\000\000\026\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_sv3d9namfmaua[9] = "{} *ptr;";
const uint8_t gm_sv61f25m0b9gs[4] = "lib";
const uint8_t gm_sv89ta651gq7u[20] = "#define F (*gm_Fp)\012";
const uint8_t gm_svaet5ugb1fj3[22] = "0x7fffffffffffffffull";
const uint8_t gm_svdi2pk0uienp[45] = "a tuple pattern needs a tuple, but this is `";
const uint8_t gm_svegu6hi5aibp[40] = "`---` requires a plain-data type, but `";
const uint8_t gm_svghvlr5om8a6[29] = "memcpy(&{}, a{}, sizeof {});";
const uint8_t gm_svl5ddjf64ul7[18] = "(); } return gm_g";
const uint8_t gm_svp314e9ficjs[14] = " + 0; memcpy(";
const uint8_t gm_svqq3k9j6h1lu[8] = "LaneMax";
const uint8_t gm_svsegvhrrmf7h[18] = "cannot use {}: {}";
const uint8_t gm_svthrqc59hf0v[21] = "function returning `";
uint8_t gm_c08nj74isec[14] __attribute__((aligned(1)));
uint8_t gm_c0s28vl4pd8[17] __attribute__((aligned(1)));
uint8_t gm_c1a8m7m72ud[19] __attribute__((aligned(1)));
uint8_t gm_c1m49utf476[4] __attribute__((aligned(1)));
uint8_t gm_c1nbl7q1do7[23] __attribute__((aligned(1)));
uint8_t gm_c23qivcb2qk[16] __attribute__((aligned(8)));
uint8_t gm_c2hp1hl5ap9[6] __attribute__((aligned(1)));
uint8_t gm_c2pgnmr38k4[4] __attribute__((aligned(1)));
uint8_t gm_c3seud9n9be[1] __attribute__((aligned(1)));
uint8_t gm_c4i68jc4bv0[16] __attribute__((aligned(8)));
uint8_t gm_c5dlid9hf1q[12] __attribute__((aligned(1)));
uint8_t gm_c5llbmao9o5[3] __attribute__((aligned(1)));
uint8_t gm_c62sev3r509[1] __attribute__((aligned(1)));
uint8_t gm_c7167246mac[5] __attribute__((aligned(1)));
uint8_t gm_c72j1g8d455[1] __attribute__((aligned(1)));
uint8_t gm_c73ube8thpg[12] __attribute__((aligned(1)));
uint8_t gm_c78rm6v1bc8[2] __attribute__((aligned(1)));
uint8_t gm_c7bha71empo[20] __attribute__((aligned(1)));
uint8_t gm_c7k2o7e3uv7[3] __attribute__((aligned(1)));
uint8_t gm_c8lries3c2a[1442] __attribute__((aligned(1)));
uint8_t gm_c8qi7rt473p[96] __attribute__((aligned(1)));
uint8_t gm_c8tck9qi6pl[16] __attribute__((aligned(8)));
uint8_t gm_c90nlp6169j[1] __attribute__((aligned(1)));
uint8_t gm_c929husmmes[11] __attribute__((aligned(1)));
uint8_t gm_c9smt58mcql[5] __attribute__((aligned(1)));
uint8_t gm_cbkljfd0bjn[13] __attribute__((aligned(1)));
uint8_t gm_cbmpgied9p9[14] __attribute__((aligned(1)));
uint8_t gm_ccb2i0agl3a[10] __attribute__((aligned(1)));
uint8_t gm_cdgbrlsjvrb[16] __attribute__((aligned(8)));
uint8_t gm_cesm14gq4bu[7] __attribute__((aligned(1)));
uint8_t gm_cfhcj0skuhf[18] __attribute__((aligned(1)));
uint8_t gm_cfj2cvp0vvo[14] __attribute__((aligned(1)));
uint8_t gm_cg2gm9ndqso[4] __attribute__((aligned(1)));
uint8_t gm_cga37fdnf9v[11] __attribute__((aligned(1)));
uint8_t gm_ch037afqg9e[8] __attribute__((aligned(1)));
uint8_t gm_cio5gu1gbev[13] __attribute__((aligned(1)));
uint8_t gm_cj12hhe1r3t[6] __attribute__((aligned(1)));
uint8_t gm_cjvio2kihdk[3] __attribute__((aligned(1)));
uint8_t gm_ck98kig915p[7] __attribute__((aligned(1)));
uint8_t gm_cmkk46n1tqe[14] __attribute__((aligned(1)));
uint8_t gm_cms9a7p0moe[21] __attribute__((aligned(1)));
uint8_t gm_cmvun0d9m89[5] __attribute__((aligned(1)));
uint8_t gm_cn9tmpai0bm[9] __attribute__((aligned(1)));
uint8_t gm_cp9ta1t7ncn[10] __attribute__((aligned(1)));
uint8_t gm_crqb3tjd618[16] __attribute__((aligned(8)));
uint8_t gm_csb8vojob99[7] __attribute__((aligned(1)));
uint8_t gm_csvl88khklf[12] __attribute__((aligned(1)));
uint8_t gm_cta94eldhtr[24] __attribute__((aligned(1)));
uint8_t gm_cui00p62je5[7] __attribute__((aligned(1)));
uint8_t gm_cuk7s23am23[4] __attribute__((aligned(1)));
uint8_t gm_k0cjq15hqfg[40] __attribute__((aligned(8)));
uint8_t gm_k0gn7sac5pt[40] __attribute__((aligned(8)));
uint8_t gm_k12tcbqf2vk[40] __attribute__((aligned(8)));
uint8_t gm_k3ho3jie8mm[40] __attribute__((aligned(8)));
uint8_t gm_k3iblj1j3pp[40] __attribute__((aligned(8)));
uint8_t gm_k3pvi109nh9[40] __attribute__((aligned(8)));
uint8_t gm_k4hgfv8dkha[40] __attribute__((aligned(8)));
uint8_t gm_k6rrrg8di6i[40] __attribute__((aligned(8)));
uint8_t gm_k7peov41e38[40] __attribute__((aligned(8)));
uint8_t gm_k8fjm80oou5[40] __attribute__((aligned(8)));
uint8_t gm_ka7mhmnnlp9[40] __attribute__((aligned(8)));
uint8_t gm_kcdn8q7rsvc[40] __attribute__((aligned(8)));
uint8_t gm_kcu3lrib6j4[40] __attribute__((aligned(8)));
uint8_t gm_kdmasfkh2un[40] __attribute__((aligned(8)));
uint8_t gm_kdoa98a1tm0[40] __attribute__((aligned(8)));
uint8_t gm_kf24ji180ne[40] __attribute__((aligned(8)));
uint8_t gm_kft026ame64[40] __attribute__((aligned(8)));
uint8_t gm_kihs3be4rv6[40] __attribute__((aligned(8)));
uint8_t gm_kkpcsaqkj4k[40] __attribute__((aligned(8)));
uint8_t gm_kl5b02juaoa[40] __attribute__((aligned(8)));
uint8_t gm_knvhkf6cslq[40] __attribute__((aligned(8)));
uint8_t gm_kp550dhuma7[40] __attribute__((aligned(8)));
uint8_t gm_kr85ffi32os[24] __attribute__((aligned(8)));
uint8_t gm_krr4pbvd2tj[40] __attribute__((aligned(8)));
uint8_t gm_ksdcjqjfqsj[40] __attribute__((aligned(8)));
uint8_t gm_ksrlu8s6v53[40] __attribute__((aligned(8)));
uint8_t gm_ktbnd3fuoao[40] __attribute__((aligned(8)));
void *const gm_hl2p1t2nmcts[] = {(void *)gm_f_for_body_iqmk7ug};
void *const gm_hlcpgr8cgvbp[] = {(void *)gm_f_args_6h3q549, (void *)gm_f_var_otupsib, (void *)gm_f_exit_jfn9nl4};
void *const gm_hlfkkokngdk1[] = {(void *)gm_f_for_body_l1ll8kh};
void *const gm_hlinnrcqopuj[] = {(void *)gm_f_for_body_kocuepv};
void *const gm_hln6al4vijjv[] = {(void *)gm_f_for_body_8edr2np};
void *const gm_hlnhfkgd9bs8[] = {(void *)gm_f_for_body_g88hj6k};
