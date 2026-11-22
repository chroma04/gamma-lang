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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_b16_8 f3; } gm_t26d2uc07m5o2;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; gm_tl874t2atnlh3 f3; } gm_tis7ttivmj935;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t f3; uint8_t pe[3]; } gm_tkrkmsnb6m1b7;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tllos8achgacq;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t f3; uint8_t pe[2]; } gm_tppdoi1slt2pr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_tl874t2atnlh3 f2; gm_b16_8 f3; } gm_tt384v6n8ojib;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_tt7dbdq4n1egc;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t26d2uc07m5o2) == 48 && _Alignof(gm_t26d2uc07m5o2) == 8 && offsetof(gm_t26d2uc07m5o2, f0) == 0 && offsetof(gm_t26d2uc07m5o2, f1) == 16 && offsetof(gm_t26d2uc07m5o2, f2) == 28 && offsetof(gm_t26d2uc07m5o2, f3) == 32, "layout of `(name: string, span: Span, vis: Visibility, what: string)`");
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t f1; uint8_t p2[2]; gm_toceffb697p0u f2; uint8_t f3; uint8_t f4; uint8_t f5; uint8_t pe[1]; } gm_tb9j2bleq9j8f;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_trqfvmjtt4sij f2; uint8_t f3; uint8_t pe[7]; } gm_tcsu09l1pp5hf;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t p3[7]; gm_t4c7ipjj9s3fp f3; } gm_td5sh6gra0q3b;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_trqfvmjtt4sij f0; gm_t4c7ipjj9s3fp f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; } gm_tg0vmr665gcag;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tis7ttivmj935) == 56 && _Alignof(gm_tis7ttivmj935) == 8 && offsetof(gm_tis7ttivmj935, f0) == 0 && offsetof(gm_tis7ttivmj935, f1) == 16 && offsetof(gm_tis7ttivmj935, f2) == 32 && offsetof(gm_tis7ttivmj935, f3) == 44, "layout of `(span: Span, name: string, local: Span, origin: Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tppdoi1slt2pr v0; gm_t08olq97rscn3 v1; gm_tt384v6n8ojib v2; gm_tt7dbdq4n1egc v3; gm_toceffb697p0u v4; gm_tl874t2atnlh3 v5; gm_t26d2uc07m5o2 v6; gm_tllos8achgacq v7; gm_tkrkmsnb6m1b7 v8; gm_tis7ttivmj935 v9; gm_tis7ttivmj935 v10; } f1; } gm_tj5f8vajf35o7;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkrkmsnb6m1b7) == 48 && _Alignof(gm_tkrkmsnb6m1b7) == 8 && offsetof(gm_tkrkmsnb6m1b7, f0) == 0 && offsetof(gm_tkrkmsnb6m1b7, f1) == 16 && offsetof(gm_tkrkmsnb6m1b7, f2) == 32 && offsetof(gm_tkrkmsnb6m1b7, f3) == 44, "layout of `(span: Span, name: string, catch_all: Span, is_result: bool)`");
_Static_assert(sizeof(gm_tllos8achgacq) == 64 && _Alignof(gm_tllos8achgacq) == 8 && offsetof(gm_tllos8achgacq, f0) == 0 && offsetof(gm_tllos8achgacq, f1) == 16 && offsetof(gm_tllos8achgacq, f2) == 32 && offsetof(gm_tllos8achgacq, f3) == 48, "layout of `(span: Span, name: string, local: Span, why: string)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tbgd14esf0jvi f0; uint8_t f1; uint8_t pe[7]; } gm_tnvnsr1bfvtbd;
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tppdoi1slt2pr) == 32 && _Alignof(gm_tppdoi1slt2pr) == 8 && offsetof(gm_tppdoi1slt2pr, f0) == 0 && offsetof(gm_tppdoi1slt2pr, f1) == 16 && offsetof(gm_tppdoi1slt2pr, f2) == 28 && offsetof(gm_tppdoi1slt2pr, f3) == 29, "layout of `(name: string, span: Span, param: bool, assigned: bool)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_t08olq97rscn3 f2; } gm_tqtt2php6e8lk;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_trqfvmjtt4sij f1; } gm_tsu0mic7gtnv6;
_Static_assert(sizeof(gm_tt384v6n8ojib) == 56 && _Alignof(gm_tt384v6n8ojib) == 8 && offsetof(gm_tt384v6n8ojib, f0) == 0 && offsetof(gm_tt384v6n8ojib, f1) == 16 && offsetof(gm_tt384v6n8ojib, f2) == 28 && offsetof(gm_tt384v6n8ojib, f3) == 40, "layout of `(name: string, span: Span, item: Span, what: string)`");
_Static_assert(sizeof(gm_tt7dbdq4n1egc) == 28 && _Alignof(gm_tt7dbdq4n1egc) == 4 && offsetof(gm_tt7dbdq4n1egc, f0) == 0 && offsetof(gm_tt7dbdq4n1egc, f1) == 12 && offsetof(gm_tt7dbdq4n1egc, f2) == 24, "layout of `(span: Span, operand: Span, same: bool)`");
typedef struct { void * f0; gm_tg0vmr665gcag f1; gm_trqfvmjtt4sij f2; gm_b16_8 f3; } gm_t1og5k0mlcjk3;
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { gm_tnvnsr1bfvtbd f0; } gm_t5jnq73fben73;
typedef struct { uint64_t f0; gm_tqtt2php6e8lk f1; } gm_t7qqq0mh1r8ke;
_Static_assert(sizeof(gm_tb9j2bleq9j8f) == 24 && _Alignof(gm_tb9j2bleq9j8f) == 4 && offsetof(gm_tb9j2bleq9j8f, f0) == 0 && offsetof(gm_tb9j2bleq9j8f, f1) == 1 && offsetof(gm_tb9j2bleq9j8f, f2) == 4 && offsetof(gm_tb9j2bleq9j8f, f3) == 20 && offsetof(gm_tb9j2bleq9j8f, f4) == 21 && offsetof(gm_tb9j2bleq9j8f, f5) == 22, "layout of `(mutable: bool, typed: bool, op: ?Span, keep_mutable: bool, fn_lit: bool, uninit: bool)`");
_Static_assert(sizeof(gm_tcsu09l1pp5hf) == 80 && _Alignof(gm_tcsu09l1pp5hf) == 8 && offsetof(gm_tcsu09l1pp5hf, f0) == 0 && offsetof(gm_tcsu09l1pp5hf, f1) == 16 && offsetof(gm_tcsu09l1pp5hf, f2) == 32 && offsetof(gm_tcsu09l1pp5hf, f3) == 72, "layout of `SourceFile`");
_Static_assert(sizeof(gm_td5sh6gra0q3b) == 96 && _Alignof(gm_td5sh6gra0q3b) == 8 && offsetof(gm_td5sh6gra0q3b, f0) == 0 && offsetof(gm_td5sh6gra0q3b, f1) == 16 && offsetof(gm_td5sh6gra0q3b, f2) == 32 && offsetof(gm_td5sh6gra0q3b, f3) == 40, "layout of `Scope`");
_Static_assert(sizeof(gm_tg0vmr665gcag) == 216 && _Alignof(gm_tg0vmr665gcag) == 8 && offsetof(gm_tg0vmr665gcag, f0) == 0 && offsetof(gm_tg0vmr665gcag, f1) == 40 && offsetof(gm_tg0vmr665gcag, f2) == 96 && offsetof(gm_tg0vmr665gcag, f3) == 136 && offsetof(gm_tg0vmr665gcag, f4) == 176, "layout of `FileFacts`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tj5f8vajf35o7) == 72 && _Alignof(gm_tj5f8vajf35o7) == 8 && offsetof(gm_tj5f8vajf35o7, f0) == 0 && offsetof(gm_tj5f8vajf35o7, f1) == 8, "layout of `Finding`");
typedef struct { gm_t4gut2c9fj1kt f0; uint8_t f1; uint8_t pe[7]; } gm_tkc9ngh8g1s0j;
typedef struct { uint64_t f0; gm_tcsu09l1pp5hf f1; } gm_tntfcv47jbeh7;
_Static_assert(sizeof(gm_tnvnsr1bfvtbd) == 40 && _Alignof(gm_tnvnsr1bfvtbd) == 8 && offsetof(gm_tnvnsr1bfvtbd, f0) == 0 && offsetof(gm_tnvnsr1bfvtbd, f1) == 32, "layout of `?(span: Span, text: string)`");
_Static_assert(sizeof(gm_tqtt2php6e8lk) == 56 && _Alignof(gm_tqtt2php6e8lk) == 8 && offsetof(gm_tqtt2php6e8lk, f0) == 0 && offsetof(gm_tqtt2php6e8lk, f1) == 8 && offsetof(gm_tqtt2php6e8lk, f2) == 24, "layout of `Parsed`");
_Static_assert(sizeof(gm_tsu0mic7gtnv6) == 56 && _Alignof(gm_tsu0mic7gtnv6) == 8 && offsetof(gm_tsu0mic7gtnv6, f0) == 0 && offsetof(gm_tsu0mic7gtnv6, f1) == 16, "layout of `Allow`");
_Static_assert(sizeof(gm_t1og5k0mlcjk3) == 280 && _Alignof(gm_t1og5k0mlcjk3) == 8 && offsetof(gm_t1og5k0mlcjk3, f0) == 0 && offsetof(gm_t1og5k0mlcjk3, f1) == 8 && offsetof(gm_t1og5k0mlcjk3, f2) == 224 && offsetof(gm_t1og5k0mlcjk3, f3) == 264, "layout of `Walk`");
_Static_assert(sizeof(gm_t5jnq73fben73) == 40 && _Alignof(gm_t5jnq73fben73) == 8 && offsetof(gm_t5jnq73fben73, f0) == 0, "layout of `(fix: ?(at: Span, to: string))`");
_Static_assert(sizeof(gm_t7qqq0mh1r8ke) == 64 && _Alignof(gm_t7qqq0mh1r8ke) == 8 && offsetof(gm_t7qqq0mh1r8ke, f0) == 0 && offsetof(gm_t7qqq0mh1r8ke, f1) == 8, "layout of `(i: usize, x: Parsed)`");
_Static_assert(sizeof(gm_tkc9ngh8g1s0j) == 192 && _Alignof(gm_tkc9ngh8g1s0j) == 8 && offsetof(gm_tkc9ngh8g1s0j, f0) == 0 && offsetof(gm_tkc9ngh8g1s0j, f1) == 184, "layout of `?Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tb9j2bleq9j8f v0; gm_tbgd14esf0jvi v1; gm_t5jnq73fben73 v2; } f1; } gm_tl5vm0tcqvdvb;
_Static_assert(sizeof(gm_tntfcv47jbeh7) == 88 && _Alignof(gm_tntfcv47jbeh7) == 8 && offsetof(gm_tntfcv47jbeh7, f0) == 0 && offsetof(gm_tntfcv47jbeh7, f1) == 8, "layout of `(i: usize, x: SourceFile)`");
typedef struct { gm_tl5vm0tcqvdvb f0; uint8_t f1; uint8_t pe[7]; } gm_te3ir342llopq;
_Static_assert(sizeof(gm_tl5vm0tcqvdvb) == 48 && _Alignof(gm_tl5vm0tcqvdvb) == 8 && offsetof(gm_tl5vm0tcqvdvb, f0) == 0 && offsetof(gm_tl5vm0tcqvdvb, f1) == 8, "layout of `Site`");
_Static_assert(sizeof(gm_te3ir342llopq) == 56 && _Alignof(gm_te3ir342llopq) == 8 && offsetof(gm_te3ir342llopq, f0) == 0 && offsetof(gm_te3ir342llopq, f1) == 48, "layout of `?Site`");
struct gm_fr_absolute_path_gj3a0ni {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_canonical_2k2pqlg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_contains_57n7jqd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_default_config_s3r9hnq {
    gm_frame h;
    gm_tin97tt4ud0s1 l0; /* c */
    gm_tk9n3vlila9qg l3; /* r */
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_default_level_urberdg {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_deny_warnings_vk3pcl7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1ivdmti {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_4u7p3q6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_9f3suev {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kqiv2v5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n5n7pq1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qbupbr6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vu26gaj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_s4sbegm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_facts_tdo30n8 {
    gm_frame h;
    gm_tg0vmr665gcag ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_find_root_vg2av48 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_finding_diag_2vp4thv {
    gm_frame h;
    gm_t4gut2c9fj1kt l8; /* d */
    gm_t4gut2c9fj1kt l10; /* d */
    gm_tl5vm0tcqvdvb l11; /* site */
    gm_tnvnsr1bfvtbd l17; /* fix */
    gm_tl5vm0tcqvdvb l22; /* site */
    gm_t4gut2c9fj1kt l30; /* d */
    gm_t4gut2c9fj1kt l36; /* d */
    gm_t4gut2c9fj1kt l38; /* d */
    gm_t4gut2c9fj1kt l45; /* d */
    gm_t4gut2c9fj1kt l50; /* d */
    gm_t4gut2c9fj1kt l57; /* d */
    gm_t4gut2c9fj1kt l63; /* d */
    gm_t4gut2c9fj1kt l69; /* d */
    gm_t4gut2c9fj1kt l76; /* d */
    gm_tkc9ngh8g1s0j ret;
};
struct gm_fr_format_331tgtc {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_40d716h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5mja5ge {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7e5ha9c {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7h4sakv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a2pi3bt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b46cbct {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cpa0si0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_duvvd80 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ec2c39v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_iqr642o {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mqu5cbu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ojhiv11 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ooibcq2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_r4p6poi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rbr6trv {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rrko99s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tovpseb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_u8cjln1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_udg5t2e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uv4nplt {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vft2h8j {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_2auo7cg {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_p3t5tj8 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_s7cd6p5 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_in_scope_tm13267 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_level_m5neg79 {
    gm_frame h;
    gm_t9qkh0rsooc8d ret;
};
struct gm_fr_level_name_qqj3mmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_lint_diag_4vra4qu {
    gm_frame h;
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_lint_eo1tub5 {
    gm_frame h;
    gm_t4gut2c9fj1kt l3; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fu17r8d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rbj3fr6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uaa66su {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_parse_level_4iat2k8 {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l0rgbjf {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_tf0cjh1 {
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
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hm4ber8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_kfs9rll {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rule_scmud9h {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_run_n6la0ov {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* scoped */
    gm_tcsu09l1pp5hf l10; /* f */
    gm_trqfvmjtt4sij l11; /* findings */
    gm_trqfvmjtt4sij l12; /* found */
    gm_t1og5k0mlcjk3 l13; /* w */
    gm_t4c7ipjj9s3fp l14; /* when_names */
    gm_t4c7ipjj9s3fp l15; /* at */
    gm_tqtt2php6e8lk l19; /* p */
    gm_tcsu09l1pp5hf l23; /* f */
    gm_tj5f8vajf35o7 l29; /* f */
    gm_t4gut2c9fj1kt l30; /* d */
    gm_t4gut2c9fj1kt l34; /* d */
    gm_trqfvmjtt4sij l35; /* all */
    gm_t4gut2c9fj1kt l39; /* d */
    gm_tsu0mic7gtnv6 l44; /* a */
    gm_tc70a9dbbmgkg ret;
};
struct gm_fr_scope_al6a28r {
    gm_frame h;
    gm_td5sh6gra0q3b ret;
};
struct gm_fr_set_5cf9rlg {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_span_64igq5o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_ahdikpt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_text_of_lbn39it {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_walk_tech5q5 {
    gm_frame h;
    gm_t1og5k0mlcjk3 ret;
};
struct gm_fr_warning_q0m5jjs {
    gm_frame h;
    gm_t4gut2c9fj1kt l2; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_whole_lines_s23hlj6 {
    gm_frame h;
    gm_toceffb697p0u ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_path_gj3a0ni(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_dangling_notes_s4j764p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_analyze_np1m50h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_canonical_2k2pqlg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_dangling_message_s0d8vok(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_default_config_s3r9hnq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_default_level_urberdg(void * a0);
__attribute__((aligned(4))) void gm_f_deny_warnings_vk3pcl7(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1ivdmti(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4u7p3q6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_9f3suev(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kqiv2v5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5n7pq1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qbupbr6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_s4sbegm(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_file_facts_tdo30n8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_find_root_vg2av48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_finding_diag_2vp4thv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3);
inline __attribute__((aligned(4))) void gm_f_format_331tgtc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_40d716h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5mja5ge(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_format_7e5ha9c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_7h4sakv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_a2pi3bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_b46cbct(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_c97t5s6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_cpa0si0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_duvvd80(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ec2c39v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_iqr642o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mqu5cbu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ojhiv11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_r4p6poi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_rbr6trv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_rrko99s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_tovpseb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_u8cjln1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_udg5t2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_uv4nplt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_vft2h8j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_1ji4too(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_gk4t09c(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_2auo7cg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_s7cd6p5(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_in_scope_tm13267(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_level_m5neg79(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_level_name_qqj3mmj(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) void gm_f_lint_diag_4vra4qu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lint_rule_o3ccae1(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fu17r8d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rbj3fr6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uaa66su(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_names_in_1jbgo4k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_bch4n2l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_parse_level_4iat2k8(gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l0rgbjf(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_tf0cjh1(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_sokl06f(void * a0, uint32_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hm4ber8(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kfs9rll(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rule_scmud9h(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_run_n6la0ov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, void * a3, void * a4);
inline __attribute__((aligned(4))) void gm_f_scope_al6a28r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_set_5cf9rlg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_vl3t0h4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_of_lbn39it(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_walk_file_do4uq8b(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_walk_tech5q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_whole_lines_s23hlj6(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_working_dir_dj7eq93(void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_ciuj8hs0q3l[8] __attribute__((aligned(8)));
extern uint8_t gm_cqss81ml8qn[920] __attribute__((aligned(8)));
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s1fsfd0lcgf8e[17];
extern const uint8_t gm_s1gd0gkbe5tu9[55];
extern const uint8_t gm_s25qdppmkh7s6[7];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s4296alm81si3[14];
extern const uint8_t gm_s51jnn7biksj8[68];
extern const uint8_t gm_s57kk096d0ite[45];
extern const uint8_t gm_s5c6gmkd367jj[46];
extern const uint8_t gm_s5qukklsuqpi4[17];
extern const uint8_t gm_s6o3rebeo47ka[16];
extern const uint8_t gm_s6vfshjjhk2g7[19];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_s8uchl9nh4d11[33];
extern const uint8_t gm_s920j0georape[5];
extern const uint8_t gm_sa0ep7oknkrm1[18];
extern const uint8_t gm_sa0f63dcuo6q9[57];
extern const uint8_t gm_sa7chm2nt7fsk[27];
extern const uint8_t gm_sbf1f7fu8bvpt[5];
extern const uint8_t gm_sbmlclkb2d9o9[74];
extern const uint8_t gm_sc46nvblu52f3[18];
extern const uint8_t gm_sc5ekkhtqttfb[4];
extern const uint8_t gm_sch3p0ffmutvn[16];
extern const uint8_t gm_scvmbht29gva9[7];
extern const uint8_t gm_se3smlbkt8d02[37];
extern const uint8_t gm_sed3pofb5bu9b[26];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_senuk3dngdds0[17];
extern const uint8_t gm_sf50oc24o4dhk[11];
extern const uint8_t gm_sfj8otp2pq7qq[55];
extern const uint8_t gm_sg25rp8qmrbme[57];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_sgi6la00bpjt2[15];
extern const uint8_t gm_sgrd6s4kl4dhp[29];
extern const uint8_t gm_sgu334n5g1kog[16];
extern const uint8_t gm_shja5bmmh8ag2[30];
extern const uint8_t gm_shofa8pdml9fb[13];
extern const uint8_t gm_si4j8sh2qika1[3];
extern const uint8_t gm_si6c601mt9p4v[220];
extern const uint8_t gm_sidgpvfsv94q1[56];
extern const uint8_t gm_sijvlope91krv[31];
extern const uint8_t gm_siooblm685vmo[16];
extern const uint8_t gm_sirqj5pqb9rph[58];
extern const uint8_t gm_sisn94tbdoeof[65];
extern const uint8_t gm_siun1n8hqooca[9];
extern const uint8_t gm_sjqs7q3ostqbm[4];
extern const uint8_t gm_sl9dbf0dicmme[39];
extern const uint8_t gm_slb6cj7ndgvdm[6];
extern const uint8_t gm_sldmbttac8k2j[59];
extern const uint8_t gm_slhuk1lrfcjqc[32];
extern const uint8_t gm_slnlsebkdpqrk[9];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_soofuqhnovv5o[5];
extern const uint8_t gm_southrlg4k72m[6];
extern const uint8_t gm_sp0qjak52qqef[158];
extern const uint8_t gm_spn8bpm132g2m[19];
extern const uint8_t gm_spp2t9pj35hcq[7];
extern const uint8_t gm_sq1d6esradpbv[21];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_srajaqomjf1d3[19];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcne58m9cqp42d[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuktvltncsesc[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srvn31r258t3t[16];
extern const uint8_t gm_ss49sa2bohvbp[28];
extern const uint8_t gm_ss67f0gmfen1m[3];
extern const uint8_t gm_stooh1vej0ram[16];
extern const uint8_t gm_su3sef888krmb[22];
extern const uint8_t gm_sucfks6hb8bf7[44];
extern const uint8_t gm_sv242olhg8pke[13];
extern const uint8_t gm_sv44r8pinka4e[13];
extern const uint8_t gm_svo4dku3ftf1l[177];
extern const gm_tmpl gm_tpl00rv7vuig9ur;
extern const gm_tmpl gm_tpl1os40ii1iecn;
extern const gm_tmpl gm_tpl5hatm6q530pn;
extern const gm_tmpl gm_tpl7meurf74nbc1;
extern const gm_tmpl gm_tpl8un0qv11eeom;
extern const gm_tmpl gm_tplck4jpb6nd99p;
extern const gm_tmpl gm_tpldl4pn21ood35;
extern const gm_tmpl gm_tpldv5armigctqs;
extern const gm_tmpl gm_tplecdkophfbiam;
extern const gm_tmpl gm_tplekdseqnnupsj;
extern const gm_tmpl gm_tplengfdkso35ua;
extern const gm_tmpl gm_tplesi2enviniss;
extern const gm_tmpl gm_tplf8er2klne616;
extern const gm_tmpl gm_tplfsk62pc25cdb;
extern const gm_tmpl gm_tplfsstr25ojml5;
extern const gm_tmpl gm_tplibksf6f0e00q;
extern const gm_tmpl gm_tplisurri6t3aun;
extern const gm_tmpl gm_tplk6g6bjh5rkat;
extern const gm_tmpl gm_tpln8gtl1e9t7j8;
extern const gm_tmpl gm_tplq12pt4e53uj3;
extern const gm_tmpl gm_tplrd5hip684e7e;
extern const gm_tmpl gm_tpludgprff7jihr;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_absolute_path_gj3a0ni(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_absolute_path_gj3a0ni F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_f_working_dir_dj7eq93((void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t3 = gm_l0;
    gm_b16_8 t4 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_normalize_bch4n2l((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 40);
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_l2;
    gm_tbgd14esf0jvi t6;
    GM_ST(uint32_t, (uint8_t *)&t6 + 12, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
    gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t6);
    gm_l3 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcgfglnv8p059r, 92, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_canonical_2k2pqlg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonical_2k2pqlg F;
    gm_b16_8 gm_l0; /* path */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_absolute_path_gj3a0ni((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_57n7jqd F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    void * t5 = gm_l0;
    void * t6 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t7 = gm_f_find_722uua4(t5, t6);
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
        gm_trap("no match arm matched the scrutinee", gm_srcbsc23is03ttm, 173, 12);
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_default_level_urberdg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_default_level_urberdg F;
    void * gm_l0; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d gm_l1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 32));
    if (t3) {
        gm_t9qkh0rsooc8d t4;
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
        gm_l1 = t4;
    } else {
        gm_t9qkh0rsooc8d t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)1ULL));
        gm_l1 = t5;
    }
    gm_t9qkh0rsooc8d t6 = gm_l1;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1ivdmti(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1ivdmti F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 184)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4u7p3q6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_4u7p3q6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_9f3suev(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_9f3suev F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kqiv2v5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kqiv2v5 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 64)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n5n7pq1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n5n7pq1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qbupbr6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qbupbr6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 12)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vu26gaj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vu26gaj F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_s4sbegm(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_s4sbegm F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_kfs9rll(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 184)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 184);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 184)));
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
inline __attribute__((aligned(4))) void gm_f_file_facts_tdo30n8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_file_facts_tdo30n8 F;
    GM_STACK_CHECK(&F, 432u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_prbqo9j((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t3;
    gm_f_hash_map_2auo7cg((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t4;
    gm_f_list_7s4om10((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t5;
    gm_f_list_fu17r8d((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t6;
    gm_f_list_prbqo9j((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tg0vmr665gcag t7;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t7 + 0), t2);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t7 + 40), t3);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t7 + 96), t4);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t7 + 136), t5);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t7 + 176), t6);
    memmove(gm_out, ((uint8_t *)&t7), 216);
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
inline __attribute__((aligned(4))) void gm_f_find_root_vg2av48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_find_root_vg2av48 F;
    gm_b16_8 gm_l0; /* start */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_f_find_package_root_fs4fuqj((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) void gm_f_format_331tgtc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_331tgtc F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sldmbttac8k2j));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(58));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl5hatm6q530pn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_40d716h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_40d716h F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgi6la00bpjt2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplck4jpb6nd99p), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5mja5ge(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_5mja5ge F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sidgpvfsv94q1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_f_format_c97t5s6((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpludgprff7jihr), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7e5ha9c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7e5ha9c F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sirqj5pqb9rph));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(57));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplisurri6t3aun), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_7h4sakv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7h4sakv F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shja5bmmh8ag2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7meurf74nbc1), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a2pi3bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a2pi3bt F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slnlsebkdpqrk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl00rv7vuig9ur), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_b46cbct(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_b46cbct F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sucfks6hb8bf7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(43));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl8un0qv11eeom), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cpa0si0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cpa0si0 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srajaqomjf1d3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldl4pn21ood35), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_duvvd80(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_duvvd80 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5c6gmkd367jj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(45));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplecdkophfbiam), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ec2c39v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ec2c39v F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slhuk1lrfcjqc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1os40ii1iecn), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_iqr642o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_iqr642o F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sa0f63dcuo6q9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(56));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpldv5armigctqs), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mqu5cbu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mqu5cbu F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sp0qjak52qqef));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(157));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplekdseqnnupsj), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ojhiv11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ojhiv11 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8uchl9nh4d11));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfsk62pc25cdb), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ooibcq2 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf50oc24o4dhk));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk6g6bjh5rkat), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_r4p6poi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_r4p6poi F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sed3pofb5bu9b));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(25));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplq12pt4e53uj3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_rbr6trv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rbr6trv F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_se3smlbkt8d02));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpln8gtl1e9t7j8), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_rrko99s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rrko99s F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_si6c601mt9p4v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(219));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplibksf6f0e00q), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_tovpseb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tovpseb F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg25rp8qmrbme));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfsstr25ojml5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_u8cjln1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_u8cjln1 F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s57kk096d0ite));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplf8er2klne616), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_udg5t2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_udg5t2e F;
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
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scvmbht29gva9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrd5hip684e7e), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_uv4nplt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_uv4nplt F;
    uint8_t gm_l2; /* failed */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ss49sa2bohvbp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(27));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplesi2enviniss), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_vft2h8j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vft2h8j F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    uint8_t gm_l2; /* failed */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sisn94tbdoeof));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(64));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplengfdkso35ua), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_errors_if7ai0m F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 40));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_2auo7cg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_2auo7cg F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_kqiv2v5();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t5 + 40), t4);
    memmove(gm_out, ((uint8_t *)&t5), 56);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_cuh4fs9 F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_k21v01c();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t5 + 40), t4);
    memmove(gm_out, ((uint8_t *)&t5), 56);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_p3t5tj8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_p3t5tj8 F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vu26gaj();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t5 + 40), t4);
    memmove(gm_out, ((uint8_t *)&t5), 56);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_s7cd6p5(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_s7cd6p5 F;
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_1ivdmti();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&t5 + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&t5 + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t5 + 40), t4);
    memmove(gm_out, ((uint8_t *)&t5), 56);
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
inline __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_level_m5neg79(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_level_m5neg79 F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    uint64_t gm_l4;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_rule_scmud9h(t2);
    gm_l3 = t3;
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t4) {
        uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l4 = t5;
    } else {
        gm_t9qkh0rsooc8d t6;
        GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
        F.ret = t6;
        goto gm_fret;
    }
    uint64_t t7 = gm_l4;
    gm_l2 = t7;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcuktvltncsesc, 80, 12);
    gm_t9qkh0rsooc8d t12 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t9) + (uintptr_t)t11 * 1));
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_lint_diag_4vra4qu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_lint_diag_4vra4qu F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* code */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tl874t2atnlh3 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_f_lint_eo1tub5((void *)&(*(gm_t4gut2c9fj1kt *)gm_out), (void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_lint_eo1tub5 F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* code */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tl874t2atnlh3 t2 = gm_l0;
    gm_b16_8 t3 = gm_l2;
    gm_f_warning_q0m5jjs((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_l1;
    gm_th2lqbo77pvsc t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t5), t4);
    ((uint8_t *)&t5)[16] = 1;
    GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l3) + 160), t5);
    memmove(gm_out, ((uint8_t *)&F.l3), 184);
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
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7s4om10 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_r81t436();
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
inline __attribute__((aligned(4))) void gm_f_list_fu17r8d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fu17r8d F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_9f3suev();
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
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_prbqo9j F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_1th396a();
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
inline __attribute__((aligned(4))) void gm_f_list_rbj3fr6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rbj3fr6 F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_4u7p3q6();
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
inline __attribute__((aligned(4))) void gm_f_list_uaa66su(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uaa66su F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n5n7pq1();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kiclmcn F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_jbu9fo3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8skjmj4(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 32), ((uint8_t *)a1), 32);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l0rgbjf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l0rgbjf F;
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
        gm_b16_8 t9 = gm_f_empty_slice_1th396a();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_kfs9rll(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 184), ((uint8_t *)&F.l1), 184);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 184)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_tf0cjh1(void * a0, gm_t9qkh0rsooc8d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_tf0cjh1 F;
    void * gm_l0; /* l */
    gm_t9qkh0rsooc8d gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_4u7p3q6();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_hm4ber8(t8, ((uint64_t)1ULL), t10);
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
    gm_t9qkh0rsooc8d t18 = gm_l1;
    GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)t16) + (uintptr_t)t17 * 1), t18);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hm4ber8(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hm4ber8 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kfs9rll(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_kfs9rll F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)184ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rule_scmud9h(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rule_scmud9h F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_tf5srh4mp4b3i t3 = gm_f_lint_rule_o3ccae1(t2);
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_scope_al6a28r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_scope_al6a28r F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* std_root */
    uint8_t gm_l2; /* linting_std */
    GM_STACK_CHECK(&F, 218u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    uint8_t t4 = gm_l2;
    gm_t4c7ipjj9s3fp t5;
    gm_f_hash_map_p3t5tj8((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_td5sh6gra0q3b t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 33, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 34, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 36, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t3);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 32), t4);
    GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t6 + 40), t5);
    memmove(gm_out, ((uint8_t *)&t6), 96);
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
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_64igq5o F;
    uint32_t gm_l0; /* file */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK_SHALLOW(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t t2 = gm_l0;
    uint64_t t3 = gm_l1;
    uint32_t t4 = (uint32_t)t3;
    uint64_t t5 = gm_l2;
    uint32_t t6 = (uint32_t)t5;
    gm_tl874t2atnlh3 t7;
    GM_ST(uint32_t, ((uint8_t *)&t7 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t7 + 4), t4);
    GM_ST(uint32_t, ((uint8_t *)&t7 + 8), t6);
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
gm_fprop:
    return GM_CONV(gm_cseahvg8v7p0vs, F.ret);
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
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_ahdikpt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_starts_with_ahdikpt F;
    gm_b16_8 gm_l0; /* p */
    gm_b16_8 gm_l1; /* base */
    GM_STACK_CHECK(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_th2lqbo77pvsc gm_l3;
    gm_b16_8 t5 = gm_l0;
    gm_b16_8 t6 = gm_l1;
    gm_f_strip_prefix_vl3t0h4((void *)&gm_l3, (void *)NULL, cx, NULL, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (t7) {
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
        gm_trap("no match arm matched the scrutinee", gm_srcne58m9cqp42d, 120, 72);
    }
gm_lb1:;
    uint8_t t8 = gm_l2;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_of_lbn39it(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_text_of_lbn39it F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t4 = gm_f_file_o15fqku(t2, t3);
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 16));
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t4) + 16) + 8));
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t8 = (uint64_t)t7;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t10 = (uint64_t)t9;
    uint64_t t11 = (uintptr_t)t8;
    uint64_t t12 = (uintptr_t)t10;
    if (t11 > t12 || t12 > (uintptr_t)t6) gm_trap_slice(t11, t12, t6, gm_srcuktvltncsesc, 145, 76);
    uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)t8;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t5 + (uintptr_t)t8 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_trim_start_179crak(t2);
    gm_b16_8 t4 = gm_f_trim_end_6sjv6rg(t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_walk_tech5q5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_walk_tech5q5 F;
    void * gm_l0; /* sm */
    GM_STACK_CHECK(&F, 584u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_tg0vmr665gcag t3;
    gm_f_file_facts_tdo30n8((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t4;
    gm_f_list_uaa66su((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_f_empty_slice_qbupbr6();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_t1og5k0mlcjk3 t7;
    GM_ST(void *, ((uint8_t *)&t7 + 0), t2);
    GM_ST(gm_tg0vmr665gcag, ((uint8_t *)&t7 + 8), t3);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t7 + 224), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t7 + 264), t6);
    memmove(gm_out, ((uint8_t *)&t7), 280);
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
inline __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_warning_q0m5jjs F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* message */
    GM_STACK_CHECK(&F, 57u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tl874t2atnlh3 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_f_error_s6hn871((void *)&F.l2, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)1ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l2), t4);
    memmove(gm_out, ((uint8_t *)&F.l2), 184);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_canonical_2k2pqlg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_default_config_s3r9hnq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_default_config_s3r9hnq F;
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    GM_STACK_CHECK(&F, 231u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tftb1odc739t5 gm_l4;
    gm_trqfvmjtt4sij t7;
    gm_f_list_rbj3fr6((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tin97tt4ud0s1 t8;
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t8 + 0), t7);
    F.l0 = t8;
    void * t9 = (void *)((uint8_t *)gm_cqss81ml8qn);
    uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t11 = (uintptr_t)((uint64_t)23ULL);
    if (t10 > t11 || t11 > (uintptr_t)((uint64_t)23ULL)) gm_trap_slice(t10, t11, ((uint64_t)23ULL), gm_srcuktvltncsesc, 55, 14);
    uint64_t t12 = ((uintptr_t)((uint64_t)23ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(((uint8_t *)t9 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(t12));
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
                        memmove(((uint8_t *)&F.l3), (((uint8_t *)t17) + (uintptr_t)t19 * 40), 40);
                        void * t20 = (void *)((uint8_t *)&F.l0);
                        void * t21 = (void *)((uint8_t *)&F.l3);
                        gm_t9qkh0rsooc8d t22 = gm_f_default_level_urberdg(t21);
                        gm_tftb1odc739t5 t23 = gm_f_push_tf0cjh1(t20, t22);
                        gm_l4 = t23;
                        uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                        if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcuktvltncsesc, 55, 26);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 55, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t25 = gm_l2;
            uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
            uint64_t t28 = r26;
            gm_l2 = t28;
        }
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
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_default_level_urberdg(void * a0);
inline __attribute__((aligned(4))) void gm_f_deny_warnings_vk3pcl7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deny_warnings_vk3pcl7 F;
    void * gm_l0; /* c */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 150u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = ((uint64_t)0ULL);
    uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)gm_ciuj8hs0q3l));
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
                        void * t12 = gm_l0;
                        void * t13 = GM_LD(void *, ((uint8_t *)t12));
                        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
                        uint64_t t15 = gm_l3;
                        if ((uintptr_t)t15 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t15, t14, gm_srcuktvltncsesc, 74, 12);
                        gm_t9qkh0rsooc8d t16 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t13) + (uintptr_t)t15 * 1));
                        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t16) + 0));
                        uint8_t t18 = (uint8_t)((t17 == ((uint8_t)1ULL)));
                        gm_t9qkh0rsooc8d t19;
                        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)1ULL));
                        if (t18) {
                            void * t20 = gm_l0;
                            void * t21 = GM_LD(void *, ((uint8_t *)t20));
                            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t20) + 8));
                            uint64_t t23 = gm_l3;
                            if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcuktvltncsesc, 74, 41);
                            gm_t9qkh0rsooc8d t24;
                            GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)t21) + (uintptr_t)t23 * 1), t24);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 73, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t25 = gm_l2;
            uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
            uint64_t t28 = r26;
            gm_l2 = t28;
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
extern __attribute__((aligned(4))) void gm_f_find_root_vg2av48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_finding_diag_2vp4thv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3) {
    struct gm_fr_finding_diag_2vp4thv F;
    void * gm_l0; /* sm */
    void * gm_l1; /* f */
    void * gm_l2; /* sites */
    void * gm_l3; /* when_names */
    gm_b16_8 gm_l4; /* name */
    gm_tl874t2atnlh3 gm_l5; /* span */
    uint8_t gm_l6; /* param */
    uint8_t gm_l7; /* assigned */
    gm_b16_8 gm_l9; /* msg */
    uint8_t gm_l12; /* typed */
    gm_toceffb697p0u gm_l13; /* op */
    gm_tl874t2atnlh3 gm_l14; /* o */
    gm_tl874t2atnlh3 gm_l15; /* at */
    gm_b16_8 gm_l16; /* to */
    gm_tbgd14esf0jvi gm_l18; /* p */
    gm_b16_8 gm_l19; /* what */
    gm_b16_8 gm_l20; /* name */
    gm_tl874t2atnlh3 gm_l21; /* span */
    uint8_t gm_l23; /* mutable */
    uint8_t gm_l24; /* typed */
    gm_toceffb697p0u gm_l25; /* op */
    uint8_t gm_l26; /* keep_mutable */
    uint8_t gm_l27; /* fn_lit */
    uint8_t gm_l28; /* uninit */
    gm_b16_8 gm_l29; /* marker */
    gm_tl874t2atnlh3 gm_l31; /* o */
    gm_b16_8 gm_l32; /* name */
    gm_tl874t2atnlh3 gm_l33; /* span */
    gm_tl874t2atnlh3 gm_l34; /* item */
    gm_b16_8 gm_l35; /* what */
    gm_tl874t2atnlh3 gm_l37; /* lines */
    gm_b16_8 gm_l39; /* name */
    gm_tl874t2atnlh3 gm_l40; /* span */
    gm_t9qkh0rsooc8d gm_l41; /* vis */
    gm_b16_8 gm_l42; /* what */
    gm_b16_8 gm_l43; /* marker */
    gm_b16_8 gm_l44; /* why */
    gm_tl874t2atnlh3 gm_l46; /* span */
    gm_b16_8 gm_l47; /* name */
    gm_tl874t2atnlh3 gm_l48; /* local */
    gm_b16_8 gm_l49; /* why */
    gm_tl874t2atnlh3 gm_l51; /* span */
    gm_b16_8 gm_l52; /* name */
    gm_tl874t2atnlh3 gm_l53; /* catch_all */
    uint8_t gm_l54; /* is_result */
    gm_b16_8 gm_l55; /* value */
    gm_b16_8 gm_l56; /* absent */
    gm_tl874t2atnlh3 gm_l58; /* span */
    gm_b16_8 gm_l59; /* name */
    gm_tl874t2atnlh3 gm_l60; /* local */
    gm_tl874t2atnlh3 gm_l61; /* origin */
    gm_b16_8 gm_l62; /* src */
    gm_tl874t2atnlh3 gm_l64; /* span */
    gm_b16_8 gm_l65; /* name */
    gm_tl874t2atnlh3 gm_l66; /* list */
    gm_tl874t2atnlh3 gm_l67; /* growth */
    gm_b16_8 gm_l68; /* src */
    gm_tl874t2atnlh3 gm_l70; /* span */
    gm_tl874t2atnlh3 gm_l71; /* operand */
    uint8_t gm_l72; /* same */
    gm_b16_8 gm_l73; /* whole */
    gm_b16_8 gm_l74; /* o */
    gm_b16_8 gm_l75; /* replacement */
    gm_tl874t2atnlh3 gm_l77; /* span */
    uint8_t gm_l78; /* always */
    gm_tl874t2atnlh3 gm_l79; /* span */
    GM_STACK_CHECK(&F, 15189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l80;
    gm_tabb710d9mq2e gm_l81;
    gm_b16_8 gm_l82;
    gm_tabb710d9mq2e gm_l83;
    gm_tabb710d9mq2e gm_l84;
    gm_te3ir342llopq gm_l85;
    gm_tabb710d9mq2e gm_l86;
    gm_b16_8 gm_l87;
    gm_tabb710d9mq2e gm_l88;
    gm_te3ir342llopq gm_l89;
    gm_tl5vm0tcqvdvb gm_l90;
    uint8_t gm_l91;
    uint8_t gm_l92;
    gm_b16_8 gm_l93;
    gm_tabb710d9mq2e gm_l94;
    gm_tabb710d9mq2e gm_l95;
    gm_toceffb697p0u gm_l96;
    gm_tabb710d9mq2e gm_l97;
    gm_tabb710d9mq2e gm_l98;
    gm_b16_8 gm_l99;
    gm_b16_8 gm_l100;
    gm_tabb710d9mq2e gm_l101;
    gm_tabb710d9mq2e gm_l102;
    gm_tabb710d9mq2e gm_l103;
    gm_tabb710d9mq2e gm_l104;
    gm_tabb710d9mq2e gm_l105;
    gm_tabb710d9mq2e gm_l106;
    gm_tabb710d9mq2e gm_l107;
    gm_tabb710d9mq2e gm_l108;
    uint8_t gm_l109;
    uint8_t gm_l110;
    uint8_t gm_l111;
    gm_tabb710d9mq2e gm_l112;
    gm_tabb710d9mq2e gm_l113;
    gm_tabb710d9mq2e gm_l114;
    gm_tabb710d9mq2e gm_l115;
    {
        void * t32 = gm_l1;
        {
            {
                {
                    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)0ULL));
                    if (!t34) {
                        goto gm_lb2;
                    }
                    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 8));
                    gm_l4 = t35;
                    gm_tl874t2atnlh3 t36 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 16));
                    gm_l5 = t36;
                    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 28));
                    gm_l6 = t37;
                    uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 29));
                    gm_l7 = t38;
                    void * t39 = gm_l3;
                    gm_b16_8 t40 = gm_l4;
                    uint8_t t41 = gm_f_contains_57n7jqd(t39, t40);
                    if (t41) {
                        gm_tkc9ngh8g1s0j t42;
                        memset(&t42, 0, sizeof t42);
                        memmove(gm_out, ((uint8_t *)&t42), 192);
                        goto gm_fdone;
                    }
                    uint8_t t43 = gm_l6;
                    if (t43) {
                        gm_tl874t2atnlh3 t44 = gm_l5;
                        gm_b16_8 t45;
                        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s5qukklsuqpi4));
                        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(16));
                        gm_b16_8 t46 = gm_l4;
                        gm_tnpifv20irnph t47;
                        GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t46);
                        gm_f_format_ojhiv11((void *)&gm_l80, (void *)NULL, cx, NULL, t47);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l80))[0] != 0);
                        if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l80) + 8, gm_srcuktvltncsesc, 215, 58);
                        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l80) + 8));
                        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                        gm_t4gut2c9fj1kt t51;
                        gm_f_lint_diag_4vra4qu((void *)&t51, (void *)NULL, cx, NULL, t44, t45, t50);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l8 = t51;
                        void * t52 = (void *)((uint8_t *)&F.l8);
                        gm_b16_8 t53 = gm_l4;
                        gm_tnpifv20irnph t54;
                        GM_ST(gm_b16_8, ((uint8_t *)&t54 + 0), t53);
                        gm_f_format_b46cbct((void *)&gm_l81, (void *)NULL, cx, NULL, t54);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                        if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 8, gm_srcuktvltncsesc, 216, 28);
                        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l81) + 8));
                        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                        gm_f_add_note_46v502s(t52, t57);
                        gm_t4gut2c9fj1kt t58 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l8));
                        gm_tkc9ngh8g1s0j t59;
                        GM_ST(uint8_t, (uint8_t *)&t59 + 185, 0);
                        GM_ST(uint16_t, (uint8_t *)&t59 + 186, 0);
                        GM_ST(uint32_t, (uint8_t *)&t59 + 188, 0);
                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t59), t58);
                        ((uint8_t *)&t59)[184] = 1;
                        memmove(gm_out, ((uint8_t *)&t59), 192);
                        goto gm_fdone;
                    }
                    uint8_t t60 = gm_l7;
                    if (t60) {
                        gm_b16_8 t61 = gm_l4;
                        gm_tnpifv20irnph t62;
                        GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
                        gm_f_format_u8cjln1((void *)&gm_l83, (void *)NULL, cx, NULL, t62);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l83))[0] != 0);
                        if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l83) + 8, gm_srcuktvltncsesc, 220, 17);
                        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l83) + 8));
                        gm_l82 = t64;
                    } else {
                        gm_b16_8 t65 = gm_l4;
                        gm_tnpifv20irnph t66;
                        GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
                        gm_f_format_ec2c39v((void *)&gm_l84, (void *)NULL, cx, NULL, t66);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
                        if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l84) + 8, gm_srcuktvltncsesc, 222, 17);
                        gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l84) + 8));
                        gm_l82 = t68;
                    }
                    gm_b16_8 t69 = gm_l82;
                    gm_l9 = t69;
                    gm_tl874t2atnlh3 t70 = gm_l5;
                    gm_b16_8 t71;
                    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_stooh1vej0ram));
                    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(15));
                    gm_b16_8 t72 = gm_l9;
                    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                    gm_t4gut2c9fj1kt t74;
                    gm_f_lint_diag_4vra4qu((void *)&t74, (void *)NULL, cx, NULL, t70, t71, t73);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l10 = t74;
                    {
                        {
                            void * t75 = gm_l2;
                            gm_tl874t2atnlh3 t76 = gm_l5;
                            gm_f_get_gk4t09c((void *)&gm_l85, t75, GM_CONV(gm_cseahvg8v7p0vs, t76));
                            uint8_t t77 = (uint8_t)((((uint8_t *)&gm_l85))[48] != 0);
                            if (!t77) {
                                goto gm_lb4;
                            }
                            memmove(((uint8_t *)&F.l11), ((uint8_t *)&gm_l85), 48);
                            {
                                {
                                    uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                    uint8_t t79 = (uint8_t)(t78 == ((uint8_t)0ULL));
                                    if (!t79) {
                                        goto gm_lb6;
                                    }
                                    uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)&F.l11) + 8) + 1));
                                    gm_l12 = t80;
                                    gm_toceffb697p0u t81 = GM_LD(gm_toceffb697p0u, ((((uint8_t *)&F.l11) + 8) + 4));
                                    gm_l13 = t81;
                                    {
                                        {
                                            uint8_t t82 = gm_l12;
                                            uint8_t t83 = (uint8_t)!t82;
                                            if (!t83) {
                                                goto gm_lb8;
                                            }
                                            uint8_t t84 = gm_l7;
                                            uint8_t t85 = (uint8_t)!t84;
                                            if (!t85) {
                                                goto gm_lb8;
                                            }
                                            {
                                                {
                                                    uint8_t t86 = (uint8_t)((((uint8_t *)&gm_l13))[12] != 0);
                                                    if (!t86) {
                                                        goto gm_lb10;
                                                    }
                                                    gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l13));
                                                    gm_l14 = t87;
                                                    void * t88 = (void *)((uint8_t *)&F.l10);
                                                    gm_b16_8 t89;
                                                    GM_ST(void *, (uint8_t *)&t89, (void *)(gm_sgrd6s4kl4dhp));
                                                    GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(28));
                                                    uint32_t t90 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
                                                    uint32_t t91 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                                                    uint32_t t92 = GM_LD(uint32_t, (((uint8_t *)&gm_l14) + 8));
                                                    gm_tl874t2atnlh3 t93;
                                                    GM_ST(uint32_t, ((uint8_t *)&t93 + 0), t90);
                                                    GM_ST(uint32_t, ((uint8_t *)&t93 + 4), t91);
                                                    GM_ST(uint32_t, ((uint8_t *)&t93 + 8), t92);
                                                    gm_b16_8 t94;
                                                    GM_ST(void *, (uint8_t *)&t94, (void *)(gm_sc5ekkhtqttfb));
                                                    GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(3));
                                                    gm_b0_1 t95 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t88, t89, t93, t94);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_t4gut2c9fj1kt t96 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l10));
                                                    gm_tkc9ngh8g1s0j t97;
                                                    GM_ST(uint8_t, (uint8_t *)&t97 + 185, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t97 + 186, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t97 + 188, 0);
                                                    GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t97), t96);
                                                    ((uint8_t *)&t97)[184] = 1;
                                                    memmove(gm_out, ((uint8_t *)&t97), 192);
                                                    goto gm_fdone;
                                                }
