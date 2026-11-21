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
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_csfq64m1m1rt6m { int64_t f0; } gm_csfq64m1m1rt6m;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint32_t a[1]; } gm_tdvq40fmif5j0;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { double f0; uint8_t f1; uint8_t pe[7]; } gm_th2bqc58ss467;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t a[4096]; } gm_tpbv7nqggnvqr;
typedef struct { uint8_t a[4]; } gm_tpvg3mjn3dp06;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; int32_t v1; } f1; } gm_tsoiri44d0i6q;
typedef struct { uint8_t f1; } gm_ttsm01t0d8srg;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 a[9]; } gm_t142fel6vb03l;
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_b16_8 f7; } gm_t2t9klu4dfekp;
typedef struct { gm_b16_8 f0; uint64_t f1; int64_t f2; } gm_t2u4qamcbbgu0;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_b16_8 v1; } f1; } gm_t5uv522f0p612;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
_Static_assert(sizeof(gm_tdvq40fmif5j0) == 4 && _Alignof(gm_tdvq40fmif5j0) == 4 && offsetof(gm_tdvq40fmif5j0, a) == 0, "layout of `[1]u32`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; uint64_t f1; } gm_te0mgnjdphs16;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[5]; gm_b16_8 f3; } gm_tftmsj5vtqvej;
_Static_assert(sizeof(gm_th2bqc58ss467) == 16 && _Alignof(gm_th2bqc58ss467) == 8 && offsetof(gm_th2bqc58ss467, f0) == 0 && offsetof(gm_th2bqc58ss467, f1) == 8, "layout of `?f64`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
typedef struct { gm_b16_8 a[5]; } gm_tjgkb0mi112kg;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; gm_b16_8 f10; } gm_tk5njge13tk1d;
typedef struct { gm_t9805qtonbsag f0; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_t9805qtonbsag f3; } gm_tk8ebsa8coi4p;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_tsoiri44d0i6q v1; } f1; } gm_tlmjd3nndlf3r;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_tpbv7nqggnvqr) == 4096 && _Alignof(gm_tpbv7nqggnvqr) == 1 && offsetof(gm_tpbv7nqggnvqr, a) == 0, "layout of `[4096]u8`");
_Static_assert(sizeof(gm_tpvg3mjn3dp06) == 4 && _Alignof(gm_tpvg3mjn3dp06) == 1 && offsetof(gm_tpvg3mjn3dp06, a) == 0, "layout of `[4]u8`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint8_t f3; uint8_t pe[7]; } gm_ts1lka1v5g5ua;
_Static_assert(sizeof(gm_tsoiri44d0i6q) == 8 && _Alignof(gm_tsoiri44d0i6q) == 4 && offsetof(gm_tsoiri44d0i6q, f0) == 0 && offsetof(gm_tsoiri44d0i6q, f1) == 4, "layout of `ExitStatus`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_ttsm01t0d8srg) == 1 && _Alignof(gm_ttsm01t0d8srg) == 1 && offsetof(gm_ttsm01t0d8srg, f1) == 0, "layout of `?()`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
typedef struct { gm_b16_8 f0; gm_tk5njge13tk1d f1; gm_b16_8 f2; uint8_t f3; uint8_t f4; uint8_t pe[6]; } gm_t0u6dopi4f21f;
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t142fel6vb03l) == 144 && _Alignof(gm_t142fel6vb03l) == 8 && offsetof(gm_t142fel6vb03l, a) == 0, "layout of `[9]string`");
_Static_assert(sizeof(gm_t2t9klu4dfekp) == 80 && _Alignof(gm_t2t9klu4dfekp) == 8 && offsetof(gm_t2t9klu4dfekp, f0) == 0 && offsetof(gm_t2t9klu4dfekp, f1) == 8 && offsetof(gm_t2t9klu4dfekp, f2) == 16 && offsetof(gm_t2t9klu4dfekp, f3) == 24 && offsetof(gm_t2t9klu4dfekp, f4) == 40 && offsetof(gm_t2t9klu4dfekp, f5) == 56 && offsetof(gm_t2t9klu4dfekp, f6) == 57 && offsetof(gm_t2t9klu4dfekp, f7) == 64, "layout of `(u64, bool, u64, string, string, bool, bool, string)`");
_Static_assert(sizeof(gm_t2u4qamcbbgu0) == 32 && _Alignof(gm_t2u4qamcbbgu0) == 8 && offsetof(gm_t2u4qamcbbgu0, f0) == 0 && offsetof(gm_t2u4qamcbbgu0, f1) == 16 && offsetof(gm_t2u4qamcbbgu0, f2) == 24, "layout of `(string, u64, i64)`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; uint8_t p3[5]; gm_th2lqbo77pvsc f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_th2lqbo77pvsc f7; } gm_t32dckm4jfarl;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_trqfvmjtt4sij v1; } f1; } gm_t470aqkia5212;
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t5uv522f0p612) == 24 && _Alignof(gm_t5uv522f0p612) == 8 && offsetof(gm_t5uv522f0p612, f0) == 0 && offsetof(gm_t5uv522f0p612, f1) == 8, "layout of `IoError!string`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
typedef struct { void * f0; gm_trqfvmjtt4sij f1; gm_tk8ebsa8coi4p f2; gm_t0rfe1ou14h21 f3; } gm_ta4jakbus8leg;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
_Static_assert(sizeof(gm_te0mgnjdphs16) == 16 && _Alignof(gm_te0mgnjdphs16) == 8 && offsetof(gm_te0mgnjdphs16, f0) == 0 && offsetof(gm_te0mgnjdphs16, f1) == 8, "layout of `Chunk`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_trqfvmjtt4sij f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_teg5ofd5qf10s;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftmsj5vtqvej) == 24 && _Alignof(gm_tftmsj5vtqvej) == 8 && offsetof(gm_tftmsj5vtqvej, f0) == 0 && offsetof(gm_tftmsj5vtqvej, f1) == 1 && offsetof(gm_tftmsj5vtqvej, f2) == 2 && offsetof(gm_tftmsj5vtqvej, f3) == 8, "layout of `Options`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_te0mgnjdphs16 f0; uint8_t f1; uint8_t pe[7]; } gm_thdrvp9q0u7b5;
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
_Static_assert(sizeof(gm_tjgkb0mi112kg) == 80 && _Alignof(gm_tjgkb0mi112kg) == 8 && offsetof(gm_tjgkb0mi112kg, a) == 0, "layout of `[5]string`");
typedef struct { gm_tsoiri44d0i6q f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; uint8_t f3; uint8_t pe[7]; } gm_tjm4rm50v5mle;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tk2lc304hfpci;
_Static_assert(sizeof(gm_tk5njge13tk1d) == 176 && _Alignof(gm_tk5njge13tk1d) == 8 && offsetof(gm_tk5njge13tk1d, f0) == 0 && offsetof(gm_tk5njge13tk1d, f1) == 16 && offsetof(gm_tk5njge13tk1d, f2) == 32 && offsetof(gm_tk5njge13tk1d, f3) == 48 && offsetof(gm_tk5njge13tk1d, f4) == 64 && offsetof(gm_tk5njge13tk1d, f5) == 80 && offsetof(gm_tk5njge13tk1d, f6) == 96 && offsetof(gm_tk5njge13tk1d, f7) == 112 && offsetof(gm_tk5njge13tk1d, f8) == 128 && offsetof(gm_tk5njge13tk1d, f9) == 144 && offsetof(gm_tk5njge13tk1d, f10) == 160, "layout of `Parts`");
_Static_assert(sizeof(gm_tk8ebsa8coi4p) == 32 && _Alignof(gm_tk8ebsa8coi4p) == 4 && offsetof(gm_tk8ebsa8coi4p, f0) == 0 && offsetof(gm_tk8ebsa8coi4p, f1) == 8 && offsetof(gm_tk8ebsa8coi4p, f2) == 16 && offsetof(gm_tk8ebsa8coi4p, f3) == 24, "layout of `Ambient`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tlmjd3nndlf3r) == 12 && _Alignof(gm_tlmjd3nndlf3r) == 4 && offsetof(gm_tlmjd3nndlf3r, f0) == 0 && offsetof(gm_tlmjd3nndlf3r, f1) == 4, "layout of `IoError!ExitStatus`");
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t p3[5]; gm_trqfvmjtt4sij f3; } gm_tm4vhr17lpr3p;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts1lka1v5g5ua) == 56 && _Alignof(gm_ts1lka1v5g5ua) == 8 && offsetof(gm_ts1lka1v5g5ua, f0) == 0 && offsetof(gm_ts1lka1v5g5ua, f1) == 16 && offsetof(gm_ts1lka1v5g5ua, f2) == 32 && offsetof(gm_ts1lka1v5g5ua, f3) == 48, "layout of `Toolchain`");
typedef struct { gm_ts1lka1v5g5ua f0; uint8_t f1; uint8_t pe[7]; } gm_ttfsv7cb524b2;
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; uint64_t v1; } f1; } gm_tutnubt6u4hoq;
_Static_assert(sizeof(gm_t0u6dopi4f21f) == 216 && _Alignof(gm_t0u6dopi4f21f) == 8 && offsetof(gm_t0u6dopi4f21f, f0) == 0 && offsetof(gm_t0u6dopi4f21f, f1) == 16 && offsetof(gm_t0u6dopi4f21f, f2) == 192 && offsetof(gm_t0u6dopi4f21f, f3) == 208 && offsetof(gm_t0u6dopi4f21f, f4) == 209, "layout of `Generated`");
_Static_assert(sizeof(gm_t32dckm4jfarl) == 80 && _Alignof(gm_t32dckm4jfarl) == 8 && offsetof(gm_t32dckm4jfarl, f0) == 0 && offsetof(gm_t32dckm4jfarl, f1) == 1 && offsetof(gm_t32dckm4jfarl, f2) == 2 && offsetof(gm_t32dckm4jfarl, f3) == 8 && offsetof(gm_t32dckm4jfarl, f4) == 32 && offsetof(gm_t32dckm4jfarl, f5) == 48 && offsetof(gm_t32dckm4jfarl, f6) == 49 && offsetof(gm_t32dckm4jfarl, f7) == 56, "layout of `SpawnOptions`");
_Static_assert(sizeof(gm_t470aqkia5212) == 48 && _Alignof(gm_t470aqkia5212) == 8 && offsetof(gm_t470aqkia5212, f0) == 0 && offsetof(gm_t470aqkia5212, f1) == 8, "layout of `string!List(string)`");
_Static_assert(sizeof(gm_ta4jakbus8leg) == 144 && _Alignof(gm_ta4jakbus8leg) == 8 && offsetof(gm_ta4jakbus8leg, f0) == 0 && offsetof(gm_ta4jakbus8leg, f1) == 8 && offsetof(gm_ta4jakbus8leg, f2) == 48 && offsetof(gm_ta4jakbus8leg, f3) == 80, "layout of `Prog`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_tjm4rm50v5mle v1; } f1; } gm_tat31u3lvqjk4;
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_tr8dvlu9ik4gj f0; uint8_t f1; uint8_t pe[7]; } gm_tctputthhrk9k;
_Static_assert(sizeof(gm_teg5ofd5qf10s) == 72 && _Alignof(gm_teg5ofd5qf10s) == 8 && offsetof(gm_teg5ofd5qf10s, f0) == 0 && offsetof(gm_teg5ofd5qf10s, f1) == 16 && offsetof(gm_teg5ofd5qf10s, f2) == 24 && offsetof(gm_teg5ofd5qf10s, f3) == 64, "layout of `CompilePool`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t0u6dopi4f21f v1; } f1; } gm_tetdkgoreqmn1;
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_thdrvp9q0u7b5) == 24 && _Alignof(gm_thdrvp9q0u7b5) == 8 && offsetof(gm_thdrvp9q0u7b5, f0) == 0 && offsetof(gm_thdrvp9q0u7b5, f1) == 16, "layout of `?Chunk`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tjm4rm50v5mle) == 96 && _Alignof(gm_tjm4rm50v5mle) == 8 && offsetof(gm_tjm4rm50v5mle, f0) == 0 && offsetof(gm_tjm4rm50v5mle, f1) == 8 && offsetof(gm_tjm4rm50v5mle, f2) == 48 && offsetof(gm_tjm4rm50v5mle, f3) == 88, "layout of `Output`");
_Static_assert(sizeof(gm_tk2lc304hfpci) == 112 && _Alignof(gm_tk2lc304hfpci) == 8 && offsetof(gm_tk2lc304hfpci, f0) == 0 && offsetof(gm_tk2lc304hfpci, f1) == 40 && offsetof(gm_tk2lc304hfpci, f2) == 80 && offsetof(gm_tk2lc304hfpci, f3) == 96, "layout of `Link`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_thdrvp9q0u7b5 v1; } f1; } gm_tlbmih0s72in3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tk2lc304hfpci v1; } f1; } gm_tllkbcpqeka9b;
_Static_assert(sizeof(gm_tm4vhr17lpr3p) == 48 && _Alignof(gm_tm4vhr17lpr3p) == 8 && offsetof(gm_tm4vhr17lpr3p, f0) == 0 && offsetof(gm_tm4vhr17lpr3p, f1) == 1 && offsetof(gm_tm4vhr17lpr3p, f2) == 2 && offsetof(gm_tm4vhr17lpr3p, f3) == 8, "layout of `Options`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
typedef struct { gm_t0u6dopi4f21f f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tt1457on428g4;
_Static_assert(sizeof(gm_ttfsv7cb524b2) == 64 && _Alignof(gm_ttfsv7cb524b2) == 8 && offsetof(gm_ttfsv7cb524b2, f0) == 0 && offsetof(gm_ttfsv7cb524b2, f1) == 56, "layout of `?Toolchain`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tutnubt6u4hoq) == 24 && _Alignof(gm_tutnubt6u4hoq) == 8 && offsetof(gm_tutnubt6u4hoq, f0) == 0 && offsetof(gm_tutnubt6u4hoq, f1) == 8, "layout of `ParseError!usize`");
typedef struct { gm_tt1457on428g4 f0; uint8_t f1; uint8_t pe[7]; } gm_t5i6e9e007icp;
_Static_assert(sizeof(gm_tat31u3lvqjk4) == 104 && _Alignof(gm_tat31u3lvqjk4) == 8 && offsetof(gm_tat31u3lvqjk4, f0) == 0 && offsetof(gm_tat31u3lvqjk4, f1) == 8, "layout of `RunError!Output`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; gm_t9qkh0rsooc8d f4; uint8_t p5[3]; gm_th2lqbo77pvsc f5; uint8_t f6; uint8_t f7; uint8_t f8; uint8_t p9[5]; gm_b16_8 f9; uint8_t f10; uint8_t p11[7]; gm_tf5srh4mp4b3i f11; uint8_t f12; uint8_t p13[7]; gm_th2lqbo77pvsc f13; uint8_t f14; uint8_t f15; uint8_t p16[6]; uint64_t f16; gm_th2bqc58ss467 f17; uint8_t f18; uint8_t f19; uint8_t p20[6]; gm_th2lqbo77pvsc f20; gm_th2lqbo77pvsc f21; uint8_t f22; uint8_t f23; uint8_t p24[6]; gm_th2lqbo77pvsc f24; uint8_t f25; uint8_t p26[7]; gm_b16_8 f26; gm_b16_8 f27; gm_tctputthhrk9k f28; gm_th2lqbo77pvsc f29; gm_t0rfe1ou14h21 f30; gm_ttfsv7cb524b2 f31; gm_th2lqbo77pvsc f32; gm_tm4vhr17lpr3p f33; } gm_tcn5ttqhv05g6;
_Static_assert(sizeof(gm_tctputthhrk9k) == 128 && _Alignof(gm_tctputthhrk9k) == 8 && offsetof(gm_tctputthhrk9k, f0) == 0 && offsetof(gm_tctputthhrk9k, f1) == 120, "layout of `?Extras`");
_Static_assert(sizeof(gm_tetdkgoreqmn1) == 224 && _Alignof(gm_tetdkgoreqmn1) == 8 && offsetof(gm_tetdkgoreqmn1, f0) == 0 && offsetof(gm_tetdkgoreqmn1, f1) == 8, "layout of `Unsup!Generated`");
_Static_assert(sizeof(gm_tlbmih0s72in3) == 32 && _Alignof(gm_tlbmih0s72in3) == 8 && offsetof(gm_tlbmih0s72in3, f0) == 0 && offsetof(gm_tlbmih0s72in3, f1) == 8, "layout of `IoError!?Chunk`");
_Static_assert(sizeof(gm_tllkbcpqeka9b) == 120 && _Alignof(gm_tllkbcpqeka9b) == 8 && offsetof(gm_tllkbcpqeka9b, f0) == 0 && offsetof(gm_tllkbcpqeka9b, f1) == 8, "layout of `string!Link`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_tt1457on428g4) == 248 && _Alignof(gm_tt1457on428g4) == 8 && offsetof(gm_tt1457on428g4, f0) == 0 && offsetof(gm_tt1457on428g4, f1) == 216 && offsetof(gm_tt1457on428g4, f2) == 232, "layout of `Built`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t5i6e9e007icp) == 256 && _Alignof(gm_t5i6e9e007icp) == 8 && offsetof(gm_t5i6e9e007icp, f0) == 0 && offsetof(gm_t5i6e9e007icp, f1) == 248, "layout of `?Built`");
_Static_assert(sizeof(gm_tcn5ttqhv05g6) == 624 && _Alignof(gm_tcn5ttqhv05g6) == 8 && offsetof(gm_tcn5ttqhv05g6, f0) == 0 && offsetof(gm_tcn5ttqhv05g6, f1) == 1 && offsetof(gm_tcn5ttqhv05g6, f2) == 2 && offsetof(gm_tcn5ttqhv05g6, f3) == 3 && offsetof(gm_tcn5ttqhv05g6, f4) == 4 && offsetof(gm_tcn5ttqhv05g6, f5) == 8 && offsetof(gm_tcn5ttqhv05g6, f6) == 32 && offsetof(gm_tcn5ttqhv05g6, f7) == 33 && offsetof(gm_tcn5ttqhv05g6, f8) == 34 && offsetof(gm_tcn5ttqhv05g6, f9) == 40 && offsetof(gm_tcn5ttqhv05g6, f10) == 56 && offsetof(gm_tcn5ttqhv05g6, f11) == 64 && offsetof(gm_tcn5ttqhv05g6, f12) == 80 && offsetof(gm_tcn5ttqhv05g6, f13) == 88 && offsetof(gm_tcn5ttqhv05g6, f14) == 112 && offsetof(gm_tcn5ttqhv05g6, f15) == 113 && offsetof(gm_tcn5ttqhv05g6, f16) == 120 && offsetof(gm_tcn5ttqhv05g6, f17) == 128 && offsetof(gm_tcn5ttqhv05g6, f18) == 144 && offsetof(gm_tcn5ttqhv05g6, f19) == 145 && offsetof(gm_tcn5ttqhv05g6, f20) == 152 && offsetof(gm_tcn5ttqhv05g6, f21) == 176 && offsetof(gm_tcn5ttqhv05g6, f22) == 200 && offsetof(gm_tcn5ttqhv05g6, f23) == 201 && offsetof(gm_tcn5ttqhv05g6, f24) == 208 && offsetof(gm_tcn5ttqhv05g6, f25) == 232 && offsetof(gm_tcn5ttqhv05g6, f26) == 240 && offsetof(gm_tcn5ttqhv05g6, f27) == 256 && offsetof(gm_tcn5ttqhv05g6, f28) == 272 && offsetof(gm_tcn5ttqhv05g6, f29) == 400 && offsetof(gm_tcn5ttqhv05g6, f30) == 424 && offsetof(gm_tcn5ttqhv05g6, f31) == 488 && offsetof(gm_tcn5ttqhv05g6, f32) == 552 && offsetof(gm_tcn5ttqhv05g6, f33) == 576, "layout of `Options`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
struct gm_fr_base_cflags_irg71ui {
    gm_frame h;
    gm_t0rfe1ou14h21 l0; /* target */
    gm_trqfvmjtt4sij l1; /* flags */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_build_flags_sn2j41p {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* flags */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_build_object_r4906f3 {
    gm_frame h;
    gm_tcn5ttqhv05g6 l2; /* opts */
    gm_ts1lka1v5g5ua l5; /* tc */
    gm_trqfvmjtt4sij l6; /* roots */
    gm_tt1457on428g4 l7; /* b */
    uint8_t ret;
};
struct gm_fr_build_vkmrjt3 {
    gm_frame h;
    gm_tcn5ttqhv05g6 l3; /* opts */
    uint8_t ret;
};
struct gm_fr_cached_program_5afqj27 {
    gm_frame h;
    gm_tcn5ttqhv05g6 l1; /* opts */
    gm_tr8dvlu9ik4gj l2; /* extras */
    gm_ts1lka1v5g5ua l3; /* tc */
    gm_trqfvmjtt4sij l8; /* parts */
    gm_t142fel6vb03l l9; /* <temporary array> */
    gm_tdmi4fk9nhqa7 l10; /* <temporary array> */
    gm_tl572d7579hns ret;
};
struct gm_fr_clear_dvdem7p {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_compile_built_8o9nmfu {
    gm_frame h;
    gm_tcn5ttqhv05g6 l2; /* opts */
    gm_ts1lka1v5g5ua l8; /* tc */
    gm_tr8dvlu9ik4gj l9; /* extras */
    gm_tk2lc304hfpci l10; /* link */
    uint8_t ret;
};
struct gm_fr_compile_c_bkb1gkm {
    gm_frame h;
    gm_tcn5ttqhv05g6 l4; /* opts */
    gm_trqfvmjtt4sij l16; /* flags */
    gm_trqfvmjtt4sij l17; /* sources */
    gm_trqfvmjtt4sij l20; /* uflags */
    gm_trqfvmjtt4sij l28; /* c_files */
    gm_trqfvmjtt4sij l35; /* flags */
    gm_trqfvmjtt4sij l37; /* inputs */
    gm_trqfvmjtt4sij l38; /* objects */
    gm_trqfvmjtt4sij l42; /* unit_flags */
    gm_trqfvmjtt4sij l47; /* link_flags */
    gm_trqfvmjtt4sij l51; /* tail */
    gm_trqfvmjtt4sij l54; /* argv */
    uint8_t ret;
};
struct gm_fr_compile_cdm3hj7 {
    gm_frame h;
    gm_tcn5ttqhv05g6 l3; /* opts */
    uint8_t ret;
};
struct gm_fr_compile_entries_l9qlniu {
    gm_frame h;
    gm_tcn5ttqhv05g6 l4; /* opts */
    gm_tt1457on428g4 l5; /* b */
    uint8_t ret;
};
struct gm_fr_compile_object_k7kbtpr {
    gm_frame h;
    gm_tcn5ttqhv05g6 l3; /* opts */
    gm_trqfvmjtt4sij l8; /* flags */
    gm_tdmi4fk9nhqa7 l9; /* tail */
    gm_trqfvmjtt4sij l10; /* argv */
    uint8_t ret;
};
struct gm_fr_compile_objects_mu5secm {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* argvs */
    gm_tdmi4fk9nhqa7 l10; /* tail */
    gm_teg5ofd5qf10s l11; /* pool */
    gm_trqfvmjtt4sij l14; /* threads */
    uint8_t ret;
};
struct gm_fr_compile_one_4djvgo4 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* err */
    gm_tpbv7nqggnvqr l3; /* buf */
    gm_tl572d7579hns ret;
};
struct gm_fr_compile_worker_5cv5pj9 {
    gm_frame h;
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cpu_count_0o5882g {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_debug_flags_gv15c46 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_default_exe_194tkju {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_display_30kc1i9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_emitted_units_7f3nie0 {
    gm_frame h;
    gm_ttu09lij36f8c ret;
};
struct gm_fr_empty_slice_4e376v7 {
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
struct gm_fr_empty_slice_fklmfmo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_iv39hj3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ju26jbp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_exit_code_dv2nn7j {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_7hs67uj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_vogsd72 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extras_joquqpb {
    gm_frame h;
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_f16c_flag_vpo7tss {
    gm_frame h;
    gm_t0rfe1ou14h21 l0; /* target */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_format_09mcfes {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_12q2s8v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_359meqs {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4hckftk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5029eea {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6gakl8s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7os4s22 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a1j85ht {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cncadt9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cv7qh05 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ddl06ea {
    gm_frame h;
    gm_t2t9klu4dfekp l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t2t9klu4dfekp l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f2pdud5 {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hcgsrs3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_i9dlnsg {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jicque0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kdp6nh1 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tq3m5qgvkpje8 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pf7dqps {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q8i7gsb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rjqt2eo {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_generate_object_rgfvem4 {
    gm_frame h;
    gm_tetdkgoreqmn1 ret;
};
struct gm_fr_generate_program_9mr9pv8 {
    gm_frame h;
    gm_tcn5ttqhv05g6 l4; /* opts */
    gm_trqfvmjtt4sij l5; /* roots */
    gm_ta4jakbus8leg l9; /* p */
    gm_tetdkgoreqmn1 l11; /* result */
    gm_t0u6dopi4f21f l16; /* generated */
    gm_t5i6e9e007icp ret;
};
struct gm_fr_generate_tests_462i0us {
    gm_frame h;
    gm_tetdkgoreqmn1 ret;
};
struct gm_fr_has_extension_sdfv93u {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_heap_allocator_gihki37 {
    gm_frame h;
    gm_t3mf58gv9kf3n ret;
};
struct gm_fr_io_message_p7jbrea {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_clang_7du2shk {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_empty_8l7niee {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_gcc_epb17a6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_r8j2dp1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_library_stamps_mjeqafa {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* dirs */
    gm_trqfvmjtt4sij l7; /* stamps */
    gm_tfeab279c16rr l14; /* <tmp> */
    gm_t9bmh80edou9c l15; /* <tmp> */
    gm_b16_8 ret;
};
struct gm_fr_list_242hv68 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7h11c55 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7vplt15 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fnnocfv {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_lto_link_flags_0rmh9d3 {
    gm_frame h;
    gm_trqfvmjtt4sij l0; /* flags */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mutex_t7if00c {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_no_link_a2212mt {
    gm_frame h;
    gm_tk2lc304hfpci ret;
};
struct gm_fr_opt_level_gnm5dcm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_optimized_unit_count_5mur81l {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_pid_token_c444nkv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_push_0hkhfv5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_6ct7n6b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_fp59o9c {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_047mue0 {
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
struct gm_fr_regrow_skbsn8n {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_t85vg90 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_unsupported_9rg6nq1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_rt_flags_oeql34c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* flags */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_run_compiler_ulaqcb1 {
    gm_frame h;
    gm_tjm4rm50v5mle l3; /* result */
    uint8_t ret;
};
struct gm_fr_run_dg4eogs {
    gm_frame h;
    gm_tcn5ttqhv05g6 l3; /* opts */
    gm_tt1457on428g4 l7; /* b */
    gm_tpbv7nqggnvqr l11; /* buf */
    gm_trqfvmjtt4sij l13; /* runner */
    gm_trqfvmjtt4sij l14; /* argv */
    gm_t32dckm4jfarl l15; /* spawn */
    uint8_t ret;
};
struct gm_fr_runtime_source_skilk06 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_runtime_text_37culso {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_stem_of_t9qmlhi {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_success_q3lb6vt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_system_io_mcjcl9m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_target_text_s4sugoj {
    gm_frame h;
    gm_t0rfe1ou14h21 l0; /* t */
    gm_b16_8 ret;
};
struct gm_fr_thin_lto_klroq4v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_unit_count_dk0d2e4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_unit_path_3tdv4sg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_sources_329det7 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* sources */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_unlock_9f1vcl6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_wasi_target_lp6g335 {
    gm_frame h;
    gm_t0rfe1ou14h21 l0; /* target */
    uint8_t ret;
};
struct gm_fr_windows_libs_hmrgs9s {
    gm_frame h;
    gm_t0rfe1ou14h21 l0; /* target */
    gm_trqfvmjtt4sij l1; /* libs */
    gm_tud7oovfvnf43 l2; /* <tmp> */
    gm_tjgkb0mi112kg l3; /* <tmp> */
    gm_b16_8 ret;
};
struct gm_fr_write_generated_c_kb09043 {
    gm_frame h;
    gm_tpbv7nqggnvqr l3; /* buf */
    uint8_t ret;
};
struct gm_fr_write_units_hc5dqp8 {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_absolute_6pqhsaj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_asyncify_bs28ebq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_base_cflags_irg71ui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_build_flags_sn2j41p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_build_object_r4906f3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_build_vkmrjt3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) void gm_f_cache_dir_81h90qu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_cache_key_tf6ebrd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_cached_program_5afqj27(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_check_export_names_mirc4me(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_clear_dvdem7p(void * a0);
__attribute__((aligned(4))) void gm_f_command_b2u33fl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_compile_built_8o9nmfu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_compile_c_bkb1gkm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, const void * a4);
__attribute__((aligned(4))) uint8_t gm_f_compile_cdm3hj7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) uint8_t gm_f_compile_entries_l9qlniu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3, const void * a4);
__attribute__((aligned(4))) uint8_t gm_f_compile_object_k7kbtpr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) uint8_t gm_f_compile_objects_mu5secm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_compile_one_4djvgo4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_compile_worker_5cv5pj9(void * a0);
__attribute__((aligned(4))) void gm_f_compiler_version_hj63jgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_flags_gv15c46(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_flags_gv15c46_c(void * a0, gm_th2lqbo77pvsc a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_default_exe_194tkju(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_display_30kc1i9(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_emitted_units_7f3nie0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4e376v7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fklmfmo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_iv39hj3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ju26jbp(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_entry_point_n2gu79u(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_exit_code_dv2nn7j(gm_tsoiri44d0i6q a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_extras_joquqpb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_f16c_flag_vpo7tss(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_f16c_flag_vpo7tss_c(gm_t0rfe1ou14h21 a0);
__attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_format_09mcfes(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_12q2s8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_2kt92vg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_359meqs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_4hckftk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5029eea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6gakl8s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_74pd3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_7os4s22(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_92fech4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_a1j85ht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_b73b468(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_cncadt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_cv7qh05(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ddl06ea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_f2pdud5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_g6autgh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_hcgsrs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_i9dlnsg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_jicque0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_kdp6nh1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mj50pfs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_pf7dqps(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_q8i7gsb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_rjqt2eo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_s2bvqvf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_t22qvc1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_generate_6no5tn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) void gm_f_generate_entries_br1bd7f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, const void * a3);
inline __attribute__((aligned(4))) void gm_f_generate_object_rgfvem4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_generate_program_9mr9pv8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint8_t a3, const void * a4);
inline __attribute__((aligned(4))) void gm_f_generate_tests_462i0us(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_has_extension_sdfv93u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_hex_euih242(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_clang_7du2shk(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_default_24c3cui(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_8l7niee(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6(const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_join_r8j2dp1(gm_csfq64m1m1rt6m a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_library_args_lijr5kp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_library_stamps_mjeqafa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_list_242hv68(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7h11c55(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7vplt15(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fnnocfv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_lock_u5lks01(void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_lto_link_flags_0rmh9d3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_cache_dirs_3bvvbb9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_make_temp_dir_vi8f88d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_mode_name_vpt4nrc(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_mutex_t7if00c(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_native_exe_name_rqchv3n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_no_link_a2212mt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_level_gnm5dcm(void * a0);
inline __attribute__((aligned(4))) uint64_t gm_f_optimized_unit_count_5mur81l(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) void gm_f_output_mg6eie5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_package_link_q0071pm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, uint8_t a4);
__attribute__((aligned(4))) void gm_f_parse_int_3ce02np(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_pid_token_c444nkv(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_prepare_codegen_4hnlio1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_private_name_u7i0rip(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_prog_61arbs1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_program_on_path_fm43kjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_prune_eu93ghe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0hkhfv5(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_6ct7n6b(void * a0, gm_csfq64m1m1rt6m a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_fp59o9c(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_047mue0(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_skbsn8n(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t85vg90(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_all_h0k1qkh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_remove_objects_1vq49q8(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_rename_or_remove_l80dbfs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_unsupported_9rg6nq1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_resolve_lines_h4bf30f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rfind_ai5s2js(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_rt_flags_oeql34c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_run_compiler_ulaqcb1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_run_dg4eogs(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_run_error_gs1dinp(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
__attribute__((aligned(4))) void gm_f_runner_for_79o4h4m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_runtime_dir_qd07iff(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_runtime_object_oihv3ss(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_runtime_source_skilk06(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_runtime_text_37culso(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_spawn_et0s5ea(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_split_by_file_hgjqgsp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_split_jendjdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tlmjd3nndlf3r gm_f_status_20hgo8k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_std_root_e0juohb(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_stem_of_t9qmlhi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_success_q3lb6vt(gm_tsoiri44d0i6q a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void);
__attribute__((aligned(4))) gm_b16_8 gm_f_target_text_s4sugoj(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_temp_dir_i6apmsd(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_thin_lto_klroq4v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_touch_frggs6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint64_t gm_f_unit_count_dk0d2e4(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) void gm_f_unit_objects_3f31tcd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, const void * a3, uint64_t a4, uint8_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_unit_path_3tdv4sg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_unit_sources_329det7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_unlock_9f1vcl6(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_wasi_link_flags_6mu198c(void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_wasi_target_lp6g335(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_wasi_target_lp6g335_c(gm_t0rfe1ou14h21 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_wasm_opt_version_hapilm6(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_windows_libs_hmrgs9s(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_all_o8ucaji(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_write_generated_c_kb09043(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_write_lldb_formatters_m0rjk7q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_write_units_hc5dqp8(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_c1a92ugfgjv[112] __attribute__((aligned(8)));
extern uint8_t gm_c302jkvduvb[16] __attribute__((aligned(8)));
extern uint8_t gm_c69snm9duhi[16] __attribute__((aligned(8)));
extern uint8_t gm_c7mc3uaor1r[80] __attribute__((aligned(8)));
extern uint8_t gm_cc7b06pnl1q[1] __attribute__((aligned(1)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cfa8lmn3344[48] __attribute__((aligned(8)));
extern uint8_t gm_cgd1khc5sal[24] __attribute__((aligned(8)));
extern uint8_t gm_cgsmfcq937e[16] __attribute__((aligned(8)));
extern uint8_t gm_cl1ove1i7cb[16] __attribute__((aligned(8)));
extern uint8_t gm_clslitrb510[16] __attribute__((aligned(8)));
extern void *const gm_hd3g3ujqvah4[];
extern void *const gm_hd3t1u951rul[];
extern void *const gm_hdaqemfrshlp[];
extern void *const gm_hdf4dspmusp5[];
extern void *const gm_hdffkeesmui4[];
extern void *const gm_hdu8v6n5i2qd[];
extern void *const gm_hdvslis4o40u[];
extern const uint8_t gm_s048hmhpmbi7c[8];
extern const uint8_t gm_s067qe7d1tphl[31];
extern const uint8_t gm_s06m3met1mb9v[3];
extern const uint8_t gm_s0pi0emdqd3vb[2];
extern const uint8_t gm_s0ucactj6hj0a[4];
extern const uint8_t gm_s17cjlkd2o01f[29];
extern const uint8_t gm_s1ctr86inn5iu[24];
extern const uint8_t gm_s1lekaltijhvv[8];
extern const uint8_t gm_s29cvp3di8ou1[4];
extern const uint8_t gm_s2bp4nvcgkqlc[13];
extern const uint8_t gm_s2cgbvfh1b7rp[6];
extern const uint8_t gm_s3hhdo4dfru76[7];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3vboaodpmi9a[4];
extern const uint8_t gm_s463po27vtmnn[15];
extern const uint8_t gm_s4lj2bemc87jp[3];
extern const uint8_t gm_s4vd6e3mkiiso[3];
extern const uint8_t gm_s55g9qp4arkn7[8];
extern const uint8_t gm_s5g0cpgo64peg[104];
extern const uint8_t gm_s5hode9408atm[9];
extern const uint8_t gm_s5lqhiu2bn6of[8];
extern const uint8_t gm_s6077qnlc299h[4];
extern const uint8_t gm_s622mvvjraklk[43];
extern const uint8_t gm_s65ftmvtj458k[11];
extern const uint8_t gm_s65qt8qo64cje[3];
extern const uint8_t gm_s6vtoqhgjb0nd[6];
extern const uint8_t gm_s7qmtc95nc11i[24];
extern const uint8_t gm_s9o66vhtid5af[4];
extern const uint8_t gm_sad95m298up5o[44];
extern const uint8_t gm_sagpv7fgcnm3e[64];
extern const uint8_t gm_sb7kik80lfbge[11];
extern const uint8_t gm_sbkvlhvo2a3ic[7];
extern const uint8_t gm_sc149ns1jm5on[9];
extern const uint8_t gm_sck9nihohhsb7[15];
extern const uint8_t gm_scq91tgm9lph4[6];
extern const uint8_t gm_sdahvc1997d01[14];
extern const uint8_t gm_sdbvbbg6vc50j[27];
extern const uint8_t gm_sdtdaesvk7dfl[8];
extern const uint8_t gm_sehokd57s045g[11];
extern const uint8_t gm_setv7ejg35cg9[43];
extern const uint8_t gm_sf1ljria5bdei[8];
extern const uint8_t gm_sfeuhas7a05k6[6];
extern const uint8_t gm_sfjuf1jo6fusr[8];
extern const uint8_t gm_sfss9c30cdmun[8];
extern const uint8_t gm_sftkp1r7uetfv[10];
extern const uint8_t gm_sg5khilhaakst[16];
extern const uint8_t gm_sg87p4r5apham[36];
extern const uint8_t gm_sguhoqhco0rfk[9];
extern const uint8_t gm_shgi3g6gdv99o[4];
extern const uint8_t gm_sir0litoa3fk0[18];
extern const uint8_t gm_sjcgs2v314rl7[19];
extern const uint8_t gm_sje4rbc88csdu[7];
extern const uint8_t gm_sk4cv563hhed7[2];
extern const uint8_t gm_skbmmrtbskpao[8];
extern const uint8_t gm_skm48m2qkkghv[116];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skp6qpvju2t5u[19];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl5h9isb3bijl[5];
extern const uint8_t gm_sldsondfm6ju2[9];
extern const uint8_t gm_slgjpcgk90nuf[5];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_sm8m7jldttgpj[38];
extern const uint8_t gm_smaqn3dnro8sl[14];
extern const uint8_t gm_smi2s23d2qqne[6];
extern const uint8_t gm_smuti066jdthl[24];
extern const uint8_t gm_sn0peadng5i31[3];
extern const uint8_t gm_sn2furlv6e7jl[4];
extern const uint8_t gm_sn3ltks10rrv7[8];
extern const uint8_t gm_sn6vmked7l7pf[7];
extern const uint8_t gm_snekrjln06tfl[14];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_sngj7pcs9ksq7[8];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so78814tnt5g5[2];
extern const uint8_t gm_so99jjk23jgg8[5];
extern const uint8_t gm_sog4rksrmsqd2[5];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_sqd102436v4lo[9];
extern const uint8_t gm_sqdfr4k3vea5p[4];
extern const uint8_t gm_sr2df1mc0vbea[4];
extern const uint8_t gm_sr3et5tffndla[28];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr98hl2rjopi2[8];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src7u0d314efpdm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srccfhjr4r9mdef[];
extern const char gm_srcqr4ch56kv6bv[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srct4i4cmesm57g[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_ssm3bl5los9iv[5];
extern const uint8_t gm_ssmr7jgj9e42t[25];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_st9f93lco88fg[3];
extern const uint8_t gm_sukbpodfceoe1[9];
extern const uint8_t gm_summega2vonc9[6];
extern const uint8_t gm_svaibtfuu0gf2[48];
extern const uint8_t gm_svl13be5ca8rt[5];
extern const uint8_t gm_svtktff6b3snn[3];
extern const gm_tmpl gm_tpl00b7kgp37ng7;
extern const gm_tmpl gm_tpl0eumfheo3vtt;
extern const gm_tmpl gm_tpl0fhs0t9qdf1o;
extern const gm_tmpl gm_tpl233lm3m83499;
extern const gm_tmpl gm_tpl4bpbmadpmu1i;
extern const gm_tmpl gm_tpl8tri2h8bpkpg;
extern const gm_tmpl gm_tplacjkb64s2e83;
extern const gm_tmpl gm_tplcre592qvlrqu;
extern const gm_tmpl gm_tpld6a9rk5r1ghn;
extern const gm_tmpl gm_tplei8v65ors6jc;
extern const gm_tmpl gm_tplepdvp8ofjal7;
extern const gm_tmpl gm_tplhksbien67lqe;
extern const gm_tmpl gm_tpllfhth9mee4q8;
extern const gm_tmpl gm_tplncscrjg4euhr;
extern const gm_tmpl gm_tplngekt6vfvnsv;
extern const gm_tmpl gm_tplnr3ridveqobn;
extern const gm_tmpl gm_tplrspdrqd1dpc8;
extern const gm_tmpl gm_tplsmpaecrda5mc;
extern const gm_tmpl gm_tplvkvnlb2orh55;
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
inline __attribute__((aligned(4))) void gm_f_clear_dvdem7p(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_dvdem7p F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
    if (t6 > t7 || t7 > (uintptr_t)t5) gm_trap_slice(t6, t7, t5, gm_srcrgc9bkquf6nr, 140, 15);
    uint64_t t8 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    gm_b16_8 t5 = gm_l0;
    gm_b16_8 t6 = gm_l1;
    gm_tf5srh4mp4b3i t7 = gm_f_find_nab29co(t5, t6);
    gm_l3 = t7;
    {
        {
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (t8) {
                goto gm_lb2;
            }
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 84, 50);
    }
gm_lb1:;
    uint8_t t9 = gm_l2;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_cpu_count_0o5882g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cpu_count_0o5882g F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint32_t t2 = gm_cpu_count();
    F.ret = t2;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_display_30kc1i9(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_display_30kc1i9 F;
    void * gm_l0; /* tc */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)t2));
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s3p0rbs9hc3lg));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
    gm_f_join_sfknljb((void *)&gm_l1, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_src7u0d314efpdm, 178, 66);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4e376v7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_4e376v7 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fklmfmo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fklmfmo F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_iv39hj3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_iv39hj3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ju26jbp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ju26jbp F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_7hs67uj F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_2onqfca(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 4);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_vogsd72 F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_0jmnnpk(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 16);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 16)));
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
inline __attribute__((aligned(4))) void gm_f_extras_joquqpb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_extras_joquqpb F;
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_7h11c55((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_epvducg((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t4;
    gm_f_list_epvducg((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tr8dvlu9ik4gj t5;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t5 + 0), t2);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t5 + 40), t3);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t5 + 80), t4);
    memmove(gm_out, ((uint8_t *)&t5), 120);
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
inline __attribute__((aligned(4))) void gm_f_format_09mcfes(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_09mcfes F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sfeuhas7a05k6));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_t22qvc1((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplrspdrqd1dpc8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_12q2s8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_12q2s8v F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s067qe7d1tphl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(30));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplngekt6vfvnsv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_359meqs(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_359meqs F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sf1ljria5bdei));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(7));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0fhs0t9qdf1o), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_4hckftk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4hckftk F;
    gm_t3877pk7lv748 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdtdaesvk7dfl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(7));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_2kt92vg((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplncscrjg4euhr), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5029eea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5029eea F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl5h9isb3bijl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(4));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_74pd3in((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplei8v65ors6jc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6gakl8s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6gakl8s F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s17cjlkd2o01f));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl8tri2h8bpkpg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7os4s22(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7os4s22 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfjuf1jo6fusr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl233lm3m83499), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a1j85ht(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_a1j85ht F;
    gm_t2u4qamcbbgu0 gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t2u4qamcbbgu0 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sukbpodfceoe1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t2u4qamcbbgu0 t3 = GM_LD(gm_t2u4qamcbbgu0, ((uint8_t *)a0));
    gm_f_format_92fech4((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4bpbmadpmu1i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cncadt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cncadt9 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssm3bl5los9iv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplepdvp8ofjal7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cv7qh05(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cv7qh05 F;
    gm_t5hdnnftkup6m gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t5hdnnftkup6m gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb7kik80lfbge));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t5hdnnftkup6m t3 = GM_LD(gm_t5hdnnftkup6m, ((uint8_t *)a0));
    gm_f_format_s2bvqvf((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvkvnlb2orh55), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ddl06ea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ddl06ea F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smuti066jdthl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(23));
    gm_t2t9klu4dfekp t3 = GM_LD(gm_t2t9klu4dfekp, ((uint8_t *)a0));
    gm_f_format_mj50pfs((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl00b7kgp37ng7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_f2pdud5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_f2pdud5 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s65ftmvtj458k));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_g6autgh((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpld6a9rk5r1ghn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_hcgsrs3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_hcgsrs3 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdbvbbg6vc50j));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhksbien67lqe), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_i9dlnsg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_i9dlnsg F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sldsondfm6ju2));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplsmpaecrda5mc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jicque0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jicque0 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1lekaltijhvv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnr3ridveqobn), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_kdp6nh1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kdp6nh1 F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sck9nihohhsb7));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_tq3m5qgvkpje8 t3 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
    gm_f_format_b73b468((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplacjkb64s2e83), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_pf7dqps(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_pf7dqps F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so99jjk23jgg8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl0eumfheo3vtt), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q8i7gsb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q8i7gsb F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_summega2vonc9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllfhth9mee4q8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_rjqt2eo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_rjqt2eo F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sc149ns1jm5on));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplcre592qvlrqu), t2);
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
inline __attribute__((aligned(4))) void gm_f_generate_object_rgfvem4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_generate_object_rgfvem4 F;
    void * gm_l0; /* p */
    gm_tftmsj5vtqvej gm_l1; /* opts */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    gm_tftmsj5vtqvej t5 = GM_LD(gm_tftmsj5vtqvej, ((uint8_t *)&gm_l1));
    gm_f_generate_entries_br1bd7f((void *)&(*(gm_tetdkgoreqmn1 *)gm_out), (void *)NULL, cx, NULL, t2, t4, ((uint8_t)0), &t5);
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
inline __attribute__((aligned(4))) void gm_f_generate_tests_462i0us(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_generate_tests_462i0us F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* entries */
    gm_tftmsj5vtqvej gm_l2; /* opts */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_tftmsj5vtqvej t4 = GM_LD(gm_tftmsj5vtqvej, ((uint8_t *)&gm_l2));
    gm_f_generate_entries_br1bd7f((void *)&(*(gm_tetdkgoreqmn1 *)gm_out), (void *)NULL, cx, NULL, t2, t3, ((uint8_t)1), &t4);
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
inline __attribute__((aligned(4))) gm_cs6mnftt383dnt gm_f_heap_allocator_gihki37(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_heap_allocator_gihki37 F;
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = (void *)((uint8_t *)gm_cgd1khc5sal);
    void * t3 = (void *)((uint8_t *)gm_cgd1khc5sal);
    gm_t3mf58gv9kf3n t4;
    GM_ST(void *, ((uint8_t *)&t4 + 0), t2);
    GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
gm_fprop:
    return GM_CONV(gm_cs6mnftt383dnt, F.ret);
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0) {
    struct gm_fr_io_message_p7jbrea F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toe5fmr3vvf4d t2 = gm_l0;
    gm_b16_8 t3 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_clang_7du2shk(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_clang_7du2shk F;
    gm_th2lqbo77pvsc gm_l0; /* version */
    gm_b16_8 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 gm_l2;
    uint8_t t2 = (uint8_t)((((uint8_t *)a0))[16] != 0);
    if (t2) {
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)a0));
        gm_l2 = t3;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t4 = gm_l2;
    gm_l1 = t4;
    gm_b16_8 t5 = gm_l1;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_smaqn3dnro8sl));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(13));
    uint8_t t7 = gm_f_contains_gkki4kq(t5, t6);
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_8l7niee(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_empty_8l7niee F;
    void * gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    uint8_t gm_l2;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
    if (t4) {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 40) + 8));
        uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
        gm_l2 = t7;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t8 = gm_l2;
    if (t8) {
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t9) + 80) + 8));
        uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
        gm_l1 = t11;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t12 = gm_l1;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_tcuv9rv F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 183u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tih6h4avv5nrb gm_l2;
    gm_t5j14m8ru132p gm_l3;
    gm_b16_8 t2 = gm_l0;
    gm_node *pn3 = gm_find(cx, 2126215853);
    if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 234, 13);
    gm_tih6h4avv5nrb t4;
    ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[5])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t5) {
        memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l2) + 8), 32);
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l3), 32);
    gm_t9qkh0rsooc8d t6 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 8));
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    uint8_t t8 = (uint8_t)((t7 == ((uint8_t)0ULL)));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)0ULL));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_gcc_epb17a6 F;
    gm_th2lqbo77pvsc gm_l0; /* version */
    gm_b16_8 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 gm_l2;
    uint8_t gm_l3;
    uint8_t t2 = (uint8_t)((((uint8_t *)a0))[16] != 0);
    if (t2) {
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)a0));
        gm_l2 = t3;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t4 = gm_l2;
    gm_l1 = t4;
    gm_b16_8 t5 = gm_l1;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_ssmr7jgj9e42t));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(24));
    uint8_t t7 = gm_f_contains_gkki4kq(t5, t6);
    if (t7) {
        gm_b16_8 t8 = gm_l1;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_s2cgbvfh1b7rp));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(5));
        uint8_t t10 = gm_f_contains_gkki4kq(t8, t9);
        uint8_t t11 = (uint8_t)!t10;
        gm_l3 = t11;
    } else {
        gm_l3 = ((uint8_t)0);
    }
    uint8_t t12 = gm_l3;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_join_r8j2dp1(gm_csfq64m1m1rt6m a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_join_r8j2dp1 F;
    gm_tm98ap4112hee gm_l0; /* t */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tm98ap4112hee, a0);
    int64_t t2 = GM_LD(int64_t, ((uint8_t *)&gm_l0));
    int64_t t3 = gm_thread_join(t2);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_242hv68(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_242hv68 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ju26jbp();
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
inline __attribute__((aligned(4))) void gm_f_list_7h11c55(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7h11c55 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fklmfmo();
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
inline __attribute__((aligned(4))) void gm_f_list_7vplt15(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7vplt15 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_iv39hj3();
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
inline __attribute__((aligned(4))) void gm_f_list_fnnocfv(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fnnocfv F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_4e376v7();
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 200));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 200) + 8));
    uint64_t t5 = gm_l1;
    if ((uintptr_t)t5 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t5, t4, gm_srcc2qutubarto1, 547, 78);
    void * t6 = (void *)((((uint8_t *)t3) + (uintptr_t)t5 * 184) + 56);
    gm_b16_8 t7 = gm_l2;
    gm_tf5srh4mp4b3i t8 = gm_f_get_espvf96(t6, t7);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_mutex_t7if00c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mutex_t7if00c F;
    GM_STACK_CHECK_SHALLOW(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t88rcucg1b7uk t2;
    GM_ST(uint32_t, ((uint8_t *)&t2 + 0), ((uint32_t)0ULL));
    F.ret = t2;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
inline __attribute__((aligned(4))) void gm_f_no_link_a2212mt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_no_link_a2212mt F;
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_epvducg((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_epvducg((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    gm_b16_8 t6 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_tk2lc304hfpci t8;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t8 + 0), t2);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t8 + 40), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 80), t5);
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 96), t7);
    memmove(gm_out, ((uint8_t *)&t8), 112);
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
inline __attribute__((aligned(4))) uint64_t gm_f_optimized_unit_count_5mur81l(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_optimized_unit_count_5mur81l F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* cpus */
    gm_tf5srh4mp4b3i gm_l2; /* forced */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
            gm_l3 = t5;
            uint64_t t6 = gm_l3;
            uint64_t t7 = (t6 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t6 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t6));
            F.ret = t7;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 64) + 8));
    if (((uint64_t)4194304ULL) == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 74, 13);
    uint64_t t10 = (uint64_t)(t9 / ((uint64_t)4194304ULL));
    uint64_t t11 = gm_l1;
    uint64_t t12 = (t11 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t11 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t11));
    uint64_t t13 = (t10 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t10 > t12 ? t12 : t10));
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0hkhfv5(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0hkhfv5 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_ju26jbp();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_t85vg90(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_6ct7n6b(void * a0, gm_csfq64m1m1rt6m a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_6ct7n6b F;
    void * gm_l0; /* l */
    gm_tm98ap4112hee gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tm98ap4112hee, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_iv39hj3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_skbsn8n(t8, ((uint64_t)1ULL), t10);
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
    gm_tm98ap4112hee t18 = gm_l1;
    GM_ST(gm_tm98ap4112hee, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_fp59o9c(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_fp59o9c F;
    void * gm_l0; /* l */
    gm_tl572d7579hns gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_4e376v7();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_047mue0(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_047mue0(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_047mue0 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_skbsn8n(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_skbsn8n F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t85vg90(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_t85vg90 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_runtime_source_skilk06(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_runtime_source_skilk06 F;
    void * gm_l0; /* b */
    GM_STACK_CHECK(&F, 177u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l1;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 216));
    void * t4 = gm_l0;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 232));
    gm_t6j6smj7qoctu t6;
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    gm_f_format_12q2s8v((void *)&gm_l1, (void *)NULL, cx, NULL, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_srcqr4ch56kv6bv, 229, 64);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_success_q3lb6vt(gm_tsoiri44d0i6q a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_success_q3lb6vt F;
    gm_tsoiri44d0i6q gm_l0; /* s */
    uint32_t gm_l1; /* code */
    GM_STACK_CHECK_SHALLOW(&F, 29u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)0ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
            gm_l1 = t7;
            uint32_t t8 = gm_l1;
            uint8_t t9 = ((t8 == ((uint32_t)0ULL)));
            gm_l2 = t9;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)1ULL));
            if (!t11) {
                goto gm_lb3;
            }
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srct4i4cmesm57g, 65, 12);
    }
gm_lb1:;
    uint8_t t12 = gm_l2;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_system_io_mcjcl9m(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_system_io_mcjcl9m F;
    GM_STACK_CHECK_SHALLOW(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_unit_count_dk0d2e4(void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unit_count_dk0d2e4 F;
    void * gm_l0; /* parts */
    uint64_t gm_l1; /* cpus */
    gm_tf5srh4mp4b3i gm_l2; /* forced */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
            gm_l3 = t5;
            uint64_t t6 = gm_l3;
            uint64_t t7 = (t6 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t6 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t6));
            F.ret = t7;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 64) + 8));
    if (((uint64_t)98304ULL) == 0) gm_trap("division by zero", gm_srccfhjr4r9mdef, 82, 13);
    uint64_t t10 = (uint64_t)(t9 / ((uint64_t)98304ULL));
    uint64_t t11 = gm_l1;
    uint64_t t12 = (t11 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t11 > ((uint64_t)8ULL) ? ((uint64_t)8ULL) : t11));
    uint64_t t13 = (t10 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t10 > t12 ? t12 : t10));
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_unlock_9f1vcl6(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unlock_9f1vcl6 F;
    void * gm_l0; /* m */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)t2);
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)2ULL));
    uint32_t t5 = __atomic_exchange_n(((uint32_t *)t3), ((uint32_t)0ULL), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t4))]);
    uint8_t t6 = ((t5 == ((uint32_t)2ULL)));
    if (t6) {
        void * t7 = gm_l0;
        void * t8 = (void *)((uint8_t *)t7);
        int64_t t9 = gm_futex_wake(t8, ((uint32_t)1ULL));
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_wasi_target_lp6g335(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wasi_target_lp6g335 F;
    GM_STACK_CHECK_SHALLOW(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 24));
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svl13be5ca8rt));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    uint8_t t4 = (gm_eq0iqiubfm7e(((uint8_t *)&t2), ((uint8_t *)&t3)));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_base_cflags_irg71ui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_base_cflags_irg71ui F;
    GM_STACK_CHECK(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tftb1odc739t5 gm_l2;
    gm_tftb1odc739t5 gm_l3;
    gm_trqfvmjtt4sij t4;
    gm_f_list_epvducg((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t4;
    void * t5 = (void *)((uint8_t *)&F.l1);
    void * t6 = (void *)((uint8_t *)gm_c1a92ugfgjv);
    uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t8 = (uintptr_t)((uint64_t)7ULL);
    if (t7 > t8 || t8 > (uintptr_t)((uint64_t)7ULL)) gm_trap_slice(t7, t8, ((uint64_t)7ULL), gm_srcqr4ch56kv6bv, 260, 18);
    uint64_t t9 = ((uintptr_t)((uint64_t)7ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t6 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9));
    gm_tftb1odc739t5 t11 = gm_f_extend_vogsd72(t5, t10);
    gm_l2 = t11;
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcqr4ch56kv6bv, 260, 5);
    {
        {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 24));
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s048hmhpmbi7c));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(7));
            uint8_t t15 = (!gm_eq0iqiubfm7e(((uint8_t *)&t13), ((uint8_t *)&t14)));
            if (!t15) {
                goto gm_lb2;
            }
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 24));
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_svl13be5ca8rt));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
            uint8_t t18 = (!gm_eq0iqiubfm7e(((uint8_t *)&t16), ((uint8_t *)&t17)));
            if (!t18) {
                goto gm_lb2;
            }
            void * t19 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sguhoqhco0rfk));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(8));
            gm_tftb1odc739t5 t21 = gm_f_push_3ef746b(t19, t20);
            gm_l3 = t21;
            uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t22) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcqr4ch56kv6bv, 261, 65);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) void gm_f_build_flags_sn2j41p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_build_flags_sn2j41p F;
    void * gm_l0; /* opts */
    gm_th2lqbo77pvsc gm_l1; /* version */
    gm_b16_8 gm_l3; /* f */
    GM_STACK_CHECK(&F, 350u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_th2lqbo77pvsc gm_l7;
    gm_tftb1odc739t5 gm_l8;
    void * t4 = gm_l0;
    gm_trqfvmjtt4sij t5;
    gm_f_base_cflags_irg71ui((void *)&t5, (void *)NULL, cx, NULL, &(*(gm_t0rfe1ou14h21 *)(((uint8_t *)t4) + 424)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t5;
    void * t6 = (void *)((uint8_t *)&F.l2);
    void * t7 = gm_l0;
    gm_b16_8 t8 = gm_f_opt_level_gnm5dcm(t7);
    gm_tftb1odc739t5 t9 = gm_f_push_3ef746b(t6, t8);
    gm_l4 = t9;
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcqr4ch56kv6bv, 460, 5);
    void * t11 = (void *)((uint8_t *)&F.l2);
    void * t12 = gm_l0;
    gm_b16_8 t13 = gm_f_debug_flags_gv15c46(t12, &(*(gm_th2lqbo77pvsc *)((uint8_t *)a1)));
    gm_tftb1odc739t5 t14 = gm_f_extend_vogsd72(t11, t13);
    gm_l5 = t14;
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcqr4ch56kv6bv, 461, 5);
    void * t16 = gm_l0;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t16) + 33));
    if (t17) {
        void * t18 = (void *)((uint8_t *)&F.l2);
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sn0peadng5i31));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t20 = gm_f_push_3ef746b(t18, t19);
        gm_l6 = t20;
        uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcqr4ch56kv6bv, 462, 26);
    }
    {
        {
            void * t22 = gm_l0;
            gm_t0rfe1ou14h21 t23 = GM_LD(gm_t0rfe1ou14h21, (((uint8_t *)t22) + 424));
            gm_f_f16c_flag_vpo7tss((void *)&gm_l7, &t23);
            uint8_t t24 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
            if (!t24) {
                goto gm_lb2;
            }
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
            gm_l3 = t25;
            void * t26 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t27 = gm_l3;
            gm_tftb1odc739t5 t28 = gm_f_push_3ef746b(t26, t27);
            gm_l8 = t28;
            uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcqr4ch56kv6bv, 463, 38);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
inline __attribute__((aligned(4))) uint8_t gm_f_build_object_r4906f3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_build_object_r4906f3 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l3; /* o */
    gm_b16_8 gm_l4; /* obj */
    GM_STACK_CHECK(&F, 1694u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_b16_8 gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_ts1lka1v5g5ua gm_l10;
    gm_b16_8 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_t5i6e9e007icp gm_l13;
    gm_tt1457on428g4 gm_l14;
    {
        {
            uint8_t t4 = (uint8_t)(((((uint8_t *)&F.l2) + 8))[16] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 8));
            gm_l3 = t5;
            gm_b16_8 t6 = gm_l3;
            gm_l8 = t6;
            goto gm_lb1;
        }
gm_lb2:;
        gm_b16_8 t7 = gm_l1;
        gm_b16_8 t8 = gm_f_stem_of_t9qmlhi((void *)NULL, cx, NULL, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t9;
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t8);
        gm_f_format_pf7dqps((void *)&gm_l9, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcqr4ch56kv6bv, 49, 49);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_l8 = t12;
    }
gm_lb1:;
    gm_b16_8 t13 = gm_l8;
    gm_l4 = t13;
    uint8_t t14 = (uint8_t)(((((uint8_t *)&F.l2) + 488))[56] != 0);
    if (t14) {
        memmove(((uint8_t *)&gm_l10), (((uint8_t *)&F.l2) + 488), 56);
    } else {
        uint8_t t15 = (uint8_t)(((((uint8_t *)&F.l2) + 552))[16] != 0);
        if (t15) {
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 552));
            gm_l11 = t16;
        } else {
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(0));
            gm_l11 = t17;
        }
        gm_b16_8 t18 = gm_l11;
        if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_srcqr4ch56kv6bv, 51, 9)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t18) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t18)), GM_LD(uintptr_t, ((uint8_t *)&t18) + 8), gm_srcqr4ch56kv6bv, 51, 9))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 51, 9)) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    memmove(((uint8_t *)&F.l5), ((uint8_t *)&gm_l10), 56);
    void * t19 = gm_l0;
    uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)t19) + 880) + 8));
    uint8_t t21 = ((t20 == ((uint64_t)0ULL)));
    if (t21) {
        if (gm_pf454qfrpt6r(cx, gm_skm48m2qkkghv, 115, gm_srcqr4ch56kv6bv, 55, 9)) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_trqfvmjtt4sij t22;
    gm_f_list_go2qg0b((void *)&t22, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l6 = t22;
    void * t23 = (void *)((uint8_t *)&F.l6);
    void * t24 = gm_l0;
    gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 920));
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
    gm_tftb1odc739t5 t27 = gm_f_extend_7hs67uj(t23, t26);
    gm_l12 = t27;
    uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcqr4ch56kv6bv, 59, 5);
    void * t29 = gm_l0;
    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
    gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
    gm_b16_8 t32 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
    gm_tcn5ttqhv05g6 t34 = GM_LD(gm_tcn5ttqhv05g6, ((uint8_t *)&F.l2));
    gm_f_generate_program_9mr9pv8((void *)&gm_l13, (void *)NULL, cx, NULL, t29, t31, t33, ((uint8_t)0), &t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l13))[248] != 0);
    if (t35) {
        memmove(((uint8_t *)&gm_l14), ((uint8_t *)&gm_l13), 248);
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    memmove(((uint8_t *)&F.l7), ((uint8_t *)&gm_l14), 248);
    void * t36 = (void *)((uint8_t *)&F.l7);
    void * t37 = (void *)((uint8_t *)&F.l5);
    gm_b16_8 t38 = gm_l4;
    uint8_t t39 = gm_f_compile_object_k7kbtpr((void *)NULL, cx, NULL, t36, t37, t38, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t39;
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
inline __attribute__((aligned(4))) uint8_t gm_f_build_vkmrjt3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3) {
    struct gm_fr_build_vkmrjt3 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* root */
    gm_b16_8 gm_l2; /* path */
    gm_b16_8 gm_l4; /* o */
    gm_b16_8 gm_l5; /* exe */
    gm_b16_8 gm_l6; /* printers */
    GM_STACK_CHECK(&F, 509u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&F.l3, a3, sizeof F.l3);
    gm_b16_8 gm_l7;
    gm_b16_8 gm_l8;
    gm_th2lqbo77pvsc gm_l9;
    gm_b16_8 gm_l10;
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l3) + 424) + 24));
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_soo4rtckloi4d));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(12));
    uint8_t t8 = (gm_eq0iqiubfm7e(((uint8_t *)&t6), ((uint8_t *)&t7)));
    if (t8) {
        void * t9 = gm_l0;
        gm_b16_8 t10 = gm_l2;
        uint8_t t11 = gm_f_build_object_r4906f3((void *)NULL, cx, NULL, t9, t10, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t11;
        goto gm_fret;
    }
    {
        {
            uint8_t t12 = (uint8_t)(((((uint8_t *)&F.l3) + 8))[16] != 0);
            if (!t12) {
                goto gm_lb2;
            }
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 8));
            gm_l4 = t13;
            gm_b16_8 t14 = gm_l4;
            uint8_t t15 = gm_f_has_extension_sdfv93u((void *)NULL, cx, NULL, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t15) {
                gm_b16_8 t16 = gm_l4;
                gm_l8 = t16;
            } else {
                void * t17 = (void *)((uint8_t *)&F.l3);
                gm_b16_8 t18 = gm_l4;
                gm_b16_8 t19 = gm_f_native_exe_name_rqchv3n((void *)NULL, cx, NULL, t17, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t19;
            }
            gm_b16_8 t20 = gm_l8;
            gm_l7 = t20;
            goto gm_lb1;
        }
gm_lb2:;
        void * t21 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t22 = gm_l2;
        gm_b16_8 t23 = gm_f_default_exe_194tkju(t22);
        gm_b16_8 t24 = gm_f_native_exe_name_rqchv3n((void *)NULL, cx, NULL, t21, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t24;
    }
gm_lb1:;
    gm_b16_8 t25 = gm_l7;
    gm_l5 = t25;
    void * t26 = gm_l0;
    uint64_t t27 = gm_l1;
    gm_b16_8 t28 = gm_l5;
    uint8_t t29 = gm_f_compile_cdm3hj7((void *)NULL, cx, NULL, t26, t27, t28, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l3)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t30 = (uint8_t)!t29;
    if (t30) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    {
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 33));
            if (!t31) {
                goto gm_lb4;
            }
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 32));
            uint8_t t33 = (uint8_t)!t32;
            if (!t33) {
                goto gm_lb4;
            }
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_skp6qpvju2t5u));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(18));
            gm_f_runtime_text_37culso((void *)&gm_l9, (void *)NULL, cx, NULL, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t35 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
            if (t35) {
                gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                gm_l10 = t36;
            } else {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_b16_8 t37 = gm_l10;
            gm_l6 = t37;
            gm_b16_8 t38 = gm_l5;
            gm_b16_8 t39 = gm_l6;
            uint8_t t40 = gm_f_write_lldb_formatters_m0rjk7q((void *)NULL, cx, NULL, t38, t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t40;
            goto gm_fret;
        }
gm_lb4:;
    }
gm_lb3:;
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
__attribute__((aligned(4))) void gm_f_cached_program_5afqj27(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_cached_program_5afqj27 F;
    void * gm_l0; /* b */
    gm_b16_8 gm_l4; /* cache */
    gm_b16_8 gm_l5; /* dir */
    gm_b16_8 gm_l6; /* version */
    gm_b16_8 gm_l7; /* libs */
    gm_b16_8 gm_l11; /* stem */
    gm_b16_8 gm_l12; /* exe */
    gm_b16_8 gm_l13; /* tmp */
    uint8_t gm_l14; /* built */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    GM_STACK_CHECK(&F, 2568u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tr8dvlu9ik4gj gm_l18;
    uint8_t gm_l19;
    gm_ts1lka1v5g5ua gm_l20;
    gm_th2lqbo77pvsc gm_l21;
    gm_b16_8 gm_l22;
    gm_th2lqbo77pvsc gm_l23;
    gm_b16_8 gm_l24;
    gm_tabb710d9mq2e gm_l25;
    gm_b16_8 gm_l26;
    gm_tabb710d9mq2e gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_tl572d7579hns gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_ttsm01t0d8srg gm_l37;
    uint8_t t7 = (uint8_t)(((((uint8_t *)&F.l1) + 272))[120] != 0);
    if (t7) {
        memmove(((uint8_t *)&gm_l18), (((uint8_t *)&F.l1) + 272), 120);
    } else {
        gm_f_extras_joquqpb((void *)&gm_l18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l18), 120);
    void * t8 = (void *)((uint8_t *)&F.l2);
    uint8_t t9 = gm_f_is_empty_8l7niee(t8);
    uint8_t t10 = (uint8_t)!t9;
    if (t10) {
        gm_l19 = ((uint8_t)1);
    } else {
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 32));
        gm_l19 = t11;
    }
    uint8_t t12 = gm_l19;
    if (t12) {
        gm_tl572d7579hns t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t13 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t13 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)1ULL));
        memmove(gm_out, ((uint8_t *)&t13), 24);
        goto gm_fdone;
    }
    uint8_t t14 = (uint8_t)(((((uint8_t *)&F.l1) + 488))[56] != 0);
    if (t14) {
        memmove(((uint8_t *)&gm_l20), (((uint8_t *)&F.l1) + 488), 56);
    } else {
        gm_tl572d7579hns t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t15 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t15 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)1ULL));
        memmove(gm_out, ((uint8_t *)&t15), 24);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l20), 56);
    gm_f_cache_dir_81h90qu((void *)&gm_l21, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l21))[16] != 0);
    if (t16) {
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
        gm_l22 = t17;
    } else {
        gm_tl572d7579hns t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t18 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)1ULL));
        memmove(gm_out, ((uint8_t *)&t18), 24);
        goto gm_fdone;
    }
    gm_b16_8 t19 = gm_l22;
    gm_l4 = t19;
    gm_b16_8 t20 = gm_l4;
    gm_b16_8 t21;
    GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sn2furlv6e7jl));
    GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(3));
    gm_b16_8 t22 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t20, t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t22;
    void * t23 = (void *)((uint8_t *)&F.l3);
    gm_f_compiler_version_hj63jgf((void *)&gm_l23, (void *)NULL, cx, NULL, t23);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t24 = (uint8_t)((((uint8_t *)&gm_l23))[16] != 0);
    if (t24) {
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
        gm_l24 = t25;
    } else {
        gm_tl572d7579hns t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t26 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t26 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)1ULL));
        memmove(gm_out, ((uint8_t *)&t26), 24);
        goto gm_fdone;
    }
    gm_b16_8 t27 = gm_l24;
    gm_l6 = t27;
    void * t28 = gm_l0;
    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t28) + 192));
    gm_b16_8 t30;
    GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s0pi0emdqd3vb));
    GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(1));
    gm_f_join_sfknljb((void *)&gm_l25, (void *)NULL, cx, NULL, t29, t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
    if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 8, gm_srcqr4ch56kv6bv, 701, 13);
    gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
    gm_l7 = t32;
    gm_trqfvmjtt4sij t33;
    gm_f_list_epvducg((void *)&t33, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l8 = t33;
    void * t34 = (void *)((uint8_t *)&F.l8);
    gm_b16_8 t35;
    GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sbkvlhvo2a3ic));
    GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(6));
    void * t36 = gm_l0;
    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)t36) + 216));
    void * t38 = gm_l0;
    gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t38) + 232));
    void * t40 = gm_l0;
    gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)t40));
    gm_b16_8 t42 = gm_l7;
    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
    gm_t9qkh0rsooc8d t44 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l1) + 4));
    gm_b16_8 t45 = gm_f_mode_name_vpt4nrc(t44);
    uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 33));
    if (t46) {
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sk4cv563hhed7));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(1));
        gm_l26 = t47;
    } else {
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(1));
        gm_l26 = t48;
    }
    gm_b16_8 t49 = gm_l26;
    gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t51;
    GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s0pi0emdqd3vb));
    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(1));
    gm_f_join_sfknljb((void *)&gm_l27, (void *)NULL, cx, NULL, t50, t51);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
    if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_srcqr4ch56kv6bv, 703, 141);
    gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
    gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
    gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
    gm_b16_8 t56;
    GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s0pi0emdqd3vb));
    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(1));
    gm_f_join_sfknljb((void *)&gm_l28, (void *)NULL, cx, NULL, t55, t56);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
    if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srcqr4ch56kv6bv, 703, 165);
    gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
    gm_t142fel6vb03l t60;
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t35);
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 16), t37);
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 32), t39);
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 48), t41);
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 64), t43);
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 80), t45);
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 96), t49);
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 112), t54);
    GM_ST(gm_b16_8, ((uint8_t *)&t60 + 128), t59);
    F.l9 = t60;
    void * t61 = (void *)((uint8_t *)&F.l9);
    uint64_t t62 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t63 = (uintptr_t)((uint64_t)9ULL);
    if (t62 > t63 || t63 > (uintptr_t)((uint64_t)9ULL)) gm_trap_slice(t62, t63, ((uint64_t)9ULL), gm_srcqr4ch56kv6bv, 703, 18);
    uint64_t t64 = ((uintptr_t)((uint64_t)9ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t65;
    GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t61 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(t64));
    gm_tftb1odc739t5 t66 = gm_f_extend_vogsd72(t34, t65);
    gm_l29 = t66;
    uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
    if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcqr4ch56kv6bv, 703, 5);
    void * t68 = (void *)((uint8_t *)&F.l8);
    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
    gm_b16_8 t70;
    GM_ST(void *, (uint8_t *)&t70, (void *)(gm_s0pi0emdqd3vb));
    GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(1));
    gm_f_join_sfknljb((void *)&gm_l30, (void *)NULL, cx, NULL, t69, t70);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
    if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srcqr4ch56kv6bv, 704, 28);
    gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
    gm_b16_8 t74 = gm_l6;
    gm_b16_8 t75 = gm_f_target_text_s4sugoj((void *)NULL, cx, NULL, &(*(gm_t0rfe1ou14h21 *)(((uint8_t *)&F.l1) + 424)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t76 = gm_l0;
    gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)t76) + 192));
    gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
    gm_b16_8 t79 = gm_f_library_stamps_mjeqafa((void *)NULL, cx, NULL, t77, t78);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tdmi4fk9nhqa7 t80;
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t73);
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 16), t74);
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 32), t75);
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 48), t79);
    F.l10 = t80;
    void * t81 = (void *)((uint8_t *)&F.l10);
    uint64_t t82 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t83 = (uintptr_t)((uint64_t)4ULL);
    if (t82 > t83 || t83 > (uintptr_t)((uint64_t)4ULL)) gm_trap_slice(t82, t83, ((uint64_t)4ULL), gm_srcqr4ch56kv6bv, 704, 18);
    uint64_t t84 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t85;
    GM_ST(void *, (uint8_t *)&t85, (void *)(((uint8_t *)t81 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(t84));
    gm_tftb1odc739t5 t86 = gm_f_extend_vogsd72(t68, t85);
    gm_l31 = t86;
    uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
    if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcqr4ch56kv6bv, 704, 5);
    gm_b16_8 t88 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 424) + 24));
    gm_b16_8 t89;
    GM_ST(void *, (uint8_t *)&t89, (void *)(gm_svl13be5ca8rt));
    GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(4));
    uint8_t t90 = (gm_eq0iqiubfm7e(((uint8_t *)&t88), ((uint8_t *)&t89)));
    if (t90) {
        void * t91 = (void *)((uint8_t *)&F.l8);
        gm_b16_8 t92 = gm_f_wasm_opt_version_hapilm6((void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t93 = gm_f_push_3ef746b(t91, t92);
        gm_l32 = t93;
        uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
        if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcqr4ch56kv6bv, 705, 35);
    }
    gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
    gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
    gm_b16_8 t97 = gm_f_cache_key_tf6ebrd((void *)NULL, cx, NULL, t96);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t98;
    GM_ST(gm_b16_8, ((uint8_t *)&t98 + 0), t97);
    gm_f_format_jicque0((void *)&gm_l33, (void *)NULL, cx, NULL, t98);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
    if (!t99) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srcqr4ch56kv6bv, 706, 13);
    gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
    gm_l11 = t100;
    gm_b16_8 t101 = gm_l5;
    void * t102 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t103 = gm_l11;
    gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
    gm_b16_8 t105 = gm_f_native_exe_name_rqchv3n((void *)NULL, cx, NULL, t102, t104);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t106 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t101, t105);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t106;
    gm_b16_8 t107 = gm_l12;
    uint8_t t108 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t107);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t108) {
        gm_b16_8 t109 = gm_l12;
        gm_b0_1 t110 = gm_f_touch_frggs6u((void *)NULL, cx, NULL, t109);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t111 = gm_l12;
        gm_tl572d7579hns t112;
        GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t112 + 8), t111);
        memmove(gm_out, ((uint8_t *)&t112), 24);
        goto gm_fdone;
    }
    gm_b16_8 t113 = gm_l5;
    gm_f_make_cache_dirs_3bvvbb9((void *)&gm_l34, (void *)NULL, cx, NULL, t113);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
    if (t114) {
    } else {
        gm_tl572d7579hns t115;
        GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t115 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t115 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)1ULL));
        memmove(gm_out, ((uint8_t *)&t115), 24);
        goto gm_fdone;
    }
    gm_b16_8 t116 = gm_l5;
    gm_b16_8 t117 = gm_l11;
    gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
    gm_b16_8 t119;
    GM_ST(void *, (uint8_t *)&t119, (void *)(gm_s9o66vhtid5af));
    GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(3));
    gm_b16_8 t120 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t116, t118, t119);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l13 = t120;
    void * t121 = gm_l0;
    gm_b16_8 t122 = gm_l13;
    uint8_t t123 = gm_f_compile_built_8o9nmfu((void *)NULL, cx, NULL, t121, t122, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l14 = t123;
    gm_b16_8 t124 = gm_l13;
    gm_tnpifv20irnph t125;
    GM_ST(gm_b16_8, ((uint8_t *)&t125 + 0), t124);
    gm_f_format_cncadt9((void *)&gm_l35, (void *)NULL, cx, NULL, t125);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t126 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
    if (!t126) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_srcqr4ch56kv6bv, 716, 23);
    gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
    gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
    gm_ttln0d0hd70en t129 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t128);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l16 = ((uint64_t)1ULL);
    gm_l15 = ((uint64_t)8ULL);
    {
        for (;;) {
            uint64_t t130 = gm_l16;
            uint64_t t131 = gm_l15;
            uint8_t t132 = (t130 < t131);
            if (!t132) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t133 = gm_l16;
                        gm_l17 = t133;
                        gm_b16_8 t134 = gm_l13;
                        uint64_t t135 = gm_l17;
                        gm_t3877pk7lv748 t136;
                        GM_ST(gm_b16_8, ((uint8_t *)&t136 + 0), t134);
                        GM_ST(uint64_t, ((uint8_t *)&t136 + 16), t135);
                        gm_f_format_4hckftk((void *)&gm_l36, (void *)NULL, cx, NULL, &t136);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t137 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                        if (!t137) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_srcqr4ch56kv6bv, 717, 39);
                        gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                        gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
                        gm_ttln0d0hd70en t140 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t139);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 717, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t141 = gm_l16;
            uint64_t r142; bool o143 = __builtin_add_overflow(t141, ((uint64_t)1ULL), &r142);
            uint64_t t144 = r142;
            gm_l16 = t144;
        }
    }
gm_lb1:;
    uint8_t t145 = gm_l14;
    uint8_t t146 = (uint8_t)!t145;
    if (t146) {
        gm_b16_8 t147 = gm_l13;
        gm_ttln0d0hd70en t148 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t147);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl572d7579hns t149;
        GM_ST(uint8_t, (uint8_t *)&t149 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t149 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t149 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t149 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t149 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)2ULL));
        memmove(gm_out, ((uint8_t *)&t149), 24);
        goto gm_fdone;
    }
    gm_b16_8 t150 = gm_l13;
    gm_b16_8 t151 = gm_l12;
    gm_ttsm01t0d8srg t152 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t150, t151);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l37 = t152;
    uint8_t t153 = (uint8_t)((((uint8_t *)&gm_l37))[0] != 0);
    if (t153) {
    } else {
        gm_tl572d7579hns t154;
        GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t154 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t154 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)1ULL));
        memmove(gm_out, ((uint8_t *)&t154), 24);
        goto gm_fdone;
    }
    gm_b16_8 t155 = gm_l5;
    gm_b0_1 t156 = gm_f_prune_eu93ghe((void *)NULL, cx, NULL, t155);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t157 = gm_l12;
    gm_tl572d7579hns t158;
    GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t158 + 0), ((uint8_t)0ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t158 + 8), t157);
    memmove(gm_out, ((uint8_t *)&t158), 24);
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
__attribute__((aligned(4))) uint8_t gm_f_compile_built_8o9nmfu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_compile_built_8o9nmfu F;
    void * gm_l0; /* b */
    gm_b16_8 gm_l1; /* exe */
    void * gm_l3; /* generated */
    gm_b16_8 gm_l4; /* c_file */
    gm_t6j6smj7qoctu gm_l5; /* split */
    gm_b16_8 gm_l6; /* code */
    gm_b16_8 gm_l7; /* text */
    gm_b16_8 gm_l11; /* e */
    GM_STACK_CHECK(&F, 1390u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_tabb710d9mq2e gm_l12;
    gm_ttu09lij36f8c gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_b16_8 gm_l15;
    gm_ts1lka1v5g5ua gm_l16;
    gm_b16_8 gm_l17;
    gm_tr8dvlu9ik4gj gm_l18;
    gm_tllkbcpqeka9b gm_l19;
    gm_tk2lc304hfpci gm_l20;
    {
        void * t6 = gm_l0;
        void * t7 = (void *)((uint8_t *)t6);
        gm_l3 = t7;
        gm_b16_8 t8 = gm_l1;
        gm_tnpifv20irnph t9;
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t8);
        gm_f_format_cncadt9((void *)&gm_l12, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_srcqr4ch56kv6bv, 182, 15);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_l4 = t11;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 32));
        if (t12) {
            {
                {
                    void * t13 = gm_l0;
                    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 33));
                    gm_f_emitted_units_7f3nie0((void *)&gm_l13, (void *)NULL, cx, NULL, t13, t14);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l13))[32] != 0);
                    if (!t15) {
                        goto gm_lb3;
                    }
                    memmove(((uint8_t *)&gm_l5), ((uint8_t *)&gm_l13), 32);
                    void * t16 = gm_l0;
                    gm_b16_8 t17 = gm_l1;
                    uint8_t t18 = gm_f_write_units_hc5dqp8((void *)NULL, cx, NULL, t16, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l5)), t17);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t18;
                    goto gm_fret;
                }