gm_lb10:;
                                            }
gm_lb9:;
                                            goto gm_lb7;
                                        }
gm_lb8:;
                                    }
gm_lb7:;
                                    goto gm_lb5;
                                }
gm_lb6:;
                                {
                                    uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                    uint8_t t99 = (uint8_t)(t98 == ((uint8_t)1ULL));
                                    if (!t99) {
                                        goto gm_lb11;
                                    }
                                    gm_tl874t2atnlh3 t100 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l11) + 8));
                                    gm_l15 = t100;
                                    gm_b16_8 t101 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l11) + 8) + 16));
                                    gm_l16 = t101;
                                    uint8_t t102 = gm_l7;
                                    uint8_t t103 = (uint8_t)!t102;
                                    if (t103) {
                                        void * t104 = (void *)((uint8_t *)&F.l10);
                                        gm_b16_8 t105 = gm_l16;
                                        gm_tnpifv20irnph t106;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t106 + 0), t105);
                                        gm_f_format_ooibcq2((void *)&gm_l86, (void *)NULL, cx, NULL, t106);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
                                        if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l86) + 8, gm_srcuktvltncsesc, 234, 35);
                                        gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l86) + 8));
                                        gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
                                        gm_tl874t2atnlh3 t110 = gm_l15;
                                        gm_b16_8 t111 = gm_l16;
                                        gm_b0_1 t112 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t104, t109, t110, t111);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_t4gut2c9fj1kt t113 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l10));
                                        gm_tkc9ngh8g1s0j t114;
                                        GM_ST(uint8_t, (uint8_t *)&t114 + 185, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t114 + 186, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t114 + 188, 0);
                                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t114), t113);
                                        ((uint8_t *)&t114)[184] = 1;
                                        memmove(gm_out, ((uint8_t *)&t114), 192);
                                        goto gm_fdone;
                                    }
                                    goto gm_lb5;
                                }