gm_lb3:;
            }
gm_lb2:;
            void * t19 = gm_l0;
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t19) + 216));
            void * t21 = gm_l0;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 232));
            void * t23 = gm_l3;
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)t23));
            gm_tfeab279c16rr t25;
            GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t20);
            GM_ST(gm_b16_8, ((uint8_t *)&t25 + 16), t22);
            GM_ST(gm_b16_8, ((uint8_t *)&t25 + 32), t24);
            gm_f_format_i9dlnsg((void *)&gm_l14, (void *)NULL, cx, NULL, &t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcqr4ch56kv6bv, 186, 17);
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            gm_l6 = t27;
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 33));
            if (t28) {
                gm_b16_8 t29 = gm_l6;
                gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                gm_b16_8 t31 = gm_l4;
                gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                gm_b16_8 t33 = gm_f_absolute_6pqhsaj((void *)NULL, cx, NULL, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t34 = gm_f_resolve_lines_h4bf30f((void *)NULL, cx, NULL, t30, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l15 = t34;
            } else {
                gm_b16_8 t35 = gm_l6;
                gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                gm_l15 = t36;
            }
            gm_b16_8 t37 = gm_l15;
            gm_l7 = t37;
            gm_b16_8 t38 = gm_l4;
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            gm_b16_8 t40 = gm_l7;
            uint8_t t41 = gm_f_write_generated_c_kb09043((void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t41;
            goto gm_fret;
        }
        uint8_t t42 = (uint8_t)(((((uint8_t *)&F.l2) + 488))[56] != 0);
        if (t42) {
            memmove(((uint8_t *)&gm_l16), (((uint8_t *)&F.l2) + 488), 56);
        } else {
            uint8_t t43 = (uint8_t)(((((uint8_t *)&F.l2) + 552))[16] != 0);
            if (t43) {
                gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 552));
                gm_l17 = t44;
            } else {
                gm_b16_8 t45;
                GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(0));
                gm_l17 = t45;
            }
            gm_b16_8 t46 = gm_l17;
            if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_srcqr4ch56kv6bv, 191, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t46) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t46)), GM_LD(uintptr_t, ((uint8_t *)&t46) + 8), gm_srcqr4ch56kv6bv, 191, 9))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 191, 9)) {
                goto gm_lad;
            }
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        memmove(((uint8_t *)&F.l8), ((uint8_t *)&gm_l16), 56);
        uint8_t t47 = (uint8_t)(((((uint8_t *)&F.l2) + 272))[120] != 0);
        if (t47) {
            memmove(((uint8_t *)&gm_l18), (((uint8_t *)&F.l2) + 272), 120);
        } else {
            gm_f_extras_joquqpb((void *)&gm_l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        memmove(((uint8_t *)&F.l9), ((uint8_t *)&gm_l18), 120);
        gm_tk2lc304hfpci t48;
        gm_f_no_link_a2212mt((void *)&t48, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t48;
        void * t49 = (void *)((uint8_t *)&F.l9);
        uint8_t t50 = gm_f_is_empty_8l7niee(t49);
        uint8_t t51 = (uint8_t)!t50;
        if (t51) {
            void * t52 = (void *)((uint8_t *)&F.l9);
            void * t53 = (void *)((uint8_t *)&F.l8);
            gm_b16_8 t54 = gm_l1;
            gm_t9qkh0rsooc8d t55 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 4));
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t55) + 0));
            uint8_t t57 = (uint8_t)((t56 == ((uint8_t)0ULL)));
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 33));
            gm_t9qkh0rsooc8d t59;
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
            gm_f_package_link_q0071pm((void *)&gm_l19, (void *)NULL, cx, NULL, t52, t53, t54, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (t60) {
                memmove(((uint8_t *)&gm_l20), (((uint8_t *)&gm_l19) + 8), 112);
            } else {
                gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                gm_l11 = t61;
                gm_b16_8 t62 = gm_l11;
                if ((GM_LD(uintptr_t, ((uint8_t *)&t62) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t62)), GM_LD(uintptr_t, ((uint8_t *)&t62) + 8), gm_srcqr4ch56kv6bv, 198, 13))) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 198, 13)) {
                    goto gm_lad;
                }
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            memmove(((uint8_t *)&F.l10), ((uint8_t *)&gm_l20), 112);
        }
        void * t63 = gm_l0;
        void * t64 = (void *)((uint8_t *)&F.l8);
        void * t65 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t66 = gm_l1;
        uint8_t t67 = gm_f_compile_c_bkb1gkm((void *)NULL, cx, NULL, t63, t64, t65, t66, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_ld5_1;
        }
        F.ret = t67;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld5_1;
    }
    {
        void * t68 = (void *)((uint8_t *)&F.l10);
        gm_b0_1 t69 = gm_f_remove_objects_1vq49q8((void *)NULL, cx, NULL, t68);
    }
    goto gm_ls70;
gm_ld5_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t71 = (void *)((uint8_t *)&F.l10);
        gm_b0_1 t72 = gm_f_remove_objects_1vq49q8((void *)NULL, cx, NULL, t71);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls70:;
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
__attribute__((aligned(4))) uint8_t gm_f_compile_c_bkb1gkm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, const void * a4) {
    struct gm_fr_compile_c_bkb1gkm F;
    void * gm_l0; /* b */
    void * gm_l1; /* tc */
    void * gm_l2; /* link */
    gm_b16_8 gm_l3; /* exe */
    void * gm_l5; /* generated */
    gm_b16_8 gm_l6; /* opt */
    uint8_t gm_l7; /* debug */
    gm_tf5srh4mp4b3i gm_l8; /* forced */
    gm_b16_8 gm_l9; /* v */
    uint64_t gm_l10; /* cpus */
    uint64_t gm_l11; /* n */
    uint8_t gm_l12; /* separate */
    gm_th2lqbo77pvsc gm_l13; /* version */
    uint8_t gm_l14; /* lto */
    gm_th2lqbo77pvsc gm_l15; /* runtime */
    gm_th2lqbo77pvsc gm_l18; /* cached */
    gm_t6j6smj7qoctu gm_l19; /* split */
    gm_b16_8 gm_l21; /* objs */
    gm_b16_8 gm_l22; /* prelude */
    gm_th2lqbo77pvsc gm_l23; /* split_units */
    gm_b16_8 gm_l24; /* units */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_b16_8 gm_l27; /* u */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    gm_b16_8 gm_l32; /* src */
    gm_b16_8 gm_l33; /* path */
    gm_b16_8 gm_l34; /* text */
    uint8_t gm_l36; /* wasi */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* i */
    uint8_t gm_l43; /* ok */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_b16_8 gm_l46; /* o */
    gm_b16_8 gm_l48; /* <tmp> */
    uint64_t gm_l49; /* <tmp> */
    gm_b16_8 gm_l50; /* f */
    gm_b16_8 gm_l52; /* objs */
    gm_b16_8 gm_l53; /* r */
    uint8_t gm_l55; /* ok */
    gm_b16_8 gm_l56; /* <tmp> */
    uint64_t gm_l57; /* <tmp> */
    gm_b16_8 gm_l58; /* o */
    gm_b16_8 gm_l59; /* e */
    GM_STACK_CHECK(&F, 5018u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&F.l4, a4, sizeof F.l4);
    gm_th2lqbo77pvsc gm_l60;
    gm_tutnubt6u4hoq gm_l61;
    gm_tf5srh4mp4b3i gm_l62;
    uint8_t gm_l63;
    uint8_t gm_l64;
    gm_th2lqbo77pvsc gm_l65;
    uint8_t gm_l66;
    uint8_t gm_l67;
    gm_tftb1odc739t5 gm_l68;
    uint8_t gm_l69;
    gm_ttu09lij36f8c gm_l70;
    gm_tftb1odc739t5 gm_l71;
    gm_tftb1odc739t5 gm_l72;
    gm_tl572d7579hns gm_l73;
    uint8_t gm_l74;
    uint8_t gm_l75;
    uint8_t gm_l76;
    gm_tabb710d9mq2e gm_l77;
    gm_th2lqbo77pvsc gm_l78;
    gm_tabb710d9mq2e gm_l79;
    gm_tftb1odc739t5 gm_l80;
    uint8_t gm_l81;
    gm_tabb710d9mq2e gm_l82;
    gm_tftb1odc739t5 gm_l83;
    gm_tabb710d9mq2e gm_l84;
    gm_tftb1odc739t5 gm_l85;
    uint8_t gm_l86;
    gm_tf1bjg81u0glv gm_l87;
    gm_b16_8 gm_l88;
    gm_tftb1odc739t5 gm_l89;
    uint8_t gm_l90;
    gm_tabb710d9mq2e gm_l91;
    gm_tftb1odc739t5 gm_l92;
    gm_tftb1odc739t5 gm_l93;
    gm_tftb1odc739t5 gm_l94;
    gm_tftb1odc739t5 gm_l95;
    gm_tftb1odc739t5 gm_l96;
    uint8_t gm_l97;
    gm_tftb1odc739t5 gm_l98;
    gm_tftb1odc739t5 gm_l99;
    gm_trqfvmjtt4sij gm_l100;
    gm_tftb1odc739t5 gm_l101;
    gm_tftb1odc739t5 gm_l102;
    gm_tftb1odc739t5 gm_l103;
    gm_tftb1odc739t5 gm_l104;
    gm_tftb1odc739t5 gm_l105;
    gm_tftb1odc739t5 gm_l106;
    gm_tftb1odc739t5 gm_l107;
    gm_tftb1odc739t5 gm_l108;
    gm_trqfvmjtt4sij gm_l109;
    gm_tftb1odc739t5 gm_l110;
    gm_tftb1odc739t5 gm_l111;
    gm_tftb1odc739t5 gm_l112;
    gm_tl572d7579hns gm_l113;
    {
        void * t65 = gm_l0;
        void * t66 = (void *)((uint8_t *)t65);
        gm_l5 = t66;
        void * t67 = gm_l5;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)t67) + 208));
        if (t68) {
            void * t69 = gm_l0;
            void * t70 = gm_l1;
            gm_b16_8 t71 = gm_l3;
            uint8_t t72 = gm_f_compile_object_k7kbtpr((void *)NULL, cx, NULL, t69, t70, t71, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t72;
            goto gm_fret;
        }
        void * t73 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t74 = gm_f_opt_level_gnm5dcm(t73);
        gm_l6 = t74;
        gm_t9qkh0rsooc8d t75 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l4) + 4));
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&t75) + 0));
        uint8_t t77 = (uint8_t)((t76 == ((uint8_t)0ULL)));
        gm_t9qkh0rsooc8d t78;
        GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)0ULL));
        gm_l7 = t77;
        gm_tf5srh4mp4b3i t79;
        memset(&t79, 0, sizeof t79);
        gm_l8 = t79;
        {
            {
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sdahvc1997d01));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(13));
                gm_node *pn81 = gm_find(cx, 621690230);
                if (!pn81) gm_trap("no handler installed for effect `Env`", gm_srcqr4ch56kv6bv, 311, 13);
                gm_th2lqbo77pvsc t82;
                if (pn81->kind == GM_NATIVE) {
                    const uint8_t *ev; size_t evn;
                    bool evok = gm_env_var(GM_LD(void *, &t80), GM_LD(uintptr_t, (uint8_t *)&t80 + 8), &ev, &evn);
                    gm_b16_8 t83;
                    GM_ST(void *, (uint8_t *)&t83, (void *)(ev));
                    GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(evn));
                    gm_th2lqbo77pvsc t84;
                    GM_ST(uint8_t, (uint8_t *)&t84 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t84 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t84 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t84), t83);
                    ((uint8_t *)&t84)[16] = 1;
                    gm_th2lqbo77pvsc t85;
                    memset(&t85, 0, sizeof t85);
                    t82 = evok ? t84 : t85;
                } else {
                    ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn81->clauses[1])((void *)&t82, pn81->env, pn81->hctx, pn81, t80);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l60 = t82;
                uint8_t t86 = (uint8_t)((((uint8_t *)&gm_l60))[16] != 0);
                if (!t86) {
                    goto gm_lb2;
                }
                gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l60));
                gm_l9 = t87;
                gm_b16_8 t88 = gm_l9;
                gm_f_parse_int_3ce02np((void *)&gm_l61, t88);
                uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                if (t89) {
                    uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l61) + 8));
                    gm_tf5srh4mp4b3i t91;
                    GM_ST(uint8_t, (uint8_t *)&t91 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t91 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t91 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t91), t90);
                    ((uint8_t *)&t91)[8] = 1;
                    gm_l62 = t91;
                } else {
                    gm_tf5srh4mp4b3i t92;
                    memset(&t92, 0, sizeof t92);
                    gm_l62 = t92;
                }
                gm_tf5srh4mp4b3i t93 = gm_l62;
                gm_l8 = t93;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        uint32_t t94 = gm_f_cpu_count_0o5882g();
        uint64_t t95 = (uint64_t)t94;
        gm_l10 = t95;
        gm_l11 = ((uint64_t)1ULL);
        uint8_t t96 = gm_l7;
        if (t96) {
            void * t97 = gm_l5;
            void * t98 = (void *)(((uint8_t *)t97) + 16);
            uint64_t t99 = gm_l10;
            gm_tf5srh4mp4b3i t100 = gm_l8;
            uint64_t t101 = gm_f_unit_count_dk0d2e4(t98, t99, t100);
            gm_l11 = t101;
        } else {
            uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 424) + 56));
            uint8_t t103 = (uint8_t)!t102;
            if (t103) {
                void * t104 = gm_l5;
                void * t105 = (void *)(((uint8_t *)t104) + 16);
                uint64_t t106 = gm_l10;
                gm_tf5srh4mp4b3i t107 = gm_l8;
                uint64_t t108 = gm_f_optimized_unit_count_5mur81l(t105, t106, t107);
                gm_l11 = t108;
            }
        }
    }
    {
        uint8_t t109 = gm_l7;
        if (t109) {
            gm_l64 = ((uint8_t)1);
        } else {
            uint8_t t110 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 424) + 56));
            gm_l64 = t110;
        }
        uint8_t t111 = gm_l64;
        if (t111) {
            gm_l63 = ((uint8_t)1);
        } else {
            uint64_t t112 = gm_l11;
            uint8_t t113 = (t112 > ((uint64_t)1ULL));
            gm_l63 = t113;
        }
        uint8_t t114 = gm_l63;
        gm_l12 = t114;
        uint8_t t115 = gm_l12;
        if (t115) {
            void * t116 = gm_l1;
            gm_f_compiler_version_hj63jgf((void *)&gm_l65, (void *)NULL, cx, NULL, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        } else {
            gm_th2lqbo77pvsc t117;
            memset(&t117, 0, sizeof t117);
            gm_l65 = t117;
        }
        memmove(((uint8_t *)&gm_l13), ((uint8_t *)&gm_l65), 24);
        uint8_t t118 = gm_l7;
        uint8_t t119 = (uint8_t)!t118;
        if (t119) {
            uint64_t t120 = gm_l11;
            uint8_t t121 = (t120 > ((uint64_t)1ULL));
            gm_l67 = t121;
        } else {
            gm_l67 = ((uint8_t)0);
        }
        uint8_t t122 = gm_l67;
        if (t122) {
            void * t123 = gm_l1;
            void * t124 = (void *)((uint8_t *)&F.l4);
            uint8_t t125 = gm_f_thin_lto_klroq4v((void *)NULL, cx, NULL, t123, t124, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l13)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l66 = t125;
        } else {
            gm_l66 = ((uint8_t)0);
        }
        uint8_t t126 = gm_l66;
        gm_l14 = t126;
        gm_th2lqbo77pvsc t127;
        memset(&t127, 0, sizeof t127);
        gm_l15 = t127;
        uint8_t t128 = gm_l12;
        if (t128) {
            void * t129 = (void *)((uint8_t *)&F.l4);
            gm_trqfvmjtt4sij t130;
            gm_f_rt_flags_oeql34c((void *)&t130, (void *)NULL, cx, NULL, t129);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l16 = t130;
            uint8_t t131 = gm_l14;
            if (t131) {
                void * t132 = (void *)((uint8_t *)&F.l16);
                gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)gm_c302jkvduvb));
                gm_tftb1odc739t5 t134 = gm_f_push_3ef746b(t132, t133);
                gm_l68 = t134;
                uint8_t t135 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                if (!t135) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_srcqr4ch56kv6bv, 329, 18);
            }
            void * t136 = gm_l1;
            gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
            gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
            void * t139 = gm_l0;
            gm_b16_8 t140 = gm_f_runtime_source_skilk06((void *)NULL, cx, NULL, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t141;
            GM_ST(void *, (uint8_t *)&t141, (void *)(gm_smi2s23d2qqne));
            GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(5));
            gm_th2lqbo77pvsc t142 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l13));
            gm_f_runtime_object_oihv3ss((void *)&gm_l15, (void *)NULL, cx, NULL, t136, t138, t140, t141, &t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_trqfvmjtt4sij t143;
        gm_f_list_epvducg((void *)&t143, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l17 = t143;
    }
    {
        gm_th2lqbo77pvsc t144;
        memset(&t144, 0, sizeof t144);
        gm_l18 = t144;
        {
            {
                {
                    {
                        uint8_t t145 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                        if (t145) {
                            goto gm_lb6;
                        }
                        gm_l69 = ((uint8_t)0);
                        goto gm_lb5;
                    }
gm_lb6:;
                    {
                        gm_l69 = ((uint8_t)1);
                        goto gm_lb5;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 336, 8);
                }
gm_lb5:;
                uint8_t t146 = gm_l69;
                if (!t146) {
                    goto gm_lb4;
                }
                uint64_t t147 = gm_l11;
                uint8_t t148 = (t147 > ((uint64_t)1ULL));
                if (!t148) {
                    goto gm_lb4;
                }
                {
                    {
                        void * t149 = gm_l5;
                        void * t150 = (void *)(((uint8_t *)t149) + 16);
                        gm_f_split_by_file_hgjqgsp((void *)&gm_l70, (void *)NULL, cx, NULL, t150);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t151 = (uint8_t)((((uint8_t *)&gm_l70))[32] != 0);
                        if (!t151) {
                            goto gm_lb9;
                        }
                        memmove(((uint8_t *)&gm_l19), ((uint8_t *)&gm_l70), 32);
                        void * t152 = gm_l0;
                        void * t153 = (void *)((uint8_t *)&gm_l19);
                        gm_trqfvmjtt4sij t154;
                        gm_f_unit_sources_329det7((void *)&t154, (void *)NULL, cx, NULL, t152, t153);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l17 = t154;
                        void * t155 = (void *)((uint8_t *)&F.l4);
                        gm_trqfvmjtt4sij t156;
                        gm_f_build_flags_sn2j41p((void *)&t156, (void *)NULL, cx, NULL, t155, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l13)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l20 = t156;
                        void * t157 = (void *)((uint8_t *)&F.l20);
                        gm_b16_8 t158;
                        GM_ST(void *, (uint8_t *)&t158, (void *)(gm_sm8m7jldttgpj));
                        GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(37));
                        gm_tftb1odc739t5 t159 = gm_f_push_3ef746b(t157, t158);
                        gm_l71 = t159;
                        uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                        if (!t160) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 1, gm_srcqr4ch56kv6bv, 340, 13);
                        uint8_t t161 = gm_l14;
                        if (t161) {
                            void * t162 = (void *)((uint8_t *)&F.l20);
                            gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)gm_c302jkvduvb));
                            gm_tftb1odc739t5 t164 = gm_f_push_3ef746b(t162, t163);
                            gm_l72 = t164;
                            uint8_t t165 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                            if (!t165) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 1, gm_srcqr4ch56kv6bv, 341, 22);
                        }
                        void * t166 = gm_l1;
                        gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                        gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&t167));
                        gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                        gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&t169));
                        uint64_t t171 = gm_l10;
                        uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
                        gm_th2lqbo77pvsc t173 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l13));
                        gm_f_unit_objects_3f31tcd((void *)&gm_l73, (void *)NULL, cx, NULL, t166, t168, t170, &t173, t171, t172);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        {
                            {
                                uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&gm_l73) + 0));
                                uint8_t t175 = (uint8_t)(t174 == ((uint8_t)0ULL));
                                if (!t175) {
                                    goto gm_lb11;
                                }
                                gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l73) + 8));
                                gm_l21 = t176;
                                gm_b16_8 t177 = gm_l21;
                                gm_th2lqbo77pvsc t178;
                                GM_ST(uint8_t, (uint8_t *)&t178 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t178 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t178 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t178), t177);
                                ((uint8_t *)&t178)[16] = 1;
                                gm_l18 = t178;
                                goto gm_lb10;
                            }
gm_lb11:;
                            {
                                uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l73) + 0));
                                uint8_t t180 = (uint8_t)(t179 == ((uint8_t)2ULL));
                                if (!t180) {
                                    goto gm_lb12;
                                }
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
gm_lb12:;
                            {
                                uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l73) + 0));
                                uint8_t t182 = (uint8_t)(t181 == ((uint8_t)1ULL));
                                if (!t182) {
                                    goto gm_lb13;
                                }
                                goto gm_lb10;
                            }
gm_lb13:;
                            gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 342, 13);
                        }
gm_lb10:;
                        goto gm_lb8;
                    }
gm_lb9:;
                }
gm_lb8:;
                goto gm_lb3;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    {
        {
            {
                uint8_t t183 = (uint8_t)((((uint8_t *)&gm_l18))[16] != 0);
                if (t183) {
                    goto gm_lb15;
                }
                gm_l75 = ((uint8_t)0);
                goto gm_lb14;
            }
gm_lb15:;
            {
                gm_l75 = ((uint8_t)1);
                goto gm_lb14;
            }
gm_lb16:;
            gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 349, 8);
        }
gm_lb14:;
        uint8_t t184 = gm_l75;
        if (t184) {
            gm_l74 = ((uint8_t)1);
        } else {
            uint64_t t185 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
            uint8_t t186 = (t185 > ((uint64_t)0ULL));
            gm_l74 = t186;
        }
        uint8_t t187 = gm_l74;
        if (t187) {
        } else {
            {
                {
                    uint8_t t188 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                    if (t188) {
                        goto gm_lb18;
                    }
                    gm_l76 = ((uint8_t)0);
                    goto gm_lb17;
                }
gm_lb18:;
                {
                    gm_l76 = ((uint8_t)1);
                    goto gm_lb17;
                }
gm_lb19:;
                gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 351, 15);
            }
gm_lb17:;
            uint8_t t189 = gm_l76;
            if (t189) {
                gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)gm_c69snm9duhi));
                void * t191 = gm_l0;
                gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)t191) + 216));
                gm_t6j6smj7qoctu t193;
                GM_ST(gm_b16_8, ((uint8_t *)&t193 + 0), t190);
                GM_ST(gm_b16_8, ((uint8_t *)&t193 + 16), t192);
                gm_f_format_q8i7gsb((void *)&gm_l77, (void *)NULL, cx, NULL, &t193);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t194 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                if (!t194) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 8, gm_srcqr4ch56kv6bv, 352, 20);
                gm_b16_8 t195 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l77) + 8));
                gm_l22 = t195;
                uint64_t t196 = gm_l11;
                uint8_t t197 = (t196 > ((uint64_t)1ULL));
                if (t197) {
                    void * t198 = gm_l5;
                    void * t199 = (void *)(((uint8_t *)t198) + 16);
                    uint64_t t200 = gm_l11;
                    gm_f_split_jendjdm((void *)&gm_l78, (void *)NULL, cx, NULL, t199, t200);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                } else {
                    gm_th2lqbo77pvsc t201;
                    memset(&t201, 0, sizeof t201);
                    gm_l78 = t201;
                }
                memmove(((uint8_t *)&gm_l23), ((uint8_t *)&gm_l78), 24);
                {
                    {
                        uint8_t t202 = (uint8_t)((((uint8_t *)&gm_l23))[16] != 0);
                        if (!t202) {
                            goto gm_lb21;
                        }
                        gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                        gm_l24 = t203;
                        gm_b16_8 t204 = gm_l24;
                        gm_l25 = t204;
                        gm_l26 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t205 = gm_l26;
                                uint64_t t206 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                uint8_t t207 = (t205 < t206);
                                if (!t207) {
                                    goto gm_lb22;
                                }
                                {
                                    void * t208 = GM_LD(void *, ((uint8_t *)&gm_l25));
                                    uint64_t t209 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                    uint64_t t210 = gm_l26;
                                    {
                                        {
                                            gm_b16_8 t211 = GM_LD(gm_b16_8, (((uint8_t *)t208) + (uintptr_t)t210 * 16));
                                            gm_l27 = t211;
                                            void * t212 = (void *)((uint8_t *)&F.l17);
                                            gm_b16_8 t213 = gm_l22;
                                            gm_b16_8 t214 = gm_l27;
                                            gm_t6j6smj7qoctu t215;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t215 + 0), t213);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t215 + 16), t214);
                                            gm_f_format_5029eea((void *)&gm_l79, (void *)NULL, cx, NULL, &t215);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t216 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
                                            if (!t216) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l79) + 8, gm_srcqr4ch56kv6bv, 355, 43);
                                            gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l79) + 8));
                                            gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
                                            gm_tftb1odc739t5 t219 = gm_f_push_3ef746b(t212, t218);
                                            gm_l80 = t219;
                                            uint8_t t220 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                                            if (!t220) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 1, gm_srcqr4ch56kv6bv, 355, 30);
                                            goto gm_lb25;
                                        }
gm_lb26:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 355, 13);
                                    }
gm_lb25:;
                                }
gm_lb24:;
                                uint64_t t221 = gm_l26;
                                uint64_t r222; bool o223 = __builtin_add_overflow(t221, ((uint64_t)1ULL), &r222);
                                uint64_t t224 = r222;
                                gm_l26 = t224;
                            }
                        }
gm_lb22:;
                        goto gm_lb20;
                    }
gm_lb21:;
                    {
                        {
                            uint64_t t225 = gm_l11;
                            uint8_t t226 = (t225 > ((uint64_t)1ULL));
                            if (!t226) {
                                goto gm_lb28;
                            }
                            {
                                {
                                    uint8_t t227 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                                    if (t227) {
                                        goto gm_lb30;
                                    }
                                    gm_l81 = ((uint8_t)0);
                                    goto gm_lb29;
                                }
gm_lb30:;
                                {
                                    gm_l81 = ((uint8_t)1);
                                    goto gm_lb29;
                                }
gm_lb31:;
                                gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 357, 26);
                            }
gm_lb29:;
                            uint8_t t228 = gm_l81;
                            if (!t228) {
                                goto gm_lb28;
                            }
                            if (gm_pf454qfrpt6r(cx, gm_sad95m298up5o, 43, gm_srcqr4ch56kv6bv, 358, 17)) {
                                goto gm_lad;
                            }
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
gm_lb28:;
                    }
gm_lb27:;
                    void * t229 = (void *)((uint8_t *)&F.l17);
                    gm_b16_8 t230 = gm_l22;
                    void * t231 = gm_l5;
                    gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)t231));
                    gm_t6j6smj7qoctu t233;
                    GM_ST(gm_b16_8, ((uint8_t *)&t233 + 0), t230);
                    GM_ST(gm_b16_8, ((uint8_t *)&t233 + 16), t232);
                    gm_f_format_5029eea((void *)&gm_l82, (void *)NULL, cx, NULL, &t233);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t234 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
                    if (!t234) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l82) + 8, gm_srcqr4ch56kv6bv, 361, 26);
                    gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l82) + 8));
                    gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
                    gm_tftb1odc739t5 t237 = gm_f_push_3ef746b(t229, t236);
                    gm_l83 = t237;
                    uint8_t t238 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
                    if (!t238) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l83) + 1, gm_srcqr4ch56kv6bv, 361, 13);
                }
gm_lb20:;
            } else {
                void * t239 = (void *)((uint8_t *)&F.l17);
                void * t240 = gm_l0;
                gm_b16_8 t241 = GM_LD(gm_b16_8, (((uint8_t *)t240) + 216));
                void * t242 = gm_l0;
                gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)t242) + 232));
                void * t244 = gm_l5;
                gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)t244));
                gm_tfeab279c16rr t246;
                GM_ST(gm_b16_8, ((uint8_t *)&t246 + 0), t241);
                GM_ST(gm_b16_8, ((uint8_t *)&t246 + 16), t243);
                GM_ST(gm_b16_8, ((uint8_t *)&t246 + 32), t245);
                gm_f_format_i9dlnsg((void *)&gm_l84, (void *)NULL, cx, NULL, &t246);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t247 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
                if (!t247) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l84) + 8, gm_srcqr4ch56kv6bv, 364, 22);
                gm_b16_8 t248 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l84) + 8));
                gm_b16_8 t249 = GM_LD(gm_b16_8, ((uint8_t *)&t248));
                gm_tftb1odc739t5 t250 = gm_f_push_3ef746b(t239, t249);
                gm_l85 = t250;
                uint8_t t251 = (uint8_t)!((((uint8_t *)&gm_l85))[0] != 0);
                if (!t251) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l85) + 1, gm_srcqr4ch56kv6bv, 364, 9);
            }
        }
    }
    {
        gm_trqfvmjtt4sij t252;
        gm_f_list_epvducg((void *)&t252, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l28 = t252;
        {
            {
                uint8_t t253 = (uint8_t)((((uint8_t *)&gm_l18))[16] != 0);
                if (t253) {
                    goto gm_lb33;
                }
                gm_l86 = ((uint8_t)0);
                goto gm_lb32;
            }
gm_lb33:;
            {
                gm_l86 = ((uint8_t)1);
                goto gm_lb32;
            }
gm_lb34:;
            gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 367, 8);
        }
gm_lb32:;
        uint8_t t254 = gm_l86;
        if (t254) {
            void * t255 = (void *)((uint8_t *)&F.l17);
            gm_f_clear_dvdem7p(t255);
        }
        gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
        gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&t256));
        gm_l29 = t257;
        gm_l30 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t258 = gm_l30;
                uint64_t t259 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                uint8_t t260 = (t258 < t259);
                if (!t260) {
                    goto gm_lb35;
                }
                {
                    uint64_t t261 = gm_l30;
                    void * t262 = GM_LD(void *, ((uint8_t *)&gm_l29));
                    uint64_t t263 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                    uint64_t t264 = gm_l30;
                    gm_b16_8 t265 = GM_LD(gm_b16_8, (((uint8_t *)t262) + (uintptr_t)t264 * 16));
                    gm_tf1bjg81u0glv t266;
                    GM_ST(uint64_t, ((uint8_t *)&t266 + 0), t261);
                    GM_ST(gm_b16_8, ((uint8_t *)&t266 + 8), t265);
                    gm_l87 = t266;
                    {
                        {
                            uint64_t t267 = GM_LD(uint64_t, ((uint8_t *)&gm_l87));
                            gm_l31 = t267;
                            gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l87) + 8));
                            gm_l32 = t268;
                            gm_b16_8 t269 = gm_l3;
                            uint64_t t270 = gm_l31;
                            gm_b16_8 t271 = gm_f_unit_path_3tdv4sg((void *)NULL, cx, NULL, t269, t270);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l33 = t271;
                            uint8_t t272 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
                            if (t272) {
                                gm_b16_8 t273 = gm_l32;
                                gm_b16_8 t274 = gm_l33;
                                gm_b16_8 t275 = gm_f_absolute_6pqhsaj((void *)NULL, cx, NULL, t274);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t276 = gm_f_resolve_lines_h4bf30f((void *)NULL, cx, NULL, t273, t275);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l88 = t276;
                            } else {
                                gm_b16_8 t277 = gm_l32;
                                gm_l88 = t277;
                            }
                            gm_b16_8 t278 = gm_l88;
                            gm_l34 = t278;
                            gm_b16_8 t279 = gm_l33;
                            gm_b16_8 t280 = gm_l34;
                            uint8_t t281 = gm_f_write_generated_c_kb09043((void *)NULL, cx, NULL, t279, t280);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t282 = (uint8_t)!t281;
                            if (t282) {
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            void * t283 = (void *)((uint8_t *)&F.l28);
                            gm_b16_8 t284 = gm_l33;
                            gm_tftb1odc739t5 t285 = gm_f_push_3ef746b(t283, t284);
                            gm_l89 = t285;
                            uint8_t t286 = (uint8_t)!((((uint8_t *)&gm_l89))[0] != 0);
                            if (!t286) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l89) + 1, gm_srcqr4ch56kv6bv, 373, 9);
                            goto gm_lb38;
                        }
gm_lb39:;
                        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 368, 5);
                    }
gm_lb38:;
                }
gm_lb37:;
                uint64_t t287 = gm_l30;
                uint64_t r288; bool o289 = __builtin_add_overflow(t287, ((uint64_t)1ULL), &r288);
                uint64_t t290 = r288;
                gm_l30 = t290;
            }
        }
gm_lb35:;
    }
    {
        void * t291 = (void *)((uint8_t *)&F.l4);
        gm_th2lqbo77pvsc t292 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l13));
        gm_f_build_flags_sn2j41p((void *)&F.l35, (void *)NULL, cx, NULL, t291, &t292);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t293 = gm_f_wasi_target_lp6g335(&(*(gm_t0rfe1ou14h21 *)(((uint8_t *)&F.l4) + 424)));
        gm_l36 = t293;
        gm_trqfvmjtt4sij t294;
        gm_f_list_epvducg((void *)&t294, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l37 = t294;
        gm_trqfvmjtt4sij t295;
        gm_f_list_epvducg((void *)&t295, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l38 = t295;
        uint64_t t296 = GM_LD(uint64_t, (((uint8_t *)&F.l28) + 8));
        uint8_t t297 = (t296 > ((uint64_t)1ULL));
        if (t297) {
            gm_l90 = ((uint8_t)1);
        } else {
            uint8_t t298 = gm_l36;
            gm_l90 = t298;
        }
        uint8_t t299 = gm_l90;
        if (t299) {
            gm_l40 = ((uint64_t)0ULL);
            uint64_t t300 = GM_LD(uint64_t, (((uint8_t *)&F.l28) + 8));
            gm_l39 = t300;
            {
                for (;;) {
                    uint64_t t301 = gm_l40;
                    uint64_t t302 = gm_l39;
                    uint8_t t303 = (t301 < t302);
                    if (!t303) {
                        goto gm_lb40;
                    }
                    {
                        {
                            {
                                uint64_t t304 = gm_l40;
                                gm_l41 = t304;
                                void * t305 = (void *)((uint8_t *)&F.l38);
                                gm_b16_8 t306 = gm_l3;
                                uint64_t t307 = gm_l41;
                                gm_b16_8 t308 = gm_f_pid_token_c444nkv((void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tq3m5qgvkpje8 t309;
                                GM_ST(gm_b16_8, ((uint8_t *)&t309 + 0), t306);
                                GM_ST(uint64_t, ((uint8_t *)&t309 + 16), t307);
                                GM_ST(gm_b16_8, ((uint8_t *)&t309 + 24), t308);
                                gm_f_format_kdp6nh1((void *)&gm_l91, (void *)NULL, cx, NULL, &t309);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t310 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                                if (!t310) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 8, gm_srcqr4ch56kv6bv, 383, 54);
                                gm_b16_8 t311 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l91) + 8));
                                gm_b16_8 t312 = GM_LD(gm_b16_8, ((uint8_t *)&t311));
                                gm_tftb1odc739t5 t313 = gm_f_push_3ef746b(t305, t312);
                                gm_l92 = t313;
                                uint8_t t314 = (uint8_t)!((((uint8_t *)&gm_l92))[0] != 0);
                                if (!t314) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l92) + 1, gm_srcqr4ch56kv6bv, 383, 41);
                                goto gm_lb43;
                            }
gm_lb44:;
                            gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 383, 9);
                        }
gm_lb43:;
                    }
gm_lb42:;
                    uint64_t t315 = gm_l40;
                    uint64_t r316; bool o317 = __builtin_add_overflow(t315, ((uint64_t)1ULL), &r316);
                    uint64_t t318 = r316;
                    gm_l40 = t318;
                }
            }
gm_lb40:;
            gm_trqfvmjtt4sij t319;
            gm_f_list_epvducg((void *)&t319, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.l42 = t319;
            void * t320 = (void *)((uint8_t *)&F.l42);
            gm_b16_8 t321 = GM_LD(gm_b16_8, ((uint8_t *)&F.l35));
            gm_b16_8 t322 = GM_LD(gm_b16_8, ((uint8_t *)&t321));
            gm_tftb1odc739t5 t323 = gm_f_extend_vogsd72(t320, t322);
            gm_l93 = t323;
            uint8_t t324 = (uint8_t)!((((uint8_t *)&gm_l93))[0] != 0);
            if (!t324) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l93) + 1, gm_srcqr4ch56kv6bv, 387, 9);
            void * t325 = (void *)((uint8_t *)&F.l42);
            gm_b16_8 t326;
            GM_ST(void *, (uint8_t *)&t326, (void *)(gm_sm8m7jldttgpj));
            GM_ST(uintptr_t, (uint8_t *)&t326 + 8, (uintptr_t)(37));
            gm_tftb1odc739t5 t327 = gm_f_push_3ef746b(t325, t326);
            gm_l94 = t327;
            uint8_t t328 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
            if (!t328) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l94) + 1, gm_srcqr4ch56kv6bv, 388, 9);
            void * t329 = gm_l1;
            gm_b16_8 t330 = GM_LD(gm_b16_8, ((uint8_t *)&F.l42));
            gm_b16_8 t331 = GM_LD(gm_b16_8, ((uint8_t *)&t330));
            gm_b16_8 t332 = GM_LD(gm_b16_8, ((uint8_t *)&F.l28));
            gm_b16_8 t333 = GM_LD(gm_b16_8, ((uint8_t *)&t332));
            gm_b16_8 t334 = GM_LD(gm_b16_8, ((uint8_t *)&F.l38));
            gm_b16_8 t335 = GM_LD(gm_b16_8, ((uint8_t *)&t334));
            uint64_t t336 = gm_l10;
            uint8_t t337 = gm_f_compile_objects_mu5secm((void *)NULL, cx, NULL, t329, t331, t333, t335, t336);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l43 = t337;
            uint8_t t338 = gm_l43;
            uint8_t t339 = (uint8_t)!t338;
            if (t339) {
                gm_b16_8 t340 = GM_LD(gm_b16_8, ((uint8_t *)&F.l38));
                gm_l44 = t340;
                gm_l45 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t341 = gm_l45;
                        uint64_t t342 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                        uint8_t t343 = (t341 < t342);
                        if (!t343) {
                            goto gm_lb45;
                        }
                        {
                            void * t344 = GM_LD(void *, ((uint8_t *)&gm_l44));
                            uint64_t t345 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                            uint64_t t346 = gm_l45;
                            {
                                {
                                    gm_b16_8 t347 = GM_LD(gm_b16_8, (((uint8_t *)t344) + (uintptr_t)t346 * 16));
                                    gm_l46 = t347;
                                    gm_b16_8 t348 = gm_l46;
                                    gm_ttln0d0hd70en t349 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t348);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb48;
                                }
gm_lb49:;
                                gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 391, 13);
                            }
gm_lb48:;
                        }
gm_lb47:;
                        uint64_t t350 = gm_l45;
                        uint64_t r351; bool o352 = __builtin_add_overflow(t350, ((uint64_t)1ULL), &r351);
                        uint64_t t353 = r351;
                        gm_l45 = t353;
                    }
                }
gm_lb45:;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t354 = (void *)((uint8_t *)&F.l37);
            gm_b16_8 t355 = GM_LD(gm_b16_8, ((uint8_t *)&F.l38));
            gm_b16_8 t356 = GM_LD(gm_b16_8, ((uint8_t *)&t355));
            gm_tftb1odc739t5 t357 = gm_f_extend_vogsd72(t354, t356);
            gm_l95 = t357;
            uint8_t t358 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
            if (!t358) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l95) + 1, gm_srcqr4ch56kv6bv, 394, 9);
        } else {
            void * t359 = (void *)((uint8_t *)&F.l37);
            gm_b16_8 t360 = GM_LD(gm_b16_8, ((uint8_t *)&F.l28));
            gm_b16_8 t361 = GM_LD(gm_b16_8, ((uint8_t *)&t360));
            gm_tftb1odc739t5 t362 = gm_f_extend_vogsd72(t359, t361);
            gm_l96 = t362;
            uint8_t t363 = (uint8_t)!((((uint8_t *)&gm_l96))[0] != 0);
            if (!t363) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l96) + 1, gm_srcqr4ch56kv6bv, 396, 9);
        }
    }
    {
        gm_trqfvmjtt4sij t364;
        gm_f_list_epvducg((void *)&t364, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l47 = t364;
        gm_b16_8 t365 = GM_LD(gm_b16_8, ((uint8_t *)&F.l35));
        gm_l48 = t365;
        gm_l49 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t366 = gm_l49;
                uint64_t t367 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                uint8_t t368 = (t366 < t367);
                if (!t368) {
                    goto gm_lb50;
                }
                {
                    void * t369 = GM_LD(void *, ((uint8_t *)&gm_l48));
                    uint64_t t370 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                    uint64_t t371 = gm_l49;
                    {
                        {
                            gm_b16_8 t372 = GM_LD(gm_b16_8, (((uint8_t *)t369) + (uintptr_t)t371 * 16));
                            gm_l50 = t372;
                            uint8_t t373 = gm_l36;
                            if (t373) {
                                gm_b16_8 t374 = gm_l50;
                                gm_b16_8 t375 = gm_l6;
                                uint8_t t376 = (gm_eq0iqiubfm7e(((uint8_t *)&t374), ((uint8_t *)&t375)));
                                gm_l97 = t376;
                            } else {
                                gm_l97 = ((uint8_t)0);
                            }
                            uint8_t t377 = gm_l97;
                            uint8_t t378 = (uint8_t)!t377;
                            if (t378) {
                                void * t379 = (void *)((uint8_t *)&F.l47);
                                gm_b16_8 t380 = gm_l50;
                                gm_tftb1odc739t5 t381 = gm_f_push_3ef746b(t379, t380);
                                gm_l98 = t381;
                                uint8_t t382 = (uint8_t)!((((uint8_t *)&gm_l98))[0] != 0);
                                if (!t382) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l98) + 1, gm_srcqr4ch56kv6bv, 400, 38);
                            }
                            goto gm_lb53;
                        }
gm_lb54:;
                        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 399, 5);
                    }