gm_lb11:;
                                {
                                    uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                                    uint8_t t116 = (uint8_t)(t115 == ((uint8_t)2ULL));
                                    if (!t116) {
                                        goto gm_lb12;
                                    }
                                    memmove(((uint8_t *)&F.l17), (((uint8_t *)&F.l11) + 8), 40);
                                    {
                                        {
                                            uint8_t t117 = (uint8_t)((((uint8_t *)&F.l17))[32] != 0);
                                            if (!t117) {
                                                goto gm_lb14;
                                            }
                                            memmove(((uint8_t *)&gm_l18), ((uint8_t *)&F.l17), 32);
                                            uint64_t t118 = GM_LD(uint64_t, ((((uint8_t *)&gm_l18) + 16) + 8));
                                            uint8_t t119 = ((t118 == ((uint64_t)0ULL)));
                                            if (t119) {
                                                gm_b16_8 t120;
                                                GM_ST(void *, (uint8_t *)&t120, (void *)(gm_s6vfshjjhk2g7));
                                                GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(18));
                                                gm_l87 = t120;
                                            } else {
                                                gm_b16_8 t121;
                                                GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sa0ep7oknkrm1));
                                                GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(17));
                                                gm_l87 = t121;
                                            }
                                            gm_b16_8 t122 = gm_l87;
                                            gm_l19 = t122;
                                            void * t123 = (void *)((uint8_t *)&F.l10);
                                            gm_b16_8 t124 = gm_l19;
                                            gm_tl874t2atnlh3 t125 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l18));
                                            gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 16));
                                            gm_b0_1 t127 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t123, t124, t125, t126);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_t4gut2c9fj1kt t128 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l10));
                                            gm_tkc9ngh8g1s0j t129;
                                            GM_ST(uint8_t, (uint8_t *)&t129 + 185, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t129 + 186, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t129 + 188, 0);
                                            GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t129), t128);
                                            ((uint8_t *)&t129)[184] = 1;
                                            memmove(gm_out, ((uint8_t *)&t129), 192);
                                            goto gm_fdone;
                                        }