gm_lb53:;
                }
gm_lb52:;
                uint64_t t383 = gm_l49;
                uint64_t r384; bool o385 = __builtin_add_overflow(t383, ((uint64_t)1ULL), &r384);
                uint64_t t386 = r384;
                gm_l49 = t386;
            }
        }
gm_lb50:;
        uint8_t t387 = gm_l36;
        if (t387) {
            void * t388 = (void *)((uint8_t *)&F.l47);
            gm_b16_8 t389 = gm_f_wasi_link_flags_6mu198c((void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t390 = gm_f_extend_vogsd72(t388, t389);
            gm_l99 = t390;
            uint8_t t391 = (uint8_t)!((((uint8_t *)&gm_l99))[0] != 0);
            if (!t391) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l99) + 1, gm_srcqr4ch56kv6bv, 402, 15);
        }
        uint8_t t392 = gm_l14;
        if (t392) {
            void * t393 = (void *)((uint8_t *)&F.l47);
            gm_f_lto_link_flags_0rmh9d3((void *)&gm_l100, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t394 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l100));
            gm_b16_8 t395 = GM_LD(gm_b16_8, ((uint8_t *)&t394));
            gm_tftb1odc739t5 t396 = gm_f_extend_vogsd72(t393, t395);
            gm_l101 = t396;
            uint8_t t397 = (uint8_t)!((((uint8_t *)&gm_l101))[0] != 0);
            if (!t397) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l101) + 1, gm_srcqr4ch56kv6bv, 403, 14);
        }
        gm_trqfvmjtt4sij t398;
        gm_f_list_epvducg((void *)&t398, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l51 = t398;
        void * t399 = (void *)((uint8_t *)&F.l51);
        gm_b16_8 t400;
        GM_ST(void *, (uint8_t *)&t400, (void *)(gm_s65qt8qo64cje));
        GM_ST(uintptr_t, (uint8_t *)&t400 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t401 = gm_f_push_3ef746b(t399, t400);
        gm_l102 = t401;
    }
    {
        uint8_t t402 = (uint8_t)!((((uint8_t *)&gm_l102))[0] != 0);
        if (!t402) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l102) + 1, gm_srcqr4ch56kv6bv, 405, 5);
        void * t403 = (void *)((uint8_t *)&F.l51);
        gm_b16_8 t404 = gm_l3;
        gm_tftb1odc739t5 t405 = gm_f_push_3ef746b(t403, t404);
        gm_l103 = t405;
        uint8_t t406 = (uint8_t)!((((uint8_t *)&gm_l103))[0] != 0);
        if (!t406) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l103) + 1, gm_srcqr4ch56kv6bv, 406, 5);
        void * t407 = (void *)((uint8_t *)&F.l51);
        gm_b16_8 t408 = GM_LD(gm_b16_8, ((uint8_t *)&F.l37));
        gm_b16_8 t409 = GM_LD(gm_b16_8, ((uint8_t *)&t408));
        gm_tftb1odc739t5 t410 = gm_f_extend_vogsd72(t407, t409);
        gm_l104 = t410;
        uint8_t t411 = (uint8_t)!((((uint8_t *)&gm_l104))[0] != 0);
        if (!t411) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l104) + 1, gm_srcqr4ch56kv6bv, 407, 5);
        {
            {
                uint8_t t412 = (uint8_t)((((uint8_t *)&gm_l18))[16] != 0);
                if (!t412) {
                    goto gm_lb56;
                }
                gm_b16_8 t413 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                gm_l52 = t413;
                void * t414 = (void *)((uint8_t *)&F.l51);
                gm_b16_8 t415 = gm_l52;
                gm_tftb1odc739t5 t416 = gm_f_extend_vogsd72(t414, t415);
                gm_l105 = t416;
                uint8_t t417 = (uint8_t)!((((uint8_t *)&gm_l105))[0] != 0);
                if (!t417) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l105) + 1, gm_srcqr4ch56kv6bv, 408, 25);
                goto gm_lb55;
            }
gm_lb56:;
        }
gm_lb55:;
        {
            {
                uint8_t t418 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                if (!t418) {
                    goto gm_lb58;
                }
                gm_b16_8 t419 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                gm_l53 = t419;
                void * t420 = (void *)((uint8_t *)&F.l51);
                gm_b16_8 t421 = gm_l53;
                gm_tftb1odc739t5 t422 = gm_f_push_3ef746b(t420, t421);
                gm_l106 = t422;
                uint8_t t423 = (uint8_t)!((((uint8_t *)&gm_l106))[0] != 0);
                if (!t423) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l106) + 1, gm_srcqr4ch56kv6bv, 409, 23);
                goto gm_lb57;
            }
gm_lb58:;
        }
gm_lb57:;
        void * t424 = (void *)((uint8_t *)&F.l51);
        void * t425 = gm_l2;
        gm_b16_8 t426 = GM_LD(gm_b16_8, ((uint8_t *)t425));
        gm_b16_8 t427 = GM_LD(gm_b16_8, ((uint8_t *)&t426));
        gm_tftb1odc739t5 t428 = gm_f_extend_vogsd72(t424, t427);
        gm_l107 = t428;
        uint8_t t429 = (uint8_t)!((((uint8_t *)&gm_l107))[0] != 0);
        if (!t429) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l107) + 1, gm_srcqr4ch56kv6bv, 410, 5);
        void * t430 = (void *)((uint8_t *)&F.l51);
        void * t431 = gm_l1;
        gm_b16_8 t432 = GM_LD(gm_b16_8, (((uint8_t *)t431) + 32));
        gm_tftb1odc739t5 t433 = gm_f_extend_vogsd72(t430, t432);
        gm_l108 = t433;
        uint8_t t434 = (uint8_t)!((((uint8_t *)&gm_l108))[0] != 0);
        if (!t434) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l108) + 1, gm_srcqr4ch56kv6bv, 411, 5);
        void * t435 = (void *)((uint8_t *)&F.l51);
        void * t436 = gm_l2;
        void * t437 = gm_l5;
        gm_b16_8 t438 = GM_LD(gm_b16_8, (((uint8_t *)t437) + 192));
        gm_f_library_args_lijr5kp((void *)&gm_l109, (void *)NULL, cx, NULL, t436, t438);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t439 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l109));
        gm_b16_8 t440 = GM_LD(gm_b16_8, ((uint8_t *)&t439));
        gm_tftb1odc739t5 t441 = gm_f_extend_vogsd72(t435, t440);
        gm_l110 = t441;
    }
    {
        uint8_t t442 = (uint8_t)!((((uint8_t *)&gm_l110))[0] != 0);
        if (!t442) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l110) + 1, gm_srcqr4ch56kv6bv, 412, 5);
        void * t443 = (void *)((uint8_t *)&F.l51);
        gm_b16_8 t444;
        GM_ST(void *, (uint8_t *)&t444, (void *)(gm_s6077qnlc299h));
        GM_ST(uintptr_t, (uint8_t *)&t444 + 8, (uintptr_t)(3));
        gm_tftb1odc739t5 t445 = gm_f_push_3ef746b(t443, t444);
        gm_l111 = t445;
        uint8_t t446 = (uint8_t)!((((uint8_t *)&gm_l111))[0] != 0);
        if (!t446) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l111) + 1, gm_srcqr4ch56kv6bv, 413, 5);
        void * t447 = (void *)((uint8_t *)&F.l51);
        gm_b16_8 t448 = gm_f_windows_libs_hmrgs9s((void *)NULL, cx, NULL, &(*(gm_t0rfe1ou14h21 *)(((uint8_t *)&F.l4) + 424)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t449 = gm_f_extend_vogsd72(t447, t448);
        gm_l112 = t449;
        uint8_t t450 = (uint8_t)!((((uint8_t *)&gm_l112))[0] != 0);
        if (!t450) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l112) + 1, gm_srcqr4ch56kv6bv, 414, 5);
        void * t451 = gm_l1;
        gm_b16_8 t452 = GM_LD(gm_b16_8, ((uint8_t *)&F.l47));
        gm_b16_8 t453 = GM_LD(gm_b16_8, ((uint8_t *)&t452));
        gm_b16_8 t454 = GM_LD(gm_b16_8, ((uint8_t *)&F.l51));
        gm_b16_8 t455 = GM_LD(gm_b16_8, ((uint8_t *)&t454));
        gm_f_command_b2u33fl((void *)&F.l54, (void *)NULL, cx, NULL, t451, t453, t455);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t456 = gm_l1;
        gm_b16_8 t457 = GM_LD(gm_b16_8, ((uint8_t *)&F.l54));
        gm_b16_8 t458 = GM_LD(gm_b16_8, ((uint8_t *)&t457));
        uint8_t t459 = gm_f_run_compiler_ulaqcb1((void *)NULL, cx, NULL, t456, t458);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l55 = t459;
        gm_b16_8 t460 = GM_LD(gm_b16_8, ((uint8_t *)&F.l38));
        gm_l56 = t460;
        gm_l57 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t461 = gm_l57;
                uint64_t t462 = GM_LD(uint64_t, (((uint8_t *)&gm_l56) + 8));
                uint8_t t463 = (t461 < t462);
                if (!t463) {
                    goto gm_lb59;
                }
                {
                    void * t464 = GM_LD(void *, ((uint8_t *)&gm_l56));
                    uint64_t t465 = GM_LD(uint64_t, (((uint8_t *)&gm_l56) + 8));
                    uint64_t t466 = gm_l57;
                    {
                        {
                            gm_b16_8 t467 = GM_LD(gm_b16_8, (((uint8_t *)t464) + (uintptr_t)t466 * 16));
                            gm_l58 = t467;
                            gm_b16_8 t468 = gm_l58;
                            gm_ttln0d0hd70en t469 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t468);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb62;
                        }
gm_lb63:;
                        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 417, 5);
                    }
gm_lb62:;
                }
gm_lb61:;
                uint64_t t470 = gm_l57;
                uint64_t r471; bool o472 = __builtin_add_overflow(t470, ((uint64_t)1ULL), &r471);
                uint64_t t473 = r471;
                gm_l57 = t473;
            }
        }
gm_lb59:;
        uint8_t t474 = gm_l55;
        uint8_t t475 = (uint8_t)!t474;
        if (t475) {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        uint8_t t476 = gm_l36;
        if (t476) {
            gm_b16_8 t477 = gm_l3;
            gm_b16_8 t478 = gm_l6;
            uint8_t t479 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
            gm_f_asyncify_bs28ebq((void *)&gm_l113, (void *)NULL, cx, NULL, t477, t478, t479);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t480 = (uint8_t)!((((uint8_t *)&gm_l113))[0] != 0);
            if (t480) {
            } else {
                gm_b16_8 t481 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l113) + 8));
                gm_l59 = t481;
                gm_b16_8 t482 = gm_l59;
                if ((GM_LD(uintptr_t, ((uint8_t *)&t482) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t482)), GM_LD(uintptr_t, ((uint8_t *)&t482) + 8), gm_srcqr4ch56kv6bv, 421, 13))) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 421, 13)) {
                    goto gm_lad;
                }
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
        }
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
inline __attribute__((aligned(4))) uint8_t gm_f_compile_cdm3hj7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3) {
    struct gm_fr_compile_cdm3hj7 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* root */
    gm_b16_8 gm_l2; /* exe */
    uint64_t gm_l4; /* d */
    uint32_t gm_l5; /* main */
    gm_tdvq40fmif5j0 gm_l6; /* entries */
    GM_STACK_CHECK(&F, 486u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&F.l3, a3, sizeof F.l3);
    gm_tf5srh4mp4b3i gm_l7;
    uint64_t gm_l8;
    gm_t6qooom8qa2do gm_l9;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint64_t t6 = gm_l1;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sog4rksrmsqd2));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(4));
    gm_tf5srh4mp4b3i t8 = gm_f_lookup_59juqoo(t5, t6, t7);
    gm_l7 = t8;
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
    if (t9) {
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
        gm_l8 = t10;
    } else {
        if (gm_pf454qfrpt6r(cx, gm_setv7ejg35cg9, 42, gm_srcqr4ch56kv6bv, 85, 9)) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint64_t t11 = gm_l8;
    gm_l4 = t11;
    {
        {
            void * t12 = gm_l0;
            uint64_t t13 = gm_l4;
            gm_f_resolve_decl_pus042v((void *)&gm_l9, (void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)5ULL));
            if (!t15) {
                goto gm_lb2;
            }
            uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
            gm_l5 = t16;
            void * t17 = gm_l0;
            uint32_t t18 = gm_l5;
            uint32_t t19 = gm_f_entry_point_n2gu79u((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tdvq40fmif5j0 t20;
            GM_ST(uint32_t, ((uint8_t *)&t20 + 0), t19);
            gm_l6 = t20;
            void * t21 = gm_l0;
            void * t22 = (void *)((uint8_t *)&gm_l6);
            uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t24 = (uintptr_t)((uint64_t)1ULL);
            if (t23 > t24 || t24 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t23, t24, ((uint64_t)1ULL), gm_srcqr4ch56kv6bv, 90, 31);
            uint64_t t25 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t22 + (uintptr_t)((uint64_t)0ULL) * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
            gm_b16_8 t28 = gm_l2;
            uint8_t t29 = gm_f_compile_entries_l9qlniu((void *)NULL, cx, NULL, t21, t27, ((uint8_t)0), t28, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t29;
            goto gm_fret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_compile_entries_l9qlniu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3, const void * a4) {
    struct gm_fr_compile_entries_l9qlniu F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* entries */
    uint8_t gm_l2; /* select */
    gm_b16_8 gm_l3; /* exe */
    GM_STACK_CHECK(&F, 1238u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&F.l4, a4, sizeof F.l4);
    gm_t5i6e9e007icp gm_l6;
    gm_tt1457on428g4 gm_l7;
    void * t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_l1;
    uint8_t t5 = gm_l2;
    gm_tcn5ttqhv05g6 t6 = GM_LD(gm_tcn5ttqhv05g6, ((uint8_t *)&F.l4));
    gm_f_generate_program_9mr9pv8((void *)&gm_l6, (void *)NULL, cx, NULL, t2, t3, t4, t5, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l6))[248] != 0);
    if (t7) {
        memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l6), 248);
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    memmove(((uint8_t *)&F.l5), ((uint8_t *)&gm_l7), 248);
    void * t8 = (void *)((uint8_t *)&F.l5);
    gm_b16_8 t9 = gm_l3;
    uint8_t t10 = gm_f_compile_built_8o9nmfu((void *)NULL, cx, NULL, t8, t9, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l4)));
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
__attribute__((aligned(4))) uint8_t gm_f_compile_object_k7kbtpr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, const void * a3) {
    struct gm_fr_compile_object_k7kbtpr F;
    void * gm_l0; /* b */
    void * gm_l1; /* tc */
    gm_b16_8 gm_l2; /* obj */
    gm_b16_8 gm_l4; /* c_file */
    gm_b16_8 gm_l5; /* opt */
    gm_b16_8 gm_l6; /* prelude */
    gm_b16_8 gm_l7; /* code */
    GM_STACK_CHECK(&F, 1218u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&F.l3, a3, sizeof F.l3);
    gm_tabb710d9mq2e gm_l11;
    gm_b16_8 gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_b16_8 t4 = gm_l2;
    gm_tnpifv20irnph t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
    gm_f_format_cncadt9((void *)&gm_l11, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcqr4ch56kv6bv, 488, 15);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
    gm_l4 = t7;
    void * t8 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t9 = gm_f_opt_level_gnm5dcm(t8);
    gm_l5 = t9;
    void * t10 = gm_l0;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t10) + 209));
    if (t11) {
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s7qmtc95nc11i));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(23));
        gm_l12 = t12;
    } else {
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(0));
        gm_l12 = t13;
    }
    gm_b16_8 t14 = gm_l12;
    gm_tnpifv20irnph t15;
    GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
    gm_f_format_6gakl8s((void *)&gm_l13, (void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcqr4ch56kv6bv, 490, 16);
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
    gm_l6 = t17;
    gm_b16_8 t18 = gm_l6;
    void * t19 = gm_l0;
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t19) + 216));
    void * t21 = gm_l0;
    gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 232));
    void * t23 = gm_l0;
    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)t23));
    gm_t0uuhaljsf410 t25;
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t18);
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 16), t20);
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 32), t22);
    GM_ST(gm_b16_8, ((uint8_t *)&t25 + 48), t24);
    gm_f_format_f2pdud5((void *)&gm_l14, (void *)NULL, cx, NULL, &t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcqr4ch56kv6bv, 491, 20);
    gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
    gm_l7 = t28;
    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 33));
    if (t29) {
        gm_b16_8 t30 = gm_l7;
        gm_b16_8 t31 = gm_l4;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        gm_b16_8 t33 = gm_f_absolute_6pqhsaj((void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t34 = gm_f_resolve_lines_h4bf30f((void *)NULL, cx, NULL, t30, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t34;
    }
    gm_b16_8 t35 = gm_l4;
    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
    gm_b16_8 t37 = gm_l7;
    uint8_t t38 = gm_f_write_generated_c_kb09043((void *)NULL, cx, NULL, t36, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t39 = (uint8_t)!t38;
    if (t39) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_trqfvmjtt4sij t40;
    gm_f_list_epvducg((void *)&t40, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l8 = t40;
    void * t41 = (void *)((uint8_t *)&F.l8);
    void * t42 = (void *)((uint8_t *)gm_c1a92ugfgjv);
    uint64_t t43 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t44 = (uintptr_t)((uint64_t)7ULL);
    if (t43 > t44 || t44 > (uintptr_t)((uint64_t)7ULL)) gm_trap_slice(t43, t44, ((uint64_t)7ULL), gm_srcqr4ch56kv6bv, 495, 18);
    uint64_t t45 = ((uintptr_t)((uint64_t)7ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t46;
    GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t42 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45));
    gm_tftb1odc739t5 t47 = gm_f_extend_vogsd72(t41, t46);
    gm_l15 = t47;
    uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcqr4ch56kv6bv, 495, 5);
    void * t49 = (void *)((uint8_t *)&F.l8);
    gm_b16_8 t50;
    GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s463po27vtmnn));
    GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(14));
    gm_tftb1odc739t5 t51 = gm_f_push_3ef746b(t49, t50);
    gm_l16 = t51;
    uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcqr4ch56kv6bv, 496, 5);
    void * t53 = (void *)((uint8_t *)&F.l8);
    gm_b16_8 t54 = gm_l5;
    gm_tftb1odc739t5 t55 = gm_f_push_3ef746b(t53, t54);
    gm_l17 = t55;
    uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcqr4ch56kv6bv, 497, 5);
    {
        {
            gm_t9qkh0rsooc8d t57 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l3) + 4));
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t57) + 0));
            uint8_t t59 = (uint8_t)((t58 == ((uint8_t)0ULL)));
            if (!t59) {
                goto gm_lb2;
            }
            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 33));
            uint8_t t61 = (uint8_t)!t60;
            if (!t61) {
                goto gm_lb2;
            }
            void * t62 = (void *)((uint8_t *)&F.l8);
            void * t63 = (void *)((uint8_t *)&F.l3);
            void * t64 = gm_l1;
            gm_t9qkh0rsooc8d t65;
            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)0ULL));
            gm_th2lqbo77pvsc t66;
            gm_f_compiler_version_hj63jgf((void *)&t66, (void *)NULL, cx, NULL, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t67 = gm_f_debug_flags_gv15c46(t63, &t66);
            gm_tftb1odc739t5 t68 = gm_f_extend_vogsd72(t62, t67);
            gm_l18 = t68;
            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcqr4ch56kv6bv, 498, 54);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 33));
    if (t70) {
        void * t71 = (void *)((uint8_t *)&F.l8);
        gm_b16_8 t72;
        GM_ST(void *, (uint8_t *)&t72, (void *)(gm_sn0peadng5i31));
        GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t73 = gm_f_push_3ef746b(t71, t72);
        gm_l19 = t73;
        uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
        if (!t74) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcqr4ch56kv6bv, 499, 26);
    }
    gm_b16_8 t75;
    GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s4vd6e3mkiiso));
    GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(2));
    gm_b16_8 t76;
    GM_ST(void *, (uint8_t *)&t76, (void *)(gm_s65qt8qo64cje));
    GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(2));
    gm_b16_8 t77 = gm_l2;
    gm_b16_8 t78 = gm_l4;
    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
    gm_tdmi4fk9nhqa7 t80;
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t75);
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 16), t76);
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 32), t77);
    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 48), t79);
    F.l9 = t80;
    void * t81 = gm_l1;
    gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
    gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
    void * t84 = (void *)((uint8_t *)&F.l9);
    uint64_t t85 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t86 = (uintptr_t)((uint64_t)4ULL);
    if (t85 > t86 || t86 > (uintptr_t)((uint64_t)4ULL)) gm_trap_slice(t85, t86, ((uint64_t)4ULL), gm_srcqr4ch56kv6bv, 501, 42);
    uint64_t t87 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t88;
    GM_ST(void *, (uint8_t *)&t88, (void *)(((uint8_t *)t84 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(t87));
    gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
    gm_f_command_b2u33fl((void *)&F.l10, (void *)NULL, cx, NULL, t81, t83, t89);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t90 = gm_l1;
    gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
    gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
    uint8_t t93 = gm_f_run_compiler_ulaqcb1((void *)NULL, cx, NULL, t90, t92);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t93;
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
__attribute__((aligned(4))) uint8_t gm_f_compile_objects_mu5secm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4) {
    struct gm_fr_compile_objects_mu5secm F;
    void * gm_l0; /* tc */
    gm_b16_8 gm_l1; /* flags */
    gm_b16_8 gm_l2; /* c_files */
    gm_b16_8 gm_l3; /* objects */
    uint64_t gm_l4; /* jobs */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l9; /* c */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_tm98ap4112hee gm_l17; /* t */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_tm98ap4112hee gm_l20; /* t */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_tl572d7579hns gm_l23; /* r */
    gm_b16_8 gm_l24; /* err */
    GM_STACK_CHECK(&F, 1482u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tf1bjg81u0glv gm_l25;
    gm_trqfvmjtt4sij gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_t62ebbi2i5rkd gm_l29;
    gm_tm98ap4112hee gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_trqfvmjtt4sij t31;
    gm_f_list_242hv68((void *)&t31, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t31;
    gm_b16_8 t32 = gm_l2;
    gm_l6 = t32;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t33 = gm_l7;
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t35 = (t33 < t34);
            if (!t35) {
                goto gm_lb1;
            }
            {
                uint64_t t36 = gm_l7;
                void * t37 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t39 = gm_l7;
                gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)t37) + (uintptr_t)t39 * 16));
                gm_tf1bjg81u0glv t41;
                GM_ST(uint64_t, ((uint8_t *)&t41 + 0), t36);
                GM_ST(gm_b16_8, ((uint8_t *)&t41 + 8), t40);
                gm_l25 = t41;
                {
                    {
                        uint64_t t42 = GM_LD(uint64_t, ((uint8_t *)&gm_l25));
                        gm_l8 = t42;
                        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                        gm_l9 = t43;
                        gm_b16_8 t44;
                        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s4vd6e3mkiiso));
                        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(2));
                        gm_b16_8 t45;
                        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s65qt8qo64cje));
                        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(2));
                        void * t46 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t48 = gm_l8;
                        if ((uintptr_t)t48 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t48, t47, gm_srcqr4ch56kv6bv, 530, 39);
                        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t46) + (uintptr_t)t48 * 16));
                        gm_b16_8 t50 = gm_l9;
                        gm_tdmi4fk9nhqa7 t51;
                        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 0), t44);
                        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 16), t45);
                        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 32), t49);
                        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 48), t50);
                        F.l10 = t51;
                        void * t52 = (void *)((uint8_t *)&F.l5);
                        void * t53 = gm_l0;
                        gm_b16_8 t54 = gm_l1;
                        void * t55 = (void *)((uint8_t *)&F.l10);
                        uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t57 = (uintptr_t)((uint64_t)4ULL);
                        if (t56 > t57 || t57 > (uintptr_t)((uint64_t)4ULL)) gm_trap_slice(t56, t57, ((uint64_t)4ULL), gm_srcqr4ch56kv6bv, 531, 43);
                        uint64_t t58 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t59;
                        GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t55 + (uintptr_t)((uint64_t)0ULL) * 16)));
                        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(t58));
                        gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                        gm_f_command_b2u33fl((void *)&gm_l26, (void *)NULL, cx, NULL, t53, t54, t60);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l26));
                        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                        gm_tftb1odc739t5 t63 = gm_f_push_0hkhfv5(t52, t62);
                        gm_l27 = t63;
                        uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                        if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcqr4ch56kv6bv, 531, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 529, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t65 = gm_l7;
            uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
            uint64_t t68 = r66;
            gm_l7 = t68;
        }
    }
gm_lb1:;
    gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
    gm_trqfvmjtt4sij t71;
    gm_f_list_fnnocfv((void *)&t71, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t72 = GM_CONV(gm_t88rcucg1b7uk, gm_f_mutex_t7if00c());
    gm_teg5ofd5qf10s t73;
    GM_ST(uint32_t, (uint8_t *)&t73 + 68, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t70);
    GM_ST(uint64_t, ((uint8_t *)&t73 + 16), ((uint64_t)0ULL));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t73 + 24), t71);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t73 + 64), t72);
    F.l11 = t73;
    gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    gm_l12 = t74;
    gm_l13 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t75 = gm_l13;
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
            uint8_t t77 = (t75 < t76);
            if (!t77) {
                goto gm_lb6;
            }
            {
                void * t78 = GM_LD(void *, ((uint8_t *)&gm_l12));
                uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                uint64_t t80 = gm_l13;
                {
                    {
                        void * t81 = (void *)(((uint8_t *)&F.l11) + 24);
                        gm_tl572d7579hns t82;
                        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t82 + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&t82 + 16, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)0ULL));
                        gm_tftb1odc739t5 t83 = gm_f_push_fp59o9c(t81, &t82);
                        gm_l28 = t83;
                        uint8_t t84 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                        if (!t84) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcqr4ch56kv6bv, 534, 28);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 534, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t85 = gm_l13;
            uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
            uint64_t t88 = r86;
            gm_l13 = t88;
        }
    }
gm_lb6:;
    gm_trqfvmjtt4sij t89;
    gm_f_list_7vplt15((void *)&t89, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l14 = t89;
    gm_l16 = ((uint64_t)0ULL);
    uint64_t t90 = gm_l4;
    uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
    uint64_t t92 = (t91 > ((uint64_t)1ULL) ? t91 : ((uint64_t)1ULL));
    uint64_t t93 = (t90 < ((uint64_t)1ULL) ? ((uint64_t)1ULL) : (t90 > t92 ? t92 : t90));
    gm_l15 = t93;
    {
        for (;;) {
            uint64_t t94 = gm_l16;
            uint64_t t95 = gm_l15;
            uint8_t t96 = (t94 < t95);
            if (!t96) {
                goto gm_lb11;
            }
            {
                {
                    {
                        void * t97 = (void *)((uint8_t *)&F.l11);
                        gm_t62ebbi2i5rkd t98 = gm_f_spawn_et0s5ea(((void *)gm_f_compile_worker_5cv5pj9), t97);
                        gm_l29 = t98;
                        uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                        if (t99) {
                            gm_tm98ap4112hee t100 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l29) + 8));
                            gm_l30 = t100;
                        } else {
                            goto gm_lb11;
                        }
                        gm_tm98ap4112hee t101 = gm_l30;
                        gm_l17 = t101;
                        void * t102 = (void *)((uint8_t *)&F.l14);
                        gm_tm98ap4112hee t103 = gm_l17;
                        gm_tftb1odc739t5 t104 = gm_f_push_6ct7n6b(t102, GM_CONV(gm_csfq64m1m1rt6m, t103));
                        gm_l31 = t104;
                        uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                        if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcqr4ch56kv6bv, 538, 9);
                        goto gm_lb14;
                    }
gm_lb15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 536, 5);
                }
gm_lb14:;
            }
gm_lb13:;
            uint64_t t106 = gm_l16;
            uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
            uint64_t t109 = r107;
            gm_l16 = t109;
        }
    }
gm_lb11:;
    uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&F.l14) + 8));
    uint8_t t111 = ((t110 == ((uint64_t)0ULL)));
    if (t111) {
        void * t112 = (void *)((uint8_t *)&F.l11);
        gm_f_compile_worker_5cv5pj9(t112);
    }
    gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
    gm_l18 = t113;
    gm_l19 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t114 = gm_l19;
            uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
            uint8_t t116 = (t114 < t115);
            if (!t116) {
                goto gm_lb16;
            }
            {
                void * t117 = GM_LD(void *, ((uint8_t *)&gm_l18));
                uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                uint64_t t119 = gm_l19;
                {
                    {
                        gm_tm98ap4112hee t120 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)t117) + (uintptr_t)t119 * 8));
                        gm_l20 = t120;
                        gm_tm98ap4112hee t121 = gm_l20;
                        gm_f_join_r8j2dp1(GM_CONV(gm_csfq64m1m1rt6m, t121));
                        goto gm_lb19;
                    }
gm_lb20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 541, 5);
                }
gm_lb19:;
            }
gm_lb18:;
            uint64_t t122 = gm_l19;
            uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
            uint64_t t125 = r123;
            gm_l19 = t125;
        }
    }
gm_lb16:;
    gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 24));
    gm_l21 = t126;
    gm_l22 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t127 = gm_l22;
            uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
            uint8_t t129 = (t127 < t128);
            if (!t129) {
                goto gm_lb21;
            }
            {
                void * t130 = GM_LD(void *, ((uint8_t *)&gm_l21));
                uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                uint64_t t132 = gm_l22;
                {
                    {
                        memmove(((uint8_t *)&gm_l23), (((uint8_t *)t130) + (uintptr_t)t132 * 24), 24);
                        {
                            {
                                uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                                uint8_t t134 = (uint8_t)(t133 == ((uint8_t)0ULL));
                                if (!t134) {
                                    goto gm_lb27;
                                }
                                goto gm_lb26;
                            }
gm_lb27:;
                            {
                                uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                                uint8_t t136 = (uint8_t)(t135 == ((uint8_t)1ULL));
                                if (!t136) {
                                    goto gm_lb28;
                                }
                                void * t137 = gm_l0;
                                gm_b16_8 t138 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t137);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (gm_pf454qfrpt6r(cx, gm_sr3et5tffndla, 27, gm_srcqr4ch56kv6bv, 546, 17)) {
                                    goto gm_lad;
                                }
                                if ((GM_LD(uintptr_t, ((uint8_t *)&t138) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t138)), GM_LD(uintptr_t, ((uint8_t *)&t138) + 8), gm_srcqr4ch56kv6bv, 546, 17))) {
                                    goto gm_lad;
                                }
                                if (gm_pf454qfrpt6r(cx, gm_s622mvvjraklk, 42, gm_srcqr4ch56kv6bv, 546, 17)) {
                                    goto gm_lad;
                                }
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
gm_lb28:;
                            {
                                uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                                uint8_t t140 = (uint8_t)(t139 == ((uint8_t)2ULL));
                                if (!t140) {
                                    goto gm_lb29;
                                }
                                gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                                gm_l24 = t141;
                                gm_b16_8 t142 = gm_l24;
                                if (gm_pf454qfrpt6r(cx, gm_s1ctr86inn5iu, 23, gm_srcqr4ch56kv6bv, 550, 17)) {
                                    goto gm_lad;
                                }
                                if ((GM_LD(uintptr_t, ((uint8_t *)&t142) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t142)), GM_LD(uintptr_t, ((uint8_t *)&t142) + 8), gm_srcqr4ch56kv6bv, 550, 17))) {
                                    goto gm_lad;
                                }
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
gm_lb29:;
                            gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 543, 9);
                        }
gm_lb26:;
                        goto gm_lb24;
                    }
gm_lb25:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 542, 5);
                }
gm_lb24:;
            }
gm_lb23:;
            uint64_t t143 = gm_l22;
            uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
            uint64_t t146 = r144;
            gm_l22 = t146;
        }
    }
gm_lb21:;
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
inline __attribute__((aligned(4))) void gm_f_compile_one_4djvgo4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_compile_one_4djvgo4 F;
    gm_b16_8 gm_l0; /* argv */
    gm_tm98ap4112hee gm_l1; /* c */
    gm_te0mgnjdphs16 gm_l4; /* chunk */
    gm_tsoiri44d0i6q gm_l5; /* status */
    GM_STACK_CHECK(&F, 789u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t62ebbi2i5rkd gm_l6;
    gm_tm98ap4112hee gm_l7;
    gm_tlbmih0s72in3 gm_l8;
    gm_thdrvp9q0u7b5 gm_l9;
    gm_thdrvp9q0u7b5 gm_l10;
    gm_te0mgnjdphs16 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tlmjd3nndlf3r gm_l13;
    gm_tsoiri44d0i6q gm_l14;
    gm_b16_8 t5 = gm_l0;
    gm_t9qkh0rsooc8d t6 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)gm_cc7b06pnl1q));
    gm_th2lqbo77pvsc t7;
    memset(&t7, 0, sizeof t7);
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)gm_clslitrb510));
    gm_th2lqbo77pvsc t9;
    memset(&t9, 0, sizeof t9);
    gm_t9qkh0rsooc8d t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t11;
    GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)2ULL));
    gm_t32dckm4jfarl t12;
    GM_ST(uint8_t, (uint8_t *)&t12 + 3, 0);
    GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
    GM_ST(uint16_t, (uint8_t *)&t12 + 50, 0);
    GM_ST(uint32_t, (uint8_t *)&t12 + 52, 0);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 0), t6);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 1), t10);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t12 + 2), t11);
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t12 + 8), t7);
    GM_ST(gm_b16_8, ((uint8_t *)&t12 + 32), t8);
    GM_ST(uint8_t, ((uint8_t *)&t12 + 48), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t12 + 49), ((uint8_t)0));
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t12 + 56), t9);
    gm_node *pn13 = gm_find(cx, 2119230);
    if (!pn13) gm_trap("no handler installed for effect `Proc`", gm_srcqr4ch56kv6bv, 590, 10);
    gm_t62ebbi2i5rkd t14;
    t14 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, const void *))pn13->clauses[0])(pn13->env, pn13->hctx, pn13, t5, &t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t14;
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t15) {
        gm_tm98ap4112hee t16 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l6) + 8));
        gm_l7 = t16;
    } else {
        gm_tl572d7579hns t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t17 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t17 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)1ULL));
        memmove(gm_out, ((uint8_t *)&t17), 24);
        goto gm_fdone;
    }
    gm_tm98ap4112hee t18 = gm_l7;
    gm_l1 = t18;
    gm_trqfvmjtt4sij t19;
    gm_f_list_uvfu1k1((void *)&t19, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t19;
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                gm_tm98ap4112hee t20 = gm_l1;
                void * t21 = (void *)((uint8_t *)&F.l3);
                uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t23 = (uintptr_t)((uint64_t)4096ULL);
                if (t22 > t23 || t23 > (uintptr_t)((uint64_t)4096ULL)) gm_trap_slice(t22, t23, ((uint64_t)4096ULL), gm_srcqr4ch56kv6bv, 594, 26);
                uint64_t t24 = ((uintptr_t)((uint64_t)4096ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t25;
                GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
                gm_node *pn26 = gm_find(cx, 2119230);
                if (!pn26) gm_trap("no handler installed for effect `Proc`", gm_srcqr4ch56kv6bv, 594, 19);
                gm_tlbmih0s72in3 t27;
                ((void (*)(void *, void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn26->clauses[3])((void *)&t27, pn26->env, pn26->hctx, pn26, t20, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t27;
                uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                if (t28) {
                    memmove(((uint8_t *)&gm_l9), (((uint8_t *)&gm_l8) + 8), 24);
                } else {
                    goto gm_lb1;
                }
                memmove(((uint8_t *)&gm_l10), ((uint8_t *)&gm_l9), 24);
                uint8_t t29 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
                if (t29) {
                    gm_te0mgnjdphs16 t30 = GM_LD(gm_te0mgnjdphs16, ((uint8_t *)&gm_l10));
                    gm_l11 = t30;
                } else {
                    goto gm_lb1;
                }
                gm_te0mgnjdphs16 t31 = gm_l11;
                gm_l4 = t31;
                void * t32 = (void *)((uint8_t *)&F.l2);
                void * t33 = (void *)((uint8_t *)&F.l3);
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t36 = (uintptr_t)t34;
                if (t35 > t36 || t36 > (uintptr_t)((uint64_t)4096ULL)) gm_trap_slice(t35, t36, ((uint64_t)4096ULL), gm_srcqr4ch56kv6bv, 595, 20);
                uint64_t t37 = ((uintptr_t)t34) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t38;
                GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t33 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37));
                gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                gm_tftb1odc739t5 t40 = gm_f_extend_0cs85gs(t32, t39);
                gm_l12 = t40;
                uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcqr4ch56kv6bv, 595, 9);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    gm_tm98ap4112hee t42 = gm_l1;
    gm_node *pn43 = gm_find(cx, 2119230);
    if (!pn43) gm_trap("no handler installed for effect `Proc`", gm_srcqr4ch56kv6bv, 597, 15);
    gm_tlmjd3nndlf3r t44;
    t44 = ((gm_tlmjd3nndlf3r (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn43->clauses[4])(pn43->env, pn43->hctx, pn43, t42);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l13 = t44;
    uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (t45) {
        gm_tsoiri44d0i6q t46 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&gm_l13) + 4));
        gm_l14 = t46;
    } else {
        gm_tsoiri44d0i6q t47;
        GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t47 + 4), ((uint32_t)1ULL));
        gm_l14 = t47;
    }
    gm_tsoiri44d0i6q t48 = gm_l14;
    gm_l5 = t48;
    gm_tsoiri44d0i6q t49 = gm_l5;
    uint8_t t50 = gm_f_success_q3lb6vt(t49);
    if (t50) {
        gm_tl572d7579hns t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t51 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t51 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)0ULL));
        memmove(gm_out, ((uint8_t *)&t51), 24);
        goto gm_fdone;
    }
    gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
    gm_tl572d7579hns t54;
    GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)2ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t54 + 8), t53);
    memmove(gm_out, ((uint8_t *)&t54), 24);
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
inline __attribute__((aligned(4))) void gm_f_compile_worker_5cv5pj9(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_compile_worker_5cv5pj9 F;
    void * gm_l0; /* p */
    uint64_t gm_l1; /* j */
    GM_STACK_CHECK(&F, 257u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t3mf58gv9kf3n t6 = GM_CONV(gm_t3mf58gv9kf3n, gm_f_heap_allocator_gihki37());
        gm_t3mf58gv9kf3n t7 = t6;
        gm_node wn8 = {.eff = 587034433, .kind = GM_DECL, .clauses = gm_hd3t1u951rul, .env = (void *)&t7, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b0_1 t9 = gm_f_system_io_mcjcl9m();
        gm_b0_1 t10 = GM_ZERO(gm_b0_1);
        gm_node wn11 = {.eff = 1018230276, .kind = GM_DECL, .clauses = gm_hd3g3ujqvah4, .env = (void *)&t10, .hctx = (&wn8), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn8)};
        gm_node wn12 = {.eff = 1579066750, .kind = GM_DECL, .clauses = gm_hdf4dspmusp5, .env = (void *)&t10, .hctx = (&wn8), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn11)};
        gm_node wn13 = {.eff = 1087599154, .kind = GM_DECL, .clauses = gm_hdvslis4o40u, .env = (void *)&t10, .hctx = (&wn8), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn12)};
        gm_node wn14 = {.eff = 2126215853, .kind = GM_DECL, .clauses = gm_hdu8v6n5i2qd, .env = (void *)&t10, .hctx = (&wn8), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn13)};
        gm_node wn15 = {.eff = 163658498, .kind = GM_DECL, .clauses = gm_hdffkeesmui4, .env = (void *)&t10, .hctx = (&wn8), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn14)};
        gm_node wn16 = {.eff = 2119230, .kind = GM_DECL, .clauses = gm_hdaqemfrshlp, .env = (void *)&t10, .hctx = (&wn8), .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = (&wn15)};
        {
            for (;;) {
                if (!((uint8_t)1)) {
                    goto gm_lb2;
                }
                {
                    void * t19 = gm_l0;
                    void * t20 = (void *)(((uint8_t *)t19) + 64);
                    gm_f_lock_u5lks01(t20);
                    void * t21 = gm_l0;
                    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 16));
                    gm_l1 = t22;
                    void * t23 = gm_l0;
                    void * t24 = gm_l0;
                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 16));
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    if (o27) gm_trap("integer overflow in `+`", gm_srcqr4ch56kv6bv, 580, 13);
                    uint64_t t28 = r26;
                    GM_ST(uint64_t, (((uint8_t *)t23) + 16), t28);
                    void * t29 = gm_l0;
                    void * t30 = (void *)(((uint8_t *)t29) + 64);
                    gm_f_unlock_9f1vcl6(t30);
                    uint64_t t31 = gm_l1;
                    void * t32 = gm_l0;
                    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 8));
                    uint8_t t34 = (t31 >= t33);
                    if (t34) {
                        goto gm_lb2;
                    }
                    void * t35 = gm_l0;
                    void * t36 = GM_LD(void *, (((uint8_t *)t35) + 24));
                    uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)t35) + 24) + 8));
                    uint64_t t38 = gm_l1;
                    if ((uintptr_t)t38 >= (uintptr_t)(t37)) gm_trap_index((uintptr_t)t38, t37, gm_srcqr4ch56kv6bv, 583, 13);
                    void * t39 = gm_l0;
                    void * t40 = GM_LD(void *, ((uint8_t *)t39));
                    uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)t39) + 8));
                    uint64_t t42 = gm_l1;
                    if ((uintptr_t)t42 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t42, t41, gm_srcqr4ch56kv6bv, 583, 46);
                    gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)t40) + (uintptr_t)t42 * 16));
                    gm_tl572d7579hns t44;
                    gm_f_compile_one_4djvgo4((void *)&t44, (void *)NULL, (&wn16), NULL, t43);
                    if (gm_unw.kind) {
                        goto gm_ll18;
                    }
                    GM_ST(gm_tl572d7579hns, (((uint8_t *)t36) + (uintptr_t)t38 * 24), t44);
                }
gm_lb4:;
            }
        }
gm_lb2:;
    }
    goto gm_ls45;
gm_ll18:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we17; }
    goto gm_lad;
gm_ls45:;
gm_lb1:;
we17:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_debug_flags_gv15c46(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_debug_flags_gv15c46 F;
    void * gm_l0; /* opts */
    gm_th2lqbo77pvsc gm_l1; /* version */
    GM_STACK_CHECK(&F, 167u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t4 = gm_l0;
            gm_t9qkh0rsooc8d t5 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t4) + 4));
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
            uint8_t t7 = (uint8_t)((t6 == ((uint8_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            void * t8 = gm_l0;
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t8) + 33));
            uint8_t t10 = (uint8_t)!t9;
            if (!t10) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t11;
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            uint8_t t12 = gm_f_is_gcc_epb17a6(&(*(gm_th2lqbo77pvsc *)((uint8_t *)a1)));
            if (!t12) {
                goto gm_lb2;
            }
            void * t13 = (void *)((uint8_t *)gm_cfa8lmn3344);
            uint64_t t14 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t15 = (uintptr_t)((uint64_t)3ULL);
            if (t14 > t15 || t15 > (uintptr_t)((uint64_t)3ULL)) gm_trap_slice(t14, t15, ((uint64_t)3ULL), gm_srcqr4ch56kv6bv, 173, 81);
            uint64_t t16 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t13 + (uintptr_t)((uint64_t)0ULL) * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16));
            F.ret = t17;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t18 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
    F.ret = t19;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_flags_gv15c46_c(void * a0, gm_th2lqbo77pvsc a1) { return gm_f_debug_flags_gv15c46(a0, &a1); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_default_exe_194tkju(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_default_exe_194tkju F;
    gm_b16_8 gm_l0; /* root */
    gm_b16_8 gm_l1; /* trimmed */
    GM_STACK_CHECK_SHALLOW(&F, 506u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0ucactj6hj0a));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    uint8_t t4 = gm_f_ends_with_biriu5l(t2, t3);
    if (t4) {
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)3ULL), &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcqr4ch56kv6bv, 617, 47);
        uint64_t t10 = r8;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t6) gm_trap_slice(t11, t12, t6, gm_srcqr4ch56kv6bv, 617, 39);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        F.ret = t14;
        goto gm_fret;
    }
    gm_b16_8 t15 = gm_l0;
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(gm_st3ja0nqi37bv));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(1));
    uint8_t t17 = gm_f_ends_with_biriu5l(t15, t16);
    if (t17) {
        void * t18 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t r21; bool o22 = __builtin_sub_overflow(t20, ((uint64_t)1ULL), &r21);
        if (o22) gm_trap("integer overflow in `-`", gm_srcqr4ch56kv6bv, 618, 49);
        uint64_t t23 = r21;
        uint64_t t24 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t25 = (uintptr_t)t23;
        if (t24 > t25 || t25 > (uintptr_t)t19) gm_trap_slice(t24, t25, t19, gm_srcqr4ch56kv6bv, 618, 41);
        uint64_t t26 = ((uintptr_t)t23) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t18 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26));
        gm_l2 = t27;
    } else {
        gm_b16_8 t28 = gm_l0;
        gm_l2 = t28;
    }
    gm_b16_8 t29 = gm_l2;
    gm_l1 = t29;
    gm_b16_8 t30 = gm_l1;
    gm_b16_8 t31;
    GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sl3q52ep6aa53));
    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(1));
    gm_b16_8 t32;
    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_snenkhfev166s));
    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(2));
    gm_t6j6smj7qoctu t33;
    GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t31);
    GM_ST(gm_b16_8, ((uint8_t *)&t33 + 16), t32);
    uint8_t t34 = 0;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t33) + 0, ((uint8_t *)&t30))) t34 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t33) + 16, ((uint8_t *)&t30))) t34 = 1;
    uint8_t t35 = (uint8_t)(t34);
    if (t35) {
        gm_l4 = ((uint8_t)1);
    } else {
        gm_b16_8 t36 = gm_l1;
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_st9f93lco88fg));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(2));
        uint8_t t38 = gm_f_ends_with_biriu5l(t36, t37);
        gm_l4 = t38;
    }
    uint8_t t39 = gm_l4;
    if (t39) {
        gm_l3 = ((uint8_t)1);
    } else {
        gm_b16_8 t40 = gm_l1;
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s29cvp3di8ou1));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(3));
        uint8_t t42 = gm_f_ends_with_biriu5l(t40, t41);
        gm_l3 = t42;
    }
    uint8_t t43 = gm_l3;
    if (t43) {
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sog4rksrmsqd2));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(4));
        F.ret = t44;
        goto gm_fret;
    }
    gm_b16_8 t45 = gm_l1;
    F.ret = t45;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_emitted_units_7f3nie0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_emitted_units_7f3nie0 F;
    void * gm_l0; /* b */
    uint8_t gm_l1; /* debug_info */
    gm_b16_8 gm_l2; /* v */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK(&F, 461u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l4;
    gm_b16_8 gm_l5;
    gm_tutnubt6u4hoq gm_l6;
    uint64_t gm_l7;
    uint8_t t2 = gm_l1;
    if (t2) {
        gm_ttu09lij36f8c t3;
        memset(&t3, 0, sizeof t3);
        memmove(gm_out, ((uint8_t *)&t3), 40);
        goto gm_fdone;
    }
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sdahvc1997d01));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(13));
    gm_node *pn5 = gm_find(cx, 621690230);
    if (!pn5) gm_trap("no handler installed for effect `Env`", gm_srcqr4ch56kv6bv, 210, 10);
    gm_th2lqbo77pvsc t6;
    if (pn5->kind == GM_NATIVE) {
        const uint8_t *ev; size_t evn;
        bool evok = gm_env_var(GM_LD(void *, &t4), GM_LD(uintptr_t, (uint8_t *)&t4 + 8), &ev, &evn);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(ev));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(evn));
        gm_th2lqbo77pvsc t8;
        GM_ST(uint8_t, (uint8_t *)&t8 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t8 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t8 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t8), t7);
        ((uint8_t *)&t8)[16] = 1;
        gm_th2lqbo77pvsc t9;
        memset(&t9, 0, sizeof t9);
        t6 = evok ? t8 : t9;
    } else {
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn5->clauses[1])((void *)&t6, pn5->env, pn5->hctx, pn5, t4);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l4 = t6;
    uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
    if (t10) {
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
        gm_l5 = t11;
    } else {
        gm_ttu09lij36f8c t12;
        memset(&t12, 0, sizeof t12);
        memmove(gm_out, ((uint8_t *)&t12), 40);
        goto gm_fdone;
    }
    gm_b16_8 t13 = gm_l5;
    gm_l2 = t13;
    gm_b16_8 t14 = gm_l2;
    gm_f_parse_int_3ce02np((void *)&gm_l6, t14);
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (t15) {
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        gm_l7 = t16;
    } else {
        gm_ttu09lij36f8c t17;
        memset(&t17, 0, sizeof t17);
        memmove(gm_out, ((uint8_t *)&t17), 40);
        goto gm_fdone;
    }
    uint64_t t18 = gm_l7;
    gm_l3 = t18;
    uint64_t t19 = gm_l3;
    uint8_t t20 = (t19 < ((uint64_t)2ULL));
    if (t20) {
        gm_ttu09lij36f8c t21;
        memset(&t21, 0, sizeof t21);
        memmove(gm_out, ((uint8_t *)&t21), 40);
        goto gm_fdone;
    }
    void * t22 = gm_l0;
    void * t23 = (void *)(((uint8_t *)t22) + 16);
    gm_f_split_by_file_hgjqgsp((void *)&(*(gm_ttu09lij36f8c *)gm_out), (void *)NULL, cx, NULL, t23);
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
inline __attribute__((aligned(4))) uint8_t gm_f_exit_code_dv2nn7j(gm_tsoiri44d0i6q a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_exit_code_dv2nn7j F;
    gm_tsoiri44d0i6q gm_l0; /* status */
    uint32_t gm_l1; /* c */
    int32_t gm_l2; /* sig */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l3;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)0ULL));
            if (!t10) {
                goto gm_lb2;
            }
            uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
            gm_l1 = t11;
            uint32_t t12 = gm_l1;
            uint8_t t13 = (uint8_t)t12;
            gm_l3 = t13;
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t15 = (uint8_t)(t14 == ((uint8_t)1ULL));
                    if (!t15) {
                        goto gm_lb5;
                    }
                    int32_t t16 = GM_LD(int32_t, (((uint8_t *)&gm_l0) + 4));
                    uint8_t t17 = ((t16 == ((int32_t)4LL)));
                    if (!t17) {
                        goto gm_lb5;
                    }
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                    uint8_t t19 = (uint8_t)(t18 == ((uint8_t)1ULL));
                    if (!t19) {
                        goto gm_lb6;
                    }
                    int32_t t20 = GM_LD(int32_t, (((uint8_t *)&gm_l0) + 4));
                    uint8_t t21 = ((t20 == ((int32_t)5LL)));
                    if (!t21) {
                        goto gm_lb6;
                    }
                    goto gm_lb4;
                }
gm_lb6:;
                goto gm_lb3;
            }
gm_lb4:;
            gm_l3 = ((uint8_t)134ULL);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)1ULL));
            if (!t23) {
                goto gm_lb7;
            }
            int32_t t24 = GM_LD(int32_t, (((uint8_t *)&gm_l0) + 4));
            gm_l2 = t24;
            int32_t t25 = gm_l2;
            int32_t r26; bool o27 = __builtin_add_overflow(((int32_t)128LL), t25, &r26);
            if (o27) gm_trap("integer overflow in `+`", gm_srcqr4ch56kv6bv, 762, 24);
            int32_t t28 = r26;
            uint8_t t29 = (uint8_t)t28;
            gm_l3 = t29;
            goto gm_lb1;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 759, 54);
    }
gm_lb1:;
    uint8_t t30 = gm_l3;
    F.ret = t30;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_f16c_flag_vpo7tss(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_f16c_flag_vpo7tss F;
    GM_STACK_CHECK_SHALLOW(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_th2lqbo77pvsc gm_l1;
    {
        {
            uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a0) + 56));
            if (!t4) {
                goto gm_lb2;
            }
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 40));
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sje4rbc88csdu));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(6));
            uint8_t t7 = (gm_eq0iqiubfm7e(((uint8_t *)&t5), ((uint8_t *)&t6)));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sn6vmked7l7pf));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(6));
            gm_th2lqbo77pvsc t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t9), t8);
            ((uint8_t *)&t9)[16] = 1;
            gm_l1 = t9;
            goto gm_lb1;
        }
gm_lb2:;
        gm_th2lqbo77pvsc t10;
        memset(&t10, 0, sizeof t10);
        gm_l1 = t10;
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
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_f16c_flag_vpo7tss_c(gm_t0rfe1ou14h21 a0) { gm_th2lqbo77pvsc gm_r; gm_f_f16c_flag_vpo7tss(&gm_r, &a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_generate_program_9mr9pv8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, uint8_t a3, const void * a4) {
    struct gm_fr_generate_program_9mr9pv8 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* roots_in */
    gm_b16_8 gm_l2; /* entries */
    uint8_t gm_l3; /* select */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint32_t gm_l8; /* f */
    gm_tftmsj5vtqvej gm_l10; /* copts */
    gm_b16_8 gm_l12; /* rt */
    gm_b16_8 gm_l13; /* rt_h */
    gm_b16_8 gm_l14; /* rt_c */
    gm_t08olq97rscn3 gm_l15; /* u */
    gm_t08olq97rscn3 gm_l17; /* u */
    GM_STACK_CHECK(&F, 4556u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&F.l4, a4, sizeof F.l4);
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftmsj5vtqvej gm_l20;
    gm_th2lqbo77pvsc gm_l21;
    gm_b16_8 gm_l22;
    gm_tetdkgoreqmn1 gm_l23;
    gm_tetdkgoreqmn1 gm_l24;
    gm_tgvh15qshf643 gm_l25;
    gm_b16_8 gm_l26;
    gm_tgvh15qshf643 gm_l27;
    gm_b16_8 gm_l28;
    gm_t0u6dopi4f21f gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_tqgtd5gtgj65s gm_l31;
    gm_trqfvmjtt4sij t16;
    gm_f_list_go2qg0b((void *)&t16, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l5 = t16;
    void * t17 = (void *)((uint8_t *)&F.l5);
    gm_b16_8 t18 = gm_l1;
    gm_tftb1odc739t5 t19 = gm_f_extend_7hs67uj(t17, t18);
    gm_l18 = t19;
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcqr4ch56kv6bv, 113, 5);
    void * t21 = gm_l0;
    gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 920));
    gm_l6 = t22;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t23 = gm_l7;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t25 = (t23 < t24);
            if (!t25) {
                goto gm_lb1;
            }
            {
                void * t26 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t28 = gm_l7;
                {
                    {
                        uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)t26) + (uintptr_t)t28 * 4));
                        gm_l8 = t29;
                        uint32_t t30 = gm_l8;
                        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                        uint8_t t32 = 0;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t31) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t31))) + i * 4, ((uint8_t *)&t30))) { t32 = 1; break; }
                        uint8_t t33 = (uint8_t)(!t32);
                        if (t33) {
                            void * t34 = (void *)((uint8_t *)&F.l5);
                            uint32_t t35 = gm_l8;
                            gm_tftb1odc739t5 t36 = gm_f_push_im9d4ro(t34, t35);
                            gm_l19 = t36;
                            uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                            if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcqr4ch56kv6bv, 114, 61);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 114, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t38 = gm_l7;
            uint64_t r39; bool o40 = __builtin_add_overflow(t38, ((uint64_t)1ULL), &r39);
            uint64_t t41 = r39;
            gm_l7 = t41;
        }
    }
gm_lb1:;
    void * t42 = gm_l0;
    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
    gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
    uint8_t t45 = gm_f_prepare_codegen_4hnlio1((void *)NULL, cx, NULL, t42, t44);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t46 = (uint8_t)!t45;
    if (t46) {
        gm_t5i6e9e007icp t47;
        memset(&t47, 0, sizeof t47);
        memmove(gm_out, ((uint8_t *)&t47), 256);
        goto gm_fdone;
    }
    void * t48 = gm_l0;
    gm_ta4jakbus8leg t49;
    gm_f_prog_61arbs1((void *)&t49, (void *)NULL, cx, NULL, t48);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l9 = t49;
    {
        {
            uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 4) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)0ULL));
            if (!t51) {
                goto gm_lb7;
            }
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)gm_cgsmfcq937e));
            gm_tftmsj5vtqvej t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 3, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)1));
            GM_ST(uint8_t, ((uint8_t *)&t54 + 1), ((uint8_t)1));
            GM_ST(uint8_t, ((uint8_t *)&t54 + 2), t52);
            GM_ST(gm_b16_8, ((uint8_t *)&t54 + 8), t53);
            gm_l20 = t54;
            goto gm_lb6;
        }
gm_lb7:;
        {
            {
                {
                    uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 4) + 0));
                    uint8_t t56 = (uint8_t)(t55 == ((uint8_t)1ULL));
                    if (!t56) {
                        goto gm_lb10;
                    }
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 4) + 0));
                    uint8_t t58 = (uint8_t)(t57 == ((uint8_t)2ULL));
                    if (!t58) {
                        goto gm_lb11;
                    }
                    goto gm_lb9;
                }
gm_lb11:;
                goto gm_lb8;
            }
gm_lb9:;
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)gm_cgsmfcq937e));
            gm_tftmsj5vtqvej t61;
            GM_ST(uint8_t, (uint8_t *)&t61 + 3, 0);
            GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)1));
            GM_ST(uint8_t, ((uint8_t *)&t61 + 1), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t61 + 2), t59);
            GM_ST(gm_b16_8, ((uint8_t *)&t61 + 8), t60);
            gm_l20 = t61;
            goto gm_lb6;
        }
gm_lb8:;
        {
            uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 4) + 0));
            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)3ULL));
            if (!t63) {
                goto gm_lb12;
            }
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)gm_cgsmfcq937e));
            gm_tftmsj5vtqvej t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 3, 0);
            GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t66 + 1), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t66 + 2), t64);
            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t65);
            gm_l20 = t66;
            goto gm_lb6;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 117, 14);
    }
gm_lb6:;
    memmove(((uint8_t *)&gm_l10), ((uint8_t *)&gm_l20), 24);
    {
        {
            uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 33));
            if (!t67) {
                goto gm_lb14;
            }
            gm_b16_8 t68 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l9) + 80) + 24));
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_scq91tgm9lph4));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(5));
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sn3ltks10rrv7));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(7));
            gm_t6j6smj7qoctu t71;
            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t69);
            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 16), t70);
            uint8_t t72 = 0;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t71) + 0, ((uint8_t *)&t68))) t72 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t71) + 16, ((uint8_t *)&t68))) t72 = 1;
            uint8_t t73 = (uint8_t)(t72);
            if (!t73) {
                goto gm_lb14;
            }
            uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)&F.l9) + 80) + 56));
            uint8_t t75 = (uint8_t)!t74;
            if (!t75) {
                goto gm_lb14;
            }
            gm_b16_8 t76;
            GM_ST(void *, (uint8_t *)&t76, (void *)(gm_sg5khilhaakst));
            GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(15));
            gm_f_runtime_text_37culso((void *)&gm_l21, (void *)NULL, cx, NULL, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t77 = (uint8_t)((((uint8_t *)&gm_l21))[16] != 0);
            if (t77) {
                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                gm_l22 = t78;
            } else {
                gm_t5i6e9e007icp t79;
                memset(&t79, 0, sizeof t79);
                memmove(gm_out, ((uint8_t *)&t79), 256);
                goto gm_fdone;
            }
            gm_b16_8 t80 = gm_l22;
            GM_ST(gm_b16_8, (((uint8_t *)&gm_l10) + 8), t80);
            goto gm_lb13;
        }
gm_lb14:;
    }