gm_lb14:;
                                    }
gm_lb13:;
                                    goto gm_lb5;
                                }
gm_lb12:;
                                gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 226, 17);
                            }
gm_lb5:;
                            goto gm_lb3;
                        }
gm_lb4:;
                    }
gm_lb3:;
                    void * t130 = (void *)((uint8_t *)&F.l10);
                    gm_b16_8 t131 = gm_l4;
                    gm_tnpifv20irnph t132;
                    GM_ST(gm_b16_8, ((uint8_t *)&t132 + 0), t131);
                    gm_f_format_rbr6trv((void *)&gm_l88, (void *)NULL, cx, NULL, t132);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t133 = (uint8_t)!((((uint8_t *)&gm_l88))[0] != 0);
                    if (!t133) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l88) + 8, gm_srcuktvltncsesc, 244, 24);
                    gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l88) + 8));
                    gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                    gm_f_add_note_46v502s(t130, t135);
                    gm_t4gut2c9fj1kt t136 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l10));
                    gm_tkc9ngh8g1s0j t137;
                    GM_ST(uint8_t, (uint8_t *)&t137 + 185, 0);
                    GM_ST(uint16_t, (uint8_t *)&t137 + 186, 0);
                    GM_ST(uint32_t, (uint8_t *)&t137 + 188, 0);
                    GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t137), t136);
                    ((uint8_t *)&t137)[184] = 1;
                    memmove(gm_out, ((uint8_t *)&t137), 192);
                    goto gm_fdone;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t139 = (uint8_t)(t138 == ((uint8_t)1ULL));
                    if (!t139) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 8));
                    gm_l20 = t140;
                    gm_tl874t2atnlh3 t141 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 16));
                    gm_l21 = t141;
                    void * t142 = gm_l3;
                    gm_b16_8 t143 = gm_l20;
                    uint8_t t144 = gm_f_contains_57n7jqd(t142, t143);
                    if (t144) {
                        gm_tkc9ngh8g1s0j t145;
                        memset(&t145, 0, sizeof t145);
                        memmove(gm_out, ((uint8_t *)&t145), 192);
                        goto gm_fdone;
                    }
                    void * t146 = gm_l2;
                    gm_tl874t2atnlh3 t147 = gm_l21;
                    gm_f_get_gk4t09c((void *)&gm_l89, t146, GM_CONV(gm_cseahvg8v7p0vs, t147));
                    uint8_t t148 = (uint8_t)((((uint8_t *)&gm_l89))[48] != 0);
                    if (t148) {
                        memmove(((uint8_t *)&gm_l90), ((uint8_t *)&gm_l89), 48);
                    } else {
                        gm_tkc9ngh8g1s0j t149;
                        memset(&t149, 0, sizeof t149);
                        memmove(gm_out, ((uint8_t *)&t149), 192);
                        goto gm_fdone;
                    }
                    memmove(((uint8_t *)&F.l22), ((uint8_t *)&gm_l90), 48);
                    {
                        {
                            uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                            uint8_t t151 = (uint8_t)(t150 == ((uint8_t)0ULL));
                            if (!t151) {
                                goto gm_lb17;
                            }
                            uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 8));
                            gm_l23 = t152;
                            uint8_t t153 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 1));
                            gm_l24 = t153;
                            gm_toceffb697p0u t154 = GM_LD(gm_toceffb697p0u, ((((uint8_t *)&F.l22) + 8) + 4));
                            gm_l25 = t154;
                            uint8_t t155 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 20));
                            gm_l26 = t155;
                            uint8_t t156 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 21));
                            gm_l27 = t156;
                            uint8_t t157 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 22));
                            gm_l28 = t157;
                            uint8_t t158 = gm_l23;
                            uint8_t t159 = (uint8_t)!t158;
                            if (t159) {
                                gm_l92 = ((uint8_t)1);
                            } else {
                                uint8_t t160 = gm_l27;
                                gm_l92 = t160;
                            }
                            uint8_t t161 = gm_l92;
                            if (t161) {
                                gm_l91 = ((uint8_t)1);
                            } else {
                                uint8_t t162 = gm_l28;
                                gm_l91 = t162;
                            }
                            uint8_t t163 = gm_l91;
                            if (t163) {
                                gm_tkc9ngh8g1s0j t164;
                                memset(&t164, 0, sizeof t164);
                                memmove(gm_out, ((uint8_t *)&t164), 192);
                                goto gm_fdone;
                            }
                            uint8_t t165 = gm_l24;
                            if (t165) {
                                gm_b16_8 t166;
                                GM_ST(void *, (uint8_t *)&t166, (void *)(gm_s39mljh669qgl));
                                GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(1));
                                gm_l93 = t166;
                            } else {
                                gm_b16_8 t167;
                                GM_ST(void *, (uint8_t *)&t167, (void *)(gm_ss67f0gmfen1m));
                                GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(2));
                                gm_l93 = t167;
                            }
                            gm_b16_8 t168 = gm_l93;
                            gm_l29 = t168;
                            gm_tl874t2atnlh3 t169 = gm_l21;
                            gm_b16_8 t170;
                            GM_ST(void *, (uint8_t *)&t170, (void *)(gm_senuk3dngdds0));
                            GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(16));
                            gm_b16_8 t171 = gm_l20;
                            gm_b16_8 t172 = gm_l29;
                            gm_t6j6smj7qoctu t173;
                            GM_ST(gm_b16_8, ((uint8_t *)&t173 + 0), t171);
                            GM_ST(gm_b16_8, ((uint8_t *)&t173 + 16), t172);
                            gm_f_format_7e5ha9c((void *)&gm_l94, (void *)NULL, cx, NULL, &t173);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t174 = (uint8_t)!((((uint8_t *)&gm_l94))[0] != 0);
                            if (!t174) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l94) + 8, gm_srcuktvltncsesc, 253, 54);
                            gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l94) + 8));
                            gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&t175));
                            gm_t4gut2c9fj1kt t177;
                            gm_f_lint_diag_4vra4qu((void *)&t177, (void *)NULL, cx, NULL, t169, t170, t176);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l30 = t177;
                            {
                                {
                                    uint8_t t178 = (uint8_t)((((uint8_t *)&gm_l25))[12] != 0);
                                    if (!t178) {
                                        goto gm_lb19;
                                    }
                                    gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l25));
                                    gm_l31 = t179;
                                    uint8_t t180 = gm_l26;
                                    uint8_t t181 = (uint8_t)!t180;
                                    if (t181) {
                                        void * t182 = (void *)((uint8_t *)&F.l30);
                                        gm_b16_8 t183;
                                        GM_ST(void *, (uint8_t *)&t183, (void *)(gm_sa7chm2nt7fsk));
                                        GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(26));
                                        gm_tl874t2atnlh3 t184 = gm_l31;
                                        gm_b16_8 t185 = gm_l29;
                                        gm_b0_1 t186 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t182, t183, t184, t185);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    goto gm_lb18;
                                }
gm_lb19:;
                            }
gm_lb18:;
                            gm_t4gut2c9fj1kt t187 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l30));
                            gm_tkc9ngh8g1s0j t188;
                            GM_ST(uint8_t, (uint8_t *)&t188 + 185, 0);
                            GM_ST(uint16_t, (uint8_t *)&t188 + 186, 0);
                            GM_ST(uint32_t, (uint8_t *)&t188 + 188, 0);
                            GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t188), t187);
                            ((uint8_t *)&t188)[184] = 1;
                            memmove(gm_out, ((uint8_t *)&t188), 192);
                            goto gm_fdone;
                        }
gm_lb17:;
                        gm_tkc9ngh8g1s0j t189;
                        memset(&t189, 0, sizeof t189);
                        memmove(gm_out, ((uint8_t *)&t189), 192);
                        goto gm_fdone;
                    }
gm_lb16:;
                }
gm_lb15:;
            }
            {
                {
                    uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t191 = (uint8_t)(t190 == ((uint8_t)2ULL));
                    if (!t191) {
                        goto gm_lb20;
                    }
                    gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 8));
                    gm_l32 = t192;
                    gm_tl874t2atnlh3 t193 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 16));
                    gm_l33 = t193;
                    gm_tl874t2atnlh3 t194 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 28));
                    gm_l34 = t194;
                    gm_b16_8 t195 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 40));
                    gm_l35 = t195;
                    void * t196 = gm_l3;
                    gm_b16_8 t197 = gm_l32;
                    uint8_t t198 = gm_f_contains_57n7jqd(t196, t197);
                    if (t198) {
                        gm_tkc9ngh8g1s0j t199;
                        memset(&t199, 0, sizeof t199);
                        memmove(gm_out, ((uint8_t *)&t199), 192);
                        goto gm_fdone;
                    }
                    gm_b16_8 t200 = gm_l35;
                    gm_b16_8 t201;
                    GM_ST(void *, (uint8_t *)&t201, (void *)(gm_sg3kpa3h16no3));
                    GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(6));
                    uint8_t t202 = (gm_eq0iqiubfm7e(((uint8_t *)&t200), ((uint8_t *)&t201)));
                    if (t202) {
                        gm_tl874t2atnlh3 t203 = gm_l33;
                        gm_b16_8 t204;
                        GM_ST(void *, (uint8_t *)&t204, (void *)(gm_s4296alm81si3));
                        GM_ST(uintptr_t, (uint8_t *)&t204 + 8, (uintptr_t)(13));
                        gm_b16_8 t205 = gm_l32;
                        gm_tnpifv20irnph t206;
                        GM_ST(gm_b16_8, ((uint8_t *)&t206 + 0), t205);
                        gm_f_format_7h4sakv((void *)&gm_l95, (void *)NULL, cx, NULL, t206);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l95))[0] != 0);
                        if (!t207) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l95) + 8, gm_srcuktvltncsesc, 262, 55);
                        gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l95) + 8));
                        gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                        gm_t4gut2c9fj1kt t210;
                        gm_f_lint_diag_4vra4qu((void *)&t210, (void *)NULL, cx, NULL, t203, t204, t209);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l36 = t210;
                        {
                            {
                                void * t211 = gm_l0;
                                gm_tl874t2atnlh3 t212 = gm_l34;
                                gm_toceffb697p0u t213 = gm_f_whole_lines_s23hlj6(t211, GM_CONV(gm_cseahvg8v7p0vs, t212));
                                gm_l96 = t213;
                                uint8_t t214 = (uint8_t)((((uint8_t *)&gm_l96))[12] != 0);
                                if (!t214) {
                                    goto gm_lb22;
                                }
                                gm_tl874t2atnlh3 t215 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l96));
                                gm_l37 = t215;
                                void * t216 = (void *)((uint8_t *)&F.l36);
                                gm_b16_8 t217;
                                GM_ST(void *, (uint8_t *)&t217, (void *)(gm_sc46nvblu52f3));
                                GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(17));
                                gm_tl874t2atnlh3 t218 = gm_l37;
                                gm_b16_8 t219;
                                GM_ST(void *, (uint8_t *)&t219, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(0));
                                gm_b0_1 t220 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t216, t217, t218, t219);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb21;
                            }
gm_lb22:;
                        }
gm_lb21:;
                        gm_t4gut2c9fj1kt t221 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l36));
                        gm_tkc9ngh8g1s0j t222;
                        GM_ST(uint8_t, (uint8_t *)&t222 + 185, 0);
                        GM_ST(uint16_t, (uint8_t *)&t222 + 186, 0);
                        GM_ST(uint32_t, (uint8_t *)&t222 + 188, 0);
                        GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t222), t221);
                        ((uint8_t *)&t222)[184] = 1;
                        memmove(gm_out, ((uint8_t *)&t222), 192);
                        goto gm_fdone;
                    }
                    gm_tl874t2atnlh3 t223 = gm_l33;
                    gm_b16_8 t224;
                    GM_ST(void *, (uint8_t *)&t224, (void *)(gm_spn8bpm132g2m));
                    GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(18));
                    gm_b16_8 t225 = gm_l35;
                    gm_b16_8 t226 = gm_l32;
                    gm_t6j6smj7qoctu t227;
                    GM_ST(gm_b16_8, ((uint8_t *)&t227 + 0), t225);
                    GM_ST(gm_b16_8, ((uint8_t *)&t227 + 16), t226);
                    gm_f_format_r4p6poi((void *)&gm_l97, (void *)NULL, cx, NULL, &t227);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t228 = (uint8_t)!((((uint8_t *)&gm_l97))[0] != 0);
                    if (!t228) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l97) + 8, gm_srcuktvltncsesc, 266, 56);
                    gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l97) + 8));
                    gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&t229));
                    gm_t4gut2c9fj1kt t231;
                    gm_f_lint_diag_4vra4qu((void *)&t231, (void *)NULL, cx, NULL, t223, t224, t230);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l38 = t231;
                    void * t232 = (void *)((uint8_t *)&F.l38);
                    gm_b16_8 t233;
                    GM_ST(void *, (uint8_t *)&t233, (void *)(gm_sbmlclkb2d9o9));
                    GM_ST(uintptr_t, (uint8_t *)&t233 + 8, (uintptr_t)(73));
                    gm_f_add_note_46v502s(t232, t233);
                    gm_t4gut2c9fj1kt t234 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l38));
                    gm_tkc9ngh8g1s0j t235;
                    GM_ST(uint8_t, (uint8_t *)&t235 + 185, 0);
                    GM_ST(uint16_t, (uint8_t *)&t235 + 186, 0);
                    GM_ST(uint32_t, (uint8_t *)&t235 + 188, 0);
                    GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t235), t234);
                    ((uint8_t *)&t235)[184] = 1;
                    memmove(gm_out, ((uint8_t *)&t235), 192);
                    goto gm_fdone;
                }
gm_lb20:;
            }
            {
                {
                    uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t237 = (uint8_t)(t236 == ((uint8_t)6ULL));
                    if (!t237) {
                        goto gm_lb23;
                    }
                    gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)t32) + 8));
                    gm_l39 = t238;
                    gm_tl874t2atnlh3 t239 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 16));
                    gm_l40 = t239;
                    gm_t9qkh0rsooc8d t240 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t32) + 8) + 28));
                    gm_l41 = t240;
                    gm_b16_8 t241 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 32));
                    gm_l42 = t241;
                    void * t242 = gm_l3;
                    gm_b16_8 t243 = gm_l39;
                    uint8_t t244 = gm_f_contains_57n7jqd(t242, t243);
                    if (t244) {
                        gm_tkc9ngh8g1s0j t245;
                        memset(&t245, 0, sizeof t245);
                        memmove(gm_out, ((uint8_t *)&t245), 192);
                        goto gm_fdone;
                    }
                    gm_b16_8 t246;
                    GM_ST(void *, (uint8_t *)&t246, (void *)(gm_sjqs7q3ostqbm));
                    GM_ST(uintptr_t, (uint8_t *)&t246 + 8, (uintptr_t)(3));
                    gm_l43 = t246;
                    gm_b16_8 t247;
                    GM_ST(void *, (uint8_t *)&t247, (void *)(gm_s51jnn7biksj8));
                    GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(67));
                    gm_l44 = t247;
                    gm_t9qkh0rsooc8d t248 = gm_l41;
                    uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&t248) + 0));
                    uint8_t t250 = (uint8_t)((t249 == ((uint8_t)1ULL)));
                    gm_t9qkh0rsooc8d t251;
                    GM_ST(uint8_t, ((uint8_t *)&t251 + 0), ((uint8_t)1ULL));
                    if (t250) {
                        gm_b16_8 t252;
                        GM_ST(void *, (uint8_t *)&t252, (void *)(gm_shofa8pdml9fb));
                        GM_ST(uintptr_t, (uint8_t *)&t252 + 8, (uintptr_t)(12));
                        gm_l43 = t252;
                        gm_b16_8 t253;
                        GM_ST(void *, (uint8_t *)&t253, (void *)(gm_sl9dbf0dicmme));
                        GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(38));
                        gm_l44 = t253;
                    }
                    gm_tl874t2atnlh3 t254 = gm_l40;
                    gm_b16_8 t255;
                    GM_ST(void *, (uint8_t *)&t255, (void *)(gm_sv44r8pinka4e));
                    GM_ST(uintptr_t, (uint8_t *)&t255 + 8, (uintptr_t)(12));
                    gm_b16_8 t256 = gm_l42;
                    gm_b16_8 t257 = gm_l39;
                    gm_b16_8 t258 = gm_l43;
                    gm_b16_8 t259 = gm_l44;
                    gm_t0uuhaljsf410 t260;
                    GM_ST(gm_b16_8, ((uint8_t *)&t260 + 0), t256);
                    GM_ST(gm_b16_8, ((uint8_t *)&t260 + 16), t257);
                    GM_ST(gm_b16_8, ((uint8_t *)&t260 + 32), t258);
                    GM_ST(gm_b16_8, ((uint8_t *)&t260 + 48), t259);
                    gm_f_format_uv4nplt((void *)&gm_l98, (void *)NULL, cx, NULL, &t260);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t261 = (uint8_t)!((((uint8_t *)&gm_l98))[0] != 0);
                    if (!t261) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l98) + 8, gm_srcuktvltncsesc, 278, 50);
                    gm_b16_8 t262 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l98) + 8));
                    gm_b16_8 t263 = GM_LD(gm_b16_8, ((uint8_t *)&t262));
                    gm_t4gut2c9fj1kt t264;
                    gm_f_lint_diag_4vra4qu((void *)&t264, (void *)NULL, cx, NULL, t254, t255, t263);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l45 = t264;
                    void * t265 = (void *)((uint8_t *)&F.l45);
                    gm_b16_8 t266;
                    GM_ST(void *, (uint8_t *)&t266, (void *)(gm_sfj8otp2pq7qq));
                    GM_ST(uintptr_t, (uint8_t *)&t266 + 8, (uintptr_t)(54));
                    gm_f_add_note_46v502s(t265, t266);
                    gm_t4gut2c9fj1kt t267 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l45));
                    gm_tkc9ngh8g1s0j t268;
                    GM_ST(uint8_t, (uint8_t *)&t268 + 185, 0);
                    GM_ST(uint16_t, (uint8_t *)&t268 + 186, 0);
                    GM_ST(uint32_t, (uint8_t *)&t268 + 188, 0);
                    GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t268), t267);
                    ((uint8_t *)&t268)[184] = 1;
                    memmove(gm_out, ((uint8_t *)&t268), 192);
                    goto gm_fdone;
                }