gm_lb13:;
    uint8_t t81 = gm_l3;
    if (t81) {
        void * t82 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t83 = gm_l2;
        gm_tftmsj5vtqvej t84 = GM_LD(gm_tftmsj5vtqvej, ((uint8_t *)&gm_l10));
        gm_f_generate_tests_462i0us((void *)&gm_l23, (void *)NULL, cx, NULL, t82, t83, &t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint8_t t86 = ((t85 == ((uint64_t)0ULL)));
        if (t86) {
            void * t87 = (void *)((uint8_t *)&F.l9);
            gm_tftmsj5vtqvej t88 = GM_LD(gm_tftmsj5vtqvej, ((uint8_t *)&gm_l10));
            gm_f_generate_object_rgfvem4((void *)&gm_l24, (void *)NULL, cx, NULL, t87, &t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        } else {
            void * t89 = (void *)((uint8_t *)&F.l9);
            void * t90 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t91)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t91, gm_srcqr4ch56kv6bv, 127, 155);
            uint32_t t92 = GM_LD(uint32_t, (((uint8_t *)t90) + (uintptr_t)((uint64_t)0ULL) * 4));
            gm_tftmsj5vtqvej t93 = GM_LD(gm_tftmsj5vtqvej, ((uint8_t *)&gm_l10));
            gm_f_generate_6no5tn1((void *)&gm_l24, (void *)NULL, cx, NULL, t89, t92, &t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        memmove(((uint8_t *)&gm_l23), ((uint8_t *)&gm_l24), 224);
    }
    memmove(((uint8_t *)&F.l11), ((uint8_t *)&gm_l23), 224);
    gm_b16_8 t94 = gm_f_std_root_e0juohb((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t95 = gm_f_runtime_dir_qd07iff((void *)NULL, cx, NULL, t94);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t95;
    gm_b16_8 t96 = gm_l12;
    gm_b16_8 t97;
    GM_ST(void *, (uint8_t *)&t97, (void *)(gm_s55g9qp4arkn7));
    GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(7));
    gm_b16_8 t98 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t96, t97);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_read_all_r9rh9qd((void *)&gm_l25, (void *)NULL, cx, NULL, t98);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
    if (t99) {
        gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
        gm_l26 = t100;
    } else {
        gm_b16_8 t101 = gm_l12;
        if (gm_pf454qfrpt6r(cx, gm_sg87p4r5apham, 35, gm_srcqr4ch56kv6bv, 130, 9)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t101) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t101)), GM_LD(uintptr_t, ((uint8_t *)&t101) + 8), gm_srcqr4ch56kv6bv, 130, 9))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_srcqr4ch56kv6bv, 130, 9)) {
            goto gm_lad;
        }
        gm_t5i6e9e007icp t102;
        memset(&t102, 0, sizeof t102);
        memmove(gm_out, ((uint8_t *)&t102), 256);
        goto gm_fdone;
    }
    gm_b16_8 t103 = gm_l26;
    gm_l13 = t103;
    gm_b16_8 t104 = gm_l12;
    gm_b16_8 t105;
    GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sr98hl2rjopi2));
    GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(7));
    gm_b16_8 t106 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t104, t105);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_read_all_r9rh9qd((void *)&gm_l27, (void *)NULL, cx, NULL, t106);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
    if (t107) {
        gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
        gm_l28 = t108;
    } else {
        gm_b16_8 t109 = gm_l12;
        if (gm_pf454qfrpt6r(cx, gm_sg87p4r5apham, 35, gm_srcqr4ch56kv6bv, 134, 9)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t109) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t109)), GM_LD(uintptr_t, ((uint8_t *)&t109) + 8), gm_srcqr4ch56kv6bv, 134, 9))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_srcqr4ch56kv6bv, 134, 9)) {
            goto gm_lad;
        }
        gm_t5i6e9e007icp t110;
        memset(&t110, 0, sizeof t110);
        memmove(gm_out, ((uint8_t *)&t110), 256);
        goto gm_fdone;
    }
    gm_b16_8 t111 = gm_l28;
    gm_l14 = t111;
    uint8_t t112 = (uint8_t)!((((uint8_t *)&F.l11))[0] != 0);
    if (t112) {
        memmove(((uint8_t *)&gm_l29), (((uint8_t *)&F.l11) + 8), 216);
    } else {
        memmove(((uint8_t *)&gm_l15), (((uint8_t *)&F.l11) + 8), 32);
        void * t113 = gm_l0;
        gm_b0_1 t114 = gm_f_report_unsupported_9rg6nq1((void *)NULL, cx, NULL, t113, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l15)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5i6e9e007icp t115;
        memset(&t115, 0, sizeof t115);
        memmove(gm_out, ((uint8_t *)&t115), 256);
        goto gm_fdone;
    }
    memmove(((uint8_t *)&F.l16), ((uint8_t *)&gm_l29), 216);
    void * t116 = (void *)((uint8_t *)&F.l9);
    gm_b16_8 t117 = gm_l13;
    gm_b16_8 t118 = gm_l14;
    gm_t6j6smj7qoctu t119;
    GM_ST(gm_b16_8, ((uint8_t *)&t119 + 0), t117);
    GM_ST(gm_b16_8, ((uint8_t *)&t119 + 16), t118);
    gm_f_format_09mcfes((void *)&gm_l30, (void *)NULL, cx, NULL, &t119);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
    if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srcqr4ch56kv6bv, 141, 33);
    gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
    gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
    gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
    gm_f_check_export_names_mirc4me((void *)&gm_l31, (void *)NULL, cx, NULL, t116, t122, t123);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
    if (t124) {
    } else {
        memmove(((uint8_t *)&gm_l17), (((uint8_t *)&gm_l31) + 8), 32);
        void * t125 = gm_l0;
        gm_b0_1 t126 = gm_f_report_unsupported_9rg6nq1((void *)NULL, cx, NULL, t125, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l17)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5i6e9e007icp t127;
        memset(&t127, 0, sizeof t127);
        memmove(gm_out, ((uint8_t *)&t127), 256);
        goto gm_fdone;
    }
    gm_b16_8 t128 = gm_l13;
    gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
    gm_b16_8 t130 = gm_l14;
    gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
    gm_t0u6dopi4f21f t132 = GM_LD(gm_t0u6dopi4f21f, ((uint8_t *)&F.l16));
    gm_tt1457on428g4 t133;
    GM_ST(gm_t0u6dopi4f21f, ((uint8_t *)&t133 + 0), t132);
    GM_ST(gm_b16_8, ((uint8_t *)&t133 + 216), t129);
    GM_ST(gm_b16_8, ((uint8_t *)&t133 + 232), t131);
    gm_t5i6e9e007icp t134;
    GM_ST(uint8_t, (uint8_t *)&t134 + 249, 0);
    GM_ST(uint16_t, (uint8_t *)&t134 + 250, 0);
    GM_ST(uint32_t, (uint8_t *)&t134 + 252, 0);
    GM_ST(gm_tt1457on428g4, ((uint8_t *)&t134), t133);
    ((uint8_t *)&t134)[248] = 1;
    memmove(gm_out, ((uint8_t *)&t134), 256);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_extension_sdfv93u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_has_extension_sdfv93u F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* dot */
    GM_STACK_CHECK(&F, 229u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l3;
    gm_b16_8 gm_l4;
    gm_tf5srh4mp4b3i gm_l5;
    uint64_t gm_l6;
    gm_b16_8 t2 = gm_l0;
    gm_f_file_name_em7v8lv((void *)&gm_l3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
    if (t3) {
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
        gm_l4 = t4;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t5 = gm_l4;
    gm_l1 = t5;
    gm_b16_8 t6 = gm_l1;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sl3q52ep6aa53));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
    gm_tf5srh4mp4b3i t8 = gm_f_rfind_ai5s2js(t6, t7);
    gm_l5 = t8;
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
    if (t9) {
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
        gm_l6 = t10;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint64_t t11 = gm_l6;
    gm_l2 = t11;
    uint64_t t12 = gm_l2;
    uint8_t t13 = (t12 > ((uint64_t)0ULL));
    F.ret = t13;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_library_stamps_mjeqafa(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_library_stamps_mjeqafa F;
    gm_b16_8 gm_l0; /* libs */
    gm_b16_8 gm_l1; /* ldflags */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_b16_8 gm_l6; /* f */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* lib */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* d */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l18; /* ext */
    gm_b16_8 gm_l19; /* path */
    gm_t5j14m8ru132p gm_l20; /* info */
    GM_STACK_CHECK(&F, 1730u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf1bjg81u0glv gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tabb710d9mq2e gm_l24;
    gm_tih6h4avv5nrb gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_trqfvmjtt4sij t26;
    gm_f_list_epvducg((void *)&t26, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t26;
    gm_b16_8 t27 = gm_l1;
    gm_l3 = t27;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t28 = gm_l4;
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t30 = (t28 < t29);
            if (!t30) {
                goto gm_lb1;
            }
            {
                uint64_t t31 = gm_l4;
                void * t32 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t34 = gm_l4;
                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t32) + (uintptr_t)t34 * 16));
                gm_tf1bjg81u0glv t36;
                GM_ST(uint64_t, ((uint8_t *)&t36 + 0), t31);
                GM_ST(gm_b16_8, ((uint8_t *)&t36 + 8), t35);
                gm_l21 = t36;
                {
                    {
                        uint64_t t37 = GM_LD(uint64_t, ((uint8_t *)&gm_l21));
                        gm_l5 = t37;
                        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                        gm_l6 = t38;
                        {
                            {
                                gm_b16_8 t39 = gm_l6;
                                gm_b16_8 t40;
                                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s4lj2bemc87jp));
                                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(2));
                                uint8_t t41 = (gm_eq0iqiubfm7e(((uint8_t *)&t39), ((uint8_t *)&t40)));
                                if (!t41) {
                                    goto gm_lb7;
                                }
                                uint64_t t42 = gm_l5;
                                uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                                if (o44) gm_trap("integer overflow in `+`", gm_srcqr4ch56kv6bv, 733, 26);
                                uint64_t t45 = r43;
                                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                uint8_t t47 = (t45 < t46);
                                if (!t47) {
                                    goto gm_lb7;
                                }
                                void * t48 = (void *)((uint8_t *)&F.l2);
                                void * t49 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                uint64_t t51 = gm_l5;
                                uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                                if (o53) gm_trap("integer overflow in `+`", gm_srcqr4ch56kv6bv, 734, 31);
                                uint64_t t54 = r52;
                                if ((uintptr_t)t54 >= (uintptr_t)(t50)) gm_trap_index((uintptr_t)t54, t50, gm_srcqr4ch56kv6bv, 734, 23);
                                gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)t49) + (uintptr_t)t54 * 16));
                                gm_tftb1odc739t5 t56 = gm_f_push_3ef746b(t48, t55);
                                gm_l22 = t56;
                                uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                                if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcqr4ch56kv6bv, 734, 13);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_b16_8 t58 = gm_l6;
                            gm_b16_8 t59;
                            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s4lj2bemc87jp));
                            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(2));
                            uint8_t t60 = gm_f_starts_with_6vvt01e(t58, t59);
                            if (t60) {
                                void * t61 = (void *)((uint8_t *)&F.l2);
                                void * t62 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint64_t t64 = (uintptr_t)((uint64_t)2ULL);
                                uint64_t t65 = (uintptr_t)t63;
                                if (t64 > t65 || t65 > (uintptr_t)t63) gm_trap_slice(t64, t65, t63, gm_srcqr4ch56kv6bv, 736, 23);
                                uint64_t t66 = ((uintptr_t)t63) - (uintptr_t)((uint64_t)2ULL);
                                gm_b16_8 t67;
                                GM_ST(void *, (uint8_t *)&t67, (void *)(((uint8_t *)t62 + (uintptr_t)((uint64_t)2ULL) * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(t66));
                                gm_tftb1odc739t5 t68 = gm_f_push_3ef746b(t61, t67);
                                gm_l23 = t68;
                                uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcqr4ch56kv6bv, 736, 13);
                            }
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 732, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t70 = gm_l4;
            uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
            uint64_t t73 = r71;
            gm_l4 = t73;
        }
    }
gm_lb1:;
    gm_trqfvmjtt4sij t74;
    gm_f_list_epvducg((void *)&t74, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l7 = t74;
    gm_b16_8 t75 = gm_l0;
    gm_l8 = t75;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t76 = gm_l9;
            uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t78 = (t76 < t77);
            if (!t78) {
                goto gm_lb8;
            }
            {
                void * t79 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t81 = gm_l9;
                {
                    {
                        gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)t79) + (uintptr_t)t81 * 16));
                        gm_l10 = t82;
                        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
                        gm_l11 = t83;
                        gm_l12 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t84 = gm_l12;
                                uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                uint8_t t86 = (t84 < t85);
                                if (!t86) {
                                    goto gm_lb13;
                                }
                                {
                                    void * t87 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                    uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                    uint64_t t89 = gm_l12;
                                    {
                                        {
                                            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)t87) + (uintptr_t)t89 * 16));
                                            gm_l13 = t90;
                                            gm_b16_8 t91;
                                            GM_ST(void *, (uint8_t *)&t91, (void *)(gm_so78814tnt5g5));
                                            GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(1));
                                            gm_b16_8 t92;
                                            GM_ST(void *, (uint8_t *)&t92, (void *)(gm_svtktff6b3snn));
                                            GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(2));
                                            gm_b16_8 t93;
                                            GM_ST(void *, (uint8_t *)&t93, (void *)(gm_s6vtoqhgjb0nd));
                                            GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(5));
                                            gm_tfeab279c16rr t94;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t94 + 0), t91);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t94 + 16), t92);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t94 + 32), t93);
                                            F.l14 = t94;
                                            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 16));
                                            gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 32));
                                            gm_t9bmh80edou9c t98;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t98 + 0), t95);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t98 + 16), t96);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t98 + 32), t97);
                                            F.l15 = t98;
                                            void * t99 = (void *)((uint8_t *)&F.l15);
                                            uint64_t t100 = (uintptr_t)((uint64_t)0ULL);
                                            uint64_t t101 = (uintptr_t)((uint64_t)3ULL);
                                            if (t100 > t101 || t101 > (uintptr_t)((uint64_t)3ULL)) gm_trap_slice(t100, t101, ((uint64_t)3ULL), gm_srcqr4ch56kv6bv, 742, 13);
                                            uint64_t t102 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
                                            gm_b16_8 t103;
                                            GM_ST(void *, (uint8_t *)&t103, (void *)(((uint8_t *)t99 + (uintptr_t)((uint64_t)0ULL) * 16)));
                                            GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(t102));
                                            gm_l16 = t103;
                                            gm_l17 = ((uint64_t)0ULL);
                                            {
                                                for (;;) {
                                                    uint64_t t104 = gm_l17;
                                                    uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                                    uint8_t t106 = (t104 < t105);
                                                    if (!t106) {
                                                        goto gm_lb18;
                                                    }
                                                    {
                                                        void * t107 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                                        uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                                        uint64_t t109 = gm_l17;
                                                        {
                                                            {
                                                                gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)t107) + (uintptr_t)t109 * 16));
                                                                gm_l18 = t110;
                                                                gm_b16_8 t111 = gm_l13;
                                                                gm_b16_8 t112 = gm_l10;
                                                                gm_b16_8 t113 = gm_l18;
                                                                gm_t6j6smj7qoctu t114;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t114 + 0), t112);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t114 + 16), t113);
                                                                gm_f_format_rjqt2eo((void *)&gm_l24, (void *)NULL, cx, NULL, &t114);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                                                if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_srcqr4ch56kv6bv, 743, 38);
                                                                gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                                                                gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
                                                                gm_b16_8 t118 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t111, t117);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                gm_l19 = t118;
                                                                {
                                                                    {
                                                                        gm_b16_8 t119 = gm_l19;
                                                                        gm_node *pn120 = gm_find(cx, 2126215853);
                                                                        if (!pn120) gm_trap("no handler installed for effect `Fs`", gm_srcqr4ch56kv6bv, 744, 28);
                                                                        gm_tih6h4avv5nrb t121;
                                                                        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn120->clauses[5])((void *)&t121, pn120->env, pn120->hctx, pn120, t119);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        gm_l25 = t121;
                                                                        uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                                                        if (!t122) {
                                                                            goto gm_lb24;
                                                                        }
                                                                        memmove(((uint8_t *)&gm_l20), (((uint8_t *)&gm_l25) + 8), 32);
                                                                        void * t123 = (void *)((uint8_t *)&F.l7);
                                                                        gm_b16_8 t124 = gm_l19;
                                                                        uint64_t t125 = GM_LD(uint64_t, ((uint8_t *)&gm_l20));
                                                                        int64_t t126 = GM_LD(int64_t, (((uint8_t *)&gm_l20) + 16));
                                                                        gm_t2u4qamcbbgu0 t127;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t127 + 0), t124);
                                                                        GM_ST(uint64_t, ((uint8_t *)&t127 + 16), t125);
                                                                        GM_ST(int64_t, ((uint8_t *)&t127 + 24), t126);
                                                                        gm_f_format_a1j85ht((void *)&gm_l26, (void *)NULL, cx, NULL, &t127);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t128 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                                                        if (!t128) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_srcqr4ch56kv6bv, 745, 33);
                                                                        gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                                                                        gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                                                                        gm_tftb1odc739t5 t131 = gm_f_push_3ef746b(t123, t130);
                                                                        gm_l27 = t131;
                                                                        uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                                                        if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcqr4ch56kv6bv, 745, 21);
                                                                        goto gm_lb23;
                                                                    }
gm_lb24:;
                                                                }
gm_lb23:;
                                                                goto gm_lb21;
                                                            }
gm_lb22:;
                                                            gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 742, 13);
                                                        }
gm_lb21:;
                                                    }
gm_lb20:;
                                                    uint64_t t133 = gm_l17;
                                                    uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                                                    uint64_t t136 = r134;
                                                    gm_l17 = t136;
                                                }
                                            }
gm_lb18:;
                                            goto gm_lb16;
                                        }
gm_lb17:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 741, 9);
                                    }
gm_lb16:;
                                }
gm_lb15:;
                                uint64_t t137 = gm_l12;
                                uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                                uint64_t t140 = r138;
                                gm_l12 = t140;
                            }
                        }
gm_lb13:;
                        goto gm_lb11;
                    }
gm_lb12:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 740, 5);
                }
gm_lb11:;
            }
gm_lb10:;
            uint64_t t141 = gm_l9;
            uint64_t r142; bool o143 = __builtin_add_overflow(t141, ((uint64_t)1ULL), &r142);
            uint64_t t144 = r142;
            gm_l9 = t144;
        }
    }
gm_lb8:;
    gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
    gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&t145));
    gm_b16_8 t147;
    GM_ST(void *, (uint8_t *)&t147, (void *)(gm_s0pi0emdqd3vb));
    GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(1));
    gm_f_join_sfknljb((void *)&gm_l28, (void *)NULL, cx, NULL, t146, t147);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t148 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
    if (!t148) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srcqr4ch56kv6bv, 750, 12);
    gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
    gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
    F.ret = t150;
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
inline __attribute__((aligned(4))) void gm_f_lto_link_flags_0rmh9d3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_lto_link_flags_0rmh9d3 F;
    gm_b16_8 gm_l1; /* dir */
    GM_STACK_CHECK(&F, 318u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tftb1odc739t5 gm_l2;
    gm_tftb1odc739t5 gm_l3;
    gm_th2lqbo77pvsc gm_l4;
    gm_tabb710d9mq2e gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_trqfvmjtt4sij t4;
    gm_f_list_epvducg((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l0 = t4;
    void * t5 = (void *)((uint8_t *)&F.l0);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)gm_c302jkvduvb));
    gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
    gm_l2 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcqr4ch56kv6bv, 447, 5);
    void * t9 = (void *)((uint8_t *)&F.l0);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s2bp4nvcgkqlc));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(12));
    gm_tftb1odc739t5 t11 = gm_f_push_3ef746b(t9, t10);
    gm_l3 = t11;
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcqr4ch56kv6bv, 448, 5);
    {
        {
            gm_f_cache_dir_81h90qu((void *)&gm_l4, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
            if (!t13) {
                goto gm_lb2;
            }
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_l1 = t14;
            void * t15 = (void *)((uint8_t *)&F.l0);
            gm_b16_8 t16 = gm_l1;
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sngj7pcs9ksq7));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(7));
            gm_b16_8 t18 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t19;
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
            gm_f_format_hcgsrs3((void *)&gm_l5, (void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_srcqr4ch56kv6bv, 450, 20);
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
            gm_tftb1odc739t5 t23 = gm_f_push_3ef746b(t15, t22);
            gm_l6 = t23;
            uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcqr4ch56kv6bv, 450, 9);
            void * t25 = (void *)((uint8_t *)&F.l0);
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sagpv7fgcnm3e));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(63));
            gm_tftb1odc739t5 t27 = gm_f_push_3ef746b(t25, t26);
            gm_l7 = t27;
            uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcqr4ch56kv6bv, 451, 9);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&F.l0), 40);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_opt_level_gnm5dcm(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_opt_level_gnm5dcm F;
    void * gm_l0; /* opts */
    GM_STACK_CHECK_SHALLOW(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    gm_tqs5p4n2tq19v gm_l2;
    void * t6 = gm_l0;
    gm_t9qkh0rsooc8d t7 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t6) + 4));
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
    uint8_t t9 = (uint8_t)((t8 == ((uint8_t)0ULL)));
    void * t10 = gm_l0;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t10) + 33));
    gm_tqs5p4n2tq19v t12;
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), t9);
    GM_ST(uint8_t, ((uint8_t *)&t12 + 1), t11);
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
    gm_l2 = t12;
    {
        {
            uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
            uint8_t t15 = ((t14 == ((uint8_t)1)));
            if (!t15) {
                goto gm_lb2;
            }
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 1));
            uint8_t t17 = ((t16 == ((uint8_t)1)));
            if (!t17) {
                goto gm_lb2;
            }
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s3vboaodpmi9a));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
            gm_l1 = t18;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
            uint8_t t20 = ((t19 == ((uint8_t)1)));
            if (!t20) {
                goto gm_lb3;
            }
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 1));
            uint8_t t22 = ((t21 == ((uint8_t)0)));
            if (!t22) {
                goto gm_lb3;
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_shgi3g6gdv99o));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(3));
            gm_l1 = t23;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)&gm_l2));
            uint8_t t25 = ((t24 == ((uint8_t)0)));
            if (!t25) {
                goto gm_lb4;
            }
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sr2df1mc0vbea));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(3));
            gm_l1 = t26;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 159, 52);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_pid_token_c444nkv(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_pid_token_c444nkv F;
    gm_tpvg3mjn3dp06 gm_l0; /* r */
    GM_STACK_CHECK(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * t2 = (void *)((uint8_t *)&gm_l0);
    uint64_t t3 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t4 = (uintptr_t)((uint64_t)4ULL);
    if (t3 > t4 || t4 > (uintptr_t)((uint64_t)4ULL)) gm_trap_slice(t3, t4, ((uint64_t)4ULL), gm_srcqr4ch56kv6bv, 473, 15);
    uint64_t t5 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(((uint8_t *)t2 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(t5));
    gm_node *pn7 = gm_find(cx, 1087599154);
    if (!pn7) gm_trap("no handler installed for effect `Rand`", gm_srcqr4ch56kv6bv, 473, 5);
    gm_b0_1 t8;
    t8 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn7->clauses[0])(pn7->env, pn7->hctx, pn7, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t9 = (void *)((uint8_t *)&gm_l0);
    uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t11 = (uintptr_t)((uint64_t)4ULL);
    if (t10 > t11 || t11 > (uintptr_t)((uint64_t)4ULL)) gm_trap_slice(t10, t11, ((uint64_t)4ULL), gm_srcqr4ch56kv6bv, 474, 20);
    uint64_t t12 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12));
    gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
    gm_b16_8 t15 = gm_f_hex_euih242((void *)NULL, cx, NULL, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t15;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_unsupported_9rg6nq1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_report_unsupported_9rg6nq1 F;
    void * gm_l0; /* s */
    gm_t08olq97rscn3 gm_l1; /* u */
    void * gm_l2; /* file */
    gm_tcc1rq4n1avb2 gm_l3; /* lc */
    gm_b16_8 gm_l4; /* place */
    GM_STACK_CHECK(&F, 346u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_tl874t2atnlh3 t2 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t4 = (gm_eqff85ac10o3(((uint8_t *)&t2), ((uint8_t *)&t3)));
    if (t4) {
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(0));
        gm_l5 = t5;
    } else {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, ((uint8_t *)t6));
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)a1) + 16));
        void * t10 = gm_f_file_o15fqku(t8, t9);
        gm_l2 = t10;
        void * t11 = gm_l2;
        uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)a1) + 16) + 4));
        gm_tcc1rq4n1avb2 t13 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t11, t12));
        gm_l3 = t13;
        void * t14 = gm_l2;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)t14));
        uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
        uint32_t t17 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        gm_t5hdnnftkup6m t18;
        GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t15);
        GM_ST(uint32_t, ((uint8_t *)&t18 + 16), t16);
        GM_ST(uint32_t, ((uint8_t *)&t18 + 20), t17);
        gm_f_format_cv7qh05((void *)&gm_l6, (void *)NULL, cx, NULL, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srcqr4ch56kv6bv, 153, 9);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
        gm_l5 = t21;
    }
    gm_b16_8 t22 = gm_l5;
    gm_l4 = t22;
    gm_b16_8 t23 = gm_l4;
    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)a1));
    if ((GM_LD(uintptr_t, ((uint8_t *)&t23) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t23)), GM_LD(uintptr_t, ((uint8_t *)&t23) + 8), gm_srcqr4ch56kv6bv, 155, 5))) {
        goto gm_lad;
    }
    if (gm_pf454qfrpt6r(cx, gm_sfss9c30cdmun, 7, gm_srcqr4ch56kv6bv, 155, 5)) {
        goto gm_lad;
    }
    if ((GM_LD(uintptr_t, ((uint8_t *)&t24) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t24)), GM_LD(uintptr_t, ((uint8_t *)&t24) + 8), gm_srcqr4ch56kv6bv, 155, 5))) {
        goto gm_lad;
    }
    if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 155, 5)) {
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
inline __attribute__((aligned(4))) void gm_f_rt_flags_oeql34c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_rt_flags_oeql34c F;
    void * gm_l0; /* opts */
    gm_b16_8 gm_l2; /* f */
    GM_STACK_CHECK(&F, 281u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l3;
    gm_th2lqbo77pvsc gm_l4;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    void * t4 = gm_l0;
    gm_trqfvmjtt4sij t5;
    gm_f_base_cflags_irg71ui((void *)&t5, (void *)NULL, cx, NULL, &(*(gm_t0rfe1ou14h21 *)(((uint8_t *)t4) + 424)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t5;
    void * t6 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sr2df1mc0vbea));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(3));
    gm_tftb1odc739t5 t8 = gm_f_push_3ef746b(t6, t7);
    gm_l3 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcqr4ch56kv6bv, 273, 5);
    {
        {
            void * t10 = gm_l0;
            gm_t0rfe1ou14h21 t11 = GM_LD(gm_t0rfe1ou14h21, (((uint8_t *)t10) + 424));
            gm_f_f16c_flag_vpo7tss((void *)&gm_l4, &t11);
            uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l4))[16] != 0);
            if (!t12) {
                goto gm_lb2;
            }
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_l2 = t13;
            void * t14 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t15 = gm_l2;
            gm_tftb1odc739t5 t16 = gm_f_push_3ef746b(t14, t15);
            gm_l5 = t16;
            uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcqr4ch56kv6bv, 274, 38);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t18 = gm_l0;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t18) + 33));
    if (t19) {
        void * t20 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sn0peadng5i31));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t22 = gm_f_push_3ef746b(t20, t21);
        gm_l6 = t22;
        uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcqr4ch56kv6bv, 275, 26);
    }
    memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) uint8_t gm_f_run_compiler_ulaqcb1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_run_compiler_ulaqcb1 F;
    void * gm_l0; /* tc */
    gm_b16_8 gm_l1; /* argv */
    gm_tj5jn79kq9pnm gm_l2; /* e */
    GM_STACK_CHECK(&F, 441u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tat31u3lvqjk4 gm_l4;
    gm_tjm4rm50v5mle gm_l5;
    gm_b16_8 t2 = gm_l1;
    gm_t32dckm4jfarl t3 = GM_LD(gm_t32dckm4jfarl, ((uint8_t *)gm_c7mc3uaor1r));
    gm_f_output_mg6eie5((void *)&gm_l4, (void *)NULL, cx, NULL, t2, &t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (t4) {
        memmove(((uint8_t *)&gm_l5), (((uint8_t *)&gm_l4) + 8), 96);
    } else {
        gm_tj5jn79kq9pnm t5 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&gm_l4) + 8));
        gm_l2 = t5;
        void * t6 = gm_l0;
        gm_b16_8 t7 = gm_f_display_30kc1i9((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tj5jn79kq9pnm t8 = gm_l2;
        gm_b16_8 t9 = gm_f_run_error_gs1dinp((void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_sr3et5tffndla, 27, gm_srcqr4ch56kv6bv, 605, 9)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t7) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t7) + 8), gm_srcqr4ch56kv6bv, 605, 9))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_sqdfr4k3vea5p, 3, gm_srcqr4ch56kv6bv, 605, 9)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t9) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t9)), GM_LD(uintptr_t, ((uint8_t *)&t9) + 8), gm_srcqr4ch56kv6bv, 605, 9))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 605, 9)) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l5), 96);
    gm_tsoiri44d0i6q t10 = GM_LD(gm_tsoiri44d0i6q, ((uint8_t *)&F.l3));
    uint8_t t11 = gm_f_success_q3lb6vt(t10);
    uint8_t t12 = (uint8_t)!t11;
    if (t12) {
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        if (gm_pf454qfrpt6r(cx, gm_s1ctr86inn5iu, 23, gm_srcqr4ch56kv6bv, 609, 9)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t14) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t14)), GM_LD(uintptr_t, ((uint8_t *)&t14) + 8), gm_srcqr4ch56kv6bv, 609, 9))) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)0);
        goto gm_fret;
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
__attribute__((aligned(4))) uint8_t gm_f_run_dg4eogs(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, const void * a3) {
    struct gm_fr_run_dg4eogs F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* root */
    gm_b16_8 gm_l2; /* input */
    uint64_t gm_l4; /* d */
    uint32_t gm_l5; /* main */
    gm_tdvq40fmif5j0 gm_l6; /* entries */
    gm_b16_8 gm_l8; /* exe */
    gm_b16_8 gm_l9; /* dir */
    gm_b16_8 gm_l10; /* path */
    gm_b16_8 gm_l12; /* e */
    gm_toe5fmr3vvf4d gm_l16; /* e */
    gm_tsoiri44d0i6q gm_l17; /* status */
    GM_STACK_CHECK(&F, 3480u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&F.l3, a3, sizeof F.l3);
    gm_tf5srh4mp4b3i gm_l18;
    uint64_t gm_l19;
    gm_t6qooom8qa2do gm_l20;
    gm_t5i6e9e007icp gm_l21;
    gm_tt1457on428g4 gm_l22;
    gm_tl572d7579hns gm_l23;
    gm_t5uv522f0p612 gm_l24;
    gm_b16_8 gm_l25;
    gm_t470aqkia5212 gm_l26;
    gm_trqfvmjtt4sij gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tlmjd3nndlf3r gm_l32;
    gm_tsoiri44d0i6q gm_l33;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    uint64_t t11 = gm_l1;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sog4rksrmsqd2));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(4));
    gm_tf5srh4mp4b3i t13 = gm_f_lookup_59juqoo(t10, t11, t12);
    gm_l18 = t13;
    uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l18))[8] != 0);
    if (t14) {
        uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
        gm_l19 = t15;
    } else {
        if (gm_pf454qfrpt6r(cx, gm_setv7ejg35cg9, 42, gm_srcqr4ch56kv6bv, 632, 9)) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)1ULL);
        goto gm_fret;
    }
    uint64_t t16 = gm_l19;
    gm_l4 = t16;
    {
        {
            void * t18 = gm_l0;
            uint64_t t19 = gm_l4;
            gm_f_resolve_decl_pus042v((void *)&gm_l20, (void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_ll17;
            }
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)5ULL));
            if (!t21) {
                goto gm_lb2;
            }
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 8));
            gm_l5 = t22;
            {
                void * t24 = gm_l0;
                uint32_t t25 = gm_l5;
                uint32_t t26 = gm_f_entry_point_n2gu79u((void *)NULL, cx, NULL, t24, t25);
                if (gm_unw.kind) {
                    goto gm_ll17;
                }
                gm_tdvq40fmif5j0 t27;
                GM_ST(uint32_t, ((uint8_t *)&t27 + 0), t26);
                gm_l6 = t27;
                void * t28 = gm_l0;
                void * t29 = (void *)((uint8_t *)&gm_l6);
                uint64_t t30 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t31 = (uintptr_t)((uint64_t)1ULL);
                if (t30 > t31 || t31 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t30, t31, ((uint64_t)1ULL), gm_srcqr4ch56kv6bv, 637, 30);
                uint64_t t32 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t29 + (uintptr_t)((uint64_t)0ULL) * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32));
                gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                void * t35 = (void *)((uint8_t *)&gm_l6);
                uint64_t t36 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t37 = (uintptr_t)((uint64_t)1ULL);
                if (t36 > t37 || t37 > (uintptr_t)((uint64_t)1ULL)) gm_trap_slice(t36, t37, ((uint64_t)1ULL), gm_srcqr4ch56kv6bv, 637, 43);
                uint64_t t38 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t39;
                GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t35 + (uintptr_t)((uint64_t)0ULL) * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38));
                gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
                gm_tcn5ttqhv05g6 t41 = GM_LD(gm_tcn5ttqhv05g6, ((uint8_t *)&F.l3));
                gm_f_generate_program_9mr9pv8((void *)&gm_l21, (void *)NULL, cx, NULL, t28, t34, t40, ((uint8_t)0), &t41);
                if (gm_unw.kind) {
                    goto gm_ll17;
                }
                uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l21))[248] != 0);
                if (t42) {
                    memmove(((uint8_t *)&gm_l22), ((uint8_t *)&gm_l21), 248);
                } else {
                    F.ret = ((uint8_t)1ULL);
                    goto gm_fret;
                }
                memmove(((uint8_t *)&F.l7), ((uint8_t *)&gm_l22), 248);
                gm_b16_8 t43;
                GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(0));
                gm_l8 = t43;
                gm_b16_8 t44;
                GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(0));
                gm_l9 = t44;
                void * t45 = (void *)((uint8_t *)&F.l7);
                gm_tcn5ttqhv05g6 t46 = GM_LD(gm_tcn5ttqhv05g6, ((uint8_t *)&F.l3));
                gm_f_cached_program_5afqj27((void *)&gm_l23, (void *)NULL, cx, NULL, t45, &t46);
                if (gm_unw.kind) {
                    goto gm_ll17;
                }
                {
                    {
                        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                        uint8_t t48 = (uint8_t)(t47 == ((uint8_t)0ULL));
                        if (!t48) {
                            goto gm_lb5;
                        }
                        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                        gm_l10 = t49;
                        gm_b16_8 t50 = gm_l10;
                        gm_l8 = t50;
                        goto gm_lb4;
                    }
gm_lb5:;
                    {
                        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                        uint8_t t52 = (uint8_t)(t51 == ((uint8_t)2ULL));
                        if (!t52) {
                            goto gm_lb6;
                        }
                        F.ret = ((uint8_t)1ULL);
                        goto gm_fret;
                    }
gm_lb6:;
                    {
                        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                        uint8_t t54 = (uint8_t)(t53 == ((uint8_t)1ULL));
                        if (!t54) {
                            goto gm_lb7;
                        }
                        gm_b16_8 t55 = gm_f_temp_dir_i6apmsd((void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                        gm_b16_8 t56;
                        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sehokd57s045g));
                        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(10));
                        void * t57 = (void *)((uint8_t *)&F.l11);
                        uint64_t t58 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t59 = (uintptr_t)((uint64_t)4096ULL);
                        if (t58 > t59 || t59 > (uintptr_t)((uint64_t)4096ULL)) gm_trap_slice(t58, t59, ((uint64_t)4096ULL), gm_srcqr4ch56kv6bv, 646, 65);
                        uint64_t t60 = ((uintptr_t)((uint64_t)4096ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t61;
                        GM_ST(void *, (uint8_t *)&t61, (void *)(((uint8_t *)t57 + (uintptr_t)((uint64_t)0ULL) * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(t60));
                        gm_f_make_temp_dir_vi8f88d((void *)&gm_l24, (void *)NULL, cx, NULL, t55, t56, t61);
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                        uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                        if (t62) {
                            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                            gm_l25 = t63;
                        } else {
                            gm_b16_8 t64 = gm_f_temp_dir_i6apmsd((void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            if (gm_pf454qfrpt6r(cx, gm_svaibtfuu0gf2, 47, gm_srcqr4ch56kv6bv, 647, 17)) {
                                goto gm_ll17;
                            }
                            if ((GM_LD(uintptr_t, ((uint8_t *)&t64) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t64)), GM_LD(uintptr_t, ((uint8_t *)&t64) + 8), gm_srcqr4ch56kv6bv, 647, 17))) {
                                goto gm_ll17;
                            }
                            if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_srcqr4ch56kv6bv, 647, 17)) {
                                goto gm_ll17;
                            }
                            F.ret = ((uint8_t)1ULL);
                            goto gm_fret;
                        }
                        gm_b16_8 t65 = gm_l25;
                        gm_l9 = t65;
                        gm_b16_8 t66 = gm_l9;
                        void * t67 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t68;
                        GM_ST(void *, (uint8_t *)&t68, (void *)(gm_slgjpcgk90nuf));
                        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(4));
                        gm_b16_8 t69 = gm_f_native_exe_name_rqchv3n((void *)NULL, cx, NULL, t67, t68);
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                        gm_b16_8 t70 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t66, t69);
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                        gm_l8 = t70;
                        void * t71 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t72 = gm_l8;
                        uint8_t t73 = gm_f_compile_built_8o9nmfu((void *)NULL, cx, NULL, t71, t72, &(*(gm_tcn5ttqhv05g6 *)((uint8_t *)&F.l3)));
                        if (gm_unw.kind) {
                            goto gm_ll17;
                        }
                        uint8_t t74 = (uint8_t)!t73;
                        if (t74) {
                            gm_b16_8 t75 = gm_l9;
                            gm_ttln0d0hd70en t76 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t75);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            F.ret = ((uint8_t)1ULL);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 641, 5);
                }