gm_lb23:;
                {
                    uint8_t t269 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t270 = (uint8_t)(t269 == ((uint8_t)7ULL));
                    if (!t270) {
                        goto gm_lb24;
                    }
                    gm_tl874t2atnlh3 t271 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l46 = t271;
                    gm_b16_8 t272 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 16));
                    gm_l47 = t272;
                    gm_tl874t2atnlh3 t273 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 32));
                    gm_l48 = t273;
                    gm_b16_8 t274 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 48));
                    gm_l49 = t274;
                    gm_tl874t2atnlh3 t275 = gm_l46;
                    gm_b16_8 t276;
                    GM_ST(void *, (uint8_t *)&t276, (void *)(gm_s1fsfd0lcgf8e));
                    GM_ST(uintptr_t, (uint8_t *)&t276 + 8, (uintptr_t)(16));
                    gm_b16_8 t277 = gm_l47;
                    gm_b16_8 t278 = gm_l49;
                    gm_b16_8 t279 = gm_f_dangling_message_s0d8vok((void *)NULL, cx, NULL, t277, t278);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t4gut2c9fj1kt t280;
                    gm_f_lint_diag_4vra4qu((void *)&t280, (void *)NULL, cx, NULL, t275, t276, t279);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l50 = t280;
                    void * t281 = (void *)((uint8_t *)&F.l50);
                    gm_b16_8 t282 = gm_l47;
                    gm_tl874t2atnlh3 t283 = gm_l48;
                    gm_b0_1 t284 = gm_f_add_dangling_notes_s4j764p((void *)NULL, cx, NULL, t281, t282, t283);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t4gut2c9fj1kt t285 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l50));
                    gm_tkc9ngh8g1s0j t286;
                    GM_ST(uint8_t, (uint8_t *)&t286 + 185, 0);
                    GM_ST(uint16_t, (uint8_t *)&t286 + 186, 0);
                    GM_ST(uint32_t, (uint8_t *)&t286 + 188, 0);
                    GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t286), t285);
                    ((uint8_t *)&t286)[184] = 1;
                    memmove(gm_out, ((uint8_t *)&t286), 192);
                    goto gm_fdone;
                }
gm_lb24:;
            }
            {
                {
                    uint8_t t287 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t288 = (uint8_t)(t287 == ((uint8_t)8ULL));
                    if (!t288) {
                        goto gm_lb25;
                    }
                    gm_tl874t2atnlh3 t289 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l51 = t289;
                    gm_b16_8 t290 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 16));
                    gm_l52 = t290;
                    gm_tl874t2atnlh3 t291 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 32));
                    gm_l53 = t291;
                    uint8_t t292 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 44));
                    gm_l54 = t292;
                    uint8_t t293 = gm_l54;
                    if (t293) {
                        gm_b16_8 t294;
                        GM_ST(void *, (uint8_t *)&t294, (void *)(gm_sch3p0ffmutvn));
                        GM_ST(uintptr_t, (uint8_t *)&t294 + 8, (uintptr_t)(15));
                        gm_l99 = t294;
                    } else {
                        gm_b16_8 t295;
                        GM_ST(void *, (uint8_t *)&t295, (void *)(gm_s6o3rebeo47ka));
                        GM_ST(uintptr_t, (uint8_t *)&t295 + 8, (uintptr_t)(15));
                        gm_l99 = t295;
                    }
                    gm_b16_8 t296 = gm_l99;
                    gm_l55 = t296;
                    uint8_t t297 = gm_l54;
                    if (t297) {
                        gm_b16_8 t298;
                        GM_ST(void *, (uint8_t *)&t298, (void *)(gm_siun1n8hqooca));
                        GM_ST(uintptr_t, (uint8_t *)&t298 + 8, (uintptr_t)(8));
                        gm_l100 = t298;
                    } else {
                        gm_b16_8 t299;
                        GM_ST(void *, (uint8_t *)&t299, (void *)(gm_s25qdppmkh7s6));
                        GM_ST(uintptr_t, (uint8_t *)&t299 + 8, (uintptr_t)(6));
                        gm_l100 = t299;
                    }
                    gm_b16_8 t300 = gm_l100;
                    gm_l56 = t300;
                    gm_tl874t2atnlh3 t301 = gm_l51;
                    gm_b16_8 t302;
                    GM_ST(void *, (uint8_t *)&t302, (void *)(gm_sq1d6esradpbv));
                    GM_ST(uintptr_t, (uint8_t *)&t302 + 8, (uintptr_t)(20));
                    gm_b16_8 t303 = gm_l52;
                    gm_b16_8 t304 = gm_l55;
                    gm_b16_8 t305 = gm_l56;
                    gm_tfeab279c16rr t306;
                    GM_ST(gm_b16_8, ((uint8_t *)&t306 + 0), t303);
                    GM_ST(gm_b16_8, ((uint8_t *)&t306 + 16), t304);
                    GM_ST(gm_b16_8, ((uint8_t *)&t306 + 32), t305);
                    gm_f_format_331tgtc((void *)&gm_l101, (void *)NULL, cx, NULL, &t306);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t307 = (uint8_t)!((((uint8_t *)&gm_l101))[0] != 0);
                    if (!t307) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l101) + 8, gm_srcuktvltncsesc, 290, 58);
                    gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l101) + 8));
                    gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
                    gm_t4gut2c9fj1kt t310;
                    gm_f_lint_diag_4vra4qu((void *)&t310, (void *)NULL, cx, NULL, t301, t302, t309);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l57 = t310;
                    void * t311 = (void *)((uint8_t *)&F.l57);
                    gm_tl874t2atnlh3 t312 = gm_l53;
                    gm_b16_8 t313 = gm_l56;
                    gm_tnpifv20irnph t314;
                    GM_ST(gm_b16_8, ((uint8_t *)&t314 + 0), t313);
                    gm_f_format_cpa0si0((void *)&gm_l102, (void *)NULL, cx, NULL, t314);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t315 = (uint8_t)!((((uint8_t *)&gm_l102))[0] != 0);
                    if (!t315) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l102) + 8, gm_srcuktvltncsesc, 291, 36);
                    gm_b16_8 t316 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l102) + 8));
                    gm_b16_8 t317 = GM_LD(gm_b16_8, ((uint8_t *)&t316));
                    gm_f_add_label_50fj7vv(t311, GM_CONV(gm_cseahvg8v7p0vs, t312), t317);
                    void * t318 = (void *)((uint8_t *)&F.l57);
                    gm_b16_8 t319 = gm_l52;
                    gm_tnpifv20irnph t320;
                    GM_ST(gm_b16_8, ((uint8_t *)&t320 + 0), t319);
                    gm_f_format_rrko99s((void *)&gm_l103, (void *)NULL, cx, NULL, t320);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t321 = (uint8_t)!((((uint8_t *)&gm_l103))[0] != 0);
                    if (!t321) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l103) + 8, gm_srcuktvltncsesc, 292, 24);
                    gm_b16_8 t322 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l103) + 8));
                    gm_b16_8 t323 = GM_LD(gm_b16_8, ((uint8_t *)&t322));
                    gm_f_add_note_46v502s(t318, t323);
                    gm_t4gut2c9fj1kt t324 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l57));
                    gm_tkc9ngh8g1s0j t325;
                    GM_ST(uint8_t, (uint8_t *)&t325 + 185, 0);
                    GM_ST(uint16_t, (uint8_t *)&t325 + 186, 0);
                    GM_ST(uint32_t, (uint8_t *)&t325 + 188, 0);
                    GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t325), t324);
                    ((uint8_t *)&t325)[184] = 1;
                    memmove(gm_out, ((uint8_t *)&t325), 192);
                    goto gm_fdone;
                }
gm_lb25:;
            }
            {
                {
                    uint8_t t326 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t327 = (uint8_t)(t326 == ((uint8_t)9ULL));
                    if (!t327) {
                        goto gm_lb26;
                    }
                    gm_tl874t2atnlh3 t328 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l58 = t328;
                    gm_b16_8 t329 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 16));
                    gm_l59 = t329;
                    gm_tl874t2atnlh3 t330 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 32));
                    gm_l60 = t330;
                    gm_tl874t2atnlh3 t331 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 44));
                    gm_l61 = t331;
                    void * t332 = gm_l0;
                    gm_tl874t2atnlh3 t333 = gm_l61;
                    gm_b16_8 t334 = gm_f_text_of_lbn39it(t332, GM_CONV(gm_cseahvg8v7p0vs, t333));
                    gm_l62 = t334;
                    gm_tl874t2atnlh3 t335 = gm_l58;
                    gm_b16_8 t336;
                    GM_ST(void *, (uint8_t *)&t336, (void *)(gm_sv242olhg8pke));
                    GM_ST(uintptr_t, (uint8_t *)&t336 + 8, (uintptr_t)(12));
                    gm_b16_8 t337 = gm_l59;
                    gm_b16_8 t338 = gm_l62;
                    gm_t6j6smj7qoctu t339;
                    GM_ST(gm_b16_8, ((uint8_t *)&t339 + 0), t337);
                    GM_ST(gm_b16_8, ((uint8_t *)&t339 + 16), t338);
                    gm_f_format_vft2h8j((void *)&gm_l104, (void *)NULL, cx, NULL, &t339);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t340 = (uint8_t)!((((uint8_t *)&gm_l104))[0] != 0);
                    if (!t340) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l104) + 8, gm_srcuktvltncsesc, 297, 50);
                    gm_b16_8 t341 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l104) + 8));
                    gm_b16_8 t342 = GM_LD(gm_b16_8, ((uint8_t *)&t341));
                    gm_t4gut2c9fj1kt t343;
                    gm_f_lint_diag_4vra4qu((void *)&t343, (void *)NULL, cx, NULL, t335, t336, t342);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l63 = t343;
                    void * t344 = (void *)((uint8_t *)&F.l63);
                    gm_tl874t2atnlh3 t345 = gm_l60;
                    gm_b16_8 t346 = gm_l59;
                    gm_tnpifv20irnph t347;
                    GM_ST(gm_b16_8, ((uint8_t *)&t347 + 0), t346);
                    gm_f_format_40d716h((void *)&gm_l105, (void *)NULL, cx, NULL, t347);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t348 = (uint8_t)!((((uint8_t *)&gm_l105))[0] != 0);
                    if (!t348) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l105) + 8, gm_srcuktvltncsesc, 298, 32);
                    gm_b16_8 t349 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l105) + 8));
                    gm_b16_8 t350 = GM_LD(gm_b16_8, ((uint8_t *)&t349));
                    gm_f_add_label_50fj7vv(t344, GM_CONV(gm_cseahvg8v7p0vs, t345), t350);
                    void * t351 = (void *)((uint8_t *)&F.l63);
                    gm_b16_8 t352 = gm_l59;
                    gm_b16_8 t353 = gm_l62;
                    gm_t6j6smj7qoctu t354;
                    GM_ST(gm_b16_8, ((uint8_t *)&t354 + 0), t352);
                    GM_ST(gm_b16_8, ((uint8_t *)&t354 + 16), t353);
                    gm_f_format_mqu5cbu((void *)&gm_l106, (void *)NULL, cx, NULL, &t354);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t355 = (uint8_t)!((((uint8_t *)&gm_l106))[0] != 0);
                    if (!t355) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l106) + 8, gm_srcuktvltncsesc, 299, 24);
                    gm_b16_8 t356 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l106) + 8));
                    gm_b16_8 t357 = GM_LD(gm_b16_8, ((uint8_t *)&t356));
                    gm_f_add_note_46v502s(t351, t357);
                    gm_t4gut2c9fj1kt t358 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l63));
                    gm_tkc9ngh8g1s0j t359;
                    GM_ST(uint8_t, (uint8_t *)&t359 + 185, 0);
                    GM_ST(uint16_t, (uint8_t *)&t359 + 186, 0);
                    GM_ST(uint32_t, (uint8_t *)&t359 + 188, 0);
                    GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t359), t358);
                    ((uint8_t *)&t359)[184] = 1;
                    memmove(gm_out, ((uint8_t *)&t359), 192);
                    goto gm_fdone;
                }
gm_lb26:;
                {
                    uint8_t t360 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t361 = (uint8_t)(t360 == ((uint8_t)10ULL));
                    if (!t361) {
                        goto gm_lb27;
                    }
                    gm_tl874t2atnlh3 t362 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l64 = t362;
                    gm_b16_8 t363 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 8) + 16));
                    gm_l65 = t363;
                    gm_tl874t2atnlh3 t364 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 32));
                    gm_l66 = t364;
                    gm_tl874t2atnlh3 t365 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 44));
                    gm_l67 = t365;
                    void * t366 = gm_l0;
                    gm_tl874t2atnlh3 t367 = gm_l66;
                    gm_b16_8 t368 = gm_f_text_of_lbn39it(t366, GM_CONV(gm_cseahvg8v7p0vs, t367));
                    gm_l68 = t368;
                    gm_tl874t2atnlh3 t369 = gm_l64;
                    gm_b16_8 t370;
                    GM_ST(void *, (uint8_t *)&t370, (void *)(gm_su3sef888krmb));
                    GM_ST(uintptr_t, (uint8_t *)&t370 + 8, (uintptr_t)(21));
                    gm_b16_8 t371 = gm_l65;
                    gm_b16_8 t372 = gm_l68;
                    gm_t6j6smj7qoctu t373;
                    GM_ST(gm_b16_8, ((uint8_t *)&t373 + 0), t371);
                    GM_ST(gm_b16_8, ((uint8_t *)&t373 + 16), t372);
                    gm_f_format_iqr642o((void *)&gm_l107, (void *)NULL, cx, NULL, &t373);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t374 = (uint8_t)!((((uint8_t *)&gm_l107))[0] != 0);
                    if (!t374) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l107) + 8, gm_srcuktvltncsesc, 304, 59);
                    gm_b16_8 t375 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l107) + 8));
                    gm_b16_8 t376 = GM_LD(gm_b16_8, ((uint8_t *)&t375));
                    gm_t4gut2c9fj1kt t377;
                    gm_f_lint_diag_4vra4qu((void *)&t377, (void *)NULL, cx, NULL, t369, t370, t376);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l69 = t377;
                    void * t378 = (void *)((uint8_t *)&F.l69);
                    gm_tl874t2atnlh3 t379 = gm_l67;
                    gm_b16_8 t380 = gm_l65;
                    gm_tnpifv20irnph t381;
                    GM_ST(gm_b16_8, ((uint8_t *)&t381 + 0), t380);
                    gm_f_format_duvvd80((void *)&gm_l108, (void *)NULL, cx, NULL, t381);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t382 = (uint8_t)!((((uint8_t *)&gm_l108))[0] != 0);
                    if (!t382) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l108) + 8, gm_srcuktvltncsesc, 305, 33);
                    gm_b16_8 t383 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l108) + 8));
                    gm_b16_8 t384 = GM_LD(gm_b16_8, ((uint8_t *)&t383));
                    gm_f_add_label_50fj7vv(t378, GM_CONV(gm_cseahvg8v7p0vs, t379), t384);
                    void * t385 = (void *)((uint8_t *)&F.l69);
                    gm_b16_8 t386;
                    GM_ST(void *, (uint8_t *)&t386, (void *)(gm_svo4dku3ftf1l));
                    GM_ST(uintptr_t, (uint8_t *)&t386 + 8, (uintptr_t)(176));
                    gm_f_add_note_46v502s(t385, t386);
                    gm_t4gut2c9fj1kt t387 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l69));
                    gm_tkc9ngh8g1s0j t388;
                    GM_ST(uint8_t, (uint8_t *)&t388 + 185, 0);
                    GM_ST(uint16_t, (uint8_t *)&t388 + 186, 0);
                    GM_ST(uint32_t, (uint8_t *)&t388 + 188, 0);
                    GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t388), t387);
                    ((uint8_t *)&t388)[184] = 1;
                    memmove(gm_out, ((uint8_t *)&t388), 192);
                    goto gm_fdone;
                }