gm_lb4:;
                gm_b16_8 t77 = gm_l8;
                gm_f_runner_for_79o4h4m((void *)&gm_l26, (void *)NULL, cx, NULL, t77);
                if (gm_unw.kind) {
                    goto gm_ld23_1;
                }
                uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                if (t78) {
                    memmove(((uint8_t *)&gm_l27), (((uint8_t *)&gm_l26) + 8), 40);
                } else {
                    gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                    gm_l12 = t79;
                    gm_b16_8 t80 = gm_l12;
                    if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_srcqr4ch56kv6bv, 659, 9)) {
                        goto gm_ld23_1;
                    }
                    if ((GM_LD(uintptr_t, ((uint8_t *)&t80) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t80)), GM_LD(uintptr_t, ((uint8_t *)&t80) + 8), gm_srcqr4ch56kv6bv, 659, 9))) {
                        goto gm_ld23_1;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 659, 9)) {
                        goto gm_ld23_1;
                    }
                    F.ret = ((uint8_t)2ULL);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                    goto gm_ld23_1;
                }
                memmove(((uint8_t *)&F.l13), ((uint8_t *)&gm_l27), 40);
                gm_trqfvmjtt4sij t81;
                gm_f_list_epvducg((void *)&t81, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_ld23_1;
                }
                F.l14 = t81;
                gm_t9qkh0rsooc8d t82 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)gm_cc7b06pnl1q));
                gm_t9qkh0rsooc8d t83 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)gm_cc7b06pnl1q));
                gm_t9qkh0rsooc8d t84 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)gm_cc7b06pnl1q));
                gm_th2lqbo77pvsc t85;
                memset(&t85, 0, sizeof t85);
                gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)gm_clslitrb510));
                gm_th2lqbo77pvsc t87;
                memset(&t87, 0, sizeof t87);
                gm_t32dckm4jfarl t88;
                GM_ST(uint8_t, (uint8_t *)&t88 + 3, 0);
                GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
                GM_ST(uint16_t, (uint8_t *)&t88 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t88 + 52, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t88 + 0), t82);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t88 + 1), t83);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t88 + 2), t84);
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t88 + 8), t85);
                GM_ST(gm_b16_8, ((uint8_t *)&t88 + 32), t86);
                GM_ST(uint8_t, ((uint8_t *)&t88 + 48), ((uint8_t)0));
                GM_ST(uint8_t, ((uint8_t *)&t88 + 49), ((uint8_t)0));
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t88 + 56), t87);
                F.l15 = t88;
                uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 8));
                uint8_t t90 = ((t89 == ((uint64_t)0ULL)));
                if (t90) {
                    void * t91 = (void *)((uint8_t *)&F.l14);
                    gm_b16_8 t92 = gm_l2;
                    gm_tftb1odc739t5 t93 = gm_f_push_3ef746b(t91, t92);
                    gm_l28 = t93;
                    uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                    if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcqr4ch56kv6bv, 665, 9);
                    gm_b16_8 t95 = gm_l8;
                    gm_th2lqbo77pvsc t96;
                    GM_ST(uint8_t, (uint8_t *)&t96 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t96 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t96 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t96), t95);
                    ((uint8_t *)&t96)[16] = 1;
                    GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l15) + 56), t96);
                } else {
                    void * t97 = (void *)((uint8_t *)&F.l14);
                    gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                    gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                    gm_tftb1odc739t5 t100 = gm_f_extend_vogsd72(t97, t99);
                    gm_l29 = t100;
                    uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                    if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcqr4ch56kv6bv, 668, 9);
                    void * t102 = (void *)((uint8_t *)&F.l14);
                    gm_b16_8 t103 = gm_l8;
                    gm_tftb1odc739t5 t104 = gm_f_push_3ef746b(t102, t103);
                    gm_l30 = t104;
                    uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                    if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srcqr4ch56kv6bv, 669, 9);
                }
                void * t106 = (void *)((uint8_t *)&F.l14);
                gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
                gm_tftb1odc739t5 t108 = gm_f_extend_vogsd72(t106, t107);
                gm_l31 = t108;
                uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcqr4ch56kv6bv, 671, 5);
                gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
                gm_tlmjd3nndlf3r t112 = gm_f_status_20hgo8k((void *)NULL, cx, NULL, t111, &(*(gm_t32dckm4jfarl *)((uint8_t *)&F.l15)));
                if (gm_unw.kind) {
                    goto gm_ld23_1;
                }
                gm_l32 = t112;
                uint8_t t113 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                if (t113) {
                    gm_tsoiri44d0i6q t114 = GM_LD(gm_tsoiri44d0i6q, (((uint8_t *)&gm_l32) + 4));
                    gm_l33 = t114;
                } else {
                    gm_toe5fmr3vvf4d t115 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l32) + 4));
                    gm_l16 = t115;
                    gm_b16_8 t116 = gm_l8;
                    gm_toe5fmr3vvf4d t117 = gm_l16;
                    gm_b16_8 t118 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t117);
                    if (gm_unw.kind) {
                        goto gm_ld23_1;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_sjcgs2v314rl7, 18, gm_srcqr4ch56kv6bv, 673, 9)) {
                        goto gm_ld23_1;
                    }
                    if ((GM_LD(uintptr_t, ((uint8_t *)&t116) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t116)), GM_LD(uintptr_t, ((uint8_t *)&t116) + 8), gm_srcqr4ch56kv6bv, 673, 9))) {
                        goto gm_ld23_1;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_slgmqgkioa4u6, 2, gm_srcqr4ch56kv6bv, 673, 9)) {
                        goto gm_ld23_1;
                    }
                    if ((GM_LD(uintptr_t, ((uint8_t *)&t118) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t118)), GM_LD(uintptr_t, ((uint8_t *)&t118) + 8), gm_srcqr4ch56kv6bv, 673, 9))) {
                        goto gm_ld23_1;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 673, 9)) {
                        goto gm_ld23_1;
                    }
                    F.ret = ((uint8_t)1ULL);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                    goto gm_ld23_1;
                }
                gm_tsoiri44d0i6q t119 = gm_l33;
                gm_l17 = t119;
                gm_tsoiri44d0i6q t120 = gm_l17;
                uint8_t t121 = gm_f_exit_code_dv2nn7j(t120);
                F.ret = t121;
                gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                goto gm_ld23_1;
            }
            {
                uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint8_t t123 = (t122 > ((uint64_t)0ULL));
                if (t123) {
                    gm_b16_8 t124 = gm_l9;
                    gm_ttln0d0hd70en t125 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t124);
                }
            }
            goto gm_ls126;
gm_ld23_1:;
            {
                gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                uint64_t t127 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                uint8_t t128 = (t127 > ((uint64_t)0ULL));
                if (t128) {
                    gm_b16_8 t129 = gm_l9;
                    gm_ttln0d0hd70en t130 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t129);
                }
                gm_unw = gm_saved;
            }
            goto gm_ll17;
gm_ls126:;
gm_lb3:;
        }
        goto gm_ls131;
gm_ll17:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741825) { gm_unw.kind = GM_NONE; goto gm_lb2; }
        goto gm_lad;
gm_ls131:;
gm_lb2:;
        F.ret = ((uint8_t)1ULL);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_runtime_source_skilk06(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_runtime_text_37culso(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_runtime_text_37culso F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* rt */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tgvh15qshf643 gm_l2;
    gm_b16_8 gm_l3;
    gm_b16_8 t2 = gm_f_std_root_e0juohb((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t3 = gm_f_runtime_dir_qd07iff((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l1 = t3;
    gm_b16_8 t4 = gm_l1;
    gm_b16_8 t5 = gm_l0;
    gm_b16_8 t6 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_read_all_r9rh9qd((void *)&gm_l2, (void *)NULL, cx, NULL, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t7) {
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_l3 = t8;
    } else {
        gm_b16_8 t9 = gm_l1;
        if (gm_pf454qfrpt6r(cx, gm_sg87p4r5apham, 35, gm_srcqr4ch56kv6bv, 77, 9)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t9) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t9)), GM_LD(uintptr_t, ((uint8_t *)&t9) + 8), gm_srcqr4ch56kv6bv, 77, 9))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_s06m3met1mb9v, 2, gm_srcqr4ch56kv6bv, 77, 9)) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t10;
        memset(&t10, 0, sizeof t10);
        memmove(gm_out, ((uint8_t *)&t10), 24);
        goto gm_fdone;
    }
    gm_b16_8 t11 = gm_l3;
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
    gm_th2lqbo77pvsc t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t13 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[16] = 1;
    memmove(gm_out, ((uint8_t *)&t13), 24);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_stem_of_t9qmlhi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_stem_of_t9qmlhi F;
    gm_b16_8 gm_l0; /* path */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* base */
    GM_STACK_CHECK(&F, 290u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc gm_l3;
    gm_b16_8 gm_l4;
    gm_b16_8 gm_l5;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_default_exe_194tkju(t2);
    gm_l1 = t3;
    gm_b16_8 t4 = gm_l1;
    gm_f_file_name_em7v8lv((void *)&gm_l3, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
    if (t5) {
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
        gm_l4 = t6;
    } else {
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sog4rksrmsqd2));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(4));
        F.ret = t7;
        goto gm_fret;
    }
    gm_b16_8 t8 = gm_l4;
    gm_l2 = t8;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
    if (t10) {
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sog4rksrmsqd2));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(4));
        gm_l5 = t11;
    } else {
        gm_b16_8 t12 = gm_l2;
        gm_l5 = t12;
    }
    gm_b16_8 t13 = gm_l5;
    F.ret = t13;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_target_text_s4sugoj(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_target_text_s4sugoj F;
    GM_STACK_CHECK(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tabb710d9mq2e gm_l1;
    uint64_t t2 = GM_LD(uint64_t, ((uint8_t *)&F.l0));
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 8));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&F.l0) + 16));
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 24));
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 40));
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 56));
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 57));
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(0));
    gm_t2t9klu4dfekp t10;
    GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
    GM_ST(uint16_t, (uint8_t *)&t10 + 58, 0);
    GM_ST(uint32_t, (uint8_t *)&t10 + 60, 0);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 0), t2);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 8), t3);
    GM_ST(uint64_t, ((uint8_t *)&t10 + 16), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 24), t5);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 40), t6);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 56), t7);
    GM_ST(uint8_t, ((uint8_t *)&t10 + 57), t8);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 64), t9);
    gm_f_format_ddl06ea((void *)&gm_l1, (void *)NULL, cx, NULL, &t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_srcqr4ch56kv6bv, 754, 60);
    gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
    F.ret = t13;
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
inline __attribute__((aligned(4))) uint8_t gm_f_thin_lto_klroq4v(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_thin_lto_klroq4v F;
    void * gm_l0; /* tc */
    void * gm_l1; /* opts */
    gm_th2lqbo77pvsc gm_l2; /* version */
    GM_STACK_CHECK(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    uint8_t gm_l4;
    uint8_t gm_l5;
    uint8_t gm_l6;
    gm_th2lqbo77pvsc gm_l7;
    void * t5 = gm_l1;
    uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 424) + 56));
    if (t6) {
        gm_l4 = ((uint8_t)1);
    } else {
        void * t7 = gm_l1;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)t7) + 424) + 24));
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_s048hmhpmbi7c));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(7));
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_svl13be5ca8rt));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(4));
        gm_t6j6smj7qoctu t11;
        GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t9);
        GM_ST(gm_b16_8, ((uint8_t *)&t11 + 16), t10);
        uint8_t t12 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t11) + 0, ((uint8_t *)&t8))) t12 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t11) + 16, ((uint8_t *)&t8))) t12 = 1;
        uint8_t t13 = (uint8_t)(t12);
        gm_l4 = t13;
    }
    uint8_t t14 = gm_l4;
    if (t14) {
        gm_l3 = ((uint8_t)1);
    } else {
        void * t15 = gm_l0;
        uint8_t t16 = gm_f_is_default_24c3cui(t15);
        uint8_t t17 = (uint8_t)!t16;
        gm_l3 = t17;
    }
    uint8_t t18 = gm_l3;
    if (t18) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint8_t t19 = gm_f_is_clang_7du2shk(&(*(gm_th2lqbo77pvsc *)((uint8_t *)a2)));
    uint8_t t20 = (uint8_t)!t19;
    if (t20) {
        gm_l5 = ((uint8_t)1);
    } else {
        gm_f_cache_dir_81h90qu((void *)&gm_l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
                if (t21) {
                    goto gm_lb2;
                }
                gm_l6 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l6 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 439, 33);
        }
gm_lb1:;
        uint8_t t22 = gm_l6;
        gm_l5 = t22;
    }
    uint8_t t23 = gm_l5;
    if (t23) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s3hhdo4dfru76));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(6));
    uint8_t t25 = gm_f_program_on_path_fm43kjf((void *)NULL, cx, NULL, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t25;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_path_3tdv4sg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_unit_path_3tdv4sg F;
    gm_b16_8 gm_l0; /* exe */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK(&F, 291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l2;
    gm_tabb710d9mq2e gm_l3;
    gm_tabb710d9mq2e gm_l4;
    uint64_t t2 = gm_l1;
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        gm_b16_8 t4 = gm_l0;
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_f_format_cncadt9((void *)&gm_l3, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcqr4ch56kv6bv, 478, 77);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l2 = t8;
    } else {
        gm_b16_8 t9 = gm_l0;
        uint64_t t10 = gm_l1;
        gm_t3877pk7lv748 t11;
        GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t9);
        GM_ST(uint64_t, ((uint8_t *)&t11 + 16), t10);
        gm_f_format_4hckftk((void *)&gm_l4, (void *)NULL, cx, NULL, &t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srcqr4ch56kv6bv, 478, 111);
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
        gm_l2 = t14;
    }
    gm_b16_8 t15 = gm_l2;
    F.ret = t15;
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
inline __attribute__((aligned(4))) void gm_f_unit_sources_329det7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_unit_sources_329det7 F;
    void * gm_l0; /* b */
    void * gm_l1; /* split */
    gm_b16_8 gm_l3; /* prelude */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* u */
    GM_STACK_CHECK(&F, 496u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l7;
    gm_tabb710d9mq2e gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_trqfvmjtt4sij t7;
    gm_f_list_epvducg((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t7;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)gm_c69snm9duhi));
    void * t9 = gm_l0;
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)t9) + 216));
    void * t11 = gm_l1;
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)t11));
    gm_tfeab279c16rr t13;
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t8);
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 16), t10);
    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 32), t12);
    gm_f_format_359meqs((void *)&gm_l7, (void *)NULL, cx, NULL, &t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcqr4ch56kv6bv, 235, 16);
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
    gm_l3 = t15;
    void * t16 = gm_l1;
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t16) + 16));
    gm_l4 = t17;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t18 = gm_l5;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t20 = (t18 < t19);
            if (!t20) {
                goto gm_lb1;
            }
            {
                void * t21 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t23 = gm_l5;
                {
                    {
                        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t21) + (uintptr_t)t23 * 16));
                        gm_l6 = t24;
                        void * t25 = (void *)((uint8_t *)&F.l2);
                        gm_b16_8 t26 = gm_l3;
                        gm_b16_8 t27 = gm_l6;
                        gm_t6j6smj7qoctu t28;
                        GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t26);
                        GM_ST(gm_b16_8, ((uint8_t *)&t28 + 16), t27);
                        gm_f_format_5029eea((void *)&gm_l8, (void *)NULL, cx, NULL, &t28);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                        if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcqr4ch56kv6bv, 236, 41);
                        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
                        gm_tftb1odc739t5 t32 = gm_f_push_3ef746b(t25, t31);
                        gm_l9 = t32;
                        uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcqr4ch56kv6bv, 236, 28);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 236, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t34 = gm_l5;
            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
            uint64_t t37 = r35;
            gm_l5 = t37;
        }
    }
gm_lb1:;
    memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
extern __attribute__((aligned(4))) uint8_t gm_f_wasi_target_lp6g335(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_wasi_target_lp6g335_c(gm_t0rfe1ou14h21 a0) { return gm_f_wasi_target_lp6g335(&a0); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_windows_libs_hmrgs9s(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_windows_libs_hmrgs9s F;
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* l */
    GM_STACK_CHECK(&F, 679u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tftb1odc739t5 gm_l7;
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 24));
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s048hmhpmbi7c));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(7));
    uint8_t t9 = (!gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
    if (t9) {
        gm_b16_8 t10 = gm_f_empty_slice_5nn7ph0();
        gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
        F.ret = t11;
        goto gm_fret;
    }
    gm_trqfvmjtt4sij t12;
    gm_f_list_epvducg((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t12;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_skbmmrtbskpao));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(7));
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s5hode9408atm));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(8));
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sir0litoa3fk0));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(17));
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sftkp1r7uetfv));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(9));
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sqd102436v4lo));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(8));
    gm_tud7oovfvnf43 t18;
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t13);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 16), t14);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 32), t15);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 48), t16);
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 64), t17);
    F.l2 = t18;
    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 16));
    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 32));
    gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 48));
    gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 64));
    gm_tjgkb0mi112kg t24;
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t19);
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 16), t20);
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 32), t21);
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 48), t22);
    GM_ST(gm_b16_8, ((uint8_t *)&t24 + 64), t23);
    F.l3 = t24;
    void * t25 = (void *)((uint8_t *)&F.l3);
    uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t27 = (uintptr_t)((uint64_t)5ULL);
    if (t26 > t27 || t27 > (uintptr_t)((uint64_t)5ULL)) gm_trap_slice(t26, t27, ((uint64_t)5ULL), gm_srcqr4ch56kv6bv, 285, 5);
    uint64_t t28 = ((uintptr_t)((uint64_t)5ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t25 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(t28));
    gm_l4 = t29;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t30 = gm_l5;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t32 = (t30 < t31);
            if (!t32) {
                goto gm_lb1;
            }
            {
                void * t33 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t35 = gm_l5;
                {
                    {
                        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)t33) + (uintptr_t)t35 * 16));
                        gm_l6 = t36;
                        void * t37 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t38 = gm_l6;
                        gm_tftb1odc739t5 t39 = gm_f_push_3ef746b(t37, t38);
                        gm_l7 = t39;
                        uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcqr4ch56kv6bv, 285, 86);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 285, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t41 = gm_l5;
            uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
            uint64_t t44 = r42;
            gm_l5 = t44;
        }
    }
gm_lb1:;
    gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
    F.ret = t46;
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
inline __attribute__((aligned(4))) uint8_t gm_f_write_generated_c_kb09043(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_write_generated_c_kb09043 F;
    gm_b16_8 gm_l0; /* path */
    gm_b16_8 gm_l1; /* code */
    gm_tm98ap4112hee gm_l2; /* f */
    uint64_t gm_l4; /* n */
    GM_STACK_CHECK(&F, 422u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t62ebbi2i5rkd gm_l5;
    gm_thf5kr0is9u9i gm_l6;
    uint64_t gm_l7;
    gm_ttln0d0hd70en gm_l8;
    {
        {
            gm_b16_8 t6 = gm_l0;
            gm_t4q2b5ibsrdik t7;
            GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)1));
            GM_ST(uint8_t, ((uint8_t *)&t7 + 1), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t7 + 2), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t7 + 3), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t7 + 4), ((uint8_t)0));
            GM_ST(uint8_t, ((uint8_t *)&t7 + 5), ((uint8_t)0));
            gm_node *pn8 = gm_find(cx, 2126215853);
            if (!pn8) gm_trap("no handler installed for effect `Fs`", gm_srcqr4ch56kv6bv, 509, 13);
            gm_t62ebbi2i5rkd t9;
            t9 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn8->clauses[0])(pn8->env, pn8->hctx, pn8, t6, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t9;
            uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t10) {
                goto gm_lb2;
            }
            gm_tm98ap4112hee t11 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l5) + 8));
            gm_l2 = t11;
            gm_tm98ap4112hee t12 = gm_l2;
            void * t13 = (void *)((uint8_t *)&F.l3);
            uint64_t t14 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t15 = (uintptr_t)((uint64_t)4096ULL);
            if (t14 > t15 || t15 > (uintptr_t)((uint64_t)4096ULL)) gm_trap_slice(t14, t15, ((uint64_t)4096ULL), gm_srcqr4ch56kv6bv, 511, 25);
            uint64_t t16 = ((uintptr_t)((uint64_t)4096ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t13 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16));
            gm_node *pn18 = gm_find(cx, 2126215853);
            if (!pn18) gm_trap("no handler installed for effect `Fs`", gm_srcqr4ch56kv6bv, 511, 14);
            gm_thf5kr0is9u9i t19;
            t19 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn18->clauses[2])(pn18->env, pn18->hctx, pn18, t12, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t19;
            uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (t20) {
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                gm_l7 = t21;
            } else {
                gm_l7 = ((uint64_t)0ULL);
            }
            uint64_t t22 = gm_l7;
            gm_l4 = t22;
            gm_tm98ap4112hee t23 = gm_l2;
            gm_node *pn24 = gm_find(cx, 2126215853);
            if (!pn24) gm_trap("no handler installed for effect `Fs`", gm_srcqr4ch56kv6bv, 512, 9);
            gm_b0_1 t25;
            t25 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn24->clauses[1])(pn24->env, pn24->hctx, pn24, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                {
                    uint64_t t26 = gm_l4;
                    uint8_t t27 = (t26 > ((uint64_t)0ULL));
                    if (!t27) {
                        goto gm_lb4;
                    }
                    void * t28 = (void *)((uint8_t *)&F.l3);
                    uint64_t t29 = gm_l4;
                    uint64_t t30 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t31 = (uintptr_t)t29;
                    if (t30 > t31 || t31 > (uintptr_t)((uint64_t)4096ULL)) gm_trap_slice(t30, t31, ((uint64_t)4096ULL), gm_srcqr4ch56kv6bv, 513, 46);
                    uint64_t t32 = ((uintptr_t)t29) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t33;
                    GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t28 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32));
                    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                    gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)gm_cl1ove1i7cb));
                    uint8_t t36 = gm_f_contains_gkki4kq(t34, t35);
                    uint8_t t37 = (uint8_t)!t36;
                    if (!t37) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t38 = gm_l0;
                    if ((GM_LD(uintptr_t, ((uint8_t *)&t38) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t38)), GM_LD(uintptr_t, ((uint8_t *)&t38) + 8), gm_srcqr4ch56kv6bv, 514, 13))) {
                        goto gm_lad;
                    }
                    if (gm_pf454qfrpt6r(cx, gm_s5g0cpgo64peg, 103, gm_srcqr4ch56kv6bv, 514, 13)) {
                        goto gm_lad;
                    }
                    F.ret = ((uint8_t)0);
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
    gm_b16_8 t39 = gm_l0;
    gm_b16_8 t40 = gm_l1;
    gm_ttln0d0hd70en t41 = gm_f_write_all_o8ucaji((void *)NULL, cx, NULL, t39, t40);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t41;
    uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (t42) {
    } else {
        gm_b16_8 t43 = gm_l0;
        if (gm_pf454qfrpt6r(cx, gm_snekrjln06tfl, 13, gm_srcqr4ch56kv6bv, 519, 9)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t43) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t43)), GM_LD(uintptr_t, ((uint8_t *)&t43) + 8), gm_srcqr4ch56kv6bv, 519, 9))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcqr4ch56kv6bv, 519, 9)) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)0);
        goto gm_fret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_write_units_hc5dqp8(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2) {
    struct gm_fr_write_units_hc5dqp8 F;
    void * gm_l0; /* b */
    gm_t6j6smj7qoctu gm_l1; /* split */
    gm_b16_8 gm_l2; /* exe */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_b16_8 gm_l6; /* u */
    GM_STACK_CHECK(&F, 520u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l7;
    gm_trqfvmjtt4sij gm_l8;
    gm_tf1bjg81u0glv gm_l9;
    gm_b16_8 t7 = gm_l2;
    gm_tnpifv20irnph t8;
    GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
    gm_f_format_7os4s22((void *)&gm_l7, (void *)NULL, cx, NULL, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcqr4ch56kv6bv, 221, 30);
    gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&t10));
    void * t12 = gm_l0;
    gm_b16_8 t13 = gm_f_runtime_source_skilk06((void *)NULL, cx, NULL, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t14 = gm_f_write_generated_c_kb09043((void *)NULL, cx, NULL, t11, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t15 = (uint8_t)!t14;
    if (t15) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t16 = gm_l0;
    void * t17 = (void *)((uint8_t *)&gm_l1);
    gm_f_unit_sources_329det7((void *)&gm_l8, (void *)NULL, cx, NULL, t16, t17);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
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
                uint64_t t23 = gm_l4;
                void * t24 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t26 = gm_l4;
                gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)t24) + (uintptr_t)t26 * 16));
                gm_tf1bjg81u0glv t28;
                GM_ST(uint64_t, ((uint8_t *)&t28 + 0), t23);
                GM_ST(gm_b16_8, ((uint8_t *)&t28 + 8), t27);
                gm_l9 = t28;
                {
                    {
                        uint64_t t29 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                        gm_l5 = t29;
                        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                        gm_l6 = t30;
                        gm_b16_8 t31 = gm_l2;
                        uint64_t t32 = gm_l5;
                        gm_b16_8 t33 = gm_f_unit_path_3tdv4sg((void *)NULL, cx, NULL, t31, t32);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t34 = gm_l6;
                        uint8_t t35 = gm_f_write_generated_c_kb09043((void *)NULL, cx, NULL, t33, t34);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t36 = (uint8_t)!t35;
                        if (t36) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcqr4ch56kv6bv, 222, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t37 = gm_l4;
            uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
            uint64_t t40 = r38;
            gm_l4 = t40;
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