gm_lb27:;
            }
            {
                {
                    uint8_t t389 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t390 = (uint8_t)(t389 == ((uint8_t)3ULL));
                    if (!t390) {
                        goto gm_lb28;
                    }
                    gm_tl874t2atnlh3 t391 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l70 = t391;
                    gm_tl874t2atnlh3 t392 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t32) + 8) + 12));
                    gm_l71 = t392;
                    uint8_t t393 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 24));
                    gm_l72 = t393;
                    void * t394 = gm_l0;
                    gm_tl874t2atnlh3 t395 = gm_l70;
                    gm_b16_8 t396 = gm_f_text_of_lbn39it(t394, GM_CONV(gm_cseahvg8v7p0vs, t395));
                    gm_l73 = t396;
                    gm_b16_8 t397 = gm_l73;
                    gm_b16_8 t398;
                    GM_ST(void *, (uint8_t *)&t398, (void *)(gm_sehko5bb96s6p));
                    GM_ST(uintptr_t, (uint8_t *)&t398 + 8, (uintptr_t)(4));
                    uint8_t t399 = gm_f_contains_gkki4kq(t397, t398);
                    if (t399) {
                        gm_l109 = ((uint8_t)1);
                    } else {
                        gm_b16_8 t400 = gm_l73;
                        gm_b16_8 t401;
                        GM_ST(void *, (uint8_t *)&t401, (void *)(gm_sogehh8t1m7tn));
                        GM_ST(uintptr_t, (uint8_t *)&t401 + 8, (uintptr_t)(5));
                        uint8_t t402 = gm_f_contains_gkki4kq(t400, t401);
                        gm_l109 = t402;
                    }
                    uint8_t t403 = gm_l109;
                    uint8_t t404 = (uint8_t)!t403;
                    if (t404) {
                        gm_tkc9ngh8g1s0j t405;
                        memset(&t405, 0, sizeof t405);
                        memmove(gm_out, ((uint8_t *)&t405), 192);
                        goto gm_fdone;
                    }
                    void * t406 = gm_l0;
                    gm_tl874t2atnlh3 t407 = gm_l71;
                    gm_b16_8 t408 = gm_f_text_of_lbn39it(t406, GM_CONV(gm_cseahvg8v7p0vs, t407));
                    gm_l74 = t408;
                    gm_b16_8 t409 = gm_l74;
                    gm_l75 = t409;
                    uint8_t t410 = gm_l72;
                    uint8_t t411 = (uint8_t)!t410;
                    if (t411) {
                        gm_b16_8 t412 = gm_l74;
                        gm_b16_8 t413;
                        GM_ST(void *, (uint8_t *)&t413, (void *)(gm_southrlg4k72m));
                        GM_ST(uintptr_t, (uint8_t *)&t413 + 8, (uintptr_t)(5));
                        uint8_t t414 = gm_f_contains_gkki4kq(t412, t413);
                        if (t414) {
                            gm_l111 = ((uint8_t)1);
                        } else {
                            gm_b16_8 t415 = gm_l74;
                            gm_b16_8 t416;
                            GM_ST(void *, (uint8_t *)&t416, (void *)(gm_sbf1f7fu8bvpt));
                            GM_ST(uintptr_t, (uint8_t *)&t416 + 8, (uintptr_t)(4));
                            uint8_t t417 = gm_f_contains_gkki4kq(t415, t416);
                            gm_l111 = t417;
                        }
                        uint8_t t418 = gm_l111;
                        if (t418) {
                            gm_l110 = ((uint8_t)1);
                        } else {
                            gm_b16_8 t419 = gm_l74;
                            gm_b16_8 t420;
                            GM_ST(void *, (uint8_t *)&t420, (void *)(gm_spp2t9pj35hcq));
                            GM_ST(uintptr_t, (uint8_t *)&t420 + 8, (uintptr_t)(6));
                            uint8_t t421 = gm_f_contains_gkki4kq(t419, t420);
                            gm_l110 = t421;
                        }
                        uint8_t t422 = gm_l110;
                        if (t422) {
                            gm_b16_8 t423 = gm_l74;
                            gm_tnpifv20irnph t424;
                            GM_ST(gm_b16_8, ((uint8_t *)&t424 + 0), t423);
                            gm_f_format_a2pi3bt((void *)&gm_l112, (void *)NULL, cx, NULL, t424);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t425 = (uint8_t)!((((uint8_t *)&gm_l112))[0] != 0);
                            if (!t425) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l112) + 8, gm_srcuktvltncsesc, 316, 35);
                            gm_b16_8 t426 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l112) + 8));
                            gm_b16_8 t427 = GM_LD(gm_b16_8, ((uint8_t *)&t426));
                            gm_l75 = t427;
                        } else {
                            gm_b16_8 t428 = gm_l74;
                            gm_tnpifv20irnph t429;
                            GM_ST(gm_b16_8, ((uint8_t *)&t429 + 0), t428);
                            gm_f_format_udg5t2e((void *)&gm_l113, (void *)NULL, cx, NULL, t429);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t430 = (uint8_t)!((((uint8_t *)&gm_l113))[0] != 0);
                            if (!t430) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l113) + 8, gm_srcuktvltncsesc, 318, 35);
                            gm_b16_8 t431 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l113) + 8));
                            gm_b16_8 t432 = GM_LD(gm_b16_8, ((uint8_t *)&t431));
                            gm_l75 = t432;
                        }
                    }
                    gm_tl874t2atnlh3 t433 = gm_l70;
                    gm_b16_8 t434;
                    GM_ST(void *, (uint8_t *)&t434, (void *)(gm_srvn31r258t3t));
                    GM_ST(uintptr_t, (uint8_t *)&t434 + 8, (uintptr_t)(15));
                    gm_b16_8 t435;
                    GM_ST(void *, (uint8_t *)&t435, (void *)(gm_s1gd0gkbe5tu9));
                    GM_ST(uintptr_t, (uint8_t *)&t435 + 8, (uintptr_t)(54));
                    gm_t4gut2c9fj1kt t436;
                    gm_f_lint_diag_4vra4qu((void *)&t436, (void *)NULL, cx, NULL, t433, t434, t435);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l76 = t436;
                    void * t437 = (void *)((uint8_t *)&F.l76);
                    gm_b16_8 t438 = gm_l75;
                    gm_tnpifv20irnph t439;
                    GM_ST(gm_b16_8, ((uint8_t *)&t439 + 0), t438);
                    gm_f_format_ooibcq2((void *)&gm_l114, (void *)NULL, cx, NULL, t439);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t440 = (uint8_t)!((((uint8_t *)&gm_l114))[0] != 0);
                    if (!t440) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l114) + 8, gm_srcuktvltncsesc, 322, 23);
                    gm_b16_8 t441 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l114) + 8));
                    gm_b16_8 t442 = GM_LD(gm_b16_8, ((uint8_t *)&t441));
                    gm_tl874t2atnlh3 t443 = gm_l70;
                    gm_b16_8 t444 = gm_l75;
                    gm_b0_1 t445 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t437, t442, t443, t444);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t4gut2c9fj1kt t446 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)&F.l76));
                    gm_tkc9ngh8g1s0j t447;
                    GM_ST(uint8_t, (uint8_t *)&t447 + 185, 0);
                    GM_ST(uint16_t, (uint8_t *)&t447 + 186, 0);
                    GM_ST(uint32_t, (uint8_t *)&t447 + 188, 0);
                    GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t447), t446);
                    ((uint8_t *)&t447)[184] = 1;
                    memmove(gm_out, ((uint8_t *)&t447), 192);
                    goto gm_fdone;
                }
gm_lb28:;
            }
            {
                {
                    uint8_t t448 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t449 = (uint8_t)(t448 == ((uint8_t)4ULL));
                    if (!t449) {
                        goto gm_lb29;
                    }
                    gm_tl874t2atnlh3 t450 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l77 = t450;
                    uint8_t t451 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 12));
                    gm_l78 = t451;
                    gm_tl874t2atnlh3 t452 = gm_l77;
                    gm_b16_8 t453;
                    GM_ST(void *, (uint8_t *)&t453, (void *)(gm_siooblm685vmo));
                    GM_ST(uintptr_t, (uint8_t *)&t453 + 8, (uintptr_t)(15));
                    uint8_t t454 = gm_l78;
                    gm_t9qkh0rsooc8d t455;
                    GM_ST(uint8_t, ((uint8_t *)&t455 + 0), t454);
                    gm_f_format_5mja5ge((void *)&gm_l115, (void *)NULL, cx, NULL, t455);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t456 = (uint8_t)!((((uint8_t *)&gm_l115))[0] != 0);
                    if (!t456) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l115) + 8, gm_srcuktvltncsesc, 325, 84);
                    gm_b16_8 t457 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l115) + 8));
                    gm_b16_8 t458 = GM_LD(gm_b16_8, ((uint8_t *)&t457));
                    gm_t4gut2c9fj1kt t459;
                    gm_f_lint_diag_4vra4qu((void *)&t459, (void *)NULL, cx, NULL, t452, t453, t458);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tkc9ngh8g1s0j t460;
                    GM_ST(uint8_t, (uint8_t *)&t460 + 185, 0);
                    GM_ST(uint16_t, (uint8_t *)&t460 + 186, 0);
                    GM_ST(uint32_t, (uint8_t *)&t460 + 188, 0);
                    GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t460), t459);
                    ((uint8_t *)&t460)[184] = 1;
                    memmove(gm_out, ((uint8_t *)&t460), 192);
                    goto gm_fdone;
                }
gm_lb29:;
                {
                    uint8_t t461 = GM_LD(uint8_t, (((uint8_t *)t32) + 0));
                    uint8_t t462 = (uint8_t)(t461 == ((uint8_t)5ULL));
                    if (!t462) {
                        goto gm_lb30;
                    }
                    gm_tl874t2atnlh3 t463 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t32) + 8));
                    gm_l79 = t463;
                    gm_tl874t2atnlh3 t464 = gm_l79;
                    gm_b16_8 t465;
                    GM_ST(void *, (uint8_t *)&t465, (void *)(gm_sgu334n5g1kog));
                    GM_ST(uintptr_t, (uint8_t *)&t465 + 8, (uintptr_t)(15));
                    gm_b16_8 t466;
                    GM_ST(void *, (uint8_t *)&t466, (void *)(gm_sijvlope91krv));
                    GM_ST(uintptr_t, (uint8_t *)&t466 + 8, (uintptr_t)(30));
                    gm_t4gut2c9fj1kt t467;
                    gm_f_lint_diag_4vra4qu((void *)&t467, (void *)NULL, cx, NULL, t464, t465, t466);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tkc9ngh8g1s0j t468;
                    GM_ST(uint8_t, (uint8_t *)&t468 + 185, 0);
                    GM_ST(uint16_t, (uint8_t *)&t468 + 186, 0);
                    GM_ST(uint32_t, (uint8_t *)&t468 + 188, 0);
                    GM_ST(gm_t4gut2c9fj1kt, ((uint8_t *)&t468), t467);
                    ((uint8_t *)&t468)[184] = 1;
                    memmove(gm_out, ((uint8_t *)&t468), 192);
                    goto gm_fdone;
                }
gm_lb30:;
                gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 211, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_in_scope_tm13267(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_in_scope_tm13267 F;
    void * gm_l0; /* sc */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* p */
    gm_b16_8 gm_l3; /* dir */
    gm_b16_8 gm_l4; /* owner */
    gm_b16_8 gm_l5; /* known */
    GM_STACK_CHECK(&F, 655u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l6;
    uint8_t gm_l7;
    gm_th2lqbo77pvsc gm_l8;
    gm_b16_8 gm_l9;
    gm_th2lqbo77pvsc gm_l10;
    gm_th2lqbo77pvsc gm_l11;
    gm_b16_8 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    uint8_t gm_l14;
    gm_b16_8 t4 = gm_l1;
    gm_b16_8 t5 = gm_f_canonical_2k2pqlg((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t5;
    gm_b16_8 t6 = gm_l2;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)t7));
    uint8_t t9 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t6, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t10 = (uint8_t)!t9;
    if (t10) {
        gm_l6 = ((uint8_t)1);
    } else {
        gm_b16_8 t11 = gm_l2;
        void * t12 = gm_l0;
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 16));
        uint8_t t14 = gm_f_starts_with_ahdikpt((void *)NULL, cx, NULL, t11, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t14) {
            void * t15 = gm_l0;
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t15) + 32));
            uint8_t t17 = (uint8_t)!t16;
            gm_l7 = t17;
        } else {
            gm_l7 = ((uint8_t)0);
        }
        uint8_t t18 = gm_l7;
        gm_l6 = t18;
    }
    uint8_t t19 = gm_l6;
    if (t19) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_b16_8 t20 = gm_l2;
    gm_f_parent_lk0iero((void *)&gm_l8, (void *)NULL, cx, NULL, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l8))[16] != 0);
    if (t21) {
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
        gm_l9 = t22;
    } else {
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(0));
        gm_l9 = t23;
    }
    gm_b16_8 t24 = gm_l9;
    gm_l3 = t24;
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(0));
    gm_l4 = t25;
    {
        {
            void * t26 = gm_l0;
            void * t27 = (void *)(((uint8_t *)t26) + 40);
            gm_b16_8 t28 = gm_l3;
            gm_f_get_e3joqcc((void *)&gm_l10, t27, t28);
            uint8_t t29 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
            if (!t29) {
                goto gm_lb2;
            }
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
            gm_l5 = t30;
            gm_b16_8 t31 = gm_l5;
            gm_l4 = t31;
            goto gm_lb1;
        }
gm_lb2:;
        gm_b16_8 t32 = gm_l3;
        gm_f_find_root_vg2av48((void *)&gm_l11, (void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t33 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
        if (t33) {
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
            gm_l12 = t34;
        } else {
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(0));
            gm_l12 = t35;
        }
        gm_b16_8 t36 = gm_l12;
        gm_l4 = t36;
        void * t37 = gm_l0;
        void * t38 = (void *)(((uint8_t *)t37) + 40);
        gm_b16_8 t39 = gm_l3;
        gm_b16_8 t40 = gm_l4;
        gm_tftb1odc739t5 t41 = gm_f_put_7shfu9u(t38, t39, t40);
        gm_l13 = t41;
        uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
        if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcuktvltncsesc, 133, 9);
    }
gm_lb1:;
    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    uint8_t t44 = ((t43 == ((uint64_t)0ULL)));
    if (t44) {
        gm_l14 = ((uint8_t)1);
    } else {
        gm_b16_8 t45 = gm_l4;
        gm_b16_8 t46 = gm_f_canonical_2k2pqlg((void *)NULL, cx, NULL, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t47 = gm_l0;
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)t47));
        uint8_t t49 = (gm_eq0iqiubfm7e(((uint8_t *)&t46), ((uint8_t *)&t48)));
        gm_l14 = t49;
    }
    uint8_t t50 = gm_l14;
    F.ret = t50;
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
extern __attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_level_m5neg79(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_level_name_qqj3mmj(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_level_name_qqj3mmj F;
    gm_t9qkh0rsooc8d gm_l0; /* l */
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
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_slb6cj7ndgvdm));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(5));
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
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_soofuqhnovv5o));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(4));
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
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s920j0georape));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(4));
            gm_l1 = t14;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 29, 45);
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
extern __attribute__((aligned(4))) void gm_f_lint_diag_4vra4qu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_parse_level_4iat2k8(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parse_level_4iat2k8 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 gm_l1;
    {
        {
            gm_b16_8 t7 = gm_l0;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_slb6cj7ndgvdm));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(5));
            uint8_t t9 = (gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
            if (!t9) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t10;
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
            gm_taari1s0m51a7 t11;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11), t10);
            ((uint8_t *)&t11)[1] = 1;
            gm_l1 = t11;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_b16_8 t12 = gm_l0;
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_soofuqhnovv5o));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(4));
            uint8_t t14 = (gm_eq0iqiubfm7e(((uint8_t *)&t12), ((uint8_t *)&t13)));
            if (!t14) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t15;
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)1ULL));
            gm_taari1s0m51a7 t16;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[1] = 1;
            gm_l1 = t16;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_b16_8 t17 = gm_l0;
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s920j0georape));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
            uint8_t t19 = (gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
            if (!t19) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t20;
            GM_ST(uint8_t, ((uint8_t *)&t20 + 0), ((uint8_t)2ULL));
            gm_taari1s0m51a7 t21;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t21), t20);
            ((uint8_t *)&t21)[1] = 1;
            gm_l1 = t21;
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_taari1s0m51a7 t22;
            memset(&t22, 0, sizeof t22);
            gm_l1 = t22;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 22, 47);
    }
gm_lb1:;
    gm_taari1s0m51a7 t23 = gm_l1;
    F.ret = t23;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rule_scmud9h(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_run_n6la0ov(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, void * a3, void * a4) {
    struct gm_fr_run_n6la0ov F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* parsed */
    uint8_t gm_l2; /* program */
    void * gm_l3; /* sc */
    void * gm_l4; /* cfg */
    void * gm_l5; /* sm */
    void * gm_l6; /* diags */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    void * gm_l24; /* p */
    uint64_t gm_l25; /* before */
    void * gm_l26; /* facts */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_t2lqu8jsj511d gm_l31; /* out */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* i */
    gm_b16_8 gm_l40; /* code */
    uint8_t gm_l41; /* allowed */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    gm_b16_8 gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    gm_b16_8 gm_l47; /* r */
    GM_STACK_CHECK(&F, 3159u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tftb1odc739t5 gm_l48;
    gm_t7qqq0mh1r8ke gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tntfcv47jbeh7 gm_l51;
    uint8_t gm_l52;
    gm_tf5srh4mp4b3i gm_l53;
    uint64_t gm_l54;
    gm_tkc9ngh8g1s0j gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_tftb1odc739t5 gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_b16_8 gm_l59;
    uint8_t gm_l60;
    gm_t9qkh0rsooc8d gm_l61;
    {
        void * t50 = gm_l0;
        void * t51 = GM_LD(void *, ((uint8_t *)t50));
        void * t52 = GM_LD(void *, ((uint8_t *)t51));
        gm_l5 = t52;
        void * t53 = gm_l0;
        void * t54 = GM_LD(void *, ((uint8_t *)t53));
        void * t55 = GM_LD(void *, (((uint8_t *)t54) + 8));
        gm_l6 = t55;
        void * t56 = gm_l6;
        uint8_t t57 = gm_f_has_errors_if7ai0m(t56);
        if (t57) {
            gm_tc70a9dbbmgkg t58;
            memset(&t58, 0, sizeof t58);
            memmove(gm_out, ((uint8_t *)&t58), 24);
            goto gm_fdone;
        }
        gm_trqfvmjtt4sij t59;
        gm_f_list_s83jn2q((void *)&t59, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t59;
        void * t60 = gm_l5;
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)t60));
        gm_l8 = t61;
        gm_l9 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t62 = gm_l9;
                uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint8_t t64 = (t62 < t63);
                if (!t64) {
                    goto gm_lb1;
                }
                {
                    void * t65 = GM_LD(void *, ((uint8_t *)&gm_l8));
                    uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t67 = gm_l9;
                    {
                        {
                            memmove(((uint8_t *)&F.l10), (((uint8_t *)t65) + (uintptr_t)t67 * 80), 80);
                            void * t68 = (void *)((uint8_t *)&F.l7);
                            void * t69 = gm_l3;
                            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                            uint8_t t71 = gm_f_in_scope_tm13267((void *)NULL, cx, NULL, t69, t70);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tftb1odc739t5 t72 = gm_f_push_vd1290p(t68, t71);
                            gm_l48 = t72;
                            uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                            if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srcuktvltncsesc, 156, 31);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 156, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t74 = gm_l9;
                uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                uint64_t t77 = r75;
                gm_l9 = t77;
            }
        }
gm_lb1:;
        void * t78 = gm_l0;
        uint8_t t79 = gm_l2;
        gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
        gm_f_analyze_np1m50h((void *)&F.l11, (void *)NULL, cx, NULL, t78, t79, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t82;
        gm_f_list_prbqo9j((void *)&t82, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l12 = t82;
        void * t83 = gm_l5;
        gm_t1og5k0mlcjk3 t84;
        gm_f_walk_tech5q5((void *)&t84, (void *)NULL, cx, NULL, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t84;
        gm_t4c7ipjj9s3fp t85;
        gm_f_hash_map_cuh4fs9((void *)&t85, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l14 = t85;
        gm_t4c7ipjj9s3fp t86;
        gm_f_hash_map_s7cd6p5((void *)&t86, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l15 = t86;
        gm_b16_8 t87 = gm_l1;
        gm_l16 = t87;
    }
    {
        gm_l17 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t88 = gm_l17;
                uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint8_t t90 = (t88 < t89);
                if (!t90) {
                    goto gm_lb6;
                }
                {
                    uint64_t t91 = gm_l17;
                    void * t92 = GM_LD(void *, ((uint8_t *)&gm_l16));
                    uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint64_t t94 = gm_l17;
                    gm_tqtt2php6e8lk t95 = GM_LD(gm_tqtt2php6e8lk, (((uint8_t *)t92) + (uintptr_t)t94 * 56));
                    gm_t7qqq0mh1r8ke t96;
                    GM_ST(uint64_t, ((uint8_t *)&t96 + 0), t91);
                    GM_ST(gm_tqtt2php6e8lk, ((uint8_t *)&t96 + 8), t95);
                    gm_l49 = t96;
                    {
                        {
                            uint64_t t97 = GM_LD(uint64_t, ((uint8_t *)&gm_l49));
                            gm_l18 = t97;
                            memmove(((uint8_t *)&F.l19), (((uint8_t *)&gm_l49) + 8), 56);
                            void * t98 = (void *)((uint8_t *)&F.l15);
                            uint32_t t99 = GM_LD(uint32_t, ((uint8_t *)&F.l19));
                            uint64_t t100 = gm_l18;
                            gm_tftb1odc739t5 t101 = gm_f_put_sokl06f(t98, t99, t100);
                            gm_l50 = t101;
                            uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                            if (!t102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srcuktvltncsesc, 162, 37);
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 162, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t103 = gm_l17;
                uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                uint64_t t106 = r104;
                gm_l17 = t106;
            }
        }
gm_lb6:;
        void * t107 = gm_l5;
        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)t107));
        gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
        gm_l20 = t109;
        gm_l21 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t110 = gm_l21;
                uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                uint8_t t112 = (t110 < t111);
                if (!t112) {
                    goto gm_lb11;
                }
                {
                    uint64_t t113 = gm_l21;
                    void * t114 = GM_LD(void *, ((uint8_t *)&gm_l20));
                    uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                    uint64_t t116 = gm_l21;
                    gm_tcsu09l1pp5hf t117 = GM_LD(gm_tcsu09l1pp5hf, (((uint8_t *)t114) + (uintptr_t)t116 * 80));
                    gm_tntfcv47jbeh7 t118;
                    GM_ST(uint64_t, ((uint8_t *)&t118 + 0), t113);
                    GM_ST(gm_tcsu09l1pp5hf, ((uint8_t *)&t118 + 8), t117);
                    gm_l51 = t118;
                    {
                        {
                            uint64_t t119 = GM_LD(uint64_t, ((uint8_t *)&gm_l51));
                            gm_l22 = t119;
                            memmove(((uint8_t *)&F.l23), (((uint8_t *)&gm_l51) + 8), 80);
                            gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&F.l23));
                            gm_b16_8 t121;
                            GM_ST(void *, (uint8_t *)&t121, (void *)(gm_s1f440st4mnkp));
                            GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(1));
                            uint8_t t122 = gm_f_starts_with_6vvt01e(t120, t121);
                            if (t122) {
                                gm_l52 = ((uint8_t)1);
                            } else {
                                void * t123 = GM_LD(void *, ((uint8_t *)&F.l7));
                                uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                                uint64_t t125 = gm_l22;
                                if ((uintptr_t)t125 >= (uintptr_t)(t124)) gm_trap_index((uintptr_t)t125, t124, gm_srcuktvltncsesc, 164, 43);
                                uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)t123) + (uintptr_t)t125 * 1));
                                uint8_t t127 = (uint8_t)!t126;
                                gm_l52 = t127;
                            }
                            uint8_t t128 = gm_l52;
                            if (t128) {
                                goto gm_lb13;
                            }
                            void * t129 = GM_LD(void *, ((uint8_t *)&gm_l1));
                            uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                            void * t131 = (void *)((uint8_t *)&F.l15);
                            uint64_t t132 = gm_l22;
                            uint32_t t133 = (uint32_t)t132;
                            gm_tf5srh4mp4b3i t134 = gm_f_get_1ji4too(t131, t133);
                            gm_l53 = t134;
                            uint8_t t135 = (uint8_t)((((uint8_t *)&gm_l53))[8] != 0);
                            if (t135) {
                                uint64_t t136 = GM_LD(uint64_t, ((uint8_t *)&gm_l53));
                                gm_l54 = t136;
                            } else {
                                goto gm_lb13;
                            }
                            uint64_t t137 = gm_l54;
                            if ((uintptr_t)t137 >= (uintptr_t)(t130)) gm_trap_index((uintptr_t)t137, t130, gm_srcuktvltncsesc, 167, 15);
                            void * t138 = (void *)(((uint8_t *)t129) + (uintptr_t)t137 * 56);
                            gm_l24 = t138;
                            uint64_t t139 = GM_LD(uint64_t, (((((uint8_t *)&F.l13) + 8) + 96) + 8));
                            gm_l25 = t139;
                            void * t140 = (void *)((uint8_t *)&F.l13);
                            void * t141 = gm_l24;
                            void * t142 = (void *)(((uint8_t *)t141) + 24);
                            void * t143 = gm_l24;
                            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)t143) + 8));
                            gm_b0_1 t145 = gm_f_walk_file_do4uq8b((void *)NULL, cx, NULL, t140, t142, t144);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t146 = gm_l24;
                            gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)t146) + 8));
                            void * t148 = GM_LD(void *, ((((uint8_t *)&F.l13) + 8) + 96));
                            uint64_t t149 = GM_LD(uint64_t, (((((uint8_t *)&F.l13) + 8) + 96) + 8));
                            uint64_t t150 = gm_l25;
                            uint64_t t151 = (uintptr_t)t150;
                            uint64_t t152 = (uintptr_t)t149;
                            if (t151 > t152 || t152 > (uintptr_t)t149) gm_trap_slice(t151, t152, t149, gm_srcuktvltncsesc, 170, 28);
                            uint64_t t153 = ((uintptr_t)t149) - (uintptr_t)t150;
                            gm_b16_8 t154;
                            GM_ST(void *, (uint8_t *)&t154, (void *)(((uint8_t *)t148 + (uintptr_t)t150 * 12)));
                            GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(t153));
                            gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
                            void * t156 = (void *)((uint8_t *)&F.l14);
                            gm_b0_1 t157 = gm_f_names_in_1jbgo4k((void *)NULL, cx, NULL, t147, t155, t156);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb14;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 163, 5);
                    }
gm_lb14:;
                }
gm_lb13:;
                uint64_t t158 = gm_l21;
                uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
                uint64_t t161 = r159;
                gm_l21 = t161;
            }
        }
gm_lb11:;
    }
    {
        void * t162 = (void *)(((uint8_t *)&F.l13) + 8);
        gm_l26 = t162;
        gm_b16_8 t163 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        gm_l27 = t163;
        gm_l28 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t164 = gm_l28;
                uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                uint8_t t166 = (t164 < t165);
                if (!t166) {
                    goto gm_lb16;
                }
                {
                    void * t167 = GM_LD(void *, ((uint8_t *)&gm_l27));
                    uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                    uint64_t t169 = gm_l28;
                    {
                        {
                            memmove(((uint8_t *)&F.l29), (((uint8_t *)t167) + (uintptr_t)t169 * 72), 72);
                            {
                                {
                                    void * t170 = gm_l5;
                                    void * t171 = (void *)((uint8_t *)&F.l29);
                                    void * t172 = gm_l26;
                                    void * t173 = (void *)(((uint8_t *)t172) + 40);
                                    void * t174 = (void *)((uint8_t *)&F.l14);
                                    gm_f_finding_diag_2vp4thv((void *)&gm_l55, (void *)NULL, cx, NULL, t170, t171, t173, t174);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t175 = (uint8_t)((((uint8_t *)&gm_l55))[184] != 0);
                                    if (!t175) {
                                        goto gm_lb22;
                                    }
                                    memmove(((uint8_t *)&F.l30), ((uint8_t *)&gm_l55), 184);
                                    void * t176 = (void *)((uint8_t *)&F.l12);
                                    gm_tftb1odc739t5 t177 = gm_f_push_l0rgbjf(t176, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l30)));
                                    gm_l56 = t177;
                                    uint8_t t178 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                    if (!t178) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_srcuktvltncsesc, 174, 67);
                                    goto gm_lb21;
                                }
gm_lb22:;
                            }
gm_lb21:;
                            goto gm_lb19;
                        }
gm_lb20:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 173, 5);
                    }
gm_lb19:;
                }
gm_lb18:;
                uint64_t t179 = gm_l28;
                uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
                uint64_t t182 = r180;
                gm_l28 = t182;
            }
        }
gm_lb16:;
        void * t183 = gm_l26;
        uint64_t t184 = GM_LD(uint64_t, ((((uint8_t *)t183) + 176) + 8));
        gm_t2lqu8jsj511d t185;
        GM_ST(uint64_t, ((uint8_t *)&t185 + 0), ((uint64_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&t185 + 8), t184);
        gm_l31 = t185;
        void * t186 = gm_l26;
        gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)t186) + 176));
        gm_l32 = t187;
        gm_l33 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t188 = gm_l33;
                uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                uint8_t t190 = (t188 < t189);
                if (!t190) {
                    goto gm_lb23;
                }
                {
                    void * t191 = GM_LD(void *, ((uint8_t *)&gm_l32));
                    uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                    uint64_t t193 = gm_l33;
                    {
                        {
                            memmove(((uint8_t *)&F.l34), (((uint8_t *)t191) + (uintptr_t)t193 * 184), 184);
                            void * t194 = gm_l6;
                            gm_f_push_o4ckdr1(t194, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l34)));
                            goto gm_lb26;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 177, 5);
                    }
gm_lb26:;
                }
gm_lb25:;
                uint64_t t195 = gm_l33;
                uint64_t r196; bool o197 = __builtin_add_overflow(t195, ((uint64_t)1ULL), &r196);
                uint64_t t198 = r196;
                gm_l33 = t198;
            }
        }
gm_lb23:;
    }
    {
        gm_trqfvmjtt4sij t199;
        gm_f_list_prbqo9j((void *)&t199, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l35 = t199;
        void * t200 = (void *)((uint8_t *)&F.l35);
        void * t201 = gm_l26;
        gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)t201));
        gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&t202));
        gm_tftb1odc739t5 t204 = gm_f_extend_s4sbegm(t200, t203);
        gm_l57 = t204;
        uint8_t t205 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
        if (!t205) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srcuktvltncsesc, 180, 5);
        void * t206 = (void *)((uint8_t *)&F.l35);
        gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
        gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)&t207));
        gm_tftb1odc739t5 t209 = gm_f_extend_s4sbegm(t206, t208);
        gm_l58 = t209;
        uint8_t t210 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
        if (!t210) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_srcuktvltncsesc, 181, 5);
        gm_l37 = ((uint64_t)0ULL);
        uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)&F.l35) + 8));
        gm_l36 = t211;
        {
            for (;;) {
                uint64_t t212 = gm_l37;
                uint64_t t213 = gm_l36;
                uint8_t t214 = (t212 < t213);
                if (!t214) {
                    goto gm_lb28;
                }
                {
                    {
                        {
                            uint64_t t215 = gm_l37;
                            gm_l38 = t215;
                            void * t216 = GM_LD(void *, ((uint8_t *)&F.l35));
                            uint64_t t217 = GM_LD(uint64_t, (((uint8_t *)&F.l35) + 8));
                            uint64_t t218 = gm_l38;
                            if ((uintptr_t)t218 >= (uintptr_t)(t217)) gm_trap_index((uintptr_t)t218, t217, gm_srcuktvltncsesc, 183, 14);
                            memmove(((uint8_t *)&F.l39), (((uint8_t *)t216) + (uintptr_t)t218 * 184), 184);
                            uint8_t t219 = (uint8_t)(((((uint8_t *)&F.l39) + 160))[16] != 0);
                            if (t219) {
                                gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 160));
                                gm_l59 = t220;
                            } else {
                                gm_b16_8 t221;
                                GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(0));
                                gm_l59 = t221;
                            }
                            gm_b16_8 t222 = gm_l59;
                            gm_l40 = t222;
                            gm_l41 = ((uint8_t)0);
                            void * t223 = gm_l26;
                            gm_b16_8 t224 = GM_LD(gm_b16_8, (((uint8_t *)t223) + 136));
                            gm_l42 = t224;
                            gm_l43 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t225 = gm_l43;
                                    uint64_t t226 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                                    uint8_t t227 = (t225 < t226);
                                    if (!t227) {
                                        goto gm_lb33;
                                    }
                                    {
                                        void * t228 = GM_LD(void *, ((uint8_t *)&gm_l42));
                                        uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                                        uint64_t t230 = gm_l43;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l44), (((uint8_t *)t228) + (uintptr_t)t230 * 56), 56);
                                                {
                                                    {
                                                        uint32_t t231 = GM_LD(uint32_t, ((uint8_t *)&F.l44));
                                                        uint32_t t232 = GM_LD(uint32_t, (((uint8_t *)&F.l39) + 24));
                                                        uint8_t t233 = ((t231 == t232));
                                                        if (!t233) {
                                                            goto gm_lb39;
                                                        }
                                                        uint32_t t234 = GM_LD(uint32_t, (((uint8_t *)&F.l44) + 4));
                                                        uint32_t t235 = GM_LD(uint32_t, ((((uint8_t *)&F.l39) + 24) + 4));
                                                        uint8_t t236 = (t234 <= t235);
                                                        if (!t236) {
                                                            goto gm_lb39;
                                                        }
                                                        uint32_t t237 = GM_LD(uint32_t, ((((uint8_t *)&F.l39) + 24) + 8));
                                                        uint32_t t238 = GM_LD(uint32_t, (((uint8_t *)&F.l44) + 8));
                                                        uint8_t t239 = (t237 <= t238);
                                                        if (!t239) {
                                                            goto gm_lb39;
                                                        }
                                                        gm_b16_8 t240 = GM_LD(gm_b16_8, (((uint8_t *)&F.l44) + 16));
                                                        gm_l45 = t240;
                                                        gm_l46 = ((uint64_t)0ULL);
                                                        {
                                                            for (;;) {
                                                                uint64_t t241 = gm_l46;
                                                                uint64_t t242 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                                                                uint8_t t243 = (t241 < t242);
                                                                if (!t243) {
                                                                    goto gm_lb40;
                                                                }
                                                                {
                                                                    void * t244 = GM_LD(void *, ((uint8_t *)&gm_l45));
                                                                    uint64_t t245 = GM_LD(uint64_t, (((uint8_t *)&gm_l45) + 8));
                                                                    uint64_t t246 = gm_l46;
                                                                    {
                                                                        {
                                                                            gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)t244) + (uintptr_t)t246 * 16));
                                                                            gm_l47 = t247;
                                                                            gm_b16_8 t248 = gm_l47;
                                                                            gm_b16_8 t249 = gm_l40;
                                                                            uint8_t t250 = (gm_eq0iqiubfm7e(((uint8_t *)&t248), ((uint8_t *)&t249)));
                                                                            if (t250) {
                                                                                gm_l60 = ((uint8_t)1);
                                                                            } else {
                                                                                gm_b16_8 t251 = gm_l47;
                                                                                gm_b16_8 t252;
                                                                                GM_ST(void *, (uint8_t *)&t252, (void *)(gm_s0nf1rg04f3m5));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t252 + 8, (uintptr_t)(3));
                                                                                uint8_t t253 = (gm_eq0iqiubfm7e(((uint8_t *)&t251), ((uint8_t *)&t252)));
                                                                                gm_l60 = t253;
                                                                            }
                                                                            uint8_t t254 = gm_l60;
                                                                            if (t254) {
                                                                                gm_l41 = ((uint8_t)1);
                                                                            }
                                                                            goto gm_lb43;
                                                                        }
gm_lb44:;
                                                                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 188, 17);
                                                                    }
gm_lb43:;
                                                                }
gm_lb42:;
                                                                uint64_t t255 = gm_l46;
                                                                uint64_t r256; bool o257 = __builtin_add_overflow(t255, ((uint64_t)1ULL), &r256);
                                                                uint64_t t258 = r256;
                                                                gm_l46 = t258;
                                                            }
                                                        }
gm_lb40:;
                                                        goto gm_lb38;
                                                    }
gm_lb39:;
                                                }
gm_lb38:;
                                                goto gm_lb36;
                                            }
gm_lb37:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 186, 9);
                                        }
gm_lb36:;
                                    }
gm_lb35:;
                                    uint64_t t259 = gm_l43;
                                    uint64_t r260; bool o261 = __builtin_add_overflow(t259, ((uint64_t)1ULL), &r260);
                                    uint64_t t262 = r260;
                                    gm_l43 = t262;
                                }
                            }
gm_lb33:;
                            uint8_t t263 = gm_l41;
                            if (t263) {
                                goto gm_lb30;
                            }
                            void * t264 = gm_l4;
                            gm_b16_8 t265 = gm_l40;
                            gm_t9qkh0rsooc8d t266 = gm_f_level_m5neg79(t264, t265);
                            gm_l61 = t266;
                            {
                                {
                                    uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)&gm_l61) + 0));
                                    uint8_t t268 = (uint8_t)(t267 == ((uint8_t)0ULL));
                                    if (!t268) {
                                        goto gm_lb46;
                                    }
                                    goto gm_lb30;
                                }
gm_lb46:;
                                {
                                    uint8_t t269 = GM_LD(uint8_t, (((uint8_t *)&gm_l61) + 0));
                                    uint8_t t270 = (uint8_t)(t269 == ((uint8_t)1ULL));
                                    if (!t270) {
                                        goto gm_lb47;
                                    }
                                    uint64_t t271 = GM_LD(uint64_t, ((uint8_t *)&gm_l31));
                                    uint64_t r272; bool o273 = __builtin_add_overflow(t271, ((uint64_t)1ULL), &r272);
                                    if (o273) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 196, 24);
                                    uint64_t t274 = r272;
                                    GM_ST(uint64_t, ((uint8_t *)&gm_l31), t274);
                                    goto gm_lb45;
                                }
gm_lb47:;
                                {
                                    uint8_t t275 = GM_LD(uint8_t, (((uint8_t *)&gm_l61) + 0));
                                    uint8_t t276 = (uint8_t)(t275 == ((uint8_t)2ULL));
                                    if (!t276) {
                                        goto gm_lb48;
                                    }
                                    gm_t9qkh0rsooc8d t277;
                                    GM_ST(uint8_t, ((uint8_t *)&t277 + 0), ((uint8_t)2ULL));
                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l39), t277);
                                    uint64_t t278 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                    uint64_t r279; bool o280 = __builtin_add_overflow(t278, ((uint64_t)1ULL), &r279);
                                    if (o280) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 199, 17);
                                    uint64_t t281 = r279;
                                    GM_ST(uint64_t, (((uint8_t *)&gm_l31) + 8), t281);
                                    goto gm_lb45;
                                }
gm_lb48:;
                                gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 194, 9);
                            }
gm_lb45:;
                            void * t282 = gm_l6;
                            gm_f_push_o4ckdr1(t282, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l39)));
                            goto gm_lb31;
                        }
gm_lb32:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 182, 5);
                    }
gm_lb31:;
                }
gm_lb30:;
                uint64_t t283 = gm_l37;
                uint64_t r284; bool o285 = __builtin_add_overflow(t283, ((uint64_t)1ULL), &r284);
                uint64_t t286 = r284;
                gm_l37 = t286;
            }
        }
gm_lb28:;
    }
    {
        gm_t2lqu8jsj511d t287 = gm_l31;
        gm_tc70a9dbbmgkg t288;
        GM_ST(uint8_t, (uint8_t *)&t288 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t288 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t288 + 20, 0);
        GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t288), t287);
        ((uint8_t *)&t288)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t288), 24);
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
extern __attribute__((aligned(4))) void gm_f_scope_al6a28r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
inline __attribute__((aligned(4))) void gm_f_set_5cf9rlg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_set_5cf9rlg F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l1; /* name */
    gm_t9qkh0rsooc8d gm_l2; /* level */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 439u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l7;
    uint64_t gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_b16_8 t7 = gm_l1;
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s0nf1rg04f3m5));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(3));
    uint8_t t9 = (gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
    if (t9) {
        gm_l4 = ((uint64_t)0ULL);
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)gm_ciuj8hs0q3l));
        gm_l3 = t10;
        {
            for (;;) {
                uint64_t t11 = gm_l4;
                uint64_t t12 = gm_l3;
                uint8_t t13 = (t11 < t12);
                if (!t13) {
                    goto gm_lb1;
                }
                {
                    {
                        {
                            uint64_t t14 = gm_l4;
                            gm_l5 = t14;
                            void * t15 = gm_l0;
                            void * t16 = GM_LD(void *, ((uint8_t *)t15));
                            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t15) + 8));
                            uint64_t t18 = gm_l5;
                            if ((uintptr_t)t18 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t18, t17, gm_srcuktvltncsesc, 63, 34);
                            gm_t9qkh0rsooc8d t19 = gm_l2;
                            GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)t16) + (uintptr_t)t18 * 1), t19);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcuktvltncsesc, 63, 9);
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
        gm_th2lqbo77pvsc t24;
        memset(&t24, 0, sizeof t24);
        memmove(gm_out, ((uint8_t *)&t24), 24);
        goto gm_fdone;
    }
    gm_b16_8 t25 = gm_l1;
    gm_tf5srh4mp4b3i t26 = gm_f_rule_scmud9h(t25);
    gm_l7 = t26;
    uint8_t t27 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
    if (t27) {
        uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
        gm_l8 = t28;
    } else {
        gm_b16_8 t29 = gm_l1;
        gm_tnpifv20irnph t30;
        GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t29);
        gm_f_format_tovpseb((void *)&gm_l9, (void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srcuktvltncsesc, 66, 33);
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_th2lqbo77pvsc t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t34), t33);
        ((uint8_t *)&t34)[16] = 1;
        memmove(gm_out, ((uint8_t *)&t34), 24);
        goto gm_fdone;
    }
    uint64_t t35 = gm_l8;
    gm_l6 = t35;
    void * t36 = gm_l0;
    void * t37 = GM_LD(void *, ((uint8_t *)t36));
    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t36) + 8));
    uint64_t t39 = gm_l6;
    if ((uintptr_t)t39 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t39, t38, gm_srcuktvltncsesc, 67, 5);
    gm_t9qkh0rsooc8d t40 = gm_l2;
    GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)t37) + (uintptr_t)t39 * 1), t40);
    gm_th2lqbo77pvsc t41;
    memset(&t41, 0, sizeof t41);
    memmove(gm_out, ((uint8_t *)&t41), 24);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_text_of_lbn39it(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_whole_lines_s23hlj6(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_whole_lines_s23hlj6 F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* src */
    uint64_t gm_l3; /* lo */
    uint64_t gm_l4; /* hi */
    uint64_t gm_l5; /* start */
    uint64_t gm_l6; /* end */
    gm_b16_8 gm_l7; /* before */
    gm_b16_8 gm_l8; /* after */
    gm_b16_8 gm_l9; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 981u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_b16_8 gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    gm_b16_8 gm_l13;
    uint8_t gm_l14;
    void * t10 = gm_l0;
    uint32_t t11 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t12 = gm_f_file_o15fqku(t10, t11);
    gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 16));
    gm_l2 = t13;
    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t15 = (uint64_t)t14;
    gm_l3 = t15;
    uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t17 = (uint64_t)t16;
    gm_l4 = t17;
    uint64_t t18 = gm_l3;
    gm_l5 = t18;
    {
        for (;;) {
            uint64_t t19 = gm_l5;
            uint8_t t20 = (t19 > ((uint64_t)0ULL));
            if (!t20) {
                goto gm_lb1;
            }
            void * t21 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t23 = gm_l5;
            uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
            if (o25) gm_trap("integer overflow in `-`", gm_srcuktvltncsesc, 337, 29);
            uint64_t t26 = r24;
            if ((uintptr_t)t26 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t26, t22, gm_srcuktvltncsesc, 337, 25);
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)t26 * 1));
            uint8_t t28 = (!(t27 == ((uint8_t)10ULL)));
            if (!t28) {
                goto gm_lb1;
            }
            {
                uint64_t t29 = gm_l5;
                uint64_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint64_t)1ULL), &r30);
                if (o31) gm_trap("integer overflow in `-`", gm_srcuktvltncsesc, 337, 54);
                uint64_t t32 = r30;
                gm_l5 = t32;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t33 = gm_l4;
    gm_l6 = t33;
    {
        for (;;) {
            uint64_t t34 = gm_l6;
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t36 = (t34 < t35);
            if (!t36) {
                goto gm_lb4;
            }
            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t39 = gm_l6;
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)t39 * 1));
            uint8_t t41 = (!(t40 == ((uint8_t)10ULL)));
            if (!t41) {
                goto gm_lb4;
            }
            {
                uint64_t t42 = gm_l6;
                uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                if (o44) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 339, 52);
                uint64_t t45 = r43;
                gm_l6 = t45;
            }
gm_lb6:;
        }
    }
gm_lb4:;
    uint64_t t46 = gm_l6;
    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t48 = (t46 < t47);
    if (t48) {
        uint64_t t49 = gm_l6;
        uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
        if (o51) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 340, 24);
        uint64_t t52 = r50;
        gm_l6 = t52;
    }
    void * t53 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t55 = gm_l5;
    uint64_t t56 = gm_l3;
    uint64_t t57 = (uintptr_t)t55;
    uint64_t t58 = (uintptr_t)t56;
    if (t57 > t58 || t58 > (uintptr_t)t54) gm_trap_slice(t57, t58, t54, gm_srcuktvltncsesc, 341, 15);
    uint64_t t59 = ((uintptr_t)t56) - (uintptr_t)t55;
    gm_b16_8 t60;
    GM_ST(void *, (uint8_t *)&t60, (void *)(((uint8_t *)t53 + (uintptr_t)t55 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(t59));
    gm_l7 = t60;
    void * t61 = GM_LD(void *, ((uint8_t *)&gm_l2));
    uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t63 = gm_l4;
    uint64_t t64 = gm_l6;
    uint64_t t65 = (uintptr_t)t63;
    uint64_t t66 = (uintptr_t)t64;
    if (t65 > t66 || t66 > (uintptr_t)t62) gm_trap_slice(t65, t66, t62, gm_srcuktvltncsesc, 342, 14);
    uint64_t t67 = ((uintptr_t)t64) - (uintptr_t)t63;
    gm_b16_8 t68;
    GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t61 + (uintptr_t)t63 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(t67));
    gm_l8 = t68;
    gm_b16_8 t69 = gm_l7;
    gm_b16_8 t70 = gm_f_trim_fdrfe6i(t69);
    gm_l10 = t70;
    uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
    uint8_t t72 = (t71 > ((uint64_t)0ULL));
    if (t72) {
        gm_toceffb697p0u t73;
        memset(&t73, 0, sizeof t73);
        F.ret = t73;
        goto gm_fret;
    }
    gm_b16_8 t74 = gm_l8;
    gm_b16_8 t75 = gm_f_trim_start_179crak(t74);
    gm_l9 = t75;
    uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
    uint8_t t77 = ((t76 == ((uint64_t)0ULL)));
    if (t77) {
        gm_l12 = ((uint8_t)1);
    } else {
        gm_b16_8 t78 = gm_l9;
        gm_b16_8 t79;
        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_si4j8sh2qika1));
        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(2));
        uint8_t t80 = gm_f_starts_with_6vvt01e(t78, t79);
        gm_l12 = t80;
    }
    uint8_t t81 = gm_l12;
    if (t81) {
        gm_l11 = ((uint8_t)1);
    } else {
        gm_b16_8 t82 = gm_l8;
        gm_b16_8 t83 = gm_f_trim_fdrfe6i(t82);
        gm_l13 = t83;
        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
        uint8_t t85 = ((t84 == ((uint64_t)0ULL)));
        gm_l11 = t85;
    }
    uint8_t t86 = gm_l11;
    uint8_t t87 = (uint8_t)!t86;
    if (t87) {
        gm_toceffb697p0u t88;
        memset(&t88, 0, sizeof t88);
        F.ret = t88;
        goto gm_fret;
    }
    {
        {
            uint64_t t89 = gm_l5;
            uint8_t t90 = ((t89 == ((uint64_t)0ULL)));
            if (t90) {
                gm_l14 = ((uint8_t)1);
            } else {
                void * t91 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t93 = gm_l5;
                uint64_t t94 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t95 = (uintptr_t)t93;
                if (t94 > t95 || t95 > (uintptr_t)t92) gm_trap_slice(t94, t95, t92, gm_srcuktvltncsesc, 348, 23);
                uint64_t t96 = ((uintptr_t)t93) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t97;
                GM_ST(void *, (uint8_t *)&t97, (void *)(((uint8_t *)t91 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(t96));
                gm_b16_8 t98;
                GM_ST(void *, (uint8_t *)&t98, (void *)(gm_s8dbdqj1sqibj));
                GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(2));
                uint8_t t99 = gm_f_ends_with_biriu5l(t97, t98);
                gm_l14 = t99;
            }
            uint8_t t100 = gm_l14;
            if (!t100) {
                goto gm_lb8;
            }
            void * t101 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t103 = gm_l6;
            uint64_t t104 = (uintptr_t)t103;
            uint64_t t105 = (uintptr_t)t102;
            if (t104 > t105 || t105 > (uintptr_t)t102) gm_trap_slice(t104, t105, t102, gm_srcuktvltncsesc, 348, 59);
            uint64_t t106 = ((uintptr_t)t102) - (uintptr_t)t103;
            gm_b16_8 t107;
            GM_ST(void *, (uint8_t *)&t107, (void *)(((uint8_t *)t101 + (uintptr_t)t103 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(t106));
            gm_b16_8 t108;
            GM_ST(void *, (uint8_t *)&t108, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(1));
            uint8_t t109 = gm_f_starts_with_6vvt01e(t107, t108);
            if (!t109) {
                goto gm_lb8;
            }
            uint64_t t110 = gm_l6;
            uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
            if (o112) gm_trap("integer overflow in `+`", gm_srcuktvltncsesc, 348, 90);
            uint64_t t113 = r111;
            gm_l6 = t113;
            goto gm_lb7;
        }
gm_lb8:;
    }
gm_lb7:;
    uint32_t t114 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint64_t t115 = gm_l5;
    uint64_t t116 = gm_l6;
    gm_tl874t2atnlh3 t117 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_64igq5o(t114, t115, t116));
    gm_toceffb697p0u t118;
    GM_ST(uint8_t, (uint8_t *)&t118 + 13, 0);
    GM_ST(uint16_t, (uint8_t *)&t118 + 14, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t118), t117);
    ((uint8_t *)&t118)[12] = 1;
    F.ret = t118;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
